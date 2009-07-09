#ifndef ASSOC_H
#define ASSOC_H
/* associative array */
void assoc_init(void);
slab_item *assoc_find(const char *key, const size_t nkey);
int assoc_insert(slab_item *item);
void assoc_delete(const char *key, const size_t nkey);
void do_assoc_move_next_bucket(void);
#endif
