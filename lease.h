#ifndef _LEASE_H_
#define _LEASE_H_

#include <stdlib.h>
#include <unistd.h>
#include "types.h"
#include "9p.h"
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

    /* for lease transfers */
    int changeinprogress;
    List *changeexits;
    List *changefids;

    /* is this an exit point to a remote owner, or locally owned? */
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
    Hashtable *dir_cache;
};

enum dir_cache_type {
    DIR_CACHE_ZERO,
    DIR_CACHE_PARTIAL,
    DIR_CACHE_COMPLETE,
};

extern Hashtable *lease_by_root_pathname;

void lease_state_init(void);

/* create a lease object */
Lease *lease_new(char *pathname, Address *addr, int isexit, Claim *claim,
        int readonly);
/* add a lease object, including exit lease objects for its wavefront */
void lease_add(Lease *lease);
/* merge a lease exit into a parent lease */
void lease_merge_exit(Worker *worker, Lease *parent, Lease *child);
/* remove a lease object */
void lease_remove(Lease *lease);

/* returns the remote lease rooted at the given pathname, or NULL if the given
 * path is not a known lease exit */
Lease *lease_get_remote(char *pathname);

/* If the given pathname is covered by a local lease, find the root of that
 * lease and return the lease.  Otherwise, return NULL. */
Lease *lease_find_root(char *pathname);

void lease_link_exit(Lease *exit);
void lease_unlink_exit(Lease *exit);

/* returns true if the given path has any decendents from the local lease that
 * are exit points */
int lease_is_exit_point_parent(Lease *lease, char *pathname);

/* Freezes the given claim and its children, including relevant claim cache,
 * copies trails to lease exits, and snapshots child leases recursively.  This
 * call obtains an exclusive lock on the lease.  The new oid and the cow status
 * can be queried from the claim; there is no return value. */
void lease_snapshot(Worker *worker, Claim *claim);

/* lease migration */

enum grant_type {
    GRANT_START,
    GRANT_CONTINUE,
    GRANT_END,
    GRANT_SINGLE,
    GRANT_CHANGE_PARENT,
};

struct leaserecord *lease_to_lease_record(Lease *lease, int prefixlen);
List *lease_serialize_exits(Worker *worker, Lease *lease,
        char *oldroot, Address *addr);
void lease_add_exits(Worker *worker, Lease *lease, List *exits);
List *lease_serialize_fids(Worker *worker, Lease *lease,
        char *oldroot, Address *addr);
void lease_release_fids(Worker *worker, Lease *lease,
        char *oldroot, Address *addr);
void lease_add_fids(Worker *worker, Lease *lease, List *fids,
        char *oldroot, Address *oldaddr);
void lease_pack_message(Lease *lease, List **exits, List **fids, int size);
void lease_split(Worker *worker, Lease *lease, char *pathname, Address *addr);
void lease_merge(Worker *worker, Lease *child);
void lease_rename(Worker *worker, Lease *lease, Claim *root,
        char *oldpath, char *newpath);

#endif
