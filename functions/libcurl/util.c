#ifdef HAVE_LIBCURL
struct timeval nettime;

struct data_buffer {
  void *buf;
  size_t len;
};

struct upload_buffer {
  const void *buf;
  size_t len;
};

struct header_info {
  char *lp_path;
  int rolltime;
  char *reason;
  char *stratum_url;
  bool hadrolltime;
  bool canroll;
  bool hadexpire;
};

static void databuf_free(struct data_buffer *db) {
  if (db) {
    if (db->buf)
      free(db->buf);
    memset(db, 0, sizeof(*db));
  }
}

static size_t all_data_cb(const void *ptr, size_t size, size_t nmemb,
                          void *user_data) {
  struct data_buffer *db = (struct data_buffer *)user_data;
  size_t len = size * nmemb;
  size_t oldlen, newlen;
  void *newmem;
  static const unsigned char zero = 0;

  if (len > 0) {
    oldlen = db->len;
    newlen = oldlen + len;

    newmem = realloc(db->buf, newlen + 1);
    if (!newmem)
      return 0;

    db->buf = newmem;
    db->len = newlen;
    memcpy((uint8_t *)db->buf + oldlen, ptr, len);
    memcpy((uint8_t *)db->buf + newlen, &zero, 1); /* null terminate */
  }

  return len;
}

static size_t upload_data_cb(void *ptr, size_t size, size_t nmemb,
                             void *user_data) {
  struct upload_buffer *ub = (struct upload_buffer *)user_data;
  unsigned int len = size * nmemb;

  if (len > ub->len)
    len = ub->len;

  if (len > 0) {
    memcpy(ptr, ub->buf, len);
    ub->buf = (uint8_t *)ub->buf + len;
    ub->len -= len;
  }

  return len;
}

static size_t resp_hdr_cb(void *ptr, size_t size, size_t nmemb,
                          void *user_data) {
  struct header_info *hi = (struct header_info *)user_data;
  size_t remlen, slen, ptrlen = size * nmemb;
  char *rem, *val = NULL, *key = NULL;
  void *tmp;

  val = (char *)calloc(1, ptrlen);
  key = (char *)calloc(1, ptrlen);
  if (!key || !val)
    goto out;

  tmp = memchr(ptr, ':', ptrlen);
  if (!tmp || (tmp == ptr)) /* skip empty keys / blanks */
    goto out;
  slen = (uint8_t *)tmp - (uint8_t *)ptr;
  if ((slen + 1) == ptrlen) /* skip key w/ no value */
    goto out;
  memcpy(key, ptr, slen); /* store & nul term key */
  key[slen] = 0;

  rem = (char *)ptr + slen + 1; /* trim value's leading whitespace */
  remlen = ptrlen - slen - 1;
  while ((remlen > 0) && (isspace(*rem))) {
    remlen--;
    rem++;
  }

  memcpy(val, rem, remlen); /* store value, trim trailing ws */
  val[remlen] = 0;
  while ((*val) && (isspace(val[strlen(val) - 1])))
    val[strlen(val) - 1] = 0;

  if (!*val) /* skip blank value */
    goto out;

  if (opt_protocol)
    applog(LOG_DEBUG, "HTTP hdr(%s): %s", key, val);

  if (!strcasecmp("X-Roll-Ntime", key)) {
    hi->hadrolltime = true;
    if (!strncasecmp("N", val, 1))
      applog(LOG_DEBUG, "X-Roll-Ntime: N found");
    else {
      hi->canroll = true;

      /* Check to see if expire= is supported and if not, set
       * the rolltime to the default scantime */
      if (strlen(val) > 7 && !strncasecmp("expire=", val, 7)) {
        sscanf(val + 7, "%d", &hi->rolltime);
        hi->hadexpire = true;
      } else
        hi->rolltime = opt_scantime;
      applog(LOG_DEBUG, "X-Roll-Ntime expiry set to %d", hi->rolltime);
    }
  }

  if (!strcasecmp("X-Long-Polling", key)) {
    hi->lp_path = val; /* steal memory reference */
    val = NULL;
  }

  if (!strcasecmp("X-Reject-Reason", key)) {
    hi->reason = val; /* steal memory reference */
    val = NULL;
  }

  if (!strcasecmp("X-Stratum", key)) {
    hi->stratum_url = val;
    val = NULL;
  }

out:
  free(key);
  free(val);
  return ptrlen;
}

