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
#include "remote.h"
#include "lru.h"
#include "claim.h"
#include "walk.h"

/* General rules
 * - Lease and ownership requests always come from parent to child
 * - Ownership roots are always writable or readonly--never cow
      - therefore, path to every ownership is also not cow
 *    - readonly trees only use leases--the owner grants leases to everyone
 * - Ownership roots are always unique--further grants are only of descendents
 *   in the tree
 * - Every lease/ownership knows its exit points: other owners, leases, parent
 */

/* When a request comes in, it can encounter the following situations:
 *
 * Variables: read/write, owned/leased/remote, cow/writable/readonly
 *
 * Read requests:
 *   - Owned or leased, cow or writable or readonly: handle locally
 *   - Remote, cow or writable or readonly: forward
 * Write requests:
 *   - Owned or lease, readonly: fail
 *   - Owned, writable:
 *       a) revoke leases
 *       b) handle locally
 *   - Owned, cow:
 *       a) revoke leases
 *       b) copy from local ownership root to this node
 *       c) handle locally
 *   - Lease or remote, cow or writable: forward
 */

/* Leases are changed for the following reasons:
 *
 * - Write request comes to owner:
 *     a) owner revokes leases from children and serves request or
 *     b) owner parent grants ownership of subtree to child (after delay?)
 * - Read request comes from an envoy: owner grants lease to child (after delay?)
 * - Node shutting down:
 *     a) nominate message sent to parent
 *     b) parent revokes ownership/lease
 * - Lease/ownership expires (are they timed?):
 *     a) nominate request indicates renewal or relinquishment(?)
 */

/* Lease message can find the following situations:
 *
 * Types: grant, revoke, nominate
 *
 * Grants:
 *   - grants give leases or ownership to target
 *   - carry payload of (wavefront) exits, fully defining the boundaries
 *   - also carry payload of active fids
 *   - grants are instantly effective--storage must be flushed before grant is issued
 *   a) Grant ownership where there was a lease (but not the other way around)
 *   b) Grant ownership where there was nothing
 *   c) Grant lease where there was nothing
 *
 *   a) Revoke lease
 *   b) Revoke ownership
 *
 *   a) Nominate self to request renewal
 *   b) Nominate target (who must be owner of lease/owner of parent) to give up
 *   c) Nominate peer to transfer control (in response to traffic)
 */

/* Overview of the lease data structures:
 */

/* Example sequences:
 *
 * - Read request
 * - Write request
 * - Walk request
 * - Clone
 * - Lease given
 * - Lease revoked
 *   - set wait_for_update to prevent new transactions starting
 *   - if inflight > 0
 *     - wait on okay_to_change_lease until inflight drops to zero
 */


enum lease_type {
    LEASE_GRANT,
    LEASE_LEASE,
    LEASE_REMOTE,
};

struct lease {
    /* if this exists, transactions wait here before starting an operation */
    pthread_cond_t *wait_for_update;
    /* the number of active transactions using this lease */
    int inflight;
    /* if this exists when finishing and inflight == 0, a transaction signals
     * it to let a lease change take place. */
    pthread_cond_t *okay_to_change_lease;

    enum lease_type type;

    char *pathname;
    /* the remote envoy, owner, or parent */
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

    /* this field is only applicable for grants */
    List *subleases;
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

void lease_start_transaction(Lease *lease);
void lease_finish_transaction(Lease *lease);

void lease_state_init(void);

/* This is created when a transaction finds an out-of-date lease.  New
 * transactions intending to stake a claim on this lease wait here, and
 * rejected remote requests working off this version wait here until an
 * update.  When the lease is updated a broadcast will be sent out and the
 * claimers must recheck the lease situation.
 *
 * Example:
 *    - A request comes to a remotely-owned lease
 *    - The request is forwarded
 *    - The remote envoy issues a readonly lease and rejects the request
 *    - The rejected request finds that the lease hasn't been updated, so
 *      it creates a wait variable and blocks.
 *    - A second request comes in on this lease
 *    - The request sees the wait variable and blocks
 *    - The new lease comes in and is recorded
 *    - Both client requests awaken and are handled locally
 */

/* create a lease */
void lease_new(char *pathname, Address *addr, int leaf, List *children);

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

/* relies on claim_freeze and resets relevant claim cache and waits for
 * in-flight ops to finish and prevents new ones from starting and ... */
u64 lease_snapshot(Lease *lease);

void lease_dump_graph(Lease *lease);

#endif
