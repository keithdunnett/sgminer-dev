/*
 * Copyright 2013-2014 sgminer developers (see AUTHORS.md)
 * Copyright 2011-2013 Con Kolivas
 * Copyright 2011-2012 Luke Dashjr
 * Copyright 2010 Jeff Garzik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <ccan/opt/opt.h>
#include <jansson.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "miner.h"
#include "config_parser.h"
#include "driver-opencl.h"
#include "include/bench_block.h"
#include "include/compat.h"

#include "algorithm.h"
#include "pool.h"

//#ifdef HAVE_ADL
//#include "adl.h"
//#endif

#if defined(unix) || defined(__APPLE__)
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#endif

extern char *cnfbuf; // config file loaded
int fileconf_load;   // config file load status
extern const char def_conf[];
char *default_config;
bool config_loaded;
static int include_count;

extern int json_array_index;    // current array index being parsed
extern char *last_json_error; // last json_error
/*#define JSON_MAX_DEPTH 10
#define JSON_MAX_DEPTH_ERR "Too many levels of JSON includes (limit 10) or a
loop"*/

/***************************************
* Config Writer Functions
****************************************/

inline json_t *json_sprintf(const char *fmt, ...) {
  va_list args;
  char *buf;
  size_t bufsize;

  // build args
  va_start(args, fmt);
  // get final string buffer size
  bufsize = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  if (!(buf = (char *)malloc(++bufsize)))
    quit(1, "Malloc failure in config_parser::json_sprintf().");

  // zero out buffer
  memset(buf, '\0', bufsize);

  // get args again
  va_start(args, fmt);
  vsnprintf(buf, bufsize, fmt, args);
  va_end(args);

  // return json string
  return json_string(buf);
}

// find a profile by name
inline struct profile *get_profile(char *name) {
  int i;

  if (empty_string(name)) {
    return NULL;
  }

  for (i = 0; i < total_profiles; ++i) {
    if (!safe_cmp(profiles[i]->name, name)) {
      return profiles[i];
    }
  }

  return NULL;
}

/*******************************
 * Helper macros
 *******************************/

#define JSON_POOL_ERR "json_object_set() failed on pool(%d):%s"
#define JSON_PROFILE_ERR "json_object_set() failed on profile(%d):%s"
#define JSON_ROOT_ERR                                                          \
  "Error: config_parser::write_config():\n json_object_set() failed on %s"

#ifndef json_pool_add
#define json_pool_add(obj, key, val, id)                                       \
  if (json_object_set(obj, key, val) == -1) {                                  \
    set_last_json_error(JSON_POOL_ERR, id, key);                               \
    return NULL;                                                               \
  }
#endif

#ifndef json_profile_add
#define json_profile_add(obj, key, val, parentkey, id)                         \
  if (json_object_set(obj, key, val) == -1) {                                  \
    if (!empty_string(parentkey)) {                                            \
      set_last_json_error(JSON_PROFILE_ERR, id, key);                          \
      return NULL;                                                             \
    } else {                                                                   \
      applog(LOG_ERR, JSON_ROOT_ERR, key);                                     \
      return NULL;                                                             \
    }                                                                          \
  }
#endif

#ifndef json_add
#define json_add(obj, key, val)                                                \
  if (json_object_set(obj, key, val) == -1) {                                  \
    applog(LOG_ERR, JSON_ROOT_ERR, key);                                       \
    return;                                                                    \
  }
#endif

// helper function to add json values to pool object
static json_t *build_pool_json_add(json_t *object, const char *key,
                                   const char *val, const char *str_compare,
                                   const char *default_compare, int id) {
  // if pool value is empty, abort
  if (empty_string(val))
    return object;

  // check to see if its the same value as profile, abort if it is
  if (safe_cmp(str_compare, val) == 0)
    return object;

  // check to see if it's the same value as default profile, abort if it is
  if (safe_cmp(default_compare, val) == 0)
    return object;

  // not same value, add value to JSON
  json_pool_add(object, key, json_string(val), id);

  return object;
}