static void last_nettime(struct timeval *last) {
  rd_lock(&netacc_lock);
  last->tv_sec = nettime.tv_sec;
  last->tv_usec = nettime.tv_usec;
  rd_unlock(&netacc_lock);
}

static void set_nettime(void) {
  wr_lock(&netacc_lock);
  cgtime(&nettime);
  wr_unlock(&netacc_lock);
}

#if CURL_HAS_KEEPALIVE
static void keep_curlalive(CURL *curl) {
  const long int keepalive = 1;

  curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, keepalive);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, opt_tcp_keepalive);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, opt_tcp_keepalive);
}
#else
static void keep_curlalive(CURL *curl) {
  SOCKETTYPE sock;

  curl_easy_getinfo(curl, CURLINFO_LASTSOCKET, (long *)&sock);
  keep_sockalive(sock);
}
#endif


static int curl_debug_cb(__maybe_unused CURL *handle, curl_infotype type,
                         __maybe_unused char *data, size_t size,
                         void *userdata) {
  struct pool *pool = (struct pool *)userdata;

  switch (type) {
  case CURLINFO_HEADER_IN:
  case CURLINFO_DATA_IN:
  case CURLINFO_SSL_DATA_IN:
    pool->sgminer_pool_stats.net_bytes_received += size;
    break;
  case CURLINFO_HEADER_OUT:
  case CURLINFO_DATA_OUT:
  case CURLINFO_SSL_DATA_OUT:
    pool->sgminer_pool_stats.net_bytes_sent += size;
    break;
  case CURLINFO_TEXT:
  default:
    break;
  }
  return 0;
}

