#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
#include "connection.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "object.h"
#include "worker.h"
#include "lru.h"
#include "dir.h"
#include "claim.h"
#include "lease.h"

Lru *claim_cache;

int claim_cmp(const Claim *a, const Claim *b) {
    return strcmp(a->pathname, b->pathname);
}

int claim_key_cmp(const char *key, const Claim *elt) {
    return strcmp(key, elt->pathname);
}

u32 claim_hash(const Claim *a) {
    return string_hash(a->pathname);
}

void claim_link_child(Claim *parent, Claim *child) {
    assert(child->parent == NULL);
    parent->children =
        insertinorder((Cmpfunc) claim_cmp, parent->children, child);
    child->parent = parent;
}

void claim_unlink_child(Claim *child) {
    assert(child->parent != NULL);
    child->parent->children =
        removeinorder((Cmpfunc) claim_cmp, child->parent->children, child);
    child->parent = NULL;
}

Claim *claim_new_root(char *pathname, enum claim_access access, u64 oid) {
    Claim *claim = GC_NEW(Claim);
    assert(claim != NULL);

    claim->lock = NULL;
    claim->fids = NULL;
    claim->exclusive = 0;
    claim->deleted = 0;

    claim->lease = NULL;
    claim->parent = NULL;
    claim->children = NULL;

    claim->lastupdate = 0.0;
    claim->urgencycount = 0;
    claim->urgency = NULL;

    claim->pathname = pathname;
    claim->access = access;
    claim->oid = oid;
    claim->info = NULL;

    return claim;
}

Claim *claim_new(Claim *parent, char *name, enum claim_access access, u64 oid) {
    Claim *claim = GC_NEW(Claim);
    assert(claim != NULL);
    assert(parent != NULL);

    claim->lock = NULL;
    claim->fids = NULL;
    claim->exclusive = 0;
    claim->deleted = 0;

    claim->lease = parent->lease;
    claim->children = NULL;

    claim->lastupdate = 0.0;
    claim->urgencycount = 0;
    claim->urgency = NULL;

    claim->pathname = concatname(parent->pathname, name);
    claim->access = access;
    claim->oid = oid;
    claim->info = NULL;

    claim_link_child(parent, claim);
    claim_add_to_cache(claim);

    return claim;
}

void claim_delete(Claim *claim) {
    List *fids;

    assert(claim->lock != NULL);
    assert(null(claim->children));
    assert(!claim->deleted);
    assert(claim->parent != NULL);

    claim_remove_from_cache(claim);

    claim_unlink_child(claim);
    claim->pathname = NULL;
    claim->deleted = 1;

    for (fids = claim->fids; !null(fids); fids = cdr(fids)) {
        Fid *fid = car(fids);
        hash_remove(claim->lease->fids, fid);
        fid->pathname = NULL;
        fid_link_deleted(fid);
    }

    claim->lease = NULL;
}

void claim_clear_descendents(Claim *claim) {
    List *allclaims = hash_tolist(claim->lease->claim_cache);
    char *prefix = claim->pathname;

    for ( ; !null(allclaims); allclaims = cdr(allclaims)) {
        Claim *elt = car(allclaims);
        if (ispathprefix(elt->pathname, prefix)) {
            assert(null(elt->fids));
            claim_remove_from_cache(elt);
            if (elt->parent != NULL)
                claim_unlink_child(elt);
            elt->lease = NULL;
            elt->pathname = NULL;
        }
    }
}

void claim_release(Claim *claim) {
    /* is this a deleted entry being clunked? */
    if (claim->deleted || claim->lease == NULL)
        return;

    /* keep the claim alive if there is an exit point immediately below it */
    if (lease_is_exit_point_parent(claim->lease, claim->pathname))
        return;

    /* delete up the tree as far as we can */
    while (claim->lock == NULL && null(claim->fids) &&
            null(claim->children) && claim->parent != NULL)
    {
        Claim *parent = claim->parent;
        claim_unlink_child(claim);
        claim = parent;
    }
}

/*****************************************************************************/
/* High-level functions */

