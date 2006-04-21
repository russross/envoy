#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "vector.h"
#include "connection.h"
#include "forward.h"
#include "util.h"
#include "state.h"

/*
 * Forwarded fid state.
 */

u32 forward_create_new(Connection *conn, u32 fid, char *pathname, char *user,
        Address *raddr)
{
    Forward *fwd;
    u32 rfid;

    assert(conn != NULL);
    assert(fid != NOFID);
    assert(!emptystring(pathname));
    assert(!emptystring(user));
    assert(raddr != NULL);

    if (vector_test(conn->forward_vector, fid))
        return NOFID;

    fwd = GC_NEW(Forward);
    assert(fwd != NULL);

    rfid = vector_alloc(state->forward_fids, fwd);

    fwd->wait = NULL;
    fwd->fid = fid;
    fwd->pathname = pathname;
    fwd->user = user;
    fwd->raddr = raddr;
    fwd->rfid = rfid;

    vector_set(conn->forward_vector, fid, fwd);

    return rfid;
}

Forward *forward_lookup(Connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return vector_get(conn->forward_vector, fid);
}

Forward *forward_lookup_remove(Connection *conn, u32 fid) {
    Forward *fwd;

    assert(conn != NULL);
    assert(fid != NOFID);

    fwd = vector_get_remove(conn->forward_vector, fid);

    if (fwd != NULL)
        vector_remove(state->forward_fids, fwd->rfid);

    return fwd;
}
