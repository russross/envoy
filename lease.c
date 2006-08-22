#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "vector.h"
#include "hashtable.h"
#include "connection.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "object.h"
#include "remote.h"
#include "worker.h"
#include "lru.h"
#include "dir.h"
#include "claim.h"
#include "lease.h"
#include "walk.h"

Hashtable *lease_by_root_pathname;
Lru *dir_cache;

static int lease_cmp(const Lease *a, const Lease *b) {
    return strcmp(a->pathname, b->pathname);
}

static int lease_key_cmp(const char *key, const Lease *elt) {
    return strcmp(key, elt->pathname);
}

Lease *lease_new(char *pathname, Address *addr, int isexit, Claim *claim,
        int readonly)
{
    Lease *l = GC_NEW(Lease);
    assert(l != NULL);

    l->wait_for_update = NULL;
    l->inflight = 0;

    l->changeinprogress = 0;
    l->changeexits = NULL;
    l->changefids = NULL;

    l->pathname = pathname;
    l->addr = addr;

    l->wavefront = NULL;

    l->isexit = isexit;

    if (isexit) {
        l->claim = NULL;
        l->fids = NULL;
        l->claim_cache = NULL;
        l->dir_cache = NULL;
    } else {
        l->claim = claim;
        l->claim->lease = l;
        l->fids = hash_create(LEASE_FIDS_HASHTABLE_SIZE,
                (Hashfunc) fid_hash,
                (Cmpfunc) fid_cmp);
        l->claim_cache = hash_create(
                LEASE_CLAIM_HASHTABLE_SIZE,
                (Hashfunc) string_hash,
                (Cmpfunc) strcmp);
        l->dir_cache = hash_create(
                LEASE_DIR_HASHTABLE_SIZE,
                (Hashfunc) dir_block_hash,
                (Cmpfunc) dir_block_cmp);
    }

    l->readonly = readonly;

    return l;
}

static void lease_clear_dir_cache(Lease *lease) {
    List *allblocks = hash_tolist(lease->dir_cache);

    for ( ; !null(allblocks); allblocks = cdr(allblocks))
        lru_remove(dir_cache, car(allblocks));
}

static void lease_merge_iter_claim_cache(Lease *lease,
        char *pathname, Claim *claim)
{
    claim->lease = lease;
    hash_set(lease->claim_cache, pathname, claim);
}

static void lease_merge_iter_dir_cache(Lease *lease,
        struct dir_block *block, void *value)
{
    block->lease = lease;
    hash_set(lease->dir_cache, block, block);
}

void lease_merge_exit(Worker *worker, Lease *parent, Lease *child) {
    Claim *claim;
    assert(parent->wait_for_update == worker &&
            child->wait_for_update == worker);
    assert(parent->inflight == 0 && child->inflight == 0);
    assert(child->isexit && !parent->isexit);
    assert(!parent->readonly);
    assert(!child->readonly);

    /* prevent lookups on the old lease */
    hash_remove(lease_by_root_pathname, child->pathname);

    /* find the immediate parent and add this child */
    claim = claim_find(worker, dirname(child->pathname));
    assert(claim != NULL && claim->lease == parent);
    claim_link_child(claim, child->claim);

    /* merge the fids */
    hash_apply(child->fids, (void (*)(void *, void *, void *)) hash_set,
            parent->fids);
    child->fids = NULL;

    /* merge the claim cache */
    hash_apply(child->claim_cache,
            (void (*)(void *, void *, void *)) lease_merge_iter_claim_cache,
            parent);
    child->claim_cache = NULL;

    /* merge the dir cache */
    hash_apply(child->dir_cache,
            (void (*)(void *, void *, void *)) lease_merge_iter_dir_cache,
            parent);
    child->dir_cache = NULL;

    /* merge in the wavefront */
    for ( ; !null(child->wavefront); child->wavefront = cdr(child->wavefront))
        lease_link_exit((Lease *) car(child->wavefront));
}