Claim *claim_get_child(Worker *worker, Claim *parent, char *name) {
    char *targetpath = concatname(parent->pathname, name);
    Claim *claim = NULL;

    if (lease_get_remote(targetpath) != NULL) {
        /* the child isn't part of this lease */
    } else if ((claim = findinorder((Cmpfunc) claim_key_cmp,
                    parent->children, targetpath)) != NULL)
    {
        /* it's already on a live path */
        reserve(worker, LOCK_CLAIM, claim);
        return claim;
    } else if ((claim = claim_lookup_from_cache(parent->lease,
                    targetpath)) != NULL)
    {
        /* it's in the cache */
        reserve(worker, LOCK_CLAIM, claim);
        claim_link_child(parent, claim);
    } else if ((claim = dir_find_claim(worker, parent, name)) != NULL) {
        /* found through a directory search */
        assert(claim->parent == parent && claim->lock == worker);
    }

    return claim;
}

Claim *claim_get_parent(Worker *worker, Claim *child) {
    /* special case--the root of the hierarchy */
    if (!strcmp(child->pathname, "/"))
        return child;

    /* is the parent within the same lease? */
    if (child->parent != NULL) {
        reserve(worker, LOCK_CLAIM, child->parent);
        return child->parent;
    }

    return NULL;
}

Claim *claim_find(Worker *worker, char *targetname) {
    Lease *lease = lease_find_root(targetname);
    Claim *claim;
    List *pathparts;

    /* we only care about local leases */
    if (lease == NULL || lease->isexit)
        return NULL;

    /* lock the lease */
    lock_lease(worker, lease);

    /* walk from the root of the lease to our node */
    pathparts = splitpath(targetname + strlen(lease->pathname));

    claim = lease->claim;
    reserve(worker, LOCK_CLAIM, claim);

    while (claim != NULL && !null(pathparts) &&
            strcmp(targetname, claim->pathname))
    {
        char *name = car(pathparts);
        pathparts = cdr(pathparts);
        claim = claim_get_child(worker, claim, name);
    }

    if (claim != NULL && !strcmp(targetname, claim->pathname))
        return claim;

    return NULL;
}

/* ensure that the given claim (which must be active) is writable */
void claim_thaw(Worker *worker, Claim *claim) {
    Claim *branch = claim;
    Claim *child = NULL;

    if (claim->deleted) {
        if (claim->access == ACCESS_COW) {
            u64 newoid = object_reserve_oid(worker);
            object_clone(worker, claim->oid, newoid);
            claim->oid = newoid;
        }
        return;
    }

    /* start by locking all the claims we'll need */
    while (branch != NULL) {
        reserve(worker, LOCK_CLAIM, branch);
        if (branch->access == ACCESS_WRITEABLE)
            break;
        branch = branch->parent;
    }

    /* now clone up the tree, updating parent directories as we go */
    do {
        if (claim->access == ACCESS_COW) {
            u64 newoid = object_reserve_oid(worker);
            object_clone(worker, claim->oid, newoid);
            claim->oid = newoid;
        }

        if (child != NULL) {
            u64 oldoid = dir_change_oid(worker, claim,
                    filename(child->pathname), child->oid, 0);
            assert(oldoid != NOOID);
        }

        if (claim->access == ACCESS_COW) {
            claim->access = ACCESS_WRITEABLE;
            child = claim;
        } else {
            break;
        }
    } while ((claim = claim->parent) != NULL);
}

void claim_add_to_cache(Claim *claim) {
    /* note: important to do the lru_add first, as it may contain a stale entry
     * and try to do a hash_remove when clearing it */
    lru_add(claim_cache, claim->pathname, claim);
    hash_set(claim->lease->claim_cache, claim->pathname, claim);
}

void claim_remove_from_cache(Claim *claim) {
    lru_remove(claim_cache, claim->pathname);
}

Claim *claim_lookup_from_cache(Lease *lease, char *pathname) {
    Claim *claim = hash_get(lease->claim_cache, pathname);
    if (claim != NULL)
        assert(lru_get(claim_cache, pathname) == claim);
    return claim;
}

