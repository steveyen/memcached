/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */

#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "slab_engine.h"

#ifdef ENABLE_DTRACE
#error "dtrace support is currently broken"
#else
#define MEMCACHED_ITEM_LINK(a,b,c)
#define MEMCACHED_ITEM_UNLINK(a,b,c)
#define MEMCACHED_ITEM_REMOVE(a,b,c)
#define MEMCACHED_ITEM_UPDATE(a, b, c)
#define MEMCACHED_ITEM_REPLACE(a, b, c, d, e, f)
#endif

/* Forward Declarations */
static void item_link_q(slab_item *it);
static void item_unlink_q(slab_item *it);

static inline size_t ITEM_ntotal(struct slab_item *it) {
    size_t ret = sizeof(*it) + it->item.nkey + it->item.nbytes;
    if (it->item.iflag & ITEM_WITH_CAS) {
        ret += sizeof(uint64_t);
    }

    return ret;
}

/*
 * We only reposition items in the LRU queue if they haven't been repositioned
 * in this many seconds. That saves us from churning on frequently-accessed
 * items.
 */
#define ITEM_UPDATE_INTERVAL 60

#define LARGEST_ID 255
typedef struct {
    unsigned int evicted;
    rel_time_t evicted_time;
    unsigned int outofmemory;
    unsigned int tailrepairs;
} itemstats_t;

static slab_item *heads[LARGEST_ID];
static slab_item *tails[LARGEST_ID];
static itemstats_t itemstats[LARGEST_ID];
static unsigned int sizes[LARGEST_ID];

void item_init(void) {
    int i;
    memset(itemstats, 0, sizeof(itemstats_t) * LARGEST_ID);
    for(i = 0; i < LARGEST_ID; i++) {
        heads[i] = NULL;
        tails[i] = NULL;
        sizes[i] = 0;
    }
}

void do_item_stats_reset(void) {
    memset(itemstats, 0, sizeof(itemstats));
}


/* Get the next CAS id for a new item. */
uint64_t get_cas_id(void) {
    static uint64_t cas_id = 0;
    return ++cas_id;
}

/* Enable this for reference-count debugging. */
#if 0
# define DEBUG_REFCNT(it,op) \
                fprintf(stderr, "item %x refcnt(%c) %d %c%c%c\n", \
                        it, op, it->refcount, \
                        (it->item.iflag & ITEM_LINKED) ? 'L' : ' ', \
                        (it->item.iflag & ITEM_SLABBED) ? 'S' : ' ')
#else
# define DEBUG_REFCNT(it,op) while(0)
#endif

/*@null@*/
slab_item *do_item_alloc(struct slabber_engine *se,
                         const char *key, const size_t nkey,
                         const int flags, const rel_time_t exptime,
                         const int nbytes) {
    struct slab_item *it = NULL;
    size_t ntotal = sizeof(struct slab_item) + nkey + nbytes;

    if (se->config.use_cas) {
        ntotal += sizeof(uint64_t);
    }

    unsigned int id = slabs_clsid(ntotal);
    if (id == 0)
        return 0;

    /* do a quick check if we have any expired items in the tail.. */
    int tries = 50;
    slab_item *search;

    for (search = tails[id];
         tries > 0 && search != NULL;
         tries--, search=search->prev) {
        if (search->refcount == 0 &&
            (search->item.exptime != 0 && search->item.exptime < current_time)) {
            it = search;
            /* I don't want to actually free the object, just steal
             * the item to avoid to grab the slab mutex twice ;-)
             */
            it->refcount = 1;
            do_item_unlink(se, it);
            /* Initialize the item block: */
            it->slabs_clsid = 0;
            it->refcount = 0;
            break;
        }
    }

    if (it == NULL && (it = slabs_alloc(ntotal, id)) == NULL) {
        /*
        ** Could not find an expired item at the tail, and memory allocation
        ** failed. Try to evict some items!
        */
        tries = 50;

        /* If requested to not push old items out of cache when memory runs out,
         * we're out of luck at this point...
         */

        if (!se->config.evict_to_free) {
            itemstats[id].outofmemory++;
            return NULL;
        }

        /*
         * try to get one off the right LRU
         * don't necessariuly unlink the tail because it may be locked: refcount>0
         * search up from tail an item with refcount==0 and unlink it; give up after 50
         * tries
         */

        if (tails[id] == 0) {
            itemstats[id].outofmemory++;
            return NULL;
        }

        for (search = tails[id]; tries > 0 && search != NULL; tries--, search=search->prev) {
            if (search->refcount == 0) {
                if (search->item.exptime == 0 || search->item.exptime > current_time) {
                    itemstats[id].evicted++;
                    itemstats[id].evicted_time = current_time - search->time;
                    
                    pthread_mutex_lock(&se->stats.lock);
                    se->stats.evictions++;
                    pthread_mutex_unlock(&se->stats.lock);
                }
                do_item_unlink(se, search);
                break;
            }
        }
        it = slabs_alloc(ntotal, id);
        if (it == 0) {
            itemstats[id].outofmemory++;
            /* Last ditch effort. There is a very rare bug which causes
             * refcount leaks. We've fixed most of them, but it still happens,
             * and it may happen in the future.
             * We can reasonably assume no item can stay locked for more than
             * three hours, so if we find one in the tail which is that old,
             * free it anyway.
             */
            tries = 50;
            for (search = tails[id]; tries > 0 && search != NULL; tries--, search=search->prev) {
                if (search->refcount != 0 && search->time + TAIL_REPAIR_TIME < current_time) {
                    itemstats[id].tailrepairs++;
                    search->refcount = 0;
                    do_item_unlink(se, search);
                    break;
                }
            }
            it = slabs_alloc(ntotal, id);
            if (it == 0) {
                return NULL;
            }
        }
    }

    assert(it->slabs_clsid == 0);

    it->slabs_clsid = id;

    assert(it != heads[it->slabs_clsid]);

    it->next = it->prev = it->h_next = NULL;
    it->refcount = 1;     /* the caller will have a reference */
    DEBUG_REFCNT(it, '*');
    it->item.flags = flags;
    it->item.iflag = se->config.use_cas ? ITEM_WITH_CAS : 0;
    it->item.nkey = nkey;
    it->item.nbytes = nbytes;
    memcpy(ITEM_key(&it->item), key, nkey);
    it->item.exptime = exptime;

    return it;
}