void lease_link_exit(Lease *exit) {
    Lease *lease = lease_find_root(dirname(exit->pathname));
    lease->wavefront =
        insertinorder((Cmpfunc) lease_cmp, lease->wavefront, exit);
    lease_add(exit);
}

void lease_unlink_exit(Lease *exit) {
    Lease *lease = lease_find_root(dirname(exit->pathname));
    lease->wavefront =
        removeinorder((Cmpfunc) lease_cmp, lease->wavefront, exit);
}

static void lease_cleanup_dir_block(struct dir_block *block) {
    hash_remove(block->lease->dir_cache, block);
}

void lease_state_init(void) {
    lease_by_root_pathname = hash_create(
            LEASE_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);
    dir_cache = lru_new(
            LEASE_DIR_CACHE_SIZE,
            (Hashfunc) dir_block_hash,
            (Cmpfunc) dir_block_cmp,
            NULL,
            (void (*)(void *)) lease_cleanup_dir_block);
}

Lease *lease_get_remote(char *pathname) {
    Lease *lease = hash_get(lease_by_root_pathname, pathname);
    return (lease != NULL && lease->isexit) ? lease : NULL;
}

Lease *lease_find_root(char *pathname) {
    Lease *lease = NULL;

    while ((lease = hash_get(lease_by_root_pathname, pathname)) == NULL &&
            strcmp(pathname, "/"))
    {
        pathname = dirname(pathname);
    }

    return (lease != NULL && !lease->isexit) ? lease : NULL;
}

int lease_is_exit_point_parent(Lease *lease, char *pathname) {
    return findinorder((Cmpfunc) lease_key_cmp,
            lease->wavefront, pathname) != NULL;
}

void lease_add(Lease *lease) {
    List *exits = lease->wavefront;

    assert(!lease->isexit || null(exits));

    hash_set(lease_by_root_pathname, lease->pathname, lease);
    for ( ; !null(exits); exits = cdr(exits)) {
        Lease *exit = car(exits);
        assert(exit->isexit);
        hash_set(lease_by_root_pathname, exit->pathname, exit);
    }
}

void lease_remove(Lease *lease) {
    assert(null(lease->wavefront));
    assert(lease->fids == NULL || hash_count(lease->fids) == 0);
    assert(null(lease->changeexits));
    assert(null(lease->changefids));

    if (lease->isexit) {
        lease_unlink_exit(lease);
    } else {
        claim_clear_descendents(lease->claim);
        lease_clear_dir_cache(lease);
    }

    hash_remove(lease_by_root_pathname, lease->pathname);
    lease->pathname = NULL;
    lease->addr = NULL;
    lease->claim = NULL;
}

static void make_claim_cow(char *prefix, char *pathname, Claim *claim) {
    if (ispathprefix(pathname, prefix) && claim->access == ACCESS_WRITEABLE)
        claim->access = ACCESS_COW;
}

void lease_snapshot(Worker *worker, Claim *claim) {
    List *allexits = claim->lease->wavefront;
    List *exits = NULL;
    List *newoids = NULL;

    lock_lease_exclusive(worker, claim->lease);

    /* start by freezing everything */
    hash_apply(claim->lease->claim_cache,
            (void (*)(void *, void *, void *)) make_claim_cow,
            claim->pathname);

    /* recursively snapshot all the child leases */
    for ( ; !null(allexits); allexits = cdr(allexits)) {
        Lease *lease = car(allexits);
        if (ispathprefix(lease->pathname, claim->pathname))
            exits = cons(lease, exits);
    }
    if (!null(exits))
        newoids = remote_snapshot(worker, exits);

    /* now clone paths to the exits and update the exit parent dirs */
    while (!null(exits) && !null(newoids)) {
        Lease *exit = car(exits);
        u64 *newoid = car(newoids);
        u64 oldoid;
        Claim *parent;

        parent = claim_find(worker, dirname(exit->pathname));
        claim_thaw(worker, parent);
        oldoid = dir_change_oid(worker, parent, filename(exit->pathname),
                *newoid, ACCESS_WRITEABLE);
        assert(oldoid != NOOID);

        exits = cdr(exits);
        newoids = cdr(newoids);
    }
}

