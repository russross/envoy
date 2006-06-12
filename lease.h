#ifndef _LEASE_H_
#define _LEASE_H_

#include <pthread.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
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
    /* if this exists, transactions wait here before starting an operation */
    pthread_cond_t *wait_for_update;
    /* the number of active transactions using this lease */
    int inflight;
    /* if this exists when finishing and inflight == 0, a transaction signals
     * it to let a lease change take place. */
    pthread_cond_t *okay_to_change_lease;

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
/* remove a given claim from the cache (usually when activating it */
void lease_remove_claim_from_cache(Claim *claim);
/* search the cache for a claim for the given pathname */
Claim *lease_lookup_claim_from_cache(Lease *lease, char *pathname);
/* flush all cached claims related to the given lease */
void lease_flush_claim_cache(Lease *lease);

void lease_finish_transaction(Lease *lease);

void lease_state_init(void);

/* create a lease object */
void lease_new(char *pathname, Address *addr, int isexit, Claim *claim,
        List *wavefront, int readonly);

/* Checks if the given pathname is part of a different lease than the parent
 * (which must be covered by the given lease) and returns it if so.  Returns
 * NULL if the pathname is covered by the same lease. */
Lease *lease_check_for_lease_change(Lease *lease, char *pathname);

Lease *lease_find_remote(char *pathname);

/* If the given pathname is covered by a local lease, find the root of that
 * lease and return the lease.  Otherwise, return NULL.
 *
 * Does not call lease_start_transaction on the result. */
Lease *lease_find_root(char *pathname);

/* returns true if the given path has any decendents from the local lease that
 * are exit points */
int lease_is_exit_point_parent(Lease *lease, char *pathname);

/* relies on claim_freeze and resets relevant claim cache and waits for
 * in-flight ops to finish and prevents new ones from starting and ... */
u64 lease_snapshot(Lease *lease);

void lease_dump_graph(Lease *lease);

#endif
