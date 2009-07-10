#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>

#include "slab_engine.h"
#include "util.h"
#include "config_parser.h"

static const char* slabber_get_info(ENGINE_HANDLE* handle);
static ENGINE_ERROR_CODE slabber_initialize(ENGINE_HANDLE* handle,
                                            const char* config_str);
static void slabber_destroy(ENGINE_HANDLE* handle);
static ENGINE_ERROR_CODE slabber_item_allocate(ENGINE_HANDLE* handle,
                                               const void* cookie,
                                               item **item,
                                               const void* key,
                                               const size_t nkey,
                                               const size_t nbytes,
                                               const int flags,
                                               const rel_time_t exptime);
static ENGINE_ERROR_CODE slabber_item_delete(ENGINE_HANDLE* handle,
                                             const void* cookie,
                                             item* item);
static void slabber_item_release(ENGINE_HANDLE* handle, item* item);
static ENGINE_ERROR_CODE slabber_get(ENGINE_HANDLE* handle,
                                     const void* cookie,
                                     item** item,
                                     const void* key,
                                     const int nkey);
static ENGINE_ERROR_CODE slabber_get_stats(ENGINE_HANDLE* handle,
			      const void *cookie,
			      const char *stat_key,
			      int nkey,
			      ADD_STAT add_stat);
static void slabber_reset_stats(ENGINE_HANDLE* handle);
static ENGINE_ERROR_CODE slabber_store(ENGINE_HANDLE* handle,
                                       const void *cookie,
                                       item* item,
                                       ENGINE_STORE_OPERATION operation);
static ENGINE_ERROR_CODE slabber_arithmetic(ENGINE_HANDLE* handle,
                                            const void* cookie,
                                            const void* key,
                                            const int nkey,
                                            const bool increment,
                                            const bool create,
                                            const uint64_t delta,
                                            const uint64_t initial,
                                            const rel_time_t exptime,
                                            uint64_t *cas,
                                            uint64_t *result);
static ENGINE_ERROR_CODE slabber_flush(ENGINE_HANDLE* handle,
                                       const void* cookie, time_t when);
static ENGINE_ERROR_CODE initalize_configuration(struct config *config, 
                                                 const char *cfg_str);
static ENGINE_ERROR_CODE slabber_unknown_command(ENGINE_HANDLE* handle,
                                                 const void* cookie,
                                                 protocol_binary_request_header *request,
                                                 ADD_RESPONSE response);

ENGINE_ERROR_CODE create_instance(uint64_t interface,
                                  ENGINE_HANDLE **handle) {
   if (interface != 1) {
      return ENGINE_ENOTSUP;
   }

   struct slabber_engine* slab_engine = calloc(1, sizeof (*slab_engine));
   if (slab_engine == NULL) {
      return ENGINE_ENOMEM;
   }

   slab_engine->engine.interface.interface = 1;

   slab_engine->engine.get_info = slabber_get_info;
   slab_engine->engine.initialize = slabber_initialize;
   slab_engine->engine.destroy = slabber_destroy;
   slab_engine->engine.allocate = slabber_item_allocate;
   slab_engine->engine.remove = slabber_item_delete;
   slab_engine->engine.release = slabber_item_release;
   slab_engine->engine.get = slabber_get;
   slab_engine->engine.get_stats = slabber_get_stats;
   slab_engine->engine.reset_stats = slabber_reset_stats;
   slab_engine->engine.store = slabber_store;
   slab_engine->engine.arithmetic = slabber_arithmetic;
   slab_engine->engine.flush = slabber_flush;
   slab_engine->engine.unknow_command = slabber_unknown_command;


   *handle = (ENGINE_HANDLE*) & slab_engine->engine;
   return ENGINE_SUCCESS;
}

static inline struct slabber_engine* get_handle(ENGINE_HANDLE* handle) {
   return (struct slabber_engine*) handle;
}

static slab_item* get_slab_item(item* item) {
   slab_item it;
   size_t offset = (caddr_t)&it.item - (caddr_t)&it;
   return (slab_item*) (((caddr_t) item) - (offset));
}

