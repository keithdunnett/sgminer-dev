
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
#include "functions/config/config_parser.h"
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

/**************************************
 * Remote Config Functions (Curl Only)
 **************************************/
#ifdef HAVE_LIBCURL
struct remote_config {
  const char *filename;
  FILE *stream;
};

// curl file data write callback
static size_t fetch_remote_config_cb(void *buffer, size_t size, size_t nmemb,
                                     void *stream) {
  struct remote_config *out = (struct remote_config *)stream;

  // create file if not created
  if (out && !out->stream) {
    if (!(out->stream = fopen(out->filename, "w+")))
      return -1;
  }

  return fwrite(buffer, size, nmemb, out->stream);
}

// download remote config file - return filename on success or NULL on failure
static char *fetch_remote_config(const char *url) {
  CURL *curl;
  CURLcode res;
  char *p;
  struct remote_config file = {"", NULL};

  // get filename out of url
  if ((p = (char *)strrchr(url, '/')) == NULL) {
    applog(LOG_ERR, "Fetch remote file failed: Invalid URL");
    return NULL;
  }

  file.filename = p + 1;

  // check for empty filename
  if (file.filename[0] == '\0') {
    applog(LOG_ERR, "Fetch remote file failed: Invalid Filename");
    return NULL;
  }

  // init curl
  if ((curl = curl_easy_init()) == NULL) {
    applog(LOG_ERR, "Fetch remote file failed: curl init failed.");
    return NULL;
  }

  // https stuff - skip verification we just want the data
  if (strstr(url, "https") != NULL)
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

  // set url
  curl_easy_setopt(curl, CURLOPT_URL, url);
  // set write callback and fileinfo
  curl_easy_setopt(curl, CURLOPT_FAILONERROR,
                   1); // fail on 404 or other 4xx http codes
  curl_easy_setopt(curl, CURLOPT_TIMEOUT,
                   30); // timeout after 30 secs to prevent being stuck
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file); // stream to write data to
  curl_easy_setopt(
      curl, CURLOPT_WRITEFUNCTION,
      fetch_remote_config_cb); // callback function to write to config file

  if ((res = curl_easy_perform(curl)) != CURLE_OK)
    applog(LOG_ERR, "Fetch remote file failed: %s", curl_easy_strerror(res));

  if (file.stream)
    fclose(file.stream);

  curl_easy_cleanup(curl);

  return (char *)((res == CURLE_OK) ? file.filename : NULL);
}


extern char *load_config_withlibcurl(const char *arg, const char *parentkey, void __maybe_unused *unused) {
  json_error_t err;
  json_t *config;

  int retry = opt_remoteconf_retry;
  const char *url;

  // if detected as url
  if ((strstr(arg, "http://") != NULL) || (strstr(arg, "https://") != NULL) ||
      (strstr(arg, "ftp://") != NULL)) {
    url = strdup(arg);

    do {
      // wait for next retry
      if (retry < opt_remoteconf_retry) {
        sleep(opt_remoteconf_wait);
      }

      // download config file locally and reset arg to it so we can parse it
      if ((arg = fetch_remote_config(url)) != NULL) {
        break;
      }

      --retry;
    } while (retry);

    // file not downloaded... abort
    if (arg == NULL) {
      // if we should use last downloaded copy...
      if (opt_remoteconf_usecache) {
        char *p;

        // extract filename out of url
        if ((p = (char *)strrchr(url, '/')) == NULL) {
          quit(1, "%s: invalid URL.", url);
        }

        arg = p + 1;
      } else {
        quit(1, "%s: unable to download config file.", url);
      }
    }
  }

  // most likely useless but leaving it here for now...
  if (!cnfbuf) {
    cnfbuf = strdup(arg);
  }

  // no need to restrict the number of includes... if it causes problems,
  // restore it later
  /*if(++include_count > JSON_MAX_DEPTH)
    return JSON_MAX_DEPTH_ERR;
  */

  // check if the file exists
  if (access(arg, F_OK) == -1) {
    quit(1, "%s: file not found.", arg);
  }

#if JANSSON_MAJOR_VERSION > 1
  config = json_load_file(arg, 0, &err);
#else
  config = json_load_file(arg, &err);
#endif

  // if json root is not an object, error out
  if (!json_is_object(config)) {
    return set_last_json_error("Error: JSON decode of file \"%s\" failed:\n %s",
                               arg, err.text);
  }

  config_loaded = true;

  /* Parse the config now, so we can override it.  That can keep pointers
  * so don't free config object. */
  return parse_config(config, "", parentkey, true, -1);
}


#endif
