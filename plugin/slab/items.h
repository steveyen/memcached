#ifndef ITEMS_H
#define ITEMS_H

#include "slab_engine.h"

/* Define slabber specific flags */
#define ITEM_LINKED  (1 << 8)
#define ITEM_SLABBED (1 << 9)

struct slab_item;
typedef struct slab_item slab_item;
struct slab_item {
   slab_item *next;      /* Next object in the LRU list for this slab size */
   slab_item *prev;      /* Previous object in the LRU list for this slab size */
   slab_item *h_next;    /* hash chain next */
   rel_time_t      time;       /* least recent access */
   uint16_t  refcount;
   uint16_t  slabs_clsid;
   item item; /* MUST BE LAST!!! VARIABLE DATA WILL FOLLOW!!! */
};

/** How long an object can reasonably be assumed to be locked before
    harvesting it on a low memory condition. */
#define TAIL_REPAIR_TIME (3 * 3600)


/* See items.c */
void item_init(void);
uint64_t get_cas_id(void);

/*@null@*/
slab_item *do_item_alloc(struct slabber_engine *se,
                         const char *key, const size_t nkey, 
                         const int flags, const rel_time_t exptime, 
                         const int nbytes);
void item_free(struct slabber_engine *se, slab_item *it);

int  do_item_link(struct slabber_engine *se, slab_item *it);     /** may fail if transgresses limits */
void do_item_unlink(struct slabber_engine *se, slab_item *it);
void do_item_remove(struct slabber_engine *se, slab_item *it);
void do_item_update(struct slabber_engine *se, slab_item *it);   /** update LRU time to current and reposition */
int  do_item_replace(struct slabber_engine *se, slab_item *it, slab_item *new_it);

/*@null@*/
char *do_item_cachedump(const unsigned int slabs_clsid, const unsigned int limit, unsigned int *bytes);
void do_item_stats(ADD_STAT add_stats, const void *c);
/*@null@*/
void do_item_stats_sizes(ADD_STAT add_stats, const void *c);
void do_item_flush_expired(struct slabber_engine *se);

slab_item *do_item_get(struct slabber_engine *se, const char *key, const size_t nkey);
slab_item *do_item_get_nocheck(const char *key, const size_t nkey);
void do_item_stats_reset(void);

#endif
