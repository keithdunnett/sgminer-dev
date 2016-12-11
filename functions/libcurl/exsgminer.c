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
#include <curses.h>

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
#include "algorithm/ethash.h"
#include "pool.h"
#include <ccan/opt/opt.h>
//#ifdef HAVE_ADL
//#include "adl.h"
//#endif

#if defined(unix) || defined(__APPLE__)
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#endif

char eth_getwork_rpc[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getWork\",\"params\":[],\"id\":1}";
char eth_gethighestblock_rpc[] = "{\"jsonrpc\":\"2.0\",\"method\":\"eth_"
                                       "getBlockByNumber\",\"params\":["
                                       "\"latest\", false],\"id\":1}";



#ifdef HAVE_LIBCURL
extern pthread_mutex_t stats_lock;
extern bool opt_morenotices;
extern bool opt_lowmem;
extern bool curses_active;
extern bool opt_quiet;
extern bool opt_realquiet;
extern int opt_vote;
extern int staged_rollable;
extern char *getwork_req;
#define QUIET (opt_quiet || opt_realquiet)
extern int total_work;
extern unsigned int work_block;
extern bool jobj_binary(const json_t *obj, const char *key, void *buf,
                        size_t buflen, bool required) {
  const char *hexstr;
  json_t *tmp;

  tmp = json_object_get(obj, key);
  if (unlikely(!tmp)) {
    if (unlikely(required))
      if (opt_morenotices)
        applog(LOG_ERR, "JSON key '%s' not found", key);
    return false;
  }
  hexstr = json_string_value(tmp);
  if (unlikely(!hexstr)) {
    applog(LOG_ERR, "JSON key '%s' is not a string", key);
    return false;
  }
  if (!hex2bin((unsigned char *)buf, hexstr, buflen))
    return false;

  return true;
}
#endif

#ifdef HAVE_LIBCURL
/* Process transactions with GBT by storing the binary value of the first
 * transaction, and the hashes of the remaining transactions since these
 * remain constant with an altered coinbase when generating work. Must be
 * entered under gbt_lock */
extern bool __build_gbt_txns(struct pool *pool, json_t *res_val) {
  json_t *txn_array;
  bool ret = false;
  size_t cal_len;
  int i;

  free(pool->txn_hashes);
  pool->txn_hashes = NULL;
  pool->gbt_txns = 0;

  txn_array = json_object_get(res_val, "transactions");
  if (!json_is_array(txn_array))
    goto out;

  ret = true;
  pool->gbt_txns = json_array_size(txn_array);
  if (!pool->gbt_txns)
    goto out;

  pool->txn_hashes = (unsigned char *)calloc(32 * (pool->gbt_txns + 1), 1);
  if (unlikely(!pool->txn_hashes))
    quit(1, "Failed to calloc txn_hashes in __build_gbt_txns");

  for (i = 0; i < pool->gbt_txns; i++) {
    json_t *txn_val = json_object_get(json_array_get(txn_array, i), "data");
    const char *txn = json_string_value(txn_val);
    size_t txn_len = strlen(txn);
    unsigned char *txn_bin;

    cal_len = txn_len;
    align_len(&cal_len);
    txn_bin = (unsigned char *)calloc(cal_len, 1);
    if (unlikely(!txn_bin))
      quit(1, "Failed to calloc txn_bin in __build_gbt_txns");
    if (unlikely(!hex2bin(txn_bin, txn, txn_len / 2)))
      quit(1, "Failed to hex2bin txn_bin");

    gen_hash(txn_bin, txn_len / 2, pool->txn_hashes + (32 * i));
    free(txn_bin);
  }
out:
  return ret;
}

extern unsigned char *__gbt_merkleroot(struct pool *pool) {
  unsigned char *merkle_hash;
  int i, txns;

  merkle_hash = (unsigned char *)calloc(32 * (pool->gbt_txns + 2), 1);
  if (unlikely(!merkle_hash))
    quit(1, "Failed to calloc merkle_hash in __gbt_merkleroot");

  gen_hash(pool->coinbase, pool->coinbase_len, merkle_hash);

  if (pool->gbt_txns)
    memcpy(merkle_hash + 32, pool->txn_hashes, pool->gbt_txns * 32);

  txns = pool->gbt_txns + 1;
  while (txns > 1) {
    if (txns % 2) {
      memcpy(&merkle_hash[txns * 32], &merkle_hash[(txns - 1) * 32], 32);
      txns++;
    }
    for (i = 0; i < txns; i += 2) {
      unsigned char hashout[32];

      gen_hash(merkle_hash + (i * 32), 64, hashout);
      memcpy(merkle_hash + (i / 2 * 32), hashout, 32);
    }
    txns /= 2;
  }
  return merkle_hash;
}

extern bool work_decode(struct pool *pool, struct work *work, json_t *val);


extern void update_gbt(struct pool *pool) {
  int rolltime;
  json_t *val;
  CURL *curl;
  char curl_err_str[CURL_ERROR_SIZE];

  curl = curl_easy_init();
  if (unlikely(!curl))
    quit(1, "CURL initialisation failed in update_gbt");

  val = json_rpc_call(curl, curl_err_str, pool->rpc_url, pool->rpc_userpass,
                      pool->rpc_req, true, false, &rolltime, pool, false);

  if (val) {
    struct work *work = make_work();
    bool rc = work_decode(pool, work, val);

    total_getworks++;
    pool->getwork_requested++;
    if (rc) {
      applog(LOG_DEBUG, "Successfully retrieved and updated GBT from %s",
             get_pool_name(pool));
      cgtime(&pool->tv_idle);
      if (pool == current_pool())
        opt_work_update = true;
    } else {
      applog(LOG_DEBUG,
             "Successfully retrieved but FAILED to decipher GBT from %s",
             get_pool_name(pool));
    }
    json_decref(val);
    free_work(work);
  } else {
    applog(LOG_DEBUG, "FAILED to update GBT from %s", get_pool_name(pool));
  }
  curl_easy_cleanup(curl);
}

#endif

#ifdef HAVE_LIBCURL

extern void gen_gbt_work(struct pool *pool, struct work *work) {
  unsigned char *merkleroot;
  struct timeval now;
  uint64_t nonce2le;

  cgtime(&now);
  if (now.tv_sec - pool->tv_lastwork.tv_sec > 60)
    update_gbt(pool);

  cg_wlock(&pool->gbt_lock);
  nonce2le = htole64(pool->nonce2);
  memcpy(pool->coinbase + pool->nonce2_offset, &nonce2le, pool->n2size);
  pool->nonce2++;
  cg_dwlock(&pool->gbt_lock);
  merkleroot = __gbt_merkleroot(pool);

  memcpy(work->data, &pool->gbt_version, 4);
  memcpy(work->data + 4, pool->previousblockhash, 32);
  memcpy(work->data + 4 + 32 + 32, &pool->curtime, 4);
  memcpy(work->data + 4 + 32 + 32 + 4, &pool->gbt_bits, 4);

  memcpy(work->target, pool->gbt_target, 32);

  work->coinbase = bin2hex(pool->coinbase, pool->coinbase_len);

  /* For encoding the block data on submission */
  work->gbt_txns = pool->gbt_txns + 1;

  if (pool->gbt_workid)
    work->job_id = strdup(pool->gbt_workid);
  cg_runlock(&pool->gbt_lock);

  flip32(work->data + 4 + 32, merkleroot);
  free(merkleroot);
  memset(work->data + 4 + 32 + 32 + 4 + 4, 0, 4 + 48); /* nonce + padding */

  if (opt_debug) {
    char *header = bin2hex(work->data, 128);

    applog(LOG_DEBUG, "Generated GBT header %s", header);
    applog(LOG_DEBUG, "Work coinbase %s", work->coinbase);
    free(header);
  }

  if (pool->algorithm.calc_midstate)
    pool->algorithm.calc_midstate(work);
  local_work++;
  work->pool = pool;
  work->gbt = true;
  work->id = total_work++;
  work->longpoll = false;
  work->getwork_mode = GETWORK_MODE_GBT;
  work->work_block = work_block;
  /* Nominally allow a driver to ntime roll 60 seconds */
  work->drv_rolllimit = 0;
  // calc_diff(work, 0);
  cgtime(&work->tv_staged);
}

extern bool gbt_decode(struct pool *pool, json_t *res_val) {
  const char *previousblockhash;
  const char *target;
  const char *coinbasetxn;
  const char *longpollid;
  unsigned char hash_swap[32];
  int expires;
  int version;
  int curtime;
  bool submitold;
  const char *bits;
  const char *workid;
  size_t cbt_len, orig_len;
  uint8_t *extra_len;
  size_t cal_len;

  previousblockhash =
      json_string_value(json_object_get(res_val, "previousblockhash"));
  target = json_string_value(json_object_get(res_val, "target"));
  coinbasetxn = json_string_value(
      json_object_get(json_object_get(res_val, "coinbasetxn"), "data"));
  longpollid = json_string_value(json_object_get(res_val, "longpollid"));
  expires = json_integer_value(json_object_get(res_val, "expires"));
  version = json_integer_value(json_object_get(res_val, "version"));
  curtime = json_integer_value(json_object_get(res_val, "curtime"));
  submitold = json_is_true(json_object_get(res_val, "submitold"));
  bits = json_string_value(json_object_get(res_val, "bits"));
  workid = json_string_value(json_object_get(res_val, "workid"));

  if (!previousblockhash || !target || !coinbasetxn || !longpollid ||
      !expires || !version || !curtime || !bits) {
    applog(LOG_ERR, "JSON failed to decode GBT");
    return false;
  }

  applog(LOG_DEBUG, "previousblockhash: %s", previousblockhash);
  applog(LOG_DEBUG, "target: %s", target);
  applog(LOG_DEBUG, "coinbasetxn: %s", coinbasetxn);
  applog(LOG_DEBUG, "longpollid: %s", longpollid);
  applog(LOG_DEBUG, "expires: %d", expires);
  applog(LOG_DEBUG, "version: %d", version);
  applog(LOG_DEBUG, "curtime: %d", curtime);
  applog(LOG_DEBUG, "submitold: %s", submitold ? "true" : "false");
  applog(LOG_DEBUG, "bits: %s", bits);
  if (workid)
    applog(LOG_DEBUG, "workid: %s", workid);

  cg_wlock(&pool->gbt_lock);
  free(pool->coinbasetxn);
  pool->coinbasetxn = strdup(coinbasetxn);
  cbt_len = strlen(pool->coinbasetxn) / 2;
  /* We add 8 bytes of extra data corresponding to nonce2 */
  pool->n2size = 8;
  pool->coinbase_len = cbt_len + pool->n2size;
  cal_len = pool->coinbase_len + 1;
  align_len(&cal_len);
  free(pool->coinbase);
  pool->coinbase = (unsigned char *)calloc(cal_len, 1);
  if (unlikely(!pool->coinbase))
    quit(1, "Failed to calloc pool coinbase in gbt_decode");
  hex2bin(pool->coinbase, pool->coinbasetxn, 42);
  extra_len = (uint8_t *)(pool->coinbase + 41);
  orig_len = *extra_len;
  hex2bin(pool->coinbase + 42, pool->coinbasetxn + 84, orig_len);
  *extra_len += pool->n2size;
  hex2bin(pool->coinbase + 42 + *extra_len,
          pool->coinbasetxn + 84 + (orig_len * 2), cbt_len - orig_len - 42);
  pool->nonce2_offset = orig_len + 42;

  free(pool->longpollid);
  pool->longpollid = strdup(longpollid);
  free(pool->gbt_workid);
  if (workid)
    pool->gbt_workid = strdup(workid);
  else
    pool->gbt_workid = NULL;

  hex2bin(hash_swap, previousblockhash, 32);
  swap256(pool->previousblockhash, hash_swap);

  hex2bin(hash_swap, target, 32);
  swab256(pool->gbt_target, hash_swap);

  pool->gbt_expires = expires;
  pool->gbt_version = htobe32(version);
  pool->curtime = htobe32(curtime);
  pool->submit_old = submitold;

  hex2bin((unsigned char *)&pool->gbt_bits, bits, 4);

  __build_gbt_txns(pool, res_val);
  cg_wunlock(&pool->gbt_lock);

  return true;
}

#endif
#ifdef HAVE_LIBCURL


extern bool getwork_decode(json_t *res_val, struct work *work) {
  size_t worklen = 128;
  if (work->pool->algorithm.type == ALGO_CRE)
    worklen = 168;
  else if (work->pool->algorithm.type == ALGO_DECRED) {
    worklen = 192;
    uint16_t vote = (uint16_t)(opt_vote << 1) | 1;
    memcpy(&work->data[100], &vote, 2);
    // some random extradata to make it unique
    ((uint32_t *)work->data)[36] = (rand() * 4);
    ((uint32_t *)work->data)[37] = (rand() * 4) << 8 | work->thr_id;
  }
  if (unlikely(!jobj_binary(res_val, "data", work->data, worklen, true))) {
    if (opt_morenotices)
      applog(LOG_ERR, "%s: JSON inval data",
             isnull(get_pool_name(work->pool), ""));
    return false;
  }

  if (work->pool->algorithm.type == ALGO_CRE ||
      work->pool->algorithm.type == ALGO_SCRYPT) {
    if (!jobj_binary(res_val, "midstate", work->midstate,
                     sizeof(work->midstate), false)) {
      // Calculate it ourselves
      if (opt_morenotices) {
        applog(LOG_DEBUG, "%s: Calculating midstate locally",
               isnull(get_pool_name(work->pool), ""));
      }
      if (work->pool->algorithm.calc_midstate)
        work->pool->algorithm.calc_midstate(work);
    }
  }

  if (unlikely(!jobj_binary(res_val, "target", work->target,
                            sizeof(work->target), true))) {
    if (opt_morenotices)
      applog(LOG_ERR, "%s: JSON inval target",
             isnull(get_pool_name(work->pool), ""));
    return false;
  }
  return true;
}

/* Returns whether the pool supports local work generation or not. */
extern bool pool_localgen(struct pool *pool) {
  return (pool->has_stratum || pool->has_gbt);
}

extern bool work_decode(struct pool *pool, struct work *work, json_t *val) {
  json_t *res_val = json_object_get(val, "result");
  bool ret = false;

  cgtime(&pool->tv_lastwork);
  if (!res_val || json_is_null(res_val)) {
    applog(LOG_ERR, "JSON Failed to decode result");
    goto out;
  }

  work->pool = pool;

  if (pool->has_gbt) {
    if (unlikely(!gbt_decode(pool, res_val)))
      goto out;
    work->gbt = true;
    ret = true;
    goto out;
  } else if (unlikely(!getwork_decode(res_val, work)))
    goto out;

  memset(work->hash, 0, sizeof(work->hash));

  cgtime(&work->tv_staged);

  ret = true;

out:
  return ret;
}

bool parse_diff_ethash(unsigned char *Target, char *TgtStr);
extern bool work_decode_eth(struct pool *pool, struct work *work, json_t *val,
                            json_t *ethval2) {
  //      int i;
  bool ret = false;
  uint8_t EthWork[32], SeedHash[32], Target[32];
  //      char *EthWorkStr, *SeedHashStr, *TgtStr, *BlockHeightStr, *NetDiffStr,
  //          FinalNetDiffStr[65];
  char *EthWorkStr, *SeedHashStr, *TgtStr;

  cgtime(&pool->tv_lastwork);

  json_t *res_arr = json_object_get(val, "result");
  if (json_is_null(res_arr))
    return false;

  EthWorkStr = (char *const)json_string_value(json_array_get(res_arr, 0));

  SeedHashStr = (char *const)json_string_value(json_array_get(res_arr, 1));

  TgtStr = (char *const)json_string_value(json_array_get(res_arr, 2));

  if (EthWorkStr == NULL || SeedHashStr == NULL || TgtStr == NULL)
    goto out;

  if (!hex2bin(EthWork, EthWorkStr + 2, 32))
    goto out;

  if (!hex2bin(SeedHash, SeedHashStr + 2, 32))
    goto out;

  if (!parse_diff_ethash(Target, TgtStr))
    goto out;

  /*
     BlockHeightStr = json_string_value(json_object_get(res2_obj, "number"));

     if(!BlockHeightStr) return(false);

     for(i = 0; BlockHeightStr[i]; ++i)
     {
     if(i == 1) continue;
     if(!isxdigit(BlockHeightStr[i])) return(false);

     }

     NetDiffStr = json_string_value(json_object_get(res2_obj, "difficulty"));

     if(!NetDiffStr) return(false);

     for(i = 0; NetDiffStr[i]; ++i)
     {
     if(i == 1) continue;
     if(!isxdigit(NetDiffStr[i])) return(false);

     }

     if(NetDiffStr[1] == 'x') work->network_diff = strtoull(NetDiffStr + 2,
     NULL);

     if(strlen(NetDiffStr) != 66)
     {
     char NewNetDiffStr[65];
     uint32_t PadLen = 66 - strlen(NetDiffStr);

     memset(NewNetDiffStr, '0', PadLen);
     memcpy(NewNetDiffStr + PadLen, NetDiffStr + 2, strlen(NetDiffStr) - 2);
     NewNetDiffStr[64] = 0x00;

     if(!hex2bin(FinalNetDiffStr, NewNetDiffStr, 32UL)) return(false);
     }
     else if(!hex2bin(FinalNetDiffStr, NetDiffStr + 2, 32UL)) return(false);
   */

  cg_wlock(&pool->data_lock);
  if (memcmp(pool->SeedHash, SeedHash, 32)) {
    pool->EpochNumber = EthCalcEpochNumber(SeedHash);
    memcpy(pool->SeedHash, SeedHash, 32);
  }
  pool->diff1 = 0;
  work->network_diff = pool->diff1;
  memcpy(work->seedhash, pool->SeedHash, 32);
  work->EpochNumber = pool->EpochNumber;
  cg_wunlock(&pool->data_lock);

  memcpy(work->data, EthWork, 32);
  swab256(work->target, Target);

  // work->network_diff = eth2pow256 / le256todouble(FinalNetDiffStr);
  cgtime(&work->tv_staged);
  ret = true;

out:
  return ret;
}

#endif


#ifdef HAVE_LIBCURL

extern void text_print_status(int thr_id) {
  struct cgpu_info *cgpu;
  char logline[256];

  cgpu = get_thr_cgpu(thr_id);
  if (cgpu) {
    get_statline(logline, sizeof(logline), cgpu);
    printf("%s\n", logline);
  }
}

extern void print_status(int thr_id) {
  if (!curses_active)
    text_print_status(thr_id);
}

extern bool submit_upstream_work(struct work *work, CURL *curl,
                                 char *curl_err_str, bool resubmit) {
  char *hexstr = NULL;
  json_t *val, *res, *err;
  char *s;
  bool rc = false;
  int thr_id = work->thr_id;
  struct cgpu_info *cgpu;
  struct pool *pool = work->pool;
  int rolltime;
  struct timeval tv_submit, tv_submit_reply;
  char hashshow[64 + 4] = "";
  char worktime[200] = "";
  struct timeval now;
  double dev_runtime;

  cgpu = get_thr_cgpu(thr_id);

  if (work->pool->algorithm.type == ALGO_ETHASH) {
    s = (char *)malloc(sizeof(char) * (128 + 16 + 512));
    uint64_t tmp = bswap_64(work->Nonce);
    char *ASCIIMixHash = bin2hex(work->mixhash, 32);
    char *ASCIIPoWHash = bin2hex(work->data, 32);
    char *ASCIINonce = bin2hex((const unsigned char *)&tmp, 8);
    snprintf(s, 128 + 16 + 512,
             "{\"jsonrpc\":\"2.0\", \"method\":\"eth_submitWork\", "
             "\"params\":[\"0x%s\", \"0x%s\", \"0x%s\"],\"id\":1}",
             ASCIINonce, ASCIIPoWHash, ASCIIMixHash);

    applog(LOG_DEBUG, "eth_submitWork: {\"jsonrpc\":\"2.0\", "
                      "\"method\":\"eth_submitWork\", \"params\":[\"0x%s\", "
                      "\"0x%s\", \"0x%s\"],\"id\":1}",
           ASCIINonce, ASCIIPoWHash, ASCIIMixHash);

    free(ASCIINonce);
    free(ASCIIMixHash);
    free(ASCIIPoWHash);
  } else {
    if (work->pool->algorithm.type == ALGO_DECRED) {
      endian_flip180(work->data, work->data);
    } else if (work->pool->algorithm.type == ALGO_CRE) {
      endian_flip168(work->data, work->data);
    } else {
      endian_flip128(work->data, work->data);
    }

    /* build hex string - Make sure to restrict to 80 bytes for Neoscrypt */
    int datasize = 128;
    if (work->pool->algorithm.type == ALGO_NEOSCRYPT)
      datasize = 80;
    else if (work->pool->algorithm.type == ALGO_CRE)
      datasize = 168;
    else if (work->pool->algorithm.type == ALGO_DECRED) {
      datasize = 192;
      ((uint32_t *)work->data)[45] = 0x80000001UL;
      ((uint32_t *)work->data)[46] = 0;
      ((uint32_t *)work->data)[47] = 0x000005a0UL;
    }

    hexstr = bin2hex(work->data, datasize);

    /* build JSON-RPC request */
    if (work->gbt) {
      char *gbt_block, *varint;
      unsigned char data[80];

      flip80(data, work->data);
      gbt_block = bin2hex(data, 80);

      if (work->gbt_txns < 0xfd) {
        uint8_t val = work->gbt_txns;

        varint = bin2hex((const unsigned char *)&val, 1);
      } else if (work->gbt_txns <= 0xffff) {
        uint16_t val = htole16(work->gbt_txns);

        gbt_block = (char *)realloc_strcat(gbt_block, "fd");
        varint = bin2hex((const unsigned char *)&val, 2);
      } else {
        uint32_t val = htole32(work->gbt_txns);

        gbt_block = (char *)realloc_strcat(gbt_block, "fe");
        varint = bin2hex((const unsigned char *)&val, 4);
      }
      gbt_block = (char *)realloc_strcat(gbt_block, varint);
      free(varint);
      gbt_block = (char *)realloc_strcat(gbt_block, work->coinbase);

      s = strdup("{\"id\": 0, \"method\": \"submitblock\", \"params\": [\"");
      s = (char *)realloc_strcat(s, gbt_block);
      if (work->job_id) {
        s = (char *)realloc_strcat(s, "\", {\"workid\": \"");
        s = (char *)realloc_strcat(s, work->job_id);
        s = (char *)realloc_strcat(s, "\"}]}");
      } else
        s = (char *)realloc_strcat(s, "\", {}]}");
      free(gbt_block);
    } else {
      s = strdup("{\"method\": \"getwork\", \"params\": [ \"");
      s = (char *)realloc_strcat(s, hexstr);
      s = (char *)realloc_strcat(s, "\" ], \"id\":1}");
    }
    applog(LOG_DEBUG, "DBG: sending %s submit RPC call: %s", pool->rpc_url, s);
    s = (char *)realloc_strcat(s, "\n");

    cgtime(&tv_submit);
    /* issue JSON-RPC request */
    val = json_rpc_call(curl, curl_err_str, pool->rpc_url, pool->rpc_userpass,
                        s, false, false, &rolltime, pool, true);
    cgtime(&tv_submit_reply);
    free(s);
    if (unlikely(!val)) {
      applog(LOG_INFO, "submit_upstream_work json_rpc_call failed");
      if (!pool_tset(pool, &pool->submit_fail)) {
        total_ro++;
        pool->remotefail_occasions++;
        if (opt_lowmem) {
          applog(LOG_WARNING, "%s communication failure, discarding shares",
                 get_pool_name(pool));
          goto out;
        }
        applog(LOG_WARNING, "%s communication failure, caching submissions",
               get_pool_name(pool));
      }
      cgsleep_ms(5000);
      goto out;
    } else if (pool_tclear(pool, &pool->submit_fail))
      applog(LOG_WARNING, "%s communication resumed, submitting work",
             get_pool_name(pool));

    res = json_object_get(val, "result");
    err = json_object_get(val, "error");

    if (!QUIET) {
      show_hash(work, hashshow);

      if (opt_worktime) {
        char workclone[20];
        struct tm *tm, tm_getwork, tm_submit_reply;
        double getwork_time = tdiff((struct timeval *)&(work->tv_getwork_reply),
                                    (struct timeval *)&(work->tv_getwork));
        double getwork_to_work =
            tdiff((struct timeval *)&(work->tv_work_start),
                  (struct timeval *)&(work->tv_getwork_reply));
        double work_time = tdiff((struct timeval *)&(work->tv_work_found),
                                 (struct timeval *)&(work->tv_work_start));
        double work_to_submit =
            tdiff(&tv_submit, (struct timeval *)&(work->tv_work_found));
        double submit_time = tdiff(&tv_submit_reply, &tv_submit);
        int diffplaces = 3;

        time_t tmp_time = work->tv_getwork.tv_sec;
        tm = localtime(&tmp_time);
        memcpy(&tm_getwork, tm, sizeof(struct tm));
        tmp_time = tv_submit_reply.tv_sec;
        tm = localtime(&tmp_time);
        memcpy(&tm_submit_reply, tm, sizeof(struct tm));

        if (work->clone) {
          snprintf(workclone, sizeof(workclone), "C:%1.3f",
                   tdiff((struct timeval *)&(work->tv_cloned),
                         (struct timeval *)&(work->tv_getwork_reply)));
        } else
          strcpy(workclone, "O");

        if (work->work_difficulty < 1)
          diffplaces = 6;

        snprintf(worktime, sizeof(worktime),
                 " <-%08lx.%08lx M:%c D:%1.*f G:%02d:%02d:%02d:%1.3f %s "
                 "(%1.3f) W:%1.3f (%1.3f) S:%1.3f R:%02d:%02d:%02d",
                 (unsigned long)be32toh(*(uint32_t *)&(work->data[32])),
                 (unsigned long)be32toh(*(uint32_t *)&(work->data[28])),
                 work->getwork_mode, diffplaces, work->work_difficulty,
                 tm_getwork.tm_hour, tm_getwork.tm_min, tm_getwork.tm_sec,
                 getwork_time, workclone, getwork_to_work, work_time,
                 work_to_submit, submit_time, tm_submit_reply.tm_hour,
                 tm_submit_reply.tm_min, tm_submit_reply.tm_sec);
      }
    }
  }

  share_result(val, res, err, work, hashshow, resubmit, worktime);

  if (cgpu->dev_start_tv.tv_sec == 0)
    dev_runtime = total_secs;
  else {
    cgtime(&now);
    dev_runtime = tdiff(&now, &(cgpu->dev_start_tv));
  }

  if (dev_runtime < 1.0)
    dev_runtime = 1.0;

  cgpu->utility = cgpu->accepted / dev_runtime * 60;

  if (!opt_realquiet)
    print_status(thr_id);
  if (!want_per_device_stats) {
    char logline[256];

    get_statline(logline, sizeof(logline), cgpu);
    applog(LOG_INFO, "%s", logline);
  }

  json_decref(val);

  rc = true;
out:
  free(hexstr);
  return rc;
}
extern bool get_upstream_work(struct work *work, CURL *curl,
                              char *curl_err_str) {
  struct pool *pool = work->pool;
  struct sgminer_pool_stats *pool_stats = &(pool->sgminer_pool_stats);
  struct timeval tv_elapsed;
  json_t *val, *ethval2;
  bool rc = false;
  char *url;

  applog(LOG_DEBUG, "DBG: sending %s get RPC call: %s", pool->rpc_url,
         pool->rpc_req);

  url = pool->rpc_url;

  cgtime(&work->tv_getwork);

  if (pool->algorithm.type == ALGO_ETHASH) {
    pool->rpc_req = (char *const)eth_getwork_rpc;

    val = json_rpc_call(curl, curl_err_str, url, pool->rpc_userpass,
                        pool->rpc_req, false, false, &work->rolltime, pool,
                        false);

    ethval2 = json_rpc_call(curl, curl_err_str, pool->rpc_url,
                            pool->rpc_userpass, eth_gethighestblock_rpc, false,
                            false, &work->rolltime, pool, false);

  } else {
    val = json_rpc_call(curl, curl_err_str, url, pool->rpc_userpass,
                        pool->rpc_req, false, false, &work->rolltime, pool,
                        false);
  }

  // WARNING: if ethval2 is NULL, it'll slip in here.
  if (likely(val)) {
    rc = (pool->algorithm.type == ALGO_ETHASH)
             ? work_decode_eth(pool, work, val, ethval2)
             : work_decode(pool, work, val);
    if (unlikely(!rc))
      applog(LOG_DEBUG, "Failed to decode work in get_upstream_work");
  } else
    applog(LOG_DEBUG, "Failed json_rpc_call in get_upstream_work");

  cgtime(&work->tv_getwork_reply);
  timersub(&(work->tv_getwork_reply), &(work->tv_getwork), &tv_elapsed);
  pool_stats->getwork_wait_rolling +=
      ((double)tv_elapsed.tv_sec + ((double)tv_elapsed.tv_usec / 1000000)) *
      0.63;
  pool_stats->getwork_wait_rolling /= 1.63;

  timeradd(&tv_elapsed, &(pool_stats->getwork_wait),
           &(pool_stats->getwork_wait));
  if (timercmp(&tv_elapsed, &(pool_stats->getwork_wait_max), >)) {
    pool_stats->getwork_wait_max.tv_sec = tv_elapsed.tv_sec;
    pool_stats->getwork_wait_max.tv_usec = tv_elapsed.tv_usec;
  }
  if (timercmp(&tv_elapsed, &(pool_stats->getwork_wait_min), <)) {
    pool_stats->getwork_wait_min.tv_sec = tv_elapsed.tv_sec;
    pool_stats->getwork_wait_min.tv_usec = tv_elapsed.tv_usec;
  }
  pool_stats->getwork_calls++;

  work->pool = pool;
  work->longpoll = false;
  work->getwork_mode = GETWORK_MODE_POOL;
  calc_diff(work, 0);
  total_getworks++;
  pool->getwork_requested++;

  if (likely(val))
    json_decref(val);

  return rc;
}
#endif /* HAVE_LIBCURL */
#ifdef HAVE_LIBCURL
/* Called with pool_lock held. Recruit an extra curl if none are available for
 * this pool. */
extern void recruit_curl(struct pool *pool) {
  struct curl_ent *ce = (struct curl_ent *)calloc(sizeof(struct curl_ent), 1);

  if (unlikely(!ce))
    quit(1, "Failed to calloc in recruit_curl");

  ce->curl = curl_easy_init();
  if (unlikely(!ce->curl))
    quit(1, "Failed to init in recruit_curl");

  list_add(&ce->node, &pool->curlring);
  pool->curls++;
}

/* Grab an available curl if there is one. If not, then recruit extra curls
 * unless we are in a submit_fail situation, or we have opt_delaynet enabled
 * and there are already 5 curls in circulation. Limit total number to the
 * number of mining threads per pool as well to prevent blasting a pool during
 * network delays/outages. */
extern struct curl_ent *pop_curl_entry(struct pool *pool) {
  int curl_limit = opt_delaynet ? 5 : (mining_threads + opt_queue) * 2;
  bool recruited = false;
  struct curl_ent *ce;

  mutex_lock(&pool->pool_lock);
retry:
  if (!pool->curls) {
    recruit_curl(pool);
    recruited = true;
  } else if (list_empty(&pool->curlring)) {
    if (pool->curls >= curl_limit) {
      pthread_cond_wait(&pool->cr_cond, &pool->pool_lock);
      goto retry;
    } else {
      recruit_curl(pool);
      recruited = true;
    }
  }
  ce = list_entry(pool->curlring.next, struct curl_ent *, node);

  list_del(&ce->node);
  mutex_unlock(&pool->pool_lock);

  if (recruited)
    applog(LOG_DEBUG, "Recruited curl for %s", get_pool_name(pool));
  return ce;
}

extern void push_curl_entry(struct curl_ent *ce, struct pool *pool) {
  mutex_lock(&pool->pool_lock);
  list_add_tail(&ce->node, &pool->curlring);
  cgtime(&ce->tv);
  pthread_cond_broadcast(&pool->cr_cond);
  mutex_unlock(&pool->pool_lock);
}
#endif

#ifdef HAVE_LIBCURL
extern void *submit_work_thread(void *userdata) {
  struct work *work = (struct work *)userdata;
  struct pool *pool = work->pool;
  bool resubmit = false;
  struct curl_ent *ce;

  pthread_detach(pthread_self());

  RenameThread("SubmitWork");

  applog(LOG_DEBUG, "Creating extra submit work thread");

  ce = pop_curl_entry(pool);
  /* submit solution to bitcoin via JSON-RPC */
  while (!submit_upstream_work(work, ce->curl, ce->curl_err_str, resubmit)) {
    if (opt_lowmem) {
      applog(LOG_NOTICE, "%s share being discarded to minimise memory cache",
             get_pool_name(pool));
      break;
    }
    resubmit = true;
    if (stale_work(work, true)) {
      applog(LOG_NOTICE,
             "%s share became stale while retrying submit, discarding",
             get_pool_name(pool));

      mutex_lock(&stats_lock);
      total_stale++;
      pool->stale_shares++;
      total_diff_stale += work->work_difficulty;
      pool->diff_stale += work->work_difficulty;
      mutex_unlock(&stats_lock);

      free_work(work);
      break;
    }

    /* pause, then restart work-request loop */
    applog(LOG_INFO, "json_rpc_call failed on submit_work, retrying");
  }
  push_curl_entry(ce, pool);

  return NULL;
}

extern struct work *make_clone(struct work *work) {
  struct work *work_clone = copy_work(work);

  if (work->pool->algorithm.type == ALGO_DECRED) {
    // maybe not useful here
    ((uint32_t *)work->data)[36] = (rand() * 4);
    ((uint32_t *)work->data)[37] = (rand() * 4) << 8;
  }

  work_clone->clone = true;
  cgtime((struct timeval *)&(work_clone->tv_cloned));
  work_clone->longpoll = false;
  work_clone->mandatory = false;
  /* Make cloned work appear slightly older to bias towards keeping the
   * master work item which can be further rolled */
  work_clone->tv_staged.tv_sec -= 1;

  return work_clone;
}

extern void stage_work(struct work *work);

extern bool clone_available(void) {
  struct work *work_clone = NULL, *work, *tmp;
  bool cloned = false;

  mutex_lock(stgd_lock);
  if (!staged_rollable)
    goto out_unlock;

  HASH_ITER(hh, staged_work, work, tmp) {
    if (can_roll(work) && should_roll(work)) {
      roll_work(work);
      work_clone = make_clone(work);
      roll_work(work);
      cloned = true;
      break;
    }
  }

out_unlock:
  mutex_unlock(stgd_lock);

  if (cloned) {
    applog(LOG_DEBUG, "Pushing cloned available work to stage thread");
    stage_work(work_clone);
  }
  return cloned;
}

/* Clones work by rolling it if possible, and returning a clone instead of the
 * original work item which gets staged again to possibly be rolled again in
 * the future */
extern struct work *clone_work(struct work *work) {
  int mrs = mining_threads + opt_queue - total_staged();
  struct work *work_clone;
  bool cloned;

  if (mrs < 1)
    return work;

  cloned = false;
  work_clone = make_clone(work);
  while (mrs-- > 0 && can_roll(work) && should_roll(work)) {
    applog(LOG_DEBUG, "Pushing rolled converted work to stage thread");
    stage_work(work_clone);
    roll_work(work);
    work_clone = make_clone(work);
    /* Roll it again to prevent duplicates should this be used
     * directly later on */
    roll_work(work);
    cloned = true;
  }

  if (cloned) {
    stage_work(work);
    return work_clone;
  }

  free_work(work_clone);

  return work;
}
#endif

#ifdef HAVE_LIBCURL
/* Stage another work item from the work returned in a longpoll */
extern void convert_to_work(json_t *val, int rolltime, struct pool *pool,
                            struct timeval *tv_lp,
                            struct timeval *tv_lp_reply) {
  struct work *work;
  bool rc;
  work = make_work();
  rc = work_decode(pool, work, val);
  if (unlikely(!rc)) {
    applog(LOG_ERR, "Could not convert longpoll data to work");
    free_work(work);
    return;
  }
  total_getworks++;
  pool->getwork_requested++;
  work->pool = pool;
  work->rolltime = rolltime;
  copy_time(&work->tv_getwork, tv_lp);
  copy_time(&work->tv_getwork_reply, tv_lp_reply);
  calc_diff(work, 0);
  if (pool->state == POOL_REJECTING)
    work->mandatory = true;
  if (pool->has_gbt)
    gen_gbt_work(pool, work);
  work->longpoll = true;
  work->getwork_mode = GETWORK_MODE_LP;
  /* We'll be checking this work item twice, but we already know it's
   * from a new block so explicitly force the new block detection now
   * rather than waiting for it to hit the stage thread. This also
   * allows testwork to know whether LP discovered the block or not. */
  test_work_current(work);
  /* Don't use backup LPs as work if we have failover-only enabled. Use
   * the longpoll work from a pool that has been rejecting shares as a
   * way to detect when the pool has recovered.
   */
  if (pool != current_pool() && opt_fail_only &&
      pool->state != POOL_REJECTING) {
    free_work(work);
    return;
  }

  work = clone_work(work);
  applog(LOG_DEBUG, "Pushing converted work to stage thread");
  stage_work(work);
  applog(LOG_DEBUG, "Converted longpoll data to work");
}

/* If we want longpoll, enable it for the chosen default pool, or, if
 * the pool does not support longpoll, find the first one that does
 * and use its longpoll support */
extern struct pool *select_longpoll_pool(struct pool *cp) {
  int i;
  if (cp->hdr_path || cp->has_gbt)
    return cp;
  for (i = 0; i < total_pools; i++) {
    struct pool *pool = pools[i];
    if (pool->has_stratum || pool->hdr_path)
      return pool;
  }
  return NULL;
}
#endif /* HAVE_LIBCURL */

#ifdef HAVE_LIBCURL
extern void *longpoll_thread(void *userdata) {
  struct pool *cp = (struct pool *)userdata;
  /* This *pool is the source of the actual longpoll, not the pool we've
   * tied it to */
  struct timeval start, reply, end;
  struct pool *pool = NULL;
  char threadname[16];
  CURL *curl = NULL;
  char curl_err_str[CURL_ERROR_SIZE];
  int failures = 0;
  char lpreq[1024];
  char *lp_url;
  int rolltime;
  snprintf(threadname, sizeof(threadname), "%d/Longpoll", cp->pool_no);
  RenameThread(threadname);
  curl = curl_easy_init();
  if (unlikely(!curl)) {
    applog(LOG_ERR, "CURL initialisation failed");
    return NULL;
  }

retry_pool:
  pool = select_longpoll_pool(cp);
  if (!pool) {
    applog(LOG_WARNING, "No suitable long-poll found for %s", cp->rpc_url);
    while (!pool) {
      cgsleep_ms(60000);
      pool = select_longpoll_pool(cp);
    }
  }

  if (pool->has_stratum) {
    applog(LOG_WARNING, "Block change for %s detection via %s stratum",
           cp->rpc_url, pool->rpc_url);
    goto out;
  }

  /* Any longpoll from any pool is enough for this to be true */
  have_longpoll = true;
  wait_lpcurrent(cp);
  if (pool->has_gbt) {
    lp_url = pool->rpc_url;
    applog(LOG_WARNING, "GBT longpoll ID activated for %s", lp_url);
  } else {
    strcpy(lpreq, getwork_req);
    lp_url = pool->lp_url;
    if (cp == pool)
      applog(LOG_WARNING, "Long-polling activated for %s", lp_url);
    else
      applog(LOG_WARNING, "Long-polling activated for %s via %s", cp->rpc_url,
             lp_url);
  }

  while (42) {
    json_t *val, *soval;
    wait_lpcurrent(cp);
    cgtime(&start);
    /* Update the longpollid every time, but do it under lock to
     * avoid races */
    if (pool->has_gbt) {
      cg_rlock(&pool->gbt_lock);
      snprintf(lpreq, sizeof(lpreq),
               "{\"id\": 0, \"method\": \"getblocktemplate\", \"params\": "
               "[{\"capabilities\": [\"coinbasetxn\", \"workid\", "
               "\"coinbase/append\"], "
               "\"longpollid\": \"%s\"}]}\n",
               pool->longpollid);
      cg_runlock(&pool->gbt_lock);
    }

    /* Longpoll connections can be persistent for a very long time
     * and any number of issues could have come up in the meantime
     * so always establish a fresh connection instead of relying on
     * a persistent one. */
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
    val = json_rpc_call(curl, curl_err_str, lp_url, pool->rpc_userpass, lpreq,
                        false, true, &rolltime, pool, false);
    cgtime(&reply);
    if (likely(val)) {
      soval = json_object_get(json_object_get(val, "result"), "submitold");
      if (soval)
        pool->submit_old = json_is_true(soval);
      else
        pool->submit_old = false;
      convert_to_work(val, rolltime, pool, &start, &reply);
      failures = 0;
      json_decref(val);
    } else {
      /* Some pools regularly drop the longpoll request so
       * only see this as longpoll failure if it happens
       * immediately and just restart it the rest of the
       * time. */
      cgtime(&end);
      if (end.tv_sec - start.tv_sec > 30)
        continue;
      if (failures == 1)
        applog(LOG_WARNING, "longpoll failed for %s, retrying every 30s",
               lp_url);
      cgsleep_ms(30000);
    }

    if (pool != cp) {
      pool = select_longpoll_pool(cp);
      if (pool->has_stratum) {
        applog(LOG_WARNING, "Block change for %s detection via %s stratum",
               cp->rpc_url, pool->rpc_url);
        break;
      }
      if (unlikely(!pool))
        goto retry_pool;
    }

    if (unlikely(pool->removed))
      break;
  }

out:
  curl_easy_cleanup(curl);
  return NULL;
}

#endif
#ifdef HAVE_LIBCURL

/* We reap curls if they are unused for over a minute */
extern void reap_curl(struct pool *pool) {
  struct curl_ent *ent, *iter;
  struct timeval now;
  int reaped = 0;
  cgtime(&now);
  mutex_lock(&pool->pool_lock);
  list_for_each_entry_safe(ent, iter, &pool->curlring, node) {
    if (pool->curls < 2)
      break;
    if (now.tv_sec - ent->tv.tv_sec > 300) {
      reaped++;
      pool->curls--;
      list_del(&ent->node);
      curl_easy_cleanup(ent->curl);
      free(ent);
    }
  }
  mutex_unlock(&pool->pool_lock);
  if (reaped)
    applog(LOG_DEBUG, "Reaped %d curl%s from %s", reaped, reaped > 1 ? "s" : "",
           get_pool_name(pool));
}

#endif