static const char* slabber_get_info(ENGINE_HANDLE* handle) {
   return "Slabber engine v0.1";
}

static ENGINE_ERROR_CODE slabber_initialize(ENGINE_HANDLE* handle,
                                            const char* config_str) {
   struct slabber_engine* se = get_handle(handle);

   if (se->initialized) {
      return ENGINE_EINVAL;
   }

   ENGINE_ERROR_CODE ret = initalize_configuration(&se->config, config_str);
   if (ret != ENGINE_SUCCESS) {
      return ret;
   }

   se->assoc_expanding = false;

   if (pthread_mutex_init(&se->cache_lock, NULL) != 0) {
      return ENGINE_EINVAL;
   }

  
   if (pthread_mutex_init(&se->stats.lock, NULL) != 0) {
      pthread_mutex_destroy(&se->cache_lock);
      return ENGINE_EINVAL;
   }  
   

   item_init();
   assoc_init();
   slabs_init(se->config.maxbytes, se->config.factor, se->config.preallocate, se->config.chunk_size);
   se->initialized = true;
   return ENGINE_SUCCESS;
}

static void slabber_destroy(ENGINE_HANDLE* handle) {
   struct slabber_engine* se = get_handle(handle);

   if (se->initialized) {
      pthread_mutex_destroy(&se->cache_lock);
      pthread_mutex_destroy(&se->stats.lock);      
      se->initialized = false;
      /* @TODO clean up the other resources! */
   }
   free(se);
}

static ENGINE_ERROR_CODE slabber_item_allocate(ENGINE_HANDLE* handle,
                                               const void* cookie,
                                               item **item,
                                               const void* key,
                                               const size_t nkey,
                                               const size_t nbytes,
                                               const int flags,
                                               const rel_time_t exptime) {
   struct slabber_engine* se = get_handle(handle);
   slab_item *it;

   size_t ntotal = sizeof(slab_item) + nkey + nbytes;
   if (se->config.use_cas) {
      ntotal += sizeof(uint64_t);
   }
   unsigned int id = slabs_clsid(ntotal);
   if (id == 0) {
      return ENGINE_E2BIG;
   }


   pthread_mutex_lock(&se->cache_lock);
   it = do_item_alloc(se, key, nkey, flags, exptime, nbytes);
   pthread_mutex_unlock(&se->cache_lock);

   if (it != NULL) {
      *item = &it->item;
      return ENGINE_SUCCESS;
   } else {
      return ENGINE_ENOMEM;
   }
}

static ENGINE_ERROR_CODE slabber_item_delete(ENGINE_HANDLE* handle,
                                             const void* cookie,
                                             item* item) {
   struct slabber_engine* se = get_handle(handle);

   pthread_mutex_lock(&se->cache_lock);
   do_item_unlink(se, get_slab_item(item));
   pthread_mutex_unlock(&se->cache_lock);

   return ENGINE_SUCCESS;
}

static void slabber_item_release(ENGINE_HANDLE* handle, item* item) {
   struct slabber_engine* se = get_handle(handle);

   pthread_mutex_lock(&se->cache_lock);
   do_item_remove(se, get_slab_item(item));
   pthread_mutex_unlock(&se->cache_lock);
}

static ENGINE_ERROR_CODE slabber_get(ENGINE_HANDLE* handle,
                                     const void* cookie,
                                     item** item,
                                     const void* key,
                                     const int nkey) {
   struct slabber_engine* se = get_handle(handle);
   slab_item *it;

   pthread_mutex_lock(&se->cache_lock);
   it = do_item_get(se, key, nkey);
   pthread_mutex_unlock(&se->cache_lock);

   if (it != NULL) {
      *item = &it->item;
      return ENGINE_SUCCESS;
   } else {
      *item = NULL;
      return ENGINE_KEY_ENOENT;
   }
}

