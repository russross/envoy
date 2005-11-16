#include <assert.h>
#include <gc/gc.h>
#include "fid.h"
#include "vector.h"
#include "connection.h"

/*
 * Fid pool state
 */

int fid_insert_new(Connection *conn, u32 fid, char *uname, char *path) {
    Fid *res;

    assert(conn != NULL);
    assert(fid != NOFID);
    assert(uname != NULL && *uname);
    assert(path != NULL && *path);

    if (vector_test(conn->fid_vector, fid))
        return -1;

    res = GC_NEW(Fid);
    assert(res != NULL);

    res->fid = fid;
    res->uname = uname;
    res->path = path;
    res->fd = -1;
    res->status = STATUS_CLOSED;

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

