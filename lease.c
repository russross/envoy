#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "envoy.h"
#include "remote.h"
#include "worker.h"
#include "lru.h"
#include "dir.h"
#include "claim.h"
#include "lease.h"

Hashtable *lease_by_root_pathname;
Lru *lease_claim_cache;

static int lease_cmp(const Lease *a, const Lease *b) {
    return strcmp(a->pathname, b->pathname);
}

Lease *lease_new(char *pathname, Address *addr, int isexit, Claim *claim,
        List *wavefront, int readonly)
{
    Lease *l = GC_NEW(Lease);
    assert(l != NULL);

    l->wait_for_update = NULL;
    l->inflight = 0;

    l->pathname = pathname;
    l->addr = addr;

    l->isexit = isexit;

    l->claim = claim;
    l->claim->lease = l;

    l->wavefront = NULL;
    while (!null(wavefront)) {
        l->wavefront =
            insertinorder((Cmpfunc) lease_cmp, l->wavefront, car(wavefront));
        wavefront = cdr(wavefront);
    }

    l->fids = hash_create(LEASE_FIDS_HASHTABLE_SIZE,
            (Hashfunc) fid_hash,
            (Cmpfunc) fid_cmp);

    l->readonly = 0;

    l->claim_cache = hash_create(
            LEASE_CLAIM_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);

    return l;
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
    Claim *claim = hash_get(lease->claim_cache, pathname);
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
            (Cmpfunc) strcmp);
}

static void claim_cache_cleanup(Claim *claim) {
    hash_remove(claim->lease->claim_cache, claim->pathname);
}

void lease_state_init(void) {
    lease_by_root_pathname = hash_create(
            LEASE_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);

    lease_claim_cache = lru_new(
            LEASE_CLAIM_LRU_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp,
            NULL,
            (void (*)(void *)) claim_cache_cleanup);
}

Lease *lease_get_remote(char *pathname) {
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
    return findinorder((Cmpfunc) lease_cmp, lease->wavefront, pathname) != NULL;
}

void lease_add(Lease *lease) {
    List *exits = lease->wavefront;

    assert(!lease->isexit);

    hash_set(lease_by_root_pathname, lease->pathname, lease);
    while (!null(exits)) {
        Lease *exit = car(exits);
        exits = cdr(exits);
        assert(exit->isexit);
        hash_set(lease_by_root_pathname, exit->pathname, exit);
    }
}

static void make_claim_cow(char *env, char *pathname, Claim *claim) {
    if (startswith(pathname, env) && claim->access == ACCESS_WRITEABLE)
        claim->access = ACCESS_COW;
}

u64 lease_snapshot(Worker *worker, Claim *claim) {
    List *allexits = claim->lease->wavefront;
    List *exits = NULL;
    List *newoids;
    char *prefix = concatstrings(claim->pathname, "/");

    /* start by freezing everything */
    claim_freeze(worker, claim);
    hash_apply(claim->lease->claim_cache,
            (void (*)(void *, void *, void *)) make_claim_cow,
            prefix);

    /* recursively snapshot all the child leases */
    for ( ; !null(allexits); allexits = cdr(allexits)) {
        Lease *lease = car(allexits);
        if (startswith(lease->pathname, prefix))
            exits = cons(lease, exits);
    }
    newoids = remote_snapshot(worker, exits);

    /* next clone the root */
    claim_thaw(worker, claim);

    /* now clone paths to the exits and update the exit parent dirs */
    while (!null(exits) && !null(newoids)) {
        Lease *exit = car(exits);
        u64 *newoid = car(newoids);
        Claim *parent;

        parent = claim_find(worker, dirname(exit->pathname));
        claim_thaw(worker, parent);
        assert(dir_change_oid(worker, parent,
                filename(exit->pathname), *newoid, ACCESS_WRITEABLE) == 0);

        exits = cdr(exits);
        newoids = cdr(newoids);
    }

    return claim->oid;
}

void lease_audit(Lease *lease) {
    printf("lease_audit: %s\n", lease->pathname);
    assert(lease->wait_for_update == NULL);
    assert(lease->inflight == 0);
    if (lease->isexit) {
        assert(lease->claim == NULL);
        assert(null(lease->wavefront));
        assert(lease->fids == NULL);
        assert(!lease->readonly);
        assert(lease->claim_cache == NULL);
    } else {
        /* walk the claim tree, verifying links and gathering claims with
         * non-zero refcounts */
        Hashtable *inuse = hash_create(
                64,
                (Hashfunc) claim_hash,
                (Cmpfunc) claim_cmp);
        List *stack = cons(lease->claim, NULL);
        while (!null(stack)) {
            Claim *claim = car(stack);
            claim = cdr(stack);
            assert(claim->lock == NULL);
            assert(!claim->deleted);
            assert(claim->lease == lease);
            if (!null(claim->children)) {
                List *children = claim->children;
                while (!null(children)) {
                    Claim *child = car(children);
                    children = cdr(children);
                    assert(child->parent == claim);
                    assert(!strcmp(child->pathname,
                                concatname(claim->pathname,
                                    filename(child->pathname))));
                    stack = cons(child, stack);
                }
            } else {
                assert(claim->refcount != 0 || claim == lease->claim);
            }
            if (claim->refcount != 0) {
                int *refcount = GC_NEW_ATOMIC(int);
                assert(refcount != NULL);
                assert(hash_get(inuse, claim) == NULL);
                *refcount = claim->refcount;
                hash_set(inuse, claim, refcount);
            }
        }

        /* step through the fids and verify refcounts */
    }
}
