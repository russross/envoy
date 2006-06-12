#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include "types.h"
#include "list.h"
#include "hashtable.h"
#include "fid.h"
#include "util.h"
#include "state.h"
#include "worker.h"
#include "lru.h"
#include "claim.h"
#include "lease.h"

Hashtable *lease_by_root_pathname;
Lru *lease_claim_cache;

static int claim_cmp(const Claim *a, const Claim *b) {
    return strcmp(a->pathname, b->pathname);
}

static int lease_cmp(const Lease *a, const Lease *b) {
    return strcmp(a->pathname, b->pathname);
}


void lease_new(char *pathname, Address *addr, int isexit, Claim *claim,
        List *wavefront, int readonly)
{
    Lease *l = GC_NEW(Lease);
    assert(l != NULL);

    l->wait_for_update = NULL;
    l->inflight = 0;
    l->okay_to_change_lease = NULL;

    l->isexit = isexit;

    l->pathname = pathname;
    l->addr = addr;

    l->claim = claim;
    l->wavefront = wavefront;

    l->fids = hash_create(LEASE_FIDS_HASHTABLE_SIZE,
            (Hashfunc) fid_hash,
            (Cmpfunc) fid_cmp);

    l->readonly = 0;

    l->claim_cache = hash_create(
            CLAIM_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) claim_cmp);
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

void lease_flush_claim_cache(Lease *lease) {
    lease->claim_cache = hash_create(
            LEASE_CLAIM_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) claim_cmp);
}

static void claim_cache_cleanup(Claim *claim) {
    hash_remove(claim->lease->claim_cache, claim->pathname);
}

void lease_state_init(void) {
    lease_by_root_pathname = hash_create(
            LEASE_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) lease_cmp);

    lease_claim_cache = lru_new(
            LEASE_CLAIM_LRU_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) claim_cmp,
            NULL,
            (void (*)(void *)) claim_cache_cleanup);
}

void lease_finish_transaction(Lease *lease) {
    lease->inflight--;

    if (lease->inflight == 0 && lease->okay_to_change_lease != NULL)
        cond_signal(lease->okay_to_change_lease);
}

Lease *lease_check_for_lease_change(Lease *lease, char *pathname) {
    return hash_get(lease_by_root_pathname, pathname);
}

Lease *lease_find_remote(char *pathname) {
    Lease *lease = hash_get(lease_by_root_pathname, pathname);
    if (lease != NULL && lease->isexit)
        return lease;
    return NULL;
}

Lease *lease_find_root(char *pathname) {
    Lease *lease = NULL;

    while ((lease = hash_get(lease_by_root_pathname, pathname)) == NULL &&
            strcmp(pathname, "/"))
    {
        pathname = dirname(pathname);
    }

    if (lease != NULL && !lease->isexit)
        return lease;
    return NULL;
}

int lease_is_exit_point_parent(Lease *lease, char *pathname) {
    List *exits = lease->wavefront;

    while (!null(exits)) {
        Lease *exit = car(exits);
        exits = cdr(exits);

        if (!strcmp(pathname, exit->pathname))
            return 1;
    }

    return 0;
}
