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
#include "remote.h"
#include "worker.h"
#include "dir.h"
#include "claim.h"
#include "lease.h"

Hashtable *lease_by_root_pathname;

static int lease_cmp(const Lease *a, const Lease *b) {
    return strcmp(a->pathname, b->pathname);
}

Lease *lease_new(char *pathname, Address *addr, int isexit, Claim *claim,
        List *wavefront, int readonly)
{
    Lease *l = GC_NEW(Lease);
    assert(l != NULL);

    l->wait_for_update = NULL;
    l->inflight = 0;

    l->pathname = pathname;
    l->addr = addr;

    l->isexit = isexit;

    l->claim = claim;
    l->claim->lease = l;

    l->wavefront = NULL;
    while (!null(wavefront)) {
        l->wavefront =
            insertinorder((Cmpfunc) lease_cmp, l->wavefront, car(wavefront));
        wavefront = cdr(wavefront);
    }

    l->fids = hash_create(LEASE_FIDS_HASHTABLE_SIZE,
            (Hashfunc) fid_hash,
            (Cmpfunc) fid_cmp);

    l->readonly = 0;

    l->claim_cache = hash_create(
            LEASE_CLAIM_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);

    return l;
}

void lease_state_init(void) {
    lease_by_root_pathname = hash_create(
            LEASE_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);
}

Lease *lease_get_remote(char *pathname) {
    Lease *lease = hash_get(lease_by_root_pathname, pathname);
    if (lease != NULL && lease->isexit)
        return lease;
    return NULL;
}

Lease *lease_find_root(char *pathname) {
    Lease *lease = NULL;

    while ((lease = hash_get(lease_by_root_pathname, pathname)) == NULL &&
            strcmp(pathname, "/"))
    {
        pathname = dirname(pathname);
    }

    if (lease != NULL && !lease->isexit)
        return lease;
    return NULL;
}

int lease_is_exit_point_parent(Lease *lease, char *pathname) {
    return findinorder((Cmpfunc) lease_cmp, lease->wavefront, pathname) != NULL;
}

void lease_add(Lease *lease) {
    List *exits = lease->wavefront;

    assert(!lease->isexit);

    hash_set(lease_by_root_pathname, lease->pathname, lease);
    while (!null(exits)) {
        Lease *exit = car(exits);
        exits = cdr(exits);
        assert(exit->isexit);
        hash_set(lease_by_root_pathname, exit->pathname, exit);
    }
}

static void make_claim_cow(char *env, char *pathname, Claim *claim) {
    if (startswith(pathname, env) && claim->access == ACCESS_WRITEABLE)
        claim->access = ACCESS_COW;
}

void lease_snapshot(Worker *worker, Claim *claim) {
    List *allexits = claim->lease->wavefront;
    List *exits = NULL;
    List *newoids = NULL;
    char *prefix = concatstrings(claim->pathname, "/");

    lock_lease_exclusive(worker, claim->lease);

    /* start by freezing everything */
    claim_freeze(worker, claim);
    hash_apply(claim->lease->claim_cache,
            (void (*)(void *, void *, void *)) make_claim_cow,
            prefix);

    /* recursively snapshot all the child leases */
    for ( ; !null(allexits); allexits = cdr(allexits)) {
        Lease *lease = car(allexits);
        if (startswith(lease->pathname, prefix))
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

struct leaserecord *lease_serialize_root(Lease *lease) {
    struct leaserecord *elt = GC_NEW(struct leaserecord);
    assert(elt != NULL);

    elt->pathname = lease->pathname;
    elt->readonly = lease->readonly ? 1 : 0;
    if (lease->claim == NULL)
        elt->oid = NOOID;
    else
        elt->oid = lease->claim->oid;
    if (lease->isexit) {
        elt->address = lease->addr->ip;
        elt->port = lease->addr->port;
    } else {
        elt->address = my_address->ip;
        elt->port = my_address->port;
    }

    return elt;
}

/* prefix must include a trailing slash, addr is the grant target */
List *lease_serialize_exits(Worker *worker, Lease *lease,
        char *prefix, Address *addr)
{
    List *result = NULL;
    List *targets = NULL;
    List *exits = lease->wavefront;
    List *prev = NULL;

    /* gather exits that match the prefix, and remove them from the lease */
    for (exits = lease->wavefront; !null(exits); exits = cdr(exits)) {
        Lease *elt = car(exits);
        if (startswith(elt->pathname, prefix)) {
            targets = cons(elt, targets);
            if (prev == NULL)
                lease->wavefront = cdr(exits);
            else
                setcdr(prev, cdr(exits));
        } else {
            prev = exits;
        }
    }

    /* change the parent addresses of exits and freeze them */
    remote_grant_exits(worker, targets, addr);

    /* prepare list of records for the transfer */
    for ( ; !null(targets); targets = cdr(targets))
        result = cons(lease_serialize_root(car(targets)), result);

    return result;
}

struct lease_serialize_fids_env {
    char *prefix;
    int prefixlen;
    List *fids;
};

void lease_serialize_fids_iter(struct lease_serialize_fids_env *env,
        Fid *key, Fid *fid)
{
    struct fidrecord *elt = GC_NEW(struct fidrecord);
    assert(elt != NULL);
    assert(fid->isremote && fid->claim != NULL);

    elt->fid = fid->fid;
    assert(startswith(elt->pathname, env->prefix));
    elt->pathname = substring_rest(fid->pathname, env->prefixlen);
    elt->user = fid->user;
    elt->status = (u8) fid->status;
    elt->omode = fid->omode;
    elt->readdir_cookie = elt->readdir_cookie;
    elt->address = fid->addr->ip;
    elt->port = fid->addr->port;

    env->fids = cons(elt, env->fids);
}

List *lease_serialize_fids(Lease *lease) {
    struct lease_serialize_fids_env env;
    env.prefix = concatstrings(lease->pathname, "/");
    env.prefixlen = strlen(env.prefix);
    env.fids = NULL;
    hash_apply(lease->fids,
            (void (*)(void *, void *, void *)) lease_serialize_fids_iter,
            &env);

    return env.fids;
}

