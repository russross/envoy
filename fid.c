#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "vector.h"
#include "connection.h"
#include "fid.h"
#include "util.h"
#include "state.h"
#include "claim.h"

/*
 * Fid pool state
 */

int fid_insert_new(Connection *conn, u32 fid, char *uname, Claim *claim) {
    Fid *res;

    assert(conn != NULL);
    assert(fid != NOFID);
    assert(!emptystring(uname));
    assert(claim != NULL);

    if (vector_test(conn->fid_vector, fid))
        return -1;

    res = GC_NEW(Fid);
    assert(res != NULL);

    res->wait = NULL;
    res->claim = claim;

    res->fid = fid;
    res->uname = uname;
    res->status = STATUS_CLOSED;
    res->omode = 0;

    res->readdir_cookie = 0;
    res->readdir_offset = 0;
    res->readdir_current_block = NULL;
    res->readdir_next = NULL;

    vector_set(conn->fid_vector, res->fid, res);

    return 0;
}

Fid *fid_lookup(Connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return vector_get(conn->fid_vector, fid);
}

Fid *fid_lookup_remove(Connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return (Fid *) vector_get_remove(conn->fid_vector, fid);
}

u32 fid_hash(const Fid *fid) {
    return generic_hash(&fid->fid, sizeof(fid->fid), 1);
}

int fid_cmp(const Fid *a, const Fid *b) {
    return (int) a->fid - (int) b->fid;
}
