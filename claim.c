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
#include "worker.h"
#include "lru.h"
#include "dir.h"
#include "claim.h"
#include "lease.h"

Hashtable *path_2_claim;
Lru *claim_lru;

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

    claim->lease = NULL;
    claim->parent = NULL;
    claim->children = NULL;

    claim->pathname = pathname;
    claim->access = access;
    claim->oid = oid;
    claim->parent_oid = NOOID;
    claim->info = NULL;

    return claim;
}

Claim *claim_new(Claim *parent, char *name, enum claim_access access, u64 oid) {
    Claim *claim = GC_NEW(Claim);
    assert(claim != NULL);
    assert(parent != NULL);

    claim->lock = NULL;
    claim->refcount = 0;

    claim->lease = parent->lease;
    claim->parent = parent;
    claim->children = NULL;

    claim->pathname = concatname(parent->pathname, name);
    claim->access = access;
    claim->oid = oid;
    claim->parent_oid = parent->oid;
    claim->info = NULL;

    return claim;
}

int claim_request(Worker *worker, Claim *claim) {
    if (claim->refcount < 0)
        return -1;
    reserve(worker, LOCK_CLAIM, claim);
    claim->refcount++;
    return 0;
}

void claim_release(Claim *claim) {
    if (claim->refcount == -1) {
        claim->refcount++;
    } else {
        assert(claim->refcount > 0);
        claim->refcount--;
    }

    /* keep the claim alive if there is an exit point immediately below it */
    if (lease_is_exit_point_parent(claim->lease, claim->pathname))
        return;

    /* delete up the tree as far as we can */
    while (claim->refcount == 0 &&
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

    /* is the child not part of this lease? */
    if (lease_get_remote(targetpath) != NULL)
        goto exit;

    /* check if it's already on a live path */
    claim = findinorder((Cmpfunc) claim_key_cmp, parent->children, targetpath);
    if (claim != NULL) {
        if (claim_request(worker, claim) < 0)
            claim = NULL;
        goto exit;
    }

    /* check the cache */
    claim = lease_lookup_claim_from_cache(parent->lease, targetpath);
    if (claim != NULL) {
        /* it came from the cache, so it can't already be locked */
        assert(claim_request(worker, claim) == 0);
        claim->parent = parent;
        /* if the parent oid changed, then there has been a snapshot */
        claim->access =
            fid_access_child(claim->access, claim->parent_oid != parent->oid);
        claim->parent_oid = parent->oid;
        goto addtoparent;
    }

    /* do a directory lookup to find this name */
    claim = dir_find_claim(worker, parent, name);
    if (claim != NULL) {
        /* it was just created and can't be locked */
        assert(claim_request(worker, claim) == 0);
        goto addtoparent;
    }

    goto exit;

    addtoparent:
    parent->children =
        insertinorder((Cmpfunc) claim_cmp, parent->children, claim);

    exit:
    claim_release(parent);
    return claim;
}

static Claim *claim_walk_down_to_target(Worker *worker, Lease *lease,
        char *targetname)
{
    Claim *claim;
    List *pathparts;

    /* walk from the root of the lease to our node */
    assert(startswith(targetname, lease->pathname));
    pathparts = splitpath(targetname + strlen(lease->pathname));

    claim = lease->claim;
    if (claim_request(worker, claim) < 0)
        return NULL;

    while (claim != NULL && !null(pathparts) &&
            strcmp(targetname, claim->pathname))
    {
        char *name = car(pathparts);
        claim = claim_get_child(worker, claim, name);
        pathparts = cdr(pathparts);
    }

    if (claim != NULL && strcmp(targetname, claim->pathname)) {
        claim_release(claim);
        return NULL;
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

    /* simple case--this is within a single lease */
    if (child->parent != NULL) {
        if (claim_request(worker, child->parent) == 0)
            claim = child->parent;
        goto exit;
    }

    /* find the root of the lease */
    lease = lease_find_root(targetname);

    /* we only care about local grants and leases */
    if (lease == NULL || lease->isexit)
        goto exit;

    claim = claim_walk_down_to_target(worker, lease, targetname);

    exit:
    claim_release(child);
    return claim;
}

Claim *claim_find(Worker *worker, char *targetname) {
    Lease *lease = lease_find_root(targetname);
    Claim *claim;

    /* we only care about local leases */
    if (lease == NULL || lease->isexit)
        return NULL;

    /* lock the lease */
    lock_lease(worker, lease);

    claim = claim_walk_down_to_target(worker, lease, targetname);

    if (claim == NULL);
        unlock_lease(worker, lease);

    return claim;
}

/* ensure that the given claim is writable */
void claim_thaw(Worker *worker, Claim *claim) {
}
