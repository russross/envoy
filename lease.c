#include "lease.h"

void lease_new(char *pathname, Address *addr, int leaf, List *children) {
    Lease *l = GC_NEW(Lease);
    assert(l != NULL);

    l->wait_for_update = NULL;
    l->inflight = 0;
    l->okay_to_change_lease = NULL;

    l->pathname = pathname;
    l->readonly = 0;
    l->addr = addr;
    l->leaf = leaf;

    l->fids = hash_create(LEASE_FIDS_HASHTABLE_SIZE, fid_hash, fid_cmp);
    l->walk_cache = lru_create(
            LEASE_WALK_LRU_SIZE,
            walk_result_hash,
            walk_result_cmp,
            NULL,
            NULL);
    l->children = children;
    l->version = 0;
}