static ENGINE_ERROR_CODE slabber_get_stats(ENGINE_HANDLE* handle,
                                           const void* cookie,
                                           const char* stat_key,
                                           int nkey,
                                           ADD_STAT add_stat)
{
   struct slabber_engine* se = get_handle(handle);
   ENGINE_ERROR_CODE ret = ENGINE_SUCCESS;

   if (stat_key == NULL) {
      char val[128];
      int len;

      pthread_mutex_lock(&se->stats.lock);
      len = sprintf(val, "%llu", (unsigned long long)se->stats.evictions);
      add_stat("evictions", 9, val, len, cookie);
      len = sprintf(val, "%llu", (unsigned long long)se->stats.curr_items);
      add_stat("curr_items", 10, val, len, cookie);
      len = sprintf(val, "%llu", (unsigned long long)se->stats.total_items);
      add_stat("total_items", 11, val, len, cookie);
      len = sprintf(val, "%llu", (unsigned long long)se->stats.curr_bytes);
      add_stat("bytes", 5, val, len, cookie);
      pthread_mutex_unlock(&se->stats.lock);
   } else if (strncmp(stat_key, "slabs", 5) == 0) {
      slabs_stats(add_stat, cookie);
   } else if (strncmp(stat_key, "items", 5) == 0) {
      pthread_mutex_lock(&se->cache_lock);
      do_item_stats(add_stat, cookie);
      pthread_mutex_unlock(&se->cache_lock);
   } else if (strncmp(stat_key, "sizes", 5) == 0) {
      pthread_mutex_lock(&se->cache_lock);
      do_item_stats_sizes(add_stat, cookie);
      pthread_mutex_unlock(&se->cache_lock);
   } else {
      ret = ENGINE_KEY_ENOENT;
   }

   return ret;
}

/*
 * Stores an item in the cache according to the semantics of one of the set
 * commands. In threaded mode, this is protected by the cache lock.
 *
 * Returns the state of storage.
 */
static inline ENGINE_ERROR_CODE do_store_item(struct slabber_engine *se,
                                              slab_item *it,
                                              ENGINE_STORE_OPERATION comm) {
   char *key = ITEM_key(&it->item);
   slab_item *old_it = do_item_get(se, key, it->item.nkey);
   slab_item *new_it = NULL;
   ENGINE_ERROR_CODE stored = ENGINE_NOT_STORED;

   if (old_it != NULL && comm == OPERATION_ADD) {
      /* add only adds a nonexistent item, but promote to head of LRU */
      do_item_update(se, old_it);
   } else if (!old_it && (comm == OPERATION_REPLACE ||
                          comm == OPERATION_APPEND ||
                          comm == OPERATION_PREPEND)) {
      /* replace only replaces an existing value; don't store */
   } else if (comm == OPERATION_CAS) {
      /* validate cas operation */
      if(old_it == NULL) {
         // LRU expired
         stored = ENGINE_KEY_ENOENT;
      } else if (ITEM_get_cas(&it->item) == ITEM_get_cas(&old_it->item)) {
         // cas validates
         // it and old_it may belong to different classes.
         // I'm updating the stats for the one that's getting pushed out
         do_item_replace(se, old_it, it);
         stored = ENGINE_SUCCESS;
      } else {
         if (se->config.verbose > 1) {
            fprintf(stderr, "CAS:  failure: expected %llu, got %llu\n",
                    (unsigned long long)ITEM_get_cas(&old_it->item),
                    (unsigned long long)ITEM_get_cas(&it->item));
         }
         stored = ENGINE_KEY_EEXISTS;
      }
   } else {
      /*
       * Append - combine new and old record into single one. Here it's
       * atomic and thread-safe.
       */
      if (comm == OPERATION_APPEND || comm == OPERATION_PREPEND) {
         /*
          * Validate CAS
          */
         if (ITEM_get_cas(&it->item) != 0) {
            // CAS much be equal
            if (ITEM_get_cas(&it->item) != ITEM_get_cas(&old_it->item)) {
               stored = ENGINE_KEY_EEXISTS;
            }
         }

         if (stored == ENGINE_NOT_STORED) {
            size_t total = it->item.nbytes + old_it->item.nbytes - 2; /* crlf */
            /* we have it and old_it here - alloc memory to hold both */
            /* flags was already lost - so recover them from ITEM_suffix(it) */
            new_it = do_item_alloc(se, key, it->item.nkey, it->item.flags,
                                   old_it->item.exptime, total);

            if (new_it == NULL) {
               /* SERVER_ERROR out of memory */
               if (old_it != NULL) {
                  do_item_remove(se, old_it);
               }

               return ENGINE_NOT_STORED;
            }

            /* copy data from it and old_it to new_it */

            if (comm == OPERATION_APPEND) {
               memcpy(ITEM_data(&new_it->item), ITEM_data(&old_it->item), old_it->item.nbytes);
               memcpy(ITEM_data(&new_it->item) + old_it->item.nbytes - 2 /* CRLF */, ITEM_data(&it->item), it->item.nbytes);
            } else {
               /* OPERATION_PREPEND */
               memcpy(ITEM_data(&new_it->item), ITEM_data(&it->item), it->item.nbytes);
               memcpy(ITEM_data(&new_it->item) + it->item.nbytes - 2 /* CRLF */, ITEM_data(&old_it->item), old_it->item.nbytes);
            }

            it = new_it;
            it->item.flags = old_it->item.flags;
         }
      }

      if (stored == ENGINE_NOT_STORED) {
         if (old_it != NULL) {
            do_item_replace(se, old_it, it);
         } else {
            do_item_link(se, it);
         }

         stored = ENGINE_SUCCESS;
      }
   }

   if (old_it != NULL) {
      do_item_remove(se, old_it);         /* release our reference */
   }

   if (new_it != NULL) {
      do_item_remove(se, new_it);
   }

   return stored;
}