struct audit_env {
    Hashtable *inuse;
    Vector *rfids;
};

#define eqnull(_ptr, _name) do { \
    if ((_ptr)) \
        printf("audit: %s is non-null for %s (line %d)\n", #_ptr, \
                _name, __LINE__); \
} while (0)

//static void lease_audit_iter(struct audit_env *env, char *pathname,
//        Lease *lease)
//{
//    eqnull(lease->wait_for_update, pathname);
//    eqnull(lease->inflight, pathname);
//    if (lease->isexit) {
//        eqnull(lease->claim, pathname);
//        eqnull(lease->wavefront, pathname);
//        eqnull(lease->fids, pathname);
//        eqnull(lease->readonly, pathname);
//        eqnull(lease->claim_cache, pathname);
//    } else {
//        /* walk the claim tree, verifying links and gathering claims with
//         * non-zero refcounts */
//        List *stack = cons(lease->claim, NULL);
//        while (!null(stack)) {
//            Claim *claim = car(stack);
//            stack = cdr(stack);
//            eqnull(claim->lock, claim->pathname);
//            eqnull(claim->deleted, claim->pathname);
//            assert(claim->lease == lease);
//            if (!null(claim->children)) {
//                List *children = claim->children;
//                while (!null(children)) {
//                    Claim *child = car(children);
//                    children = cdr(children);
//                    assert(child->parent == claim);
//                    assert(!strcmp(child->pathname,
//                                concatname(claim->pathname,
//                                    filename(child->pathname))));
//                    stack = cons(child, stack);
//                }
//            } else {
//                if (claim->refcount == 0 && claim != lease->claim) {
//                    printf("audit: zero refcount, no children, non-root: %s\n",
//                            claim->pathname);
//                }
//            }
//            if (claim->refcount != 0) {
//                int *refcount = GC_NEW_ATOMIC(int);
//                assert(refcount != NULL);
//                assert(hash_get(env->inuse, claim) == NULL);
//                *refcount = claim->refcount;
//                hash_set(env->inuse, claim, refcount);
//            }
//        }
//        stack = lease->deleted;
//        while (!null(stack)) {
//            int *refcount = GC_NEW_ATOMIC(int);
//            Claim *claim = car(stack);
//            stack = cdr(stack);
//            assert(claim->deleted);
//            eqnull(claim->parent, claim->pathname);
//            eqnull(claim->children, claim->pathname);
//            assert(claim->refcount != 0);
//            assert(refcount != NULL);
//            assert(hash_get(env->inuse, claim) == NULL);
//            *refcount = claim->refcount;
//            hash_set(env->inuse, claim, refcount);
//        }
//    }
//}
//
//static void lease_audit_fid_iter(struct audit_env *env, u32 key, Fid *fid) {
//    eqnull(fid->lock, fid->pathname);
//    assert(fid->pathname != NULL && fid->user != NULL);
//    if (fid->isremote) {
//        assert(fid->raddr != NULL);
//        assert(fid->rfid != NOFID);
//        assert(vector_get(env->rfids, fid->rfid) == fid);
//        vector_remove(env->rfids, fid->rfid);
//    } else {
//        int *refcount;
//        assert(fid->claim != NULL);
//        if (strcmp(fid->claim->pathname, fid->pathname)) {
//            printf("audit: fid->claim->pathname = %s, fid->pathname = %s\n",
//                    fid->claim->pathname, fid->pathname);
//        }
//        refcount = hash_get(env->inuse, fid->claim);
//        if (refcount == NULL) {
//            assert(fid->claim->refcount == 0);
//        } else {
//            (*refcount)--;
//            if (*refcount < 0) {
//                printf("lease_audit_fid_iter: (fid count = %d) > "
//                        "(refcount = %d): %s\n",
//                        fid->claim->refcount - *refcount,
//                        fid->claim->refcount, fid->pathname);
//            }
//        }
//    }
//}
//
//static void lease_audit_conn_iter(struct audit_env *env, u32 key, Connection *conn) {
//    vector_apply(conn->fid_vector,
//            (void (*)(void *, u32, void *)) lease_audit_fid_iter,
//            env);
//}
//
//static void lease_audit_count_iter(void *env, Claim *claim, int *refcount) {
//    if (*refcount > 0) {
//        printf("lease_audit_count_iter: claim refcount %d too high: %s\n",
//                *refcount, claim->pathname);
//    }
//}
//
//static void lease_audit_check_rfid(struct audit_env *env, u32 rfid, Fid *fid) {
//    assert(fid->rfid == rfid);
//    vector_set(env->rfids, rfid, fid);
//}
//
//void lease_audit(void) {
//    struct audit_env env;
//    env.inuse = hash_create(
//            64,
//            (Hashfunc) claim_hash,
//            (Cmpfunc) claim_cmp);
//    env.rfids = vector_create(FID_REMOTE_VECTOR_SIZE);
//
//    /* check and clone the remote fid vector */
//    vector_apply(fid_remote_vector,
//            (void (*)(void *, u32, void *)) lease_audit_check_rfid,
//            &env);
//
//    /* walk all the leases & claims, and gather claims with refcount != 0 */
//    hash_apply(lease_by_root_pathname,
//            (void (*)(void *, void *, void *)) lease_audit_iter,
//            &env);
//
//    /* walk all the fids and decrement refcounts */
//    vector_apply(conn_vector,
//            (void (*)(void *, u32, void *)) lease_audit_conn_iter,
//            &env);
//
//    /* see if there were any refcounts left > 0 */
//    hash_apply(env.inuse,
//            (void (*)(void *, void *, void *)) lease_audit_count_iter,
//            NULL);
//
//    /* see if there are any orphan rfids */
//    while (vector_get_next(env.rfids) > 0) {
//        u32 rfid = vector_get_next(env.rfids) - 1;
//        Fid *fid = vector_get_remove(env.rfids, rfid);
//        printf("audit: orphan remote fid: %d -> %s\n", rfid, fid->pathname);
//        assert(0);
//    }
//}