// builds the "pools" json array for config file
static json_t *build_pool_json() {
  json_t *pool_array, *obj;
  struct pool *pool;
  struct profile *profile;
  int i;

  // create the "pools" array
  if (!(pool_array = json_array())) {
    set_last_json_error("json_array() failed on pools");
    return NULL;
  }

  // process pool entries
  for (i = 0; i < total_pools; i++) {
    pool = pools[i];

    // create a new object
    if (!(obj = json_object())) {
      set_last_json_error("json_object() failed on pool %d", pool->pool_no);
      return NULL;
    }

    // pool name
    if (!empty_string(pool->name))
      json_pool_add(obj, "name", json_string(pool->name), pool->pool_no);

    // add quota/url
    if (pool->quota != 1) {
      json_pool_add(
          obj, "quota",
          json_sprintf(
              "%s%s%s%d;%s",
              ((pool->rpc_proxy) ? (char *)proxytype(pool->rpc_proxytype) : ""),
              ((pool->rpc_proxy) ? pool->rpc_proxy : ""),
              ((pool->rpc_proxy) ? "|" : ""), pool->quota, pool->rpc_url),
          pool->pool_no);
    } else {
      json_pool_add(
          obj, "url",
          json_sprintf(
              "%s%s%s%s",
              ((pool->rpc_proxy) ? (char *)proxytype(pool->rpc_proxytype) : ""),
              ((pool->rpc_proxy) ? pool->rpc_proxy : ""),
              ((pool->rpc_proxy) ? "|" : ""), pool->rpc_url),
          pool->pool_no);
    }

    // user
    json_pool_add(obj, "user", json_string(pool->rpc_user), pool->pool_no);

    // pass
    json_pool_add(obj, "pass", json_string(pool->rpc_pass), pool->pool_no);

    if (!pool->extranonce_subscribe)
      json_pool_add(obj, "no-extranonce", json_true(), pool->pool_no);

    if (!empty_string(pool->description))
      json_pool_add(obj, "no-description", json_string(pool->description),
                    pool->pool_no);

    // if priority isnt the same as array index, specify it
    if (pool->prio != i)
      json_pool_add(obj, "priority", json_sprintf("%d", pool->prio),
                    pool->pool_no);

    // if a profile was specified, add it then compare pool/profile settings to
    // see what we write
    if (!empty_string(pool->profile)) {
      if ((profile = get_profile(pool->profile))) {
        // save profile name
        json_pool_add(obj, "profile", json_string(pool->profile),
                      pool->pool_no);
      }
      // profile not found use default profile
      else
        profile = &default_profile;
    }
    // or select default profile
    else
      profile = &default_profile;

    // if algorithm is different than profile, add it
    if (!cmp_algorithm(&pool->algorithm, &profile->algorithm)) {
      // save algorithm name
      json_pool_add(obj, "algorithm", json_string(pool->algorithm.name),
                    pool->pool_no);

      // save nfactor also
      if (pool->algorithm.type == ALGO_NSCRYPT)
        json_pool_add(obj, "nfactor",
                      json_sprintf("%d", profile->algorithm.nfactor),
                      pool->pool_no);
    }

    // if pool and profile value doesn't match below, add it
    // devices
    if (!build_pool_json_add(obj, "device", pool->devices, profile->devices,
                             default_profile.devices, pool->pool_no))
      return NULL;

    // kernelfile
    if (!build_pool_json_add(obj, "kernelfile", pool->algorithm.kernelfile,
                             profile->algorithm.kernelfile,
                             default_profile.algorithm.kernelfile,
                             pool->pool_no))
      return NULL;

    // lookup-gap
    if (!build_pool_json_add(obj, "lookup-gap", pool->lookup_gap,
                             profile->lookup_gap, default_profile.lookup_gap,
                             pool->pool_no))
      return NULL;

    // rawintensity
    if (!empty_string(pool->rawintensity)) {
      if (!build_pool_json_add(obj, "rawintensity", pool->rawintensity,
                               profile->rawintensity,
                               default_profile.rawintensity, pool->pool_no)) {
        return NULL;
      }
    }
    // xintensity
    else if (!empty_string(pool->xintensity)) {
      if (!build_pool_json_add(obj, "xintensity", pool->xintensity,
                               profile->xintensity, default_profile.xintensity,
                               pool->pool_no)) {
        return NULL;
      }
    }
    // intensity
    else if (!empty_string(pool->intensity)) {
      if (!build_pool_json_add(obj, "intensity", pool->intensity,
                               profile->intensity, default_profile.intensity,
                               pool->pool_no)) {
        return NULL;
      }
    }

    // shaders
    if (!build_pool_json_add(obj, "shaders", pool->shaders, profile->shaders,
                             default_profile.shaders, pool->pool_no))
      return NULL;

    // thread_concurrency
    if (!build_pool_json_add(obj, "thread-concurrency",
                             pool->thread_concurrency,
                             profile->thread_concurrency,
                             default_profile.thread_concurrency, pool->pool_no))
      return NULL;

    // worksize
    if (!build_pool_json_add(obj, "worksize", pool->worksize, profile->worksize,
                             default_profile.worksize, pool->pool_no))
      return NULL;

#ifdef HAVE_ADL
    // gpu_engine
    if (!build_pool_json_add(obj, "gpu-engine", pool->gpu_engine,
                             profile->gpu_engine, default_profile.gpu_engine,
                             pool->pool_no))
      return NULL;

    // gpu_memclock
    if (!build_pool_json_add(obj, "gpu-memclock", pool->gpu_memclock,
                             profile->gpu_memclock,
                             default_profile.gpu_memclock, pool->pool_no))
      return NULL;

    // gpu_threads
    if (!build_pool_json_add(obj, "gpu-threads", pool->gpu_threads,
                             profile->gpu_threads, default_profile.gpu_threads,
                             pool->pool_no))
      return NULL;

    // gpu_fan
    if (!build_pool_json_add(obj, "gpu-fan", pool->gpu_fan, profile->gpu_fan,
                             default_profile.gpu_fan, pool->pool_no))
      return NULL;

    // gpu-powertune
    if (!build_pool_json_add(obj, "gpu-powertune", pool->gpu_powertune,
                             profile->gpu_powertune,
                             default_profile.gpu_powertune, pool->pool_no))
      return NULL;

    // gpu-vddc
    if (!build_pool_json_add(obj, "gpu-vddc", pool->gpu_vddc, profile->gpu_vddc,
                             default_profile.gpu_vddc, pool->pool_no))
      return NULL;
#endif

    // all done, add pool to array...
    if (json_array_append_new(pool_array, obj) == -1) {
      set_last_json_error("json_array_append() failed on pool %d",
                          pool->pool_no);
      return NULL;
    }
  }

  return pool_array;
}

