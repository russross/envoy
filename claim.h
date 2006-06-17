#ifndef _CLAIM_H_
#define _CLAIM_H_

#include <pthread.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <unistd.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
#include "util.h"
#include "worker.h"
#include "lru.h"
#include "lease.h"

/* Claims are handles to storage objects.  At most one exists per object in the
 * local system (leases may allow read-only duplicates in the distributed
 * system).  Multiple fids may exist for a given file, but they all point to
 * the same claim.
 *
 * Claims only exist for objects owned or leased locally.  Forwarding fids do
 * not have local claims.
 *
 * The root of every ownership or lease always has an active claim.  The claims
 * that fall under a given lease always form a tree structure, so every claim
 * has a chain of parents back to the root of the lease.  If a claim is
 * required but does not already exist, then the path from the lease root to
 * that node is followed and filled in.  This normally happens when walking
 * down from existing claims, but it can also happen at lease boundaries when
 * remote envoys walk up the tree into a local lease.
 *
 * The claim for the root of an ownership is always writable.  CoW actions
 * always proceed from that root to the object in question, cloning as
 * necessary.
 *
 * Claims are held for lease leaves and for active fids, as are the claims in
 * the paths to these locations.  Other claims are removed from the tree
 * immediately (on fid clunk, or child removal, etc.) and put in a LRU cache
 * where they can be reclaimed.  When objects are deleted or migrated, matching
 * cache entries are purged.  Also, CoW actions may have to revise the access
 * field of cache entries.
 */

/*****************************************************************************/
/* High-level functions */

/* Find the claim for a given pathname if it is part of a local lease, doing
 * directory searches as necessary.  This also registers the transaction with
 * the lease, which should be completed eventually.  In addition, the result
 * Claim should be passed to claim_release eventually.  Returns NULL when the
 * pathname is not under a local lease or does not exist. */
Claim *claim_get_pathname(Worker *worker, char *targetname);

/* Find the parent of a given claim, possible doing directory searches as
 * necessary.  Returns NULL if the parent is not part of the same lease or does
 * not exist.  The result should be passed to claim_release eventually.
 * claim_release release is always called on the child argument. */
Claim *claim_get_parent(Worker *worker, Claim *child);

/* As with claim_get_parent, but find a child of a given claim. */
Claim *claim_get_child(Worker *worker, Claim *parent, char *name);

/* Get exclusive access to a claim previously obtained from claim_get_*.
 * Returns 0 on success, -1 on failure. */
int claim_make_exclusive(Claim *claim);

/* Make a claim writable, possibly cloning the path from the root of this
 * ownership to the given node. */
void claim_thaw(Worker *worker, Claim *claim);

/* Snapshot a claim, which must be the root of an ownership.  All descendents
 * within this ownership are marker CoW, the root claim is cloned, and all
 * paths to child leases are cloned.  Returns the new OID of the root node. */
u64 claim_freeze(Worker *worker, Claim *claim);

/* Scan the given directory, looking for the given filename.  Returns a claim
 * for the file if found (and if it is part of the same lease), with the
 * side-effect of priming the cache with other entries from the same directory
 * (excluding the returned entry and entries outside the lease). */
Claim *claim_scan_directory(Worker *worker, Claim *dir, char *name);

/*****************************************************************************/
/* Data structures and functions */


enum claim_access {
    ACCESS_WRITEABLE,
    ACCESS_READONLY,
    ACCESS_COW,
};

struct claim {
    pthread_cond_t *wait;

    /* number of clients for this file (fids or directory walks), -1 if it's
     * exclusive */
    int refcount;

    /* the context */
    Lease *lease;
    /* tree structure */
    Claim *parent;
    /* an ordered list of children */
    List *children;

    /* the full system path of this object */
    char *pathname;
    /* the level of access we have to this object */
    enum claim_access access;
    /* the storage system object ID */
    u64 oid;
    /* the file stat record */
    struct p9stat *info;
};

/* Create a new root claim object for the given lease */
Claim *claim_new_root(char *pathname, enum claim_access access, u64 oid);
/* Create a new claim object in the same lease as the parent */
Claim *claim_new(Claim *parent, char *name, enum claim_access access, u64 oid);

/* Attach to a claim.  Returns 0 on success, -1 if the request cannot be filled
 * because of exclusive access constraints. */
int claim_request(Claim *claim);

/* Release a claim. */
void claim_release(Claim *claim);

/* Global claim data */

extern Hashtable *path_2_claim;
extern Lru *claim_lru;

Claim *claim_lookup(char *pathname);
void claim_remove(Claim *claim);

#endif