struct leaserecord *lease_to_lease_record(Lease *lease, int prefixlen) {
    struct leaserecord *elt = GC_NEW(struct leaserecord);
    assert(elt != NULL);

    elt->readonly = lease->readonly ? 1 : 0;
    if (lease->isexit) {
        elt->pathname = substring_rest(lease->pathname, prefixlen);
        elt->oid = NOOID;
        elt->address = lease->addr->ip;
        elt->port = lease->addr->port;
    } else {
        elt->pathname = lease->pathname;
        elt->oid = lease->claim->oid;
        elt->address = lease->addr->ip;
        elt->port = lease->addr->port;
    }

    return elt;
}

/* addr is the grant target */
List *lease_serialize_exits(Worker *worker, Lease *lease,
        char *root, Address *addr)
{
    List *result = NULL;
    List *targets = NULL;
    List *exits = lease->wavefront;
    List *prev = NULL;
    int prefixlen = strlen(root) + 1;

    /* gather exits that match the prefix and remove them from the lease */
    while (!null(exits)) {
        Lease *elt = car(exits);
        if (ispathprefix(elt->pathname, root)) {
            targets = cons(elt, targets);
            hash_remove(lease_by_root_pathname, elt->pathname);

            if (prev == NULL)
                lease->wavefront = cdr(exits);
            else
                setcdr(prev, cdr(exits));
        } else {
            prev = exits;
        }
        exits = cdr(exits);
    }

    /* prepare list of records for the transfer */
    for ( ; !null(targets); targets = cdr(targets))
        result = cons(lease_to_lease_record(car(targets), prefixlen), result);

    return result;
}