// helper function to add json values to profile object
static json_t *build_profile_json_add(json_t *object, const char *key,
                                      const char *val, const char *str_compare,
                                      const bool isdefault,
                                      const char *parentkey, int id) {
  // if default profile, make sure we sync profile and default_profile values...
  if (isdefault)
    val = str_compare;

  // no value, return...
  if (empty_string(val)) {
    return object;
  }

  // if the value is the same as default profile and, the current profile is not
  // default profile, return...
  if ((safe_cmp(str_compare, val) == 0) && isdefault == false) {
    return object;
  }

  json_profile_add(object, key, json_string(val), parentkey, id);

  return object;
}

// helper function to write all the profile settings
static json_t *build_profile_settings_json(json_t *object,
                                           struct profile *profile,
                                           const bool isdefault,
                                           const char *parentkey) {
  // if algorithm is different than default profile or profile is default
  // profile, add it
  if (!cmp_algorithm(&default_profile.algorithm, &profile->algorithm) ||
      isdefault) {
    // save algorithm name
    json_profile_add(object, "algorithm", json_string(profile->algorithm.name),
                     parentkey, profile->profile_no);

    // save nfactor also
    if (profile->algorithm.type == ALGO_NSCRYPT)
      json_profile_add(object, "nfactor",
                       json_sprintf("%u", profile->algorithm.nfactor),
                       parentkey, profile->profile_no);
  }

  // devices
  if (!build_profile_json_add(object, "device", profile->devices,
                              default_profile.devices, isdefault, parentkey,
                              profile->profile_no))
    return NULL;

  // kernelfile
  if (!build_profile_json_add(object, "kernelfile",
                              profile->algorithm.kernelfile,
                              default_profile.algorithm.kernelfile, isdefault,
                              parentkey, profile->profile_no))
    return NULL;

  // lookup-gap
  if (!build_profile_json_add(object, "lookup-gap", profile->lookup_gap,
                              default_profile.lookup_gap, isdefault, parentkey,
                              profile->profile_no))
    return NULL;

  // rawintensity
  if (!empty_string(profile->rawintensity) ||
      (isdefault && !empty_string(default_profile.rawintensity))) {
    if (!build_profile_json_add(object, "rawintensity", profile->rawintensity,
                                default_profile.rawintensity, isdefault,
                                parentkey, profile->profile_no)) {
      return NULL;
    }
  }
  // xintensity
  else if (!empty_string(profile->xintensity) ||
           (isdefault && !empty_string(default_profile.xintensity))) {
    if (!build_profile_json_add(object, "xintensity", profile->xintensity,
                                default_profile.xintensity, isdefault,
                                parentkey, profile->profile_no)) {
      return NULL;
    }
  }
  // intensity
  else if (!empty_string(profile->intensity) ||
           (isdefault && !empty_string(default_profile.intensity))) {
    if (!build_profile_json_add(object, "intensity", profile->intensity,
                                default_profile.intensity, isdefault, parentkey,
                                profile->profile_no)) {
      return NULL;
    }
  }

  // shaders
  if (!build_profile_json_add(object, "shaders", profile->shaders,
                              default_profile.shaders, isdefault, parentkey,
                              profile->profile_no))
    return NULL;

  // thread_concurrency
  if (!build_profile_json_add(object, "thread-concurrency",
                              profile->thread_concurrency,
                              default_profile.thread_concurrency, isdefault,
                              parentkey, profile->profile_no))
    return NULL;

  // worksize
  if (!build_profile_json_add(object, "worksize", profile->worksize,
                              default_profile.worksize, isdefault, parentkey,
                              profile->profile_no))
    return NULL;

#ifdef HAVE_ADL
  // gpu_engine
  if (!build_profile_json_add(object, "gpu-engine", profile->gpu_engine,
                              default_profile.gpu_engine, isdefault, parentkey,
                              profile->profile_no))
    return NULL;

  // gpu_memclock
  if (!build_profile_json_add(object, "gpu-memclock", profile->gpu_memclock,
                              default_profile.gpu_memclock, isdefault,
                              parentkey, profile->profile_no))
    return NULL;

  // gpu_threads
  if (!build_profile_json_add(object, "gpu-threads", profile->gpu_threads,
                              default_profile.gpu_threads, isdefault, parentkey,
                              profile->profile_no))
    return NULL;

  // gpu_fan
  if (!build_profile_json_add(object, "gpu-fan", profile->gpu_fan,
                              default_profile.gpu_fan, isdefault, parentkey,
                              profile->profile_no))
    return NULL;

  // gpu-powertune
  if (!build_profile_json_add(object, "gpu-powertune", profile->gpu_powertune,
                              default_profile.gpu_powertune, isdefault,
                              parentkey, profile->profile_no))
    return NULL;

  // gpu-vddc
  if (!build_profile_json_add(object, "gpu-vddc", profile->gpu_vddc,
                              default_profile.gpu_vddc, isdefault, parentkey,
                              profile->profile_no))
    return NULL;
#endif

  return object;
}