static ENGINE_ERROR_CODE slabber_store(ENGINE_HANDLE* handle,
                                       const void *cookie,
                                       item* item,
                                       ENGINE_STORE_OPERATION operation) {
   ENGINE_ERROR_CODE ret;
   struct slabber_engine* se = get_handle(handle);

   pthread_mutex_lock(&se->cache_lock);
   ret = do_store_item(se, get_slab_item(item), operation);
   pthread_mutex_unlock(&se->cache_lock);

   return ret;
}

static ENGINE_ERROR_CODE slabber_arithmetic(ENGINE_HANDLE* handle,
                                            const void* cookie,
                                            const void* key,
                                            const int nkey,
                                            const bool increment,
                                            const bool create,
                                            const uint64_t delta,
                                            const uint64_t initial,
                                            const rel_time_t exptime,
                                            uint64_t *cas,
                                            uint64_t *result) {

   ENGINE_ERROR_CODE ret;
   struct slabber_engine* se = get_handle(handle);

   pthread_mutex_lock(&se->cache_lock);
   slab_item *it = do_item_get(se, key, nkey);
   if (it == NULL) {
      if (!create) {
         pthread_mutex_unlock(&se->cache_lock);
         return ENGINE_KEY_ENOENT;
      }

      it = do_item_alloc(se, key, nkey, 0, exptime, sizeof ("18446744073709551615") + 2);
      if (it != NULL) {
         sprintf(ITEM_data(&it->item), "%lld\r\n", (unsigned long long) initial);
         ret = do_store_item(se, it, OPERATION_SET);
         *result = initial;
         /* release our handle */
         do_item_remove(se, it);
         pthread_mutex_unlock(&se->cache_lock);
         return ret;
      } else {
         pthread_mutex_unlock(&se->cache_lock);
         return ENGINE_ENOMEM;
      }
   }
   if (*cas != 0 && *cas != ITEM_get_cas(&it->item)) {
      /* Incorrect CAS value */
      pthread_mutex_unlock(&se->cache_lock);
      return ENGINE_KEY_EEXISTS;
   }

   uint64_t value;
   if (!safe_strtoull(ITEM_data(&it->item), &value)) {
      pthread_mutex_unlock(&se->cache_lock);
      return ENGINE_EINVAL;
   }

   if (increment) {
      value += delta;
   } else if (delta < value) {
      value -= delta;
   } else {
      value = 0;
   }
   *result = value;
   char temp[sizeof ("18446744073709551615") + 2];
   int len = sprintf(temp, "%llu", (unsigned long long) value);

   if ((len + 2) > it->item.nbytes) { /* No room in current struct.. must realloc */
      slab_item *nit;
      nit = do_item_alloc(se, key, nkey, 0, exptime, len + 2);
      if (nit != NULL) {
         memcpy(ITEM_data(&nit->item), temp, len);
         memcpy(ITEM_data(&nit->item) + len, "\r\n", 2);
         ret = do_store_item(se, nit, OPERATION_SET);
      } else {
         ret = ENGINE_ENOMEM;
      }
      /* release our handle */
      do_item_remove(se, nit);
   } else { /* replace in-place */
      memcpy(ITEM_data(&it->item), temp, len);
      memset(ITEM_data(&it->item) + len, ' ', it->item.nbytes - len - 2);
      memcpy(ITEM_data(&it->item) + len, "\r\n", 2);
      ITEM_set_cas(&it->item, (se->config.use_cas) ? get_cas_id() : 0);
      *cas = ITEM_get_cas(&it->item);
      ret =  ENGINE_SUCCESS;
   }

   /* release our handle */
   do_item_remove(se, it);
   pthread_mutex_unlock(&se->cache_lock);
   return ret;
}

