#ifndef _LEASE_H_
#define _LEASE_H_

#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
#include "worker.h"
#include "lru.h"
#include "claim.h"

/* General rules
 * - Lease and ownership requests always come from parent to child
 *
 * Grants:
 *   - grants give ownership to target
 *   - carry payload of (wavefront) exits, fully defining the boundaries
 *   - also carry payload of active fids
 *   - grants are instantly effective--storage must be flushed before grant is
 *     issued
 *   a) Nominate self to request renewal
 *   b) Nominate target (who must be owner of lease/owner of parent) to give up
 *   c) Nominate peer to transfer control (in response to traffic)
 */

struct lease {
    /* if this exists, transactions wait here before starting an operation.
     * if this exists when finishing and inflight == 0, this worker gets
     * scheduled to let a lease change take place */
    Worker *wait_for_update;
    /* the number of active transactions using this lease */
    int inflight;

    /* is this an exit point to a remote owner, or a local grant? */
    int isexit;

    char *pathname;
    /* the remote envoy or the parent */
    Address *addr;

    /* these fields are not applicable for remote leases */

    /* root claim */
    Claim *claim;
    /* all the exits from this lease */
    List *wavefront;
    /* all active fids using this lease */
    Hashtable *fids;
    /* does this lease cover a read-only region? */
    int readonly;
    /* cache of unused claims in this lease.  these entries also appear in the
     * global LRU */
    Hashtable *claim_cache;
};

extern Hashtable *lease_by_root_pathname;
extern Lru *lease_claim_cache;

/* add the given claim to the claim cache */
void lease_add_claim_to_cache(Claim *claim);
/* remove a given claim from the cache (usually when activating it) */
void lease_remove_claim_from_cache(Claim *claim);
/* search the cache for a claim for the given pathname */
Claim *lease_lookup_claim_from_cache(Lease *lease, char *pathname);
/* flush all cached claims related to the given lease */
void lease_flush_claim_cache(Lease *lease);

void lease_finish_transaction(Lease *lease);

void lease_state_init(void);

/* create a lease object */
Lease *lease_new(char *pathname, Address *addr, int isexit, Claim *claim,
        List *wavefront, int readonly);
/* add a lease object, including exit lease objects for its wavefront */
void lease_add(Lease *lease);
/* remove a lease object, including exit lease objects for its wavefront */
void lease_remove(Lease *lease);

/* returns the remote lease rooted at the given pathname, or NULL if the given
 * path is not a known lease exit */
Lease *lease_get_remote(char *pathname);

/* If the given pathname is covered by a local lease, find the root of that
 * lease and return the lease.  Otherwise, return NULL.
 *
 * Does not call lease_start_transaction on the result. */
Lease *lease_find_root(char *pathname);

/* returns true if the given path has any decendents from the local lease that
 * are exit points */
int lease_is_exit_point_parent(Lease *lease, char *pathname);

/* Clones the lease root, Resets relevant claim cache, copies trails to lease
 * exits, and snapshots child leases recursively.  Lease must be exclusive
 * locked before the call, and the claim must be locked as well */
u64 lease_snapshot(Worker *worker, Claim *claim);

void lease_dump_graph(Lease *lease);
void lease_audit(void);

#endif