// builds the "profiles" json array for config file
json_t *build_profile_json() {
  json_t *profile_array, *obj;
  struct profile *profile;
  bool isdefault;
  int i;

  // create the "profiles" array
  if (!(profile_array = json_array())) {
    set_last_json_error("json_array() failed on profiles");
    return NULL;
  }

  // process pool entries
  for (i = 0; i < total_profiles; i++) {
    profile = profiles[i];
    isdefault = false;

    if (!empty_string(default_profile.name)) {
      if (!strcasecmp(profile->name, default_profile.name))
        isdefault = true;
    }

    // create a new object
    if (!(obj = json_object())) {
      set_last_json_error("json_object() failed on profile %d",
                          profile->profile_no);
      return NULL;
    }

    // profile name
    if (!empty_string(profile->name))
      json_profile_add(obj, "name", json_string(profile->name), "profile",
                       profile->profile_no);

    // save profile settings
    if (!build_profile_settings_json(obj, profile, isdefault, "profile"))
      return NULL;

    // all done, add pool to array...
    if (json_array_append_new(profile_array, obj) == -1) {
      set_last_json_error("json_array_append() failed on profile %d",
                          profile->profile_no);
      return NULL;
    }
  }

  return profile_array;
}

void write_config(const char *filename) {
  json_t *config, *obj;
  struct opt_table *opt;
  char *p, *optname;
  int i;

  // json root
  if (!(config = json_object())) {
    applog(LOG_ERR, "Error: config_parser::write_config():\n json_object() "
                    "failed on root.");
    return;
  }

  // build pools
  if (!(obj = build_pool_json())) {
    applog(LOG_ERR, "Error: config_parser::write_config():\n %s.",
           last_json_error);
    return;
  }

  // add pools to config
  if (json_object_set(config, "pools", obj) == -1) {
    applog(LOG_ERR, "Error: config_parser::write_config():\n "
                    "json_object_set(pools) failed.");
    return;
  }

  // build profiles
  if (!(obj = build_profile_json())) {
    applog(LOG_ERR, "Error: config_parser::write_config():\n %s.",
           last_json_error);
    return;
  }

  // add profiles to config
  if (json_object_set(config, "profiles", obj) == -1) {
    applog(LOG_ERR, "Error: config_parser::write_config():\n "
                    "json_object_set(profiles) failed.");
    return;
  }

  // pool strategy
  switch (pool_strategy) {
  case POOL_BALANCE:
    json_add(config, "balance", json_true());
    break;
  case POOL_LOADBALANCE:
    json_add(config, "load-balance", json_true());
    break;
  case POOL_ROUNDROBIN:
    json_add(config, "round-robin", json_true());
    break;
  case POOL_ROTATE:
    json_add(config, "rotate", json_sprintf("%d", opt_rotate_period));
    break;
  // default failover only
  default:
    json_add(config, "failover-only", json_true());
    break;
  }

  // if using a specific profile as default, set it
  if (!empty_string(default_profile.name)) {
    json_add(config, "default-profile", json_string(default_profile.name));
  }
  // otherwise save default profile values
  else
      // save default profile settings
      if (!build_profile_settings_json(config, &default_profile, true, ""))
    return;

  // devices
  /*if(opt_devs_enabled)
  {
    bool extra_devs = false;
    obj = json_string("");

    for(i = 0; i < MAX_DEVICES; i++)
    {
      if(devices_enabled[i])
      {
        int startd = i;

        if(extra_devs)
          obj = json_sprintf("%s%s", json_string_value(obj), ",");

        while (i < MAX_DEVICES && devices_enabled[i + 1])
          ++i;

        obj = json_sprintf("%s%d", json_string_value(obj), startd);
        if(i > startd)
          obj = json_sprintf("%s-%d", json_string_value(obj), i);
      }
    }

    if(json_object_set(config, "devices", obj) == -1)
    {
      applog(LOG_ERR, "Error: config_parser::write_config():\n json_object_set()
  failed on devices");
      return;
    }
  }*/

  // remove-disabled
  if (opt_removedisabled)
    json_add(config, "remove-disabled", json_true());

  // write gpu settings that aren't part of profiles -- only write if gpus are
  // available
  if (nDevs) {
#ifdef HAVE_ADL
    // temp-cutoff
    for (i = 0; i < nDevs; i++)
      obj = json_sprintf("%s%s%d", ((i > 0) ? json_string_value(obj) : ""),
                         ((i > 0) ? "," : ""), gpus[i].cutofftemp);

    json_add(config, "temp-cutoff", obj);

    // temp-overheat
    for (i = 0; i < nDevs; i++)
      obj = json_sprintf("%s%s%d", ((i > 0) ? json_string_value(obj) : ""),
                         ((i > 0) ? "," : ""), gpus[i].adl.overtemp);

    json_add(config, "temp-overheat", obj);

    // temp-target
    for (i = 0; i < nDevs; i++)
      obj = json_sprintf("%s%s%d", ((i > 0) ? json_string_value(obj) : ""),
                         ((i > 0) ? "," : ""), gpus[i].adl.targettemp);

    json_add(config, "temp-target", obj);

    // reorder gpus
    if (opt_reorder)
      json_add(config, "gpu-reorder", json_true());

    // gpu-memdiff - FIXME: should be moved to pool/profile options
    for (i = 0; i < nDevs; i++)
      obj = json_sprintf("%s%s%d", ((i > 0) ? json_string_value(obj) : ""),
                         ((i > 0) ? "," : ""), (int)gpus[i].gpu_memdiff);

    json_add(config, "gpu-memdiff", obj);
#endif
  }

  // add other misc options
  // shares
  json_add(config, "shares", json_sprintf("%d", opt_shares));

#if defined(unix) || defined(__APPLE__)
  // monitor
  if (opt_stderr_cmd && *opt_stderr_cmd)
    json_add(config, "monitor", json_string(opt_stderr_cmd));
#endif // defined(unix)

  // kernel path
  if (opt_kernel_path && *opt_kernel_path) {
    // strip end /
    char *kpath = strdup(opt_kernel_path);
    if (kpath[strlen(kpath) - 1] == '/')
      kpath[strlen(kpath) - 1] = 0;

    json_add(config, "kernel-path", json_string(kpath));
  }

  // sched-time
  if (schedstart.enable)
    json_add(config, "sched-time", json_sprintf("%d:%d", schedstart.tm.tm_hour,
                                                schedstart.tm.tm_min));

  // stop-time
  if (schedstop.enable)
    json_add(config, "stop-time",
             json_sprintf("%d:%d", schedstop.tm.tm_hour, schedstop.tm.tm_min));

  // socks-proxy
  if (opt_socks_proxy && *opt_socks_proxy)
    json_add(config, "socks-proxy", json_string(opt_socks_proxy));

  // api stuff
  // api-allow
  if (opt_api_allow)
    json_add(config, "api-allow", json_string(opt_api_allow));

  // api-mcast-addr
  if (strcmp(opt_api_mcast_addr, API_MCAST_ADDR) != 0)
    json_add(config, "api-mcast-addr", json_string(opt_api_mcast_addr));

  // api-mcast-code
  if (strcmp(opt_api_mcast_code, API_MCAST_CODE) != 0)
    json_add(config, "api-mcast-code", json_string(opt_api_mcast_code));

  // api-mcast-des
  if (*opt_api_mcast_des)
    json_add(config, "api-mcast-des", json_string(opt_api_mcast_des));

  // api-description
  if (strcmp(opt_api_description, PACKAGE_STRING) != 0)
    json_add(config, "api-description", json_string(opt_api_description));

  // api-groups
  if (opt_api_groups)
    json_add(config, "api-groups", json_string(opt_api_groups));

  // add other misc bool/int options
  for (opt = opt_config_table; opt->type != OPT_END; opt++) {
    optname = strdup(opt->names);

    // ignore --pool-* and --profile-* options
    if (!strstr(optname, "--pool-") && !strstr(optname, "--profile-")) {
      // get first available long form option name
      for (p = strtok(optname, "|"); p; p = strtok(NULL, "|")) {
        // skip short options
        if (p[1] != '-')
          continue;

        // type bool
        if (opt->type & OPT_NOARG &&
            ((void *)opt->cb == (void *)opt_set_bool ||
             (void *)opt->cb == (void *)opt_set_invbool) &&
            (*(bool *)opt->u.arg ==
             ((void *)opt->cb == (void *)opt_set_bool))) {
          json_add(config, p + 2, json_true());
          break; // exit for loop... so we don't enter a duplicate value if an
                 // option has multiple names
        }
        // numeric types
        else if (opt->type & OPT_HASARG &&
                 ((void *)opt->cb_arg == (void *)set_int_0_to_9999 ||
                  (void *)opt->cb_arg == (void *)set_int_1_to_65535 ||
                  (void *)opt->cb_arg == (void *)set_int_0_to_10 ||
                  (void *)opt->cb_arg == (void *)set_int_1_to_10) &&
                 opt->desc != opt_hidden) {
          json_add(config, p + 2, json_sprintf("%d", *(int *)opt->u.arg));
          break; // exit for loop... so we don't enter a duplicate value if an
                 // option has multiple names
        }
      }
    }
  }

  json_dump_file(config, filename, JSON_PRESERVE_ORDER | JSON_INDENT(2));
}