void lease_add_exits(Worker *worker, Lease *lease, List *exits) {
    List *newwavefront = NULL;
    List *local = exits;
    List *tomerge = NULL;

    /* first find any local leases that need to be merged */
    for ( ; !null(local); local = cdr(local)) {
        struct leaserecord *elt = car(local);
        char *pathname = concatname(lease->pathname, elt->pathname);
        if (elt->address == my_address->ip && elt->port == my_address->port) {
            Lease *child = lease_get_remote(pathname);
            assert(child != NULL);
            tomerge = cons(child, tomerge);
        }
    }

    /* lock all of them, blocking if necessary but not restarting */
    if (!null(tomerge))
        lock_lease_join(worker, tomerge);

    /* convert to lease objects and add to the parent lease */
    for ( ; !null(exits); exits = cdr(exits)) {
        struct leaserecord *elt = car(exits);
        char *pathname = concatname(lease->pathname, elt->pathname);
        if (elt->address == my_address->ip && elt->port == my_address->port) {
            /* merge with an existing lease */
            Lease *child = lease_get_remote(pathname);
            assert(child != NULL);
            lease_merge_exit(worker, lease, child);
        } else {
            /* add an exit */
            Address *addr = address_new(elt->address, elt->port);
            Lease *exit = lease_new(pathname, addr, 1, NULL, elt->readonly);
            lock_lease_exclusive(worker, exit);
            lease_link_exit(exit);

            /* add to the list of remote envoys that need to be notified */
            newwavefront = cons(exit, newwavefront);
        }
    }

    /* notify the remote hosts */
    remote_grant_exits(worker, newwavefront, my_address, GRANT_CHANGE_PARENT);
}

List *lease_serialize_fids(Worker *worker, Lease *lease,
        char *root, Address *addr)
{
    List *allfids = hash_tolist(lease->fids);
    List *res = NULL;
    int rootlen = strlen(root);

    for ( ; !null(allfids); allfids = cdr(allfids)) {
        Fid *fid = car(allfids);
        Connection *conn;
        struct fidrecord *elt;

        if (!ispathprefix(fid->pathname, root))
            continue;

        assert(!fid->isremote && fid->claim != NULL);
        assert(fid->raddr == NULL && fid->rfid == NOFID);

        /* if the fid comes from a client, set up a remote fid */
        conn = conn_get_incoming(fid->addr);
        assert(conn != NULL);
        if (conn->type == CONN_CLIENT_IN) {
            fid->rfid = fid_reserve_remote(worker);
            fid_set_remote(fid->rfid, fid);
            /* note: incoming requests still block until we set isremote */
        }

        elt = GC_NEW(struct fidrecord);
        assert(elt != NULL);

        elt->fid = conn->type == CONN_CLIENT_IN ? fid->rfid : fid->fid;
        elt->pathname = fid->pathname[rootlen] == 0 ? "" :
            substring_rest(fid->pathname, rootlen + 1);
        elt->user = fid->user;
        elt->status = (u8) fid->status;
        elt->omode = fid->omode;
        elt->readdir_cookie = fid->readdir_cookie;

        fid->readdir_env = NULL;

        if (conn->type == CONN_CLIENT_IN) {
            elt->address = my_address->ip;
            elt->port = my_address->port;
        } else {
            elt->address = fid->addr->ip;
            elt->port = fid->addr->port;
        }

        res = cons(elt, res);
    }

    return res;
}

void lease_release_fids(Worker *worker, Lease *lease,
        char *root, Address *addr)
{
    List *fids;

    /* delete the envoy fids and update client fids to be remote */
    for (fids = hash_tolist(lease->fids); !null(fids); fids = cdr(fids)) {
        Fid *fid = car(fids);

        if (ispathprefix(fid->pathname, root)) {
            Connection *conn = conn_get_incoming(fid->addr);
            assert(conn != NULL);
            if (conn->type == CONN_CLIENT_IN)
                fid_update_remote(fid, fid->pathname, addr, fid->rfid);
            else
                fid_remove(worker, conn, fid->fid);
        }
    }
}