void claim_rename(Claim *claim, char *pathname) {
    List *fids;
    Claim *parent = claim->parent;

    if (parent != NULL)
        claim_unlink_child(claim);

    /* change the name and update the cache index */
    claim_remove_from_cache(claim);
    claim->pathname = pathname;
    claim_add_to_cache(claim);

    /* update the fids */
    for (fids = claim->fids; !null(fids); fids = cdr(fids)) {
        Fid *fid = car(fids);
        fid->pathname = pathname;
    }

    if (parent != NULL)
        claim_link_child(parent, claim);

    if (claim->info != NULL)
        claim->info->name = filename(pathname);
}

static void claim_cache_cleanup(Claim *claim) {
    hash_remove(claim->lease->claim_cache, claim->pathname);
}

static int claim_cache_resurrect(Claim *claim) {
    return claim->parent != NULL || !null(claim->children) ||
        !strcmp(claim->pathname, claim->lease->pathname);
}

Claim *claim_update_territory_move(Claim *claim, Connection *conn) {
    int i;
    double now = now_double();
    Lease *lease = claim->lease;
    Claim *mosturgent = NULL;
    double mostgain = 0.0;
    double gain;

    assert(conn->type == CONN_ENVOY_IN || conn->type == CONN_CLIENT_IN);

    if (ter_disabled || claim->deleted)
        return NULL;

    /* walk up the tree updating counters */
    do {
        /* we don't migrate admin directories */
        if (get_admin_path_type(claim->pathname) == PATH_ADMIN)
            break;

        /* has the territory changed since this file was visited? */
        if (claim->lastupdate < lease->lastchange) {
            for (i = 0; i < claim->urgencycount; i++)
                claim->urgency[i] = 0.0;
            claim->lastupdate = now;
        } else {
            /* we need to multiply each counter
             * by (1/2)^(halflifes that have elapsed) */
            double decay = pow(0.5, (now - claim->lastupdate) / ter_halflife);

            /* decay the existing counters */
            for (i = 0; i < claim->urgencycount; i++)
                claim->urgency[i] *= decay;
            claim->lastupdate = now;
        }

        /* first transaction from this source? */
        if (claim->urgencycount <= conn->envoyindex) {
            double *urgency =
                GC_MALLOC_ATOMIC(sizeof(double) * (conn->envoyindex + 1));
            assert(urgency != NULL);
            for (i = 0; i <= conn->envoyindex; i++)
                if (i < claim->urgencycount)
                    urgency[i] = claim->urgency[i];
                else
                    urgency[i] = 0.0;
            claim->urgency = urgency;
            claim->urgencycount = conn->envoyindex + 1;
        }

        /* record this transaction */
        claim->urgency[conn->envoyindex] += 1.0;

        /* for an envoy proxy request, see if this is the most compelling move
         * so far, favoring higher-level moves over lower-level moves.
         * no move if the envoy is still connecting (null addr) */
        if (conn->envoyindex > 0 && conn->addr != NULL &&
                (gain = claim->urgency[conn->envoyindex] - claim->urgency[0]) >
                mostgain - EPSILON)
        {
            mostgain = gain;
            mosturgent = claim;
        }

        claim = claim->parent;
    } while (claim != NULL);

    if (DEBUG_TRANSFER && mosturgent != NULL) {
        printf("Most urgent candidate: [%s] to [%s]\n",
                mosturgent->pathname, addr_to_string(conn->addr));
        printf("Time since last move: [%3g] required for this move: [%3g]\n",
                now - lease->lastchange,
                ter_mintime + (ter_urgent - mostgain) * ter_rate);
    }

    /* did we find a move worth making? */
    if (mosturgent != NULL &&
            conn->envoyindex > 0 && now - lease->lastchange > ter_mintime)
    {
        /* how long should this transfer be delayed? */
        double delay = ter_mintime + (ter_urgent - mostgain) * ter_rate;

        /* have we already waited that long? is it too minor? */
        if (delay < now - lease->lastchange && delay < ter_maxtime) {
            if (DEBUG_VERBOSE || DEBUG_TRANSFER) {
                printf("Territory migration recommended: [%s] to [%s]\n",
                        mosturgent->pathname, addr_to_string(conn->addr));
            }

            return mosturgent;
        }
    }

    return NULL;
}

void claim_state_init(void) {
    claim_cache = lru_new(
            CLAIM_LRU_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp,
            (int (*)(void *)) claim_cache_resurrect,
            (void (*)(void *)) claim_cache_cleanup);
}