void item_free(struct slabber_engine *se, slab_item *it) {
    size_t ntotal = ITEM_ntotal(it);
    unsigned int clsid;
    assert((it->item.iflag & ITEM_LINKED) == 0);
    assert(it != heads[it->slabs_clsid]);
    assert(it != tails[it->slabs_clsid]);
    assert(it->refcount == 0);

    /* so slab size changer can tell later if item is already free or not */
    clsid = it->slabs_clsid;
    it->slabs_clsid = 0;
    it->item.iflag |= ITEM_SLABBED;
    DEBUG_REFCNT(it, 'F');
    slabs_free(it, ntotal, clsid);
}

static void item_link_q(slab_item *it) { /* item is the new head */
    slab_item **head, **tail;
    /* always true, warns: assert(it->slabs_clsid <= LARGEST_ID); */
    assert((it->item.iflag & ITEM_SLABBED) == 0);

    head = &heads[it->slabs_clsid];
    tail = &tails[it->slabs_clsid];
    assert(it != *head);
    assert((*head && *tail) || (*head == 0 && *tail == 0));
    it->prev = 0;
    it->next = *head;
    if (it->next) it->next->prev = it;
    *head = it;
    if (*tail == 0) *tail = it;
    sizes[it->slabs_clsid]++;
    return;
}

static void item_unlink_q(slab_item *it) {
    slab_item **head, **tail;
    /* always true, warns: assert(it->slabs_clsid <= LARGEST_ID); */
    head = &heads[it->slabs_clsid];
    tail = &tails[it->slabs_clsid];

    if (*head == it) {
        assert(it->prev == 0);
        *head = it->next;
    }
    if (*tail == it) {
        assert(it->next == 0);
        *tail = it->prev;
    }
    assert(it->next != it);
    assert(it->prev != it);

    if (it->next) it->next->prev = it->prev;
    if (it->prev) it->prev->next = it->next;
    sizes[it->slabs_clsid]--;
    return;
}

int do_item_link(struct slabber_engine *se, slab_item *it) {
    MEMCACHED_ITEM_LINK(ITEM_key(&it->item), it->item.nkey, it->item.nbytes);
    assert((it->item.iflag & (ITEM_LINKED|ITEM_SLABBED)) == 0);
    assert(it->item.nbytes < (1024 * 1024));  /* 1MB max size */
    it->item.iflag |= ITEM_LINKED;
    it->time = current_time;
    assoc_insert(it);

    pthread_mutex_lock(&se->stats.lock);
    se->stats.curr_bytes += ITEM_ntotal(it);
    se->stats.curr_items++;
    se->stats.total_items++;
    pthread_mutex_unlock(&se->stats.lock);

    /* Allocate a new CAS ID on link. */
    ITEM_set_cas(&it->item, (se->config.use_cas) ? get_cas_id() : 0);
    item_link_q(it);

    return 1;
}

