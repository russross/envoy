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

    claim->wait = NULL;
    claim->refcount = 0;

    claim->lease = parent->lease;
    claim->parent = parent;
    claim->children = NULL;

    claim->pathname = concatname(parent->pathname, name);
    claim->access = access;
    claim->oid = oid;
    claim->info = NULL;

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
    if (!lease_is_exit_point_parent(claim->lease, claim->pathname)) {
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
    if (lease == NULL || lease->isexit)
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

    /* find the root of the lease */
    lease = lease_find_root(targetname);

    /* we only care about local grants and leases */
    if (lease == NULL || lease->isexit)
        return NULL;

    /* lock the lease */
    lock_lease(worker, lease);

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

    unlock_lease(worker, lease);
    return NULL;
}


Claim *claim_get_child(Worker *worker, Claim *parent, char *name) {
    List *children;
    char *targetpath = concatname(parent->pathname, name);
    Lease *lease;
    Claim *claim;

    /* see if we need to jump to a new lease */
    lease = lease_check_for_lease_change(parent->lease, targetpath);
    if (lease != NULL) {
        if (lease->isexit)
            return NULL;

        assert(!strcmp(targetpath, lease->pathname));
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

/* ensure that the given claim is writable */
void claim_thaw(Worker *worker, Claim *claim) {
}

Claim *claim_get_pathname(Worker *worker, char *targetname) {
    Lease *lease = lease_find_root(targetname);
    Claim *claim;
    List *names;
    int len;

    if (lease == NULL)
        return NULL;

    /* lock the lease */
    lock_lease(worker, lease);

    /* get a list of path parts to navigate */
    assert(startswith(targetname, lease->pathname));
    len = strlen(lease->pathname);
    names = splitpath(substring(targetname, len, strlen(targetname) - len));
    claim = lease->claim;

    while (!null(names)) {
        claim = claim_get_child(worker, claim, (char *) car(names));

        if (claim == NULL) {
            unlock_lease(worker, lease);
            return NULL;
        }

        names = cdr(names);
    }

    return claim;
}
