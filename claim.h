#ifndef _CLAIM_H_
#define _CLAIM_H_

#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
#include "worker.h"
#include "lru.h"
#include "lease.h"

/* Claims are handles to storage objects.  At most one exists per object in the
 * system.  Multiple fids may exist for a given file, but they all point to the
 * same claim.
 *
 * Claims only exist for locally leased objects.  Forwarding fids do not have
 * local claims.
 *
 * The root of every lease always has an active, non-CoW claim.  The claims
 * that fall under a given lease always form a tree structure, so every claim
 * has a chain of parents back to the root of the lease.  If a claim is
 * required but does not already exist, then the path from the lease root to
 * that node is followed and filled in.
 *
 * The claim for the root of a lease is always writable or readonly.  CoW
 * actions always proceed from that root to the object in question, cloning as
 * necessary.
 *
 * Claims are held for the parents of lease exits and for active fids, as are
 * the claims in the paths to these locations.  Other claims are removed from
 * the tree immediately (on fid clunk, or child removal, etc.) and put in a LRU
 * cache where they can be reclaimed.  When objects are deleted or migrated,
 * matching cache entries are purged.  Also, CoW actions revise the access field
 * of cache entries as necessary.
 */

/*****************************************************************************/
/* Data structure */

struct claim {
    Worker *lock;

    /* fids referring directly to this claim */
    List *fids;

    /* true if this file has been opened and has DMEXCL marked */
    int exclusive;

    /* if the file has been deleted (but not necessary closed) */
    int deleted;

    /* the context */
    Lease *lease;
    /* tree structure */
    Claim *parent;
    /* an ordered list of children */
    List *children;

    /* the full system path of this object */
    char *pathname;
    /* the level of access we have to this object */
    enum claim_access {
        ACCESS_WRITEABLE,
        ACCESS_READONLY,
        ACCESS_COW,
    } access;
    /* the storage system object ID */
    u64 oid;
    /* the file stat record */
    struct p9stat *info;
};

extern Lru *claim_cache;
extern Hashtable *claim_fully_cached_dirs;

void claim_state_init(void);

/*****************************************************************************/
/* High-level functions */

/* Find the claim for a given pathname if it is part of a local lease, doing
 * directory searches as necessary.  This also registers the transaction with
 * the lease.  Returns NULL when the pathname is not under a local lease or does
 * not exist. */
Claim *claim_find(Worker *worker, char *targetname);

/* Find the parent of a given claim.  Returns NULL if the parent is not part of
 * the same lease or does not exist. */
Claim *claim_get_parent(Worker *worker, Claim *child);

/* Find a child of a given claim, doing a directory search if necessary.
 * Returns NULL if the child is not part of the same lease or does not exist. */
Claim *claim_get_child(Worker *worker, Claim *parent, char *name);

/* Make a claim writable, possibly cloning the path from the root of the lease
 * to the given node. */
void claim_thaw(Worker *worker, Claim *claim);

/* Snapshot a claim, which must be the root of a lease.  All descendents
 * within this lease are marker CoW.  Assumes the lease is locked */
void claim_freeze(Worker *worker, Claim *claim);

/*****************************************************************************/
/* Low-level functions */

/* Create a new root claim object for the given lease */
Claim *claim_new_root(char *pathname, enum claim_access access, u64 oid);

/* Create a new claim object in the same lease as the parent */
Claim *claim_new(Claim *parent, char *name, enum claim_access access, u64 oid);

/* Release a claim. */
void claim_release(Claim *claim);
void claim_delete(Claim *claim);

/* add the given claim to the claim cache */
void claim_add_to_cache(Claim *claim);
/* remove a given claim from the cache (usually when activating it) */
void claim_remove_from_cache(Claim *claim);
/* search the cache for a claim for the given pathname */
Claim *claim_lookup_from_cache(Lease *lease, char *pathname);
/* flush all cached claims related to the given lease */
void lease_flush_claim_cache(Lease *lease);

int claim_cmp(const Claim *a, const Claim *b);
int claim_key_cmp(const char *key, const Claim *elt);
u32 claim_hash(const Claim *a);

#endif
