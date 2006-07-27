#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
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
#include "worker.h"
#include "claim.h"

/*
 * Fid pool state
 */

Vector *fid_remote_vector;
List *fid_deleted_list;

void fid_insert_local(Connection *conn, u32 fid, char *user, Claim *claim) {
    Fid *res = GC_NEW(Fid);

    assert(res != NULL);
    assert(conn != NULL);
    assert(fid != NOFID);
    assert(!emptystring(user));
    assert(claim != NULL);

    assert(!vector_test(conn->fid_vector, fid));

    res->lock = NULL;

    res->fid = fid;
    res->pathname = claim->pathname;
    res->user = user;
    res->status = STATUS_UNOPENNED;
    res->omode = 0;
    res->readdir_cookie = 0;
    res->addr = conn->addr;
    res->isremote = 0;

    res->claim = NULL;
    res->readdir_env = NULL;

    res->raddr = NULL;
    res->rfid = NOFID;

    fid_link_claim(res, claim);
    vector_set(conn->fid_vector, res->fid, res);
    hash_set(res->claim->lease->fids, res, res);
}

void fid_insert_remote(Connection *conn, u32 fid, char *pathname, char *user,
        Address *raddr, u32 rfid)
{
    Fid *res = GC_NEW(Fid);

    assert(res != NULL);
    assert(conn != NULL);
    assert(fid != NOFID);
    assert(rfid != NOFID);
    assert(!emptystring(pathname));
    assert(!emptystring(user));
    assert(raddr != NULL);

    assert(!vector_test(conn->fid_vector, fid));

    res->lock = NULL;

    res->fid = fid;
    res->pathname = pathname;
    res->user = user;
    res->status = STATUS_UNOPENNED;
    res->omode = 0;
    res->readdir_cookie = 0;
    res->addr = conn->addr;
    res->isremote = 1;

    res->claim = NULL;
    res->readdir_env = NULL;

    res->raddr = raddr;
    res->rfid = rfid;

    vector_set(conn->fid_vector, res->fid, res);
    fid_set_remote(rfid, res);
}

void fid_update_remote(Fid *fid, char *pathname, Address *raddr, u32 rfid) {
    fid->pathname = pathname;
    fid->isremote = 1;
    if (fid->claim != NULL) {
        hash_remove(fid->claim->lease->fids, fid);
        fid_unlink_claim(fid);
    }
    fid->raddr = raddr;
    fid->rfid = rfid;
    fid_set_remote(rfid, fid);
}

void fid_update_local(Fid *fid, Claim *claim) {
    fid->pathname = claim->pathname;
    fid->isremote = 0;
    if (fid->claim != NULL && fid->claim != claim) {
        hash_remove(fid->claim->lease->fids, fid);
        fid_unlink_claim(fid);
    }
    fid->claim = NULL;
    fid->readdir_cookie = 0;
    fid->readdir_env = NULL;
    fid->raddr = NULL;
    fid->rfid = NOFID;

    fid_link_claim(fid, claim);
    hash_set(fid->claim->lease->fids, fid, fid);
}

int fid_cmp(const Fid *a, const Fid *b) {
    int res = addr_cmp(a->addr, b->addr);
    if (res == 0)
        return (int) a->fid - (int) b->fid;
    return res;
}

u32 fid_hash(const Fid *fid) {
    return generic_hash(&fid->fid, sizeof(fid->fid), addr_hash(fid->addr));
}

Fid *fid_lookup(Connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return vector_get(conn->fid_vector, fid);
}

void fid_remove(Worker *worker, Connection *conn, u32 fid) {
    Fid *elt;

    assert(conn != NULL);
    assert(fid != NOFID);

    elt = (Fid *) vector_get_remove(conn->fid_vector, fid);

    assert(elt != NULL);

    if (elt->isremote) {
        fid_release_remote(elt->rfid);
    } else {
        Claim *claim = elt->claim;
        assert(claim != NULL);

        fid_unlink_claim(elt);

        if (claim->deleted)
            fid_unlink_deleted(elt);
        else
            hash_remove(claim->lease->fids, elt);

        if (claim->exclusive && elt->status != STATUS_UNOPENNED)
            claim->exclusive = 0;

        /* was this the last unlink on a deleted file? */
        if (claim->deleted && null(claim->fids) &&
                claim->access == ACCESS_WRITEABLE)
        {
            object_delete(worker, claim->oid);
        }
    }
}

enum claim_access fid_access_child(enum claim_access access, int cowlink) {
    if (access == ACCESS_WRITEABLE && cowlink)
        return ACCESS_COW;
    else
        return access;
}

void fid_state_init(void) {
    fid_remote_vector = vector_create(FID_REMOTE_VECTOR_SIZE);
    fid_deleted_list = NULL;
}

u32 fid_reserve_remote(Worker *worker) {
    u32 result = vector_alloc(fid_remote_vector, (void *) 0xdeadbeef);
    worker_cleanup_add(worker, LOCK_REMOTE_FID, (void *) result);
    return result;
}

void fid_set_remote(u32 rfid, Fid *fid) {
    assert(vector_test(fid_remote_vector, rfid));
    vector_set(fid_remote_vector, rfid, fid);
}

Fid *fid_get_remote(u32 rfid) {
    return vector_get(fid_remote_vector, rfid);
}

void fid_release_remote(u32 fid) {
    vector_remove(fid_remote_vector, fid);
}

List *fid_gather_groups(List *fids) {
    Address *addr = NULL;
    List *groups = NULL;
    List *group = NULL;

    /* gather the fids into groups by address */
    for ( ; !null(fids); fids = cdr(fids)) {
        Fid *fid = car(fids);
        if (!addr_cmp(fid->addr, addr)) {
            group = cons(fid, group);
        } else {
            if (!null(group))
                groups = cons(group, groups);
            group = cons(fid, NULL);
            addr = fid->addr;
        }
    }
    if (!null(group))
        groups = cons(group, groups);
    return groups;
}

void fid_link_claim(Fid *fid, Claim *claim) {
    assert(fid->claim == NULL);
    assert(claim->lease != NULL);
    assert(claim->parent != NULL || !null(claim->children) ||
            !strcmp(claim->lease->pathname, claim->pathname));
    fid->claim = claim;
    claim->fids = insertinorder((Cmpfunc) fid_cmp, claim->fids, fid);
}

void fid_unlink_claim(Fid *fid) {
    assert(fid->claim != NULL);
    fid->claim->fids = removeinorder((Cmpfunc) fid_cmp, fid->claim->fids, fid);
    fid->claim = NULL;
}

void fid_link_deleted(Fid *fid) {
    assert(fid->claim != NULL);
    assert(fid->claim->deleted);
    assert(fid->pathname == NULL);
    fid_deleted_list = insertinorder((Cmpfunc) fid_cmp, fid_deleted_list, fid);
}

void fid_unlink_deleted(Fid *fid) {
    assert(fid->claim == NULL);
    fid_deleted_list = removeinorder((Cmpfunc) fid_cmp, fid_deleted_list, fid);
}