static ENGINE_ERROR_CODE slabber_flush(ENGINE_HANDLE* handle,
                                       const void* cookie, time_t when) {

   struct slabber_engine* se = get_handle(handle);

   /*
     If exptime is zero realtime() would return zero too, and
     realtime(exptime) - 1 would overflow to the max unsigned
     value.  So we process exptime == 0 the same way we do when
     no delay is given at all.
   */
   if (when > 0)
      se->config.oldest_live = realtime(when) - 1;
   else /* exptime == 0 */
      se->config.oldest_live = current_time - 1;

   pthread_mutex_lock(&se->cache_lock);
   do_item_flush_expired(se);
   pthread_mutex_unlock(&se->cache_lock);

   return ENGINE_SUCCESS;
}

static void slabber_reset_stats(ENGINE_HANDLE* handle) {
   struct slabber_engine* se = get_handle(handle);
   pthread_mutex_lock(&se->cache_lock);
   do_item_stats_reset();
   pthread_mutex_unlock(&se->cache_lock);

   pthread_mutex_lock(&se->stats.lock);
   se->stats.evictions = 0;
   se->stats.total_items = 0;
   pthread_mutex_unlock(&se->stats.lock);
}

static ENGINE_ERROR_CODE initalize_configuration(struct config *config, 
                                                 const char *cfg_str) {
   struct config default_config = {
      .use_cas = true,
      .verbose = 0,
      .oldest_live = 0,
      .evict_to_free = true,
      .maxbytes = 64 * 1024 * 1024,
      .preallocate = false,
      .factor = 1.25,
      .chunk_size = 48      
   };

   *config = default_config;
   ENGINE_ERROR_CODE ret = ENGINE_SUCCESS;
  
   if (cfg_str != NULL) {
      struct config_item items[] = {
         { .key = "use_cas", 
           .datatype = DT_BOOL, 
           .value.dt_bool = &config->use_cas },
         { .key = "verbose", 
           .datatype = DT_SIZE, 
           .value.dt_size = &config->verbose},
         { .key = "eviction", 
           .datatype = DT_BOOL, 
           .value.dt_bool = &config->evict_to_free},
         { .key = "cache_size", 
           .datatype = DT_SIZE, 
           .value.dt_size = &config->maxbytes},
         { .key = "preallocate", 
           .datatype = DT_BOOL, 
           .value.dt_bool = &config->preallocate},
         { .key = "factor", 
           .datatype = DT_FLOAT, 
           .value.dt_float = &config->factor},
         { .key = "chunk_size", 
           .datatype = DT_SIZE, 
           .value.dt_size = &config->chunk_size},
         { .key = "config_file", 
           .datatype = DT_CONFIGFILE },
         { .key = NULL}
      };

      ret = parse_config(cfg_str, items, stderr);
   }

   return ret;
}

static ENGINE_ERROR_CODE slabber_unknown_command(ENGINE_HANDLE* handle,
                                                 const void* cookie,
                                                 protocol_binary_request_header *request,
                                                 ADD_RESPONSE response)
{
    return ENGINE_ENOTSUP;
}
