#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "vector.h"
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
    res->isremote = 0;

    res->claim = claim;
    res->readdir_env = NULL;

    res->raddr = NULL;
    res->rfid = NOFID;

    res->claim->refcount++;

    vector_set(conn->fid_vector, res->fid, res);
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
    res->isremote = 1;

    res->claim = NULL;
    res->readdir_env = NULL;

    res->raddr = raddr;
    res->rfid = rfid;

    vector_set(conn->fid_vector, res->fid, res);
}

void fid_update_remote(Fid *fid, char *pathname, Address *raddr, u32 rfid) {
    fid->pathname = pathname;
    fid->isremote = 1;
    if (fid->claim != NULL)
        fid->claim->refcount--;
    fid->claim = NULL;
    fid->raddr = raddr;
    fid->rfid = rfid;
}

void fid_update_local(Fid *fid, Claim *claim) {
    fid->pathname = claim->pathname;
    fid->isremote = 0;
    if (fid->claim != NULL && fid->claim != claim)
        fid->claim->refcount--;
    fid->claim = claim;
    fid->claim->refcount++;
    fid->readdir_env = NULL;
    fid->raddr = NULL;
    fid->rfid = NOFID;
}

Fid *fid_lookup(Connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return vector_get(conn->fid_vector, fid);
}

Fid *fid_lookup_remove(Connection *conn, u32 fid) {
    Fid *res;

    assert(conn != NULL);
    assert(fid != NOFID);

    res = (Fid *) vector_get_remove(conn->fid_vector, fid);

    if (res->claim != NULL && res->claim->exclusive &&
            res->status != STATUS_UNOPENNED)
    {
        res->claim->exclusive = 0;
    }

    if (res->claim != NULL)
        res->claim->refcount--;

    return res;
}

u32 fid_hash(const Fid *fid) {
    return generic_hash(&fid->fid, sizeof(fid->fid), 1);
}

int fid_cmp(const Fid *a, const Fid *b) {
    return (int) a->fid - (int) b->fid;
}

enum claim_access fid_access_child(enum claim_access access, int cowlink) {
    if (access == ACCESS_WRITEABLE && cowlink)
        return ACCESS_COW;
    else
        return access;
}

void fid_state_init(void) {
    fid_remote_vector = vector_create(FID_REMOTE_VECTOR_SIZE);
}

u32 fid_reserve_remote(void) {
    return vector_alloc(fid_remote_vector, (void *) 0xdeadbeef);
}

void fid_release_remote(u32 fid) {
    vector_remove(fid_remote_vector, fid);
}