void do_item_unlink(struct slabber_engine *se, slab_item *it) {
    MEMCACHED_ITEM_UNLINK(ITEM_key(&it->item), it->item.nkey, it->item.nbytes);
    if ((it->item.iflag & ITEM_LINKED) != 0) {
        it->item.iflag &= ~ITEM_LINKED;

        pthread_mutex_lock(&se->stats.lock);
        se->stats.curr_bytes -= ITEM_ntotal(it);
        se->stats.curr_items--;
        pthread_mutex_unlock(&se->stats.lock);

        assoc_delete(ITEM_key(&it->item), it->item.nkey);
        item_unlink_q(it);
        if (it->refcount == 0) item_free(se, it);
    }
}

void do_item_remove(struct slabber_engine *se, slab_item *it) {
    MEMCACHED_ITEM_REMOVE(ITEM_key(&it->item), it->item.nkey, it->item.nbytes);
    assert((it->item.iflag & ITEM_SLABBED) == 0);
    if (it->refcount != 0) {
        it->refcount--;
        DEBUG_REFCNT(it, '-');
    }
    if (it->refcount == 0 && (it->item.iflag & ITEM_LINKED) == 0) {
        item_free(se, it);
    }
}

void do_item_update(struct slabber_engine *se, slab_item *it) {
    MEMCACHED_ITEM_UPDATE(ITEM_key(&it->item), it->item.nkey, it->item.nbytes);
    if (it->time < current_time - ITEM_UPDATE_INTERVAL) {
        assert((it->item.iflag & ITEM_SLABBED) == 0);

        if ((it->item.iflag & ITEM_LINKED) != 0) {
            item_unlink_q(it);
            it->time = current_time;
            item_link_q(it);
        }
    }
}

int do_item_replace(struct slabber_engine *se, slab_item *it, slab_item *new_it) {
    MEMCACHED_ITEM_REPLACE(ITEM_key(&it->item), it->item.nkey, it->item.nbytes,
                           ITEM_key(new_it), new_it->item.nkey, new_it->item.nbytes);
    assert((it->item.iflag & ITEM_SLABBED) == 0);

    do_item_unlink(se, it);
    return do_item_link(se, new_it);
}

/*@null@*/
char *do_item_cachedump(const unsigned int slabs_clsid, const unsigned int limit, unsigned int *bytes) {
#if 0
    unsigned int memlimit = 2 * 1024 * 1024;   /* 2MB max response size */
    char *buffer;
    unsigned int bufcurr;
    slab_item *it;
    unsigned int len;
    unsigned int shown = 0;
    char key_temp[KEY_MAX_LENGTH + 1];
    char temp[512];

    if (slabs_clsid > LARGEST_ID) return NULL;
    it = heads[slabs_clsid];

    buffer = malloc((size_t)memlimit);
    if (buffer == 0) return NULL;
    bufcurr = 0;

    while (it != NULL && (limit == 0 || shown < limit)) {
        assert(it->item.nkey <= KEY_MAX_LENGTH);
        /* Copy the key since it may not be null-terminated in the struct */
        strncpy(key_temp, ITEM_key(&it->item), it->item.nkey);
        key_temp[it->item.nkey] = 0x00; /* terminate */
        len = snprintf(temp, sizeof(temp), "ITEM %s [%d b; %lu s]\r\n",
                       key_temp, it->item.nbytes - 2,
                       (unsigned long)it->item.exptime + process_started);
        if (bufcurr + len + 6 > memlimit)  /* 6 is END\r\n\0 */
            break;
        strcpy(buffer + bufcurr, temp);
        bufcurr += len;
        shown++;
        it = it->next;
    }

    memcpy(buffer + bufcurr, "END\r\n", 6);
    bufcurr += 5;

    *bytes = bufcurr;
    return buffer;
#endif
    return NULL;
}

/** Append a simple stat with a stat name, value format and value */
#define APPEND_STAT(name, fmt, val) \
    append_stat(name, add_stats, c, fmt, val);

/** Append an indexed stat with a stat name (with format), value format
    and value */
#define APPEND_NUM_FMT_STAT(name_fmt, num, name, fmt, val)   \
    klen = sprintf(key_str, name_fmt, num, name);            \
    vlen = sprintf(val_str, fmt, val);                       \
    add_stats(key_str, klen, val_str, vlen, c);

/** Common APPEND_NUM_FMT_STAT format. */
#define APPEND_NUM_STAT(num, name, fmt, val) \
    APPEND_NUM_FMT_STAT("%d:%s", num, name, fmt, val)

extern void append_stat(const char *name, ADD_STAT add_stats, const void *c,
                        const char *fmt, ...);