void lease_add_fids(Worker *worker, Lease *lease, List *fids,
        char *root, Address *oldaddr)
{
    List *newremotefids = NULL;
    List *groups = NULL;

    for ( ; !null(fids); fids = cdr(fids)) {
        struct fidrecord *elt = car(fids);
        Address *addr = address_new(elt->address, elt->port);
        char *pathname = concatname(root, elt->pathname);
        Claim *claim = claim_find(worker, pathname);
        Fid *fid;

        assert(claim != NULL && claim->lease == lease);

        if (!addr_cmp(my_address, addr)) {
            /* a remote fid has come home--the system works! */
            fid = fid_get_remote(elt->fid);
            assert(fid != NULL && fid != (void *) 0xdeadbeef);
            fid_update_local(fid, claim);
            fid_release_remote(elt->fid);
            assert(!strcmp(fid->user, elt->user));
        } else {
            Connection *conn = conn_get_incoming(addr);
            if (conn == NULL)
                conn = conn_insert_new_stub(addr);
            fid_insert_local(conn, elt->fid, elt->user, claim);
            fid = fid_lookup(conn, elt->fid);

            /* contact the remote envoy only when it isn't the former owner */
            if (addr_cmp(addr, oldaddr)) {
                newremotefids =
                    insertinorder((Cmpfunc) fid_cmp, newremotefids, fid);
            }
        }

        fid->status = elt->status;
        fid->omode = elt->omode;
        fid->readdir_cookie = elt->readdir_cookie;
    }

    /* notify the remote envoys of the fid updates */
    if (!null((groups = fid_gather_groups(newremotefids))))
        remote_migrate(worker, groups);
}

void lease_pack_message(Lease *lease, List **exits, List **fids, int size) {
    *exits = NULL;
    while (!null(lease->changeexits)) {
        struct leaserecord *elt = car(lease->changeexits);
        int eltsize = leaserecordsize(elt);
        if (eltsize <= size) {
            *exits = cons(elt, *exits);
            lease->changeexits = cdr(lease->changeexits);
            size -= eltsize;
        } else {
            break;
        }
    }
    *fids = NULL;
    while (!null(lease->changefids)) {
        struct fidrecord *elt = car(lease->changefids);
        int eltsize = fidrecordsize(elt);
        if (eltsize <= size) {
            *fids = cons(elt, *fids);
            lease->changefids = cdr(lease->changefids);
            size -= eltsize;
        } else {
            break;
        }
    }
}

void lease_split(Worker *worker, Lease *lease, char *pathname, Address *addr) {
    List *exits;
    List *fids;
    Claim *claim = claim_find(worker, pathname);
    Lease *child;
    struct leaserecord *root;
    enum grant_type type = GRANT_START;

    assert(claim != NULL);
    assert(claim->lease == lease);

    lock_lease_exclusive(worker, lease);

    /* clear any cache references to this lease */
    walk_flush();
    object_cache_invalidate_all();

    if (claim->access == ACCESS_COW)
        claim_thaw(worker, claim);

    root = GC_NEW(struct leaserecord);
    assert(root != NULL);

    root->readonly = (claim->access == ACCESS_READONLY);
    root->pathname = pathname;
    root->oid = claim->oid;
    root->address = my_address->ip;
    root->port = my_address->port;

    lease->changeexits = lease_serialize_exits(worker, lease, pathname, addr);
    lease->changefids = lease_serialize_fids(worker, lease, pathname, addr);

    /* send the grant in as many steps as necessary */
    do {
        lease_pack_message(lease, &exits, &fids,
                (TEGRANT_SIZE_FIXED + leaserecordsize(root)));
        if (null(lease->changeexits) && null(lease->changefids)) {
            if (type == GRANT_START)
                type = GRANT_SINGLE;
            else
                type = GRANT_END;
        }
        remote_grant(worker, addr, type, root, my_address, exits, fids);
        type = GRANT_CONTINUE;
    } while (!null(lease->changeexits) || !null(lease->changefids));

    lease_release_fids(worker, lease, pathname, addr);

    child = lease_new(pathname, addr, 1, NULL,
            (claim->access == ACCESS_READONLY));
    lease_link_exit(child);

    assert(null(claim->children));
    claim_clear_descendents(claim);
    lease_clear_dir_cache(lease);
}

