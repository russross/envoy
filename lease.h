#ifndef _LEASE_H_
#define _LEASE_H_

#include <foo.h>

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

/* note: read-only lease + remote address => remote address is the owner */
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

void lease_owned_new(char *pathname, Address *addr, int leaf, List *children);
void lease_shared_new(char *pathname, Address *addr, int leaf, List *children);
Lease *lease_forward_to(char *pathname, int write);

#endif
