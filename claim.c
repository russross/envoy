#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
#include "util.h"
#include "worker.h"
#include "lru.h"
#include "dir.h"
#include "claim.h"
#include "lease.h"

Hashtable *path_2_claim;
Lru *claim_lru;

Claim *claim_new_root(char *pathname, enum claim_access access, u64 oid) {
    Claim *claim = GC_NEW(Claim);
    assert(claim != NULL);

    claim->wait = NULL;
    claim->refcount = 0;

    claim->parent = NULL;
    claim->children = NULL;

    claim->pathname = pathname;
    claim->access = access;
    claim->oid = oid;

    return claim;
}

Claim *claim_new(Claim *parent, char *name, enum claim_access access, u64 oid) {
    Claim *claim = GC_NEW(Claim);
    assert(claim != NULL);
    assert(parent != NULL);

    claim->wait = NULL;
    claim->refcount = 0;

    claim->parent = parent;
    claim->children = NULL;

    claim->pathname = concatname(parent->pathname, name);
    claim->access = access;
    claim->oid = oid;

    return claim;
}

int claim_request(Claim *claim) {
    if (claim->refcount < 0)
        return -1;
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

    /* should we close down this claim? */
    if (!lease_is_exit_point_parent(claim->pathname)) {
        Claim *elt = claim;

        /* delete up the tree as far as we can */
        while (elt->wait == NULL && elt->refcount == 0 &&
                null(claim->children) && elt->parent != NULL)
        {
            List *list = claim->parent->children;
            assert(!null(list));

            /* remove this claim from the parent's children list */
            if (car(list) == elt) {
                claim->parent->children = cdr(list);
            } else {
                assert(!null(cdr(list)));
                while (cadr(list) != elt) {
                    list = cdr(list);
                    assert(!null(cdr(list)));
                }
                setcdr(list, cddr(list));
            }
        }
    }
}

/*****************************************************************************/
/* High-level functions */

Claim *claim_get_parent(Worker *worker, Claim *child) {
    Lease *lease;
    Claim *claim;
    List *pathparts;
    char *pathname;
    char *targetname;

    /* simple case--this is within a single lease */
    if (child->parent != NULL) {
        if (claim_request(child->parent) < 0)
            return NULL;
        else
            return child->parent;
    }

    /* special case--the root of the hierarchy */
    if (!strcmp(child->pathname, "/"))
        return NULL;

    /* find the root of the lease */
    targetname = dirname(child->pathname);
    lease = lease_find_root(targetname);

    /* we only care about local grants and leases */
    if (lease == NULL || lease->type == LEASE_REMOTE)
        return NULL;

    /* walk from the root of the lease to our node */
    assert(startswith(targetname, lease->pathname));
    pathparts = splitpath(targetname + strlen(lease->pathname));

    claim = lease->claim;
    if (claim_request(claim) < 0)
        return NULL;

    while (claim != NULL && !null(pathparts) &&
            strcmp(targetname, claim->pathname))
    {
        char *name = car(pathparts);
        Claim *next = claim_get_child(worker, claim, name);

        /* claim_get_child does a request for us, but we need to release the
         * parent at each step */
        claim_release(claim);

        claim = next;
        pathparts = cdr(pathparts);
    }

    if (claim == NULL || strcmp(targetname, claim->pathname))
        return NULL;

    return claim;
}

Claim *claim_find(Worker *worker, char *targetname) {
    Lease *lease;
    Claim *claim;
    List *pathparts;
    char *pathname;

    /* loop in case the lease changes before we lock it */
    do {
        /* find the root of the lease */
        lease = lease_find_root(targetname);

        /* we only care about local grants and leases */
        if (lease == NULL || lease->type == LEASE_REMOTE)
            return NULL;

        /* lock the lease */
    } while (lease_start_transaction(worker, lease) < 0);

    /* walk from the root of the lease to our node */
    assert(startswith(targetname, lease->pathname));
    pathparts = splitpath(targetname + strlen(lease->pathname));

    claim = lease->claim;
    if (claim_request(claim) < 0)
        goto release_lease;

    while (claim != NULL && !null(pathparts) &&
            strcmp(targetname, claim->pathname))
    {
        char *name = car(pathparts);
        Claim *next = claim_get_child(worker, claim, name);

        /* claim_get_child does a claim_request for us, but we need to release
         * the parent at each step */
        claim_release(claim);

        claim = next;
        pathparts = cdr(pathparts);
    }

    if (claim == NULL || strcmp(targetname, claim->pathname))
        goto release_lease;

    return claim;

    /* failed exit */
    release_lease:

    lease_finish_transaction(worker, lease);
    return NULL;
}


Claim *claim_get_child(Worker *worker, Claim *parent, char *name) {
    List *children;
    char *targetpath = concatname(parent->pathname, name);
    Lease *lease;
    Claim *claim;

    /* see if we need to jump to a new lease */
    lease = lease_check_for_lease_change(parent->lease, targetname);
    if (lease != NULL) {
        if (lease->type == LEASE_REMOTE)
            return NULL;

        assert(!strcmp(targetname, lease->pathname));
        claim = lease->claim;
        if (claim_request(claim) < 0)
            return NULL;

        /* TODO: register in-flight transaction with the new lease... */
        return claim;
    }

    /* check if it already exists on an active path */
    for (children = parent->children; !null(children);
            children = cdr(children))
    {
        claim = car(children);
        if (!strcmp(targetpath, claim->pathname)) {
            if (claim_request(claim) < 0)
                return NULL;
            return claim;
        }
    }

    /* check the cache */
    claim = lease_lookup_claim_from_cache(parent->lease, targetpath);
    if (claim != NULL) {
        if (claim_request(claim) < 0)
            return NULL;
        else
            return claim;
    }

    /* do a directory lookup to find this name */
    claim = dir_find_claim(worker, parent, name);
    if (claim != NULL && claim_request(claim) == 0)
        return claim;

    return NULL;
}