void lease_merge(Worker *worker, Lease *child) {
    Lease *lease = lease_find_root(dirname(child->pathname));
    enum grant_type revoketype = GRANT_START;
    List *exits;
    List *fids;
    char *oldpath = child->pathname;
    Address *oldaddr = child->addr;
    struct leaserecord *root;

    assert(lease != NULL);
    assert(!lease->isexit);
    assert(child->isexit);

    walk_flush();

    lock_lease_exclusive(worker, lease);
    lock_lease_join(worker, cons(child, NULL));

    lease_remove(child);

    while (revoketype != GRANT_END) {
        remote_revoke(worker, oldaddr, revoketype, oldpath,
                my_address, &revoketype, &root, &exits, &fids);

        if (!null(exits))
            lease_add_exits(worker, lease, exits);
        if (!null(fids))
            lease_add_fids(worker, lease, fids, oldpath, oldaddr);
    }

    remote_revoke(worker, oldaddr, GRANT_END, oldpath, my_address,
            &revoketype, &root, &exits, &fids);
    assert(revoketype == GRANT_END);
}

void lease_rename(Worker *worker, Lease *lease, Claim *root,
        char *oldpath, char *newpath)
{
    int prefixlen = strlen(oldpath);
    List *remotefids = NULL;
    List *remoteleases = NULL;
    List *exits;
    List *allclaims;

    lock_lease_exclusive(worker, lease);

    /* update the claims */
    allclaims = hash_tolist(lease->claim_cache);
    for ( ; !null(allclaims); allclaims = cdr(allclaims)) {
        Claim *elt = car(allclaims);
        List *fids = elt->fids;

        if (ispathprefix(elt->pathname, oldpath)) {
            claim_rename(elt, concatname(newpath, elt->pathname + prefixlen));

            /* update all the fids that reference this elt */
            for ( ; !null(fids) ; fids = cdr(fids)) {
                Fid *fid = car(fids);
                Connection *conn = conn_get_incoming(fid->addr);

                /* gather all fids coming from remote envoys */
                if (conn->type == CONN_ENVOY_IN) {
                    remotefids =
                        insertinorder((Cmpfunc) fid_cmp, remotefids, fid);
                }
            }
        }
    }

    /* gather the exits that need updating */
    exits = lease->wavefront;
    lease->wavefront = NULL;
    for ( ; !null(exits); exits = cdr(exits)) {
        Lease *elt = car(exits);

        /* is this exit a descendent of our renamed directory? */
        if (ispathprefix(elt->pathname, oldpath)) {
            lock_lease_join(worker, cons(elt, NULL));
            remoteleases = cons(elt, remoteleases);
        } else {
            lease_link_exit(elt);
        }
    }

    /* update the remote exits and remote fids */
    if (!null(remoteleases) || !null(remotefids)) {
        remote_renametree(worker, oldpath, newpath,
                remoteleases, fid_gather_groups(remotefids));
    }

    /* now update the local exit stubs and the wavefront list */
    for ( ; !null(remoteleases); remoteleases = cdr(remoteleases)) {
        Lease *elt = car(remoteleases);
        hash_remove(lease_by_root_pathname, elt->pathname);
        elt->pathname = concatname(newpath, elt->pathname + prefixlen);
        lease_link_exit(elt);
    }

    /* see if the lease itself needs renaming */
    if (ispathprefix(lease->pathname, oldpath)) {
        hash_remove(lease_by_root_pathname, lease->pathname);
        lease->pathname = concatname(newpath, lease->pathname + prefixlen);
        hash_set(lease_by_root_pathname, lease->pathname, lease);
    }
}
