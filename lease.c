#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include "types.h"
#include "list.h"
#include "hashtable.h"
#include "fid.h"
#include "state.h"
#include "envoy.h"
#include "worker.h"
#include "lru.h"
#include "claim.h"
#include "lease.h"
#include "walk.h"

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

void lease_add_claim_to_cache(Claim *claim) {
    /* note: important to do the lru_add first, as it may contain a stale entry
     * and try to do a hash_remove when clearing it */
    lru_add(lease_claim_cache, claim->pathname, claim);
    hash_set(claim->lease->claim_cache, claim->pathname, claim);
}

void lease_remove_claim_from_cache(Claim *claim) {
    hash_remove(claim->lease->claim_cache, claim->pathname);
}

Claim *lease_lookup_claim_from_cache(Lease *lease, char *pathname) {
    Claim *claim = hash_get(claim->lease->claim_cache, pathname);
    if (claim == NULL)
        return NULL;
    /* refresh the LRU entry and verify the hit */
    assert(lru_get(lease_claim_cache, pathname) == claim);
    return claim;
}

static int claim_cmp(const Claim *a, const Claim *b) {
    return strcmp(a->pathname, b->pathname);
}

static int lease_cmp(const Lease *a, const Lease *b) {
    return strcmp(a->pathname, b->pathname);
}

void lease_flush_claim_cache(Lease *lease) {
    claim->lease->claim_cache = hash_create(
            LEASE_CLAIM_HASHTABLE_SIZE,
            string_hash,
            claim_cmp);
}

static void claim_cache_cleanup(Claim *claim) {
    hash_remove(claim->lease->claim_cache, claim->pathname);
}

void lease_state_init(void) {
    lease_by_root_pathname = hash_create(
            LEASE_HASHTABLE_SIZE,
            string_hash,
            lease_cmp);

    lease_claim_cache = lru_new(
            LEASE_CLAIM_LRU_SIZE,
            string_hash,
            claim_cmp,
            NULL,
            claim_cache_cleanup);
}

int lease_start_transaction(Lease *lease) {
    /* if the lease is changing, wait for the update and start over */
    if (lease->wait_for_update != NULL) {
        while (lease->wait_for_update != NULL)
            cond_wait(lease->wait_for_update);
        return -1;
    }

    lease->inflight++;
    return 0;
}

void lease_finish_transaction(Lease *lease) {
    lease->inflight--;

    if (lease->inflight == 0 && lease->okay_to_change_lease != NULL)
        cond_signal(lease->okay_to_change_lease);
}
