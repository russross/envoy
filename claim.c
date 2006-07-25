#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
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
Hashtable *claim_fully_cached_dirs;

int claim_cmp(const Claim *a, const Claim *b) {
    return strcmp(a->pathname, b->pathname);
}

int claim_key_cmp(const char *key, const Claim *elt) {
    return strcmp(key, elt->pathname);
}

u32 claim_hash(const Claim *a) {
    return string_hash(a->pathname);
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
    claim->parent = parent;
    claim->children = NULL;

    claim->pathname = concatname(parent->pathname, name);
    claim->access = access;
    claim->oid = oid;
    claim->info = NULL;

    return claim;
}

void claim_delete(Claim *claim) {
    assert(null(claim->children));
    assert(!claim->deleted);
    assert(claim->parent != NULL);

    /* remove from the parent's children list */
    claim->parent->children =
        removeinorder((Cmpfunc) claim_cmp, claim->parent->children, claim);

    claim->parent = NULL;
    claim->deleted = 1;
    claim->pathname = NULL;

    if (!null(claim->fids)) {
        List *fids = claim->fids;
        for (fids = claim->fids; !null(fids); fids = cdr(fids)) {
            Fid *fid = car(fids);
            if (fid->claim != NULL)
                hash_remove(fid->claim->lease->fids, fid);
            fid->pathname = NULL;
            fid_deleted_list = insertinorder((Cmpfunc) fid_cmp,
                    fid_deleted_list, fid);
        }
    }
}

struct claim_clear_descendents_env {
    List *toremove;
    char *prefix;
};

static void claim_clear_descendents_iter(
        struct claim_clear_descendents_env *env, void *key, Claim *value)
{
    if (startswith(value->pathname, env->prefix))
        env->toremove = cons(value, env->toremove);
}

void claim_clear_descendents(Claim *claim) {
    List *stack = cons(claim, NULL);
    struct claim_clear_descendents_env env;

    env.toremove = NULL;
    env.prefix = concatname(claim->pathname, "/");

    /* clear the claim cache */
    hash_apply(claim->lease->claim_cache,
            (void (*)(void *, void *, void *)) claim_clear_descendents_iter,
            &env);
    for ( ; !null(env.toremove); env.toremove = cdr(env.toremove))
        claim_remove_from_cache((Claim *) car(env.toremove));

    /* clear the claim tree */
    while (!null(stack)) {
        Claim *elt = car(stack);
        stack = cdr(stack);

        assert(null(elt->fids));
        elt->parent = NULL;
        elt->lease = NULL;
        elt->pathname = NULL;
        for ( ; !null(elt->children); elt->children = cdr(elt->children))
            stack = cons(car(elt->children), stack);
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

        /* remove this claim from the parent's children list */
        parent->children =
            removeinorder((Cmpfunc) claim_cmp, parent->children, claim);

        /* put this claim in the cache */
        claim->parent = NULL;
        claim_add_to_cache(claim);

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
        if (claim->deleted)
            return NULL;

        /* it's already on a live path */
        reserve(worker, LOCK_CLAIM, claim);
        return claim;
    } else if ((claim = claim_lookup_from_cache(parent->lease,
                    targetpath)) != NULL)
    {
        assert(!claim->deleted);

        /* it's in the cache */
        reserve(worker, LOCK_CLAIM, claim);
        claim_remove_from_cache(claim);
        claim->parent = parent;
        parent->children =
            insertinorder((Cmpfunc) claim_cmp, parent->children, claim);
    } else if ((claim = dir_find_claim(worker, parent, name)) != NULL) {
        /* found through a directory search */
        reserve(worker, LOCK_CLAIM, claim);
        parent->children =
            insertinorder((Cmpfunc) claim_cmp, parent->children, claim);
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

    /* start by locking all the claims we'll need */
    while (branch != NULL && !branch->deleted) {
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

        if (claim->deleted)
            claim = NULL;
        else
            claim = claim->parent;
    } while (claim != NULL);
}

void claim_freeze(Worker *worker, Claim *root) {
    List *stack = cons(root, NULL);

    assert(root->access != ACCESS_READONLY);

    /* mark all active claims */
    while (!null(stack)) {
        Claim *claim = car(stack);
        stack = cdr(stack);
        if (claim->access == ACCESS_WRITEABLE) {
            List *children = claim->children;
            for ( ; !null(children); children = cdr(children))
                stack = cons(car(children), stack);
            claim->access = ACCESS_COW;
        }
    }
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
    if (claim == NULL)
        return NULL;

    /* refresh the LRU entry and verify the hit */
    assert(lru_get(claim_cache, pathname) == claim);
    return claim;
}

static void claim_cache_cleanup(Claim *claim) {
    hash_remove(claim->lease->claim_cache, claim->pathname);

    /* the parent directory is no longer fully cached */
    /*hash_remove(claim_fully_cached_dirs, dirname(claim->pathname));*/
}

void claim_state_init(void) {
    claim_cache = lru_new(
            CLAIM_LRU_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp,
            NULL,
            (void (*)(void *)) claim_cache_cleanup);

    claim_fully_cached_dirs = hash_create(
            CLAIM_FULLY_CACHED_DIRS_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);
}
