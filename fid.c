#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "vector.h"
#include "hashtable.h"
#include "connection.h"
#include "fid.h"
#include "util.h"
#include "config.h"
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

    res->claim = claim;
    res->readdir_env = NULL;

    res->raddr = NULL;
    res->rfid = NOFID;

    res->claim->fids = insertinorder((Cmpfunc) fid_cmp, res->claim->fids, res);
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
        fid->claim->fids =
            removeinorder((Cmpfunc) fid_cmp, fid->claim->fids, fid);
        hash_remove(fid->claim->lease->fids, fid);
    }
    fid->claim = NULL;
    fid->raddr = raddr;
    fid->rfid = rfid;
    fid_set_remote(rfid, fid);
}

void fid_update_local(Fid *fid, Claim *claim) {
    fid->pathname = claim->pathname;
    fid->isremote = 0;
    if (fid->claim != NULL && fid->claim != claim) {
        fid->claim->fids =
            removeinorder((Cmpfunc) fid_cmp, fid->claim->fids, fid);
        hash_remove(fid->claim->lease->fids, fid);
    }
    fid->claim = claim;
    fid->readdir_cookie = 0;
    fid->readdir_env = NULL;
    fid->raddr = NULL;
    fid->rfid = NOFID;

    fid->claim->fids = insertinorder((Cmpfunc) fid_cmp, fid->claim->fids, fid);
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

void fid_remove(Connection *conn, u32 fid) {
    Fid *elt;

    assert(conn != NULL);
    assert(fid != NOFID);

    elt = (Fid *) vector_get_remove(conn->fid_vector, fid);

    assert(elt != NULL);

    if (elt->isremote) {
        fid_release_remote(fid);
    } else {
        Claim *claim = elt->claim;
        assert(claim != NULL);
        if (claim->exclusive && elt->status != STATUS_UNOPENNED)
            claim->exclusive = 0;
        claim->fids = removeinorder((Cmpfunc) fid_cmp, claim->fids, elt);
        if (claim->deleted && null(claim->fids))
            removeinorder((Cmpfunc) fid_cmp, fid_deleted_list, elt);
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