json_t *json_rpc_call(CURL *curl, char *curl_err_str, const char *url,
                      const char *userpass, const char *rpc_req, bool probe,
                      bool longpoll, int *rolltime, struct pool *pool,
                      bool share) {
  long timeout = longpoll ? (60 * 60) : 60;
  struct data_buffer all_data = {NULL, 0};
  struct header_info hi = {NULL, 0, NULL, NULL, false, false, false};
  char len_hdr[64], user_agent_hdr[128];
  struct curl_slist *headers = NULL;
  struct upload_buffer upload_data;
  json_t *val, *err_val, *res_val;
  bool probing = false;
  double byte_count;
  json_error_t err;
  int rc;

  memset(&err, 0, sizeof(err));

  /* it is assumed that 'curl' is freshly [re]initialized at this pt */

  if (probe)
    probing = !pool->probed;
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);

  // CURLOPT_VERBOSE won't write to stderr if we use CURLOPT_DEBUGFUNCTION
  curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
  curl_easy_setopt(curl, CURLOPT_DEBUGDATA, (void *)pool);
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);

  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);

  /* Shares are staggered already and delays in submission can be costly
   * so do not delay them */
  if (!opt_delaynet || share)
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, all_data_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &all_data);
  curl_easy_setopt(curl, CURLOPT_READFUNCTION, upload_data_cb);
  curl_easy_setopt(curl, CURLOPT_READDATA, &upload_data);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_str);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, resp_hdr_cb);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hi);
  curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
  if (pool->rpc_proxy) {
    curl_easy_setopt(curl, CURLOPT_PROXY, pool->rpc_proxy);
    curl_easy_setopt(curl, CURLOPT_PROXYTYPE, pool->rpc_proxytype);
  } else if (opt_socks_proxy) {
    curl_easy_setopt(curl, CURLOPT_PROXY, opt_socks_proxy);
    curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4);
  }
  if (userpass) {
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpass);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
  }
  if (longpoll)
    keep_curlalive(curl);
  curl_easy_setopt(curl, CURLOPT_POST, 1);

  if (opt_protocol)
    applog(LOG_DEBUG, "JSON protocol request:\n%s", rpc_req);

  upload_data.buf = rpc_req;
  upload_data.len = strlen(rpc_req);
  sprintf(len_hdr, "Content-Length: %lu", (unsigned long)upload_data.len);
  sprintf(user_agent_hdr, "User-Agent: %s", PACKAGE_STRING);

  headers = curl_slist_append(headers, "Content-type: application/json");
  headers = curl_slist_append(
      headers, "X-Mining-Extensions: longpoll midstate rollntime submitold");

  if (likely(global_hashrate)) {
    char ghashrate[255];

    sprintf(ghashrate, "X-Mining-Hashrate: %llu", global_hashrate);
    headers = curl_slist_append(headers, ghashrate);
  }

  headers = curl_slist_append(headers, len_hdr);
  headers = curl_slist_append(headers, user_agent_hdr);
  headers = curl_slist_append(headers, "Expect:"); /* disable Expect hdr */

  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  if (opt_delaynet) {
    /* Don't delay share submission, but still track the nettime */
    if (!share) {
      long long now_msecs, last_msecs;
      struct timeval now, last;

      cgtime(&now);
      last_nettime(&last);
      now_msecs = (long long)now.tv_sec * 1000;
      now_msecs += now.tv_usec / 1000;
      last_msecs = (long long)last.tv_sec * 1000;
      last_msecs += last.tv_usec / 1000;
      if (now_msecs > last_msecs && now_msecs - last_msecs < 250) {
        struct timespec rgtp;

        rgtp.tv_sec = 0;
        rgtp.tv_nsec = (250 - (now_msecs - last_msecs)) * 1000000;
        nanosleep(&rgtp, NULL);
      }
    }
    set_nettime();
  }

  rc = curl_easy_perform(curl);
  if (rc) {
    applog(LOG_INFO, "HTTP request failed: %s", curl_err_str);
    goto err_out;
  }

  if (!all_data.buf) {
    applog(LOG_DEBUG, "Empty data received in json_rpc_call.");
    goto err_out;
  }

  pool->sgminer_pool_stats.times_sent++;
  if (curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &byte_count) == CURLE_OK)
    pool->sgminer_pool_stats.bytes_sent += byte_count;
  pool->sgminer_pool_stats.times_received++;
  if (curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &byte_count) == CURLE_OK)
    pool->sgminer_pool_stats.bytes_received += byte_count;

  if (probing) {
    pool->probed = true;
    /* If X-Long-Polling was found, activate long polling */
    if (hi.lp_path) {
      if (pool->hdr_path != NULL)
        free(pool->hdr_path);
      pool->hdr_path = hi.lp_path;
    } else
      pool->hdr_path = NULL;
    if (hi.stratum_url) {
      pool->stratum_url = hi.stratum_url;
      hi.stratum_url = NULL;
    }
  } else {
    if (hi.lp_path) {
      free(hi.lp_path);
      hi.lp_path = NULL;
    }
    if (hi.stratum_url) {
      free(hi.stratum_url);
      hi.stratum_url = NULL;
    }
  }

  *rolltime = hi.rolltime;
  pool->sgminer_pool_stats.rolltime = hi.rolltime;
  pool->sgminer_pool_stats.hadrolltime = hi.hadrolltime;
  pool->sgminer_pool_stats.canroll = hi.canroll;
  pool->sgminer_pool_stats.hadexpire = hi.hadexpire;

  val = JSON_LOADS((const char *)all_data.buf, &err);
  if (!val) {
    applog(LOG_INFO, "JSON decode failed(%d): %s", err.line, err.text);

    if (opt_protocol)
      applog(LOG_DEBUG, "JSON protocol response:\n%s", (char *)(all_data.buf));

    goto err_out;
  }

  if (opt_protocol) {
    char *s = json_dumps(val, JSON_INDENT(3));

    applog(LOG_DEBUG, "JSON protocol response:\n%s", s);
    free(s);
  }

  /* JSON-RPC valid response returns a non-null 'result',
   * and a null 'error'.
   */
  res_val = json_object_get(val, "result");
  err_val = json_object_get(val, "error");

  if (!res_val || (err_val && !json_is_null(err_val))) {
    char *s;

    if (err_val)
      s = json_dumps(err_val, JSON_INDENT(3));
    else
      s = strdup("(unknown reason)");

    applog(LOG_INFO, "JSON-RPC call failed: %s", s);

    free(s);

    goto err_out;
  }

  if (hi.reason) {
    json_object_set_new(val, "reject-reason", json_string(hi.reason));
    free(hi.reason);
    hi.reason = NULL;
  }
  successful_connect = true;
  databuf_free(&all_data);
  curl_slist_free_all(headers);
  curl_easy_reset(curl);
  return val;

err_out:
  databuf_free(&all_data);
  curl_slist_free_all(headers);
  curl_easy_reset(curl);
  if (!successful_connect)
    applog(LOG_DEBUG, "Failed to connect in json_rpc_call");
  curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
  return NULL;
}

#endif

