#ifndef _LEASE_H_
#define _LEASE_H_

#include <foo.h>

/* General rules
 * - Lease and ownership requests always come from parent to child
 * - Ownership roots are always writable or readonly--never cow
      - therefore, path to every ownership is also not cow
 *    - should readonly trees just use leases? ownership so children can delegate too?
 * - Ownership roots are always unique--further grants are only of descendents in the tree
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

/* note: read-only lease + remote address => remote address is the owner
 *       the root of an owned branch is never cow */
struct lease {
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
     *    - The rejected request finds that the lease hasn't been updated, so it
     *      creates a wait variable and blocks.
     *    - A second request comes in on this lease
     *    - The request sees the wait variable and blocks
     *    - The new lease comes in and is recorded
     *    - Both client requests awaken and are handled locally
     */
    pthread_cond_t *wait_for_update;
    int inflight;
    pthread_cond_t *okay_to_change_lease;

    /* the root path of this lease */
    char *pathname;
    /* flag indicating we have a read-only lease */
    int readonly;
    /* the address of the envoy that holds the lease (NULL means us) */
    Address *addr;
    /* flag indicating we don't know anything about the descendents of this path */
    int leaf;

    /* all the active fids with claims on this lease */
    Hashtable *fids;
    /* all the walk entries with claims on this lease. Note: NULL <=> remote lease */
    Lru *walk_cache;
    /* all known leases below this node (note: empty if leaf, else forms a wavefront) */
    List *children;
    /* version number of this lease--increments on every update */
    u32 version;
};

struct claim {
    /* the storage system object ID */
    u64 oid;
    /* the lease under which this object falls */
    Lease *lease;
    /* the version number of the lease when last checked */
    u32 lease_version;
    /* the level of access we have to this object */
    enum fid_access access;
    /* the full system path of this object */
    char *pathname;
};

/* should these be public? */
/* create a lease */
void lease_new(char *pathname, Address *addr, int leaf, List *children);

/* find the lease for a given path */
Lease *lease_forward_to(char *pathname, int write);

/* lease change requests (always from a parent) */
//lease_revoke
//lease_grant
//lease_upgrade

#endif