void do_item_stats(ADD_STAT add_stats, const void *c) {
    int i;
    for (i = 0; i < LARGEST_ID; i++) {
        if (tails[i] != NULL) {
            const char *fmt = "items:%d:%s";
            char key_str[128];
            char val_str[256];
            int klen = 0, vlen = 0;

            APPEND_NUM_FMT_STAT(fmt, i, "number", "%u", sizes[i]);
            APPEND_NUM_FMT_STAT(fmt, i, "age", "%u", tails[i]->time);
            APPEND_NUM_FMT_STAT(fmt, i, "evicted",
                                "%u", itemstats[i].evicted);
            APPEND_NUM_FMT_STAT(fmt, i, "evicted_time",
                                "%u", itemstats[i].evicted_time);
            APPEND_NUM_FMT_STAT(fmt, i, "outofmemory",
                                "%u", itemstats[i].outofmemory);
            APPEND_NUM_FMT_STAT(fmt, i, "tailrepairs",
                                "%u", itemstats[i].tailrepairs);;
        }
    }
    /* getting here means both ascii and binary terminators fit */
    add_stats(NULL, 0, NULL, 0, c);
}

/** dumps out a list of objects of each size, with granularity of 32 bytes */
/*@null@*/
void do_item_stats_sizes(ADD_STAT add_stats, const void *c) {
    /* max 1MB object, divided into 32 bytes size buckets */
    const int num_buckets = 32768;
    unsigned int *histogram = calloc(num_buckets, sizeof(int));

    if (histogram != NULL) {
        int i;

        /* build the histogram */
        for (i = 0; i < LARGEST_ID; i++) {
            slab_item *iter = heads[i];
            while (iter) {
                int ntotal = ITEM_ntotal(iter);
                int bucket = ntotal / 32;
                if ((ntotal % 32) != 0) bucket++;
                if (bucket < num_buckets) histogram[bucket]++;
                iter = iter->next;
            }
        }

        /* write the buffer */
        for (i = 0; i < num_buckets; i++) {
            if (histogram[i] != 0) {
                char key[8];
                int klen = 0;
                klen = sprintf(key, "%d", i * 32);
                assert(klen < sizeof(key));
                APPEND_STAT(key, "%u", histogram[i]);
            }
        }
        free(histogram);
    }
    add_stats(NULL, 0, NULL, 0, c);
}

/** wrapper around assoc_find which does the lazy expiration logic */
slab_item *do_item_get(struct slabber_engine *se, const char *key, const size_t nkey) {
    slab_item *it = assoc_find(key, nkey);
    int was_found = 0;

    if (se->config.verbose > 2) {
        if (it == NULL) {
            fprintf(stderr, "> NOT FOUND %s", key);
        } else {
            fprintf(stderr, "> FOUND KEY %s", ITEM_key(&it->item));
            was_found++;
        }
    }

    if (it != NULL && se->config.oldest_live != 0 && se->config.oldest_live <= current_time &&
        it->time <= se->config.oldest_live) {
        do_item_unlink(se, it);           /* MTSAFE - cache_lock held */
        it = NULL;
    }

    if (it == NULL && was_found) {
        fprintf(stderr, " -nuked by flush");
        was_found--;
    }

    if (it != NULL && it->item.exptime != 0 && it->item.exptime <= current_time) {
        do_item_unlink(se, it);           /* MTSAFE - cache_lock held */
        it = NULL;
    }

    if (it == NULL && was_found) {
        fprintf(stderr, " -nuked by expire");
        was_found--;
    }

    if (it != NULL) {
        it->refcount++;
        DEBUG_REFCNT(it, '+');
    }

    if (se->config.verbose > 2)
        fprintf(stderr, "\n");

    return it;
}

/** returns an item whether or not it's expired. */
slab_item *do_item_get_nocheck(const char *key, const size_t nkey) {
    slab_item *it = assoc_find(key, nkey);
    if (it) {
        it->refcount++;
        DEBUG_REFCNT(it, '+');
    }
    return it;
}

/* expires items that are more recent than the oldest_live setting. */
void do_item_flush_expired(struct slabber_engine *se) {
    int i;
    slab_item *iter, *next;
    if (se->config.oldest_live == 0)
        return;
    for (i = 0; i < LARGEST_ID; i++) {
        /* The LRU is sorted in decreasing time order, and an item's timestamp
         * is never newer than its last access time, so we only need to walk
         * back until we hit an item older than the oldest_live time.
         * The oldest_live checking will auto-expire the remaining items.
         */
        for (iter = heads[i]; iter != NULL; iter = next) {
            if (iter->time >= se->config.oldest_live) {
                next = iter->next;
                if ((iter->item.iflag & ITEM_SLABBED) == 0) {
                    do_item_unlink(se, iter);
                }
            } else {
                /* We've hit the first old item. Continue to the next queue. */
                break;
            }
        }
    }
}
