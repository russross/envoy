#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "util.h"
#include "object.h"
#include "envoy.h"
#include "worker.h"
#include "dir.h"
#include "claim.h"
#include "lease.h"

int claim_cmp(const Claim *a, const Claim *b) {
    return strcmp(a->pathname, b->pathname);
}

int claim_key_cmp(const char *key, const Claim *elt) {
    return strcmp(key, elt->pathname);
}

Claim *claim_new_root(char *pathname, enum claim_access access, u64 oid) {
    Claim *claim = GC_NEW(Claim);
    assert(claim != NULL);

    claim->lock = NULL;
    claim->refcount = 0;
    claim->exclusive = 0;

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
    claim->refcount = 0;
    claim->exclusive = 0;

    claim->lease = parent->lease;
    claim->parent = parent;
    claim->children = NULL;

    claim->pathname = concatname(parent->pathname, name);
    claim->access = access;
    claim->oid = oid;
    claim->info = NULL;

    return claim;
}

void claim_release(Claim *claim) {
    /* keep the claim alive if there is an exit point immediately below it */
    if (lease_is_exit_point_parent(claim->lease, claim->pathname))
        return;

    /* delete up the tree as far as we can */
    while (claim->lock == NULL && claim->refcount == 0 &&
            null(claim->children) && claim->parent != NULL)
    {
        Claim *parent = claim->parent;

        /* remove this claim from the parent's children list */
        parent->children =
            removeinorder((Cmpfunc) claim_cmp, parent->children, claim);

        /* put this claim in the cache */
        claim->parent = NULL;
        lease_add_claim_to_cache(claim);

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
    } else if ((claim = lease_lookup_claim_from_cache(parent->lease,
                    targetpath)) != NULL)
    {
        /* it's in the cache */
        reserve(worker, LOCK_CLAIM, claim);
        lease_remove_claim_from_cache(claim);
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
    Lease *lease;
    Claim *claim = NULL;
    char *targetname = dirname(child->pathname);

    /* special case--the root of the hierarchy */
    if (!strcmp(child->pathname, "/"))
        return child;

    /* is the parent within the same lease? */
    if ((claim = child->parent) != NULL) {
        reserve(worker, LOCK_CLAIM, claim);
        return claim;
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
    Claim *child = claim;

    /* start by locking all the claims we'll need */
    do {
        reserve(worker, LOCK_CLAIM, child);
        child = child->parent;
    } while (child != NULL && child->access == ACCESS_COW);

    child = NULL;

    /* now clone up the tree, updating parent directories as we go */
    do {
        if (claim->access == ACCESS_COW) {
            u64 newoid = object_reserve_oid(worker);
            object_clone(worker, claim->oid, newoid);
            claim->oid = newoid;
        }

        if (child != NULL) {
            assert(dir_change_oid(worker, claim, filename(child->pathname),
                        child->oid, ACCESS_WRITEABLE) == 0);
        }

        if (claim->access == ACCESS_COW) {
            claim->access = ACCESS_WRITEABLE;
            child = claim;
        } else {
            break;
        }

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
