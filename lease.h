#ifndef _LEASE_H_
#define _LEASE_H_

#include <stdlib.h>
#include <unistd.h>
#include "types.h"
#include "list.h"
#include "hashtable.h"
#include "worker.h"
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
    /* claims that are deleted but still open */
    List *deleted;
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

/* Freezes the given claim and its children, including relevant claim cache,
 * copies trails to lease exits, and snapshots child leases recursively.  This
 * call obtains an exclusive lock on the lease.  The new oid and the cow status
 * can be queried from the claim; there is no return value. */
void lease_snapshot(Worker *worker, Claim *claim);

void lease_dump_graph(Lease *lease);
void lease_audit(void);

#endif
