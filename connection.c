#include <assert.h>
#include <gc/gc.h>
#include "connection.h"
#include "hashtable.h"
#include "state.h"
#include "config.h"

/*
 * Connection pool state
 */

Connection *conn_insert_new(int fd, enum conn_type type,
        Address *addr, int maxSize)
{
    Connection *conn;

    assert(fd >= 0);
    assert(addr != NULL);
    assert(!vector_test(state->conn_vector, fd));
    assert(hash_get(state->addr_2_conn, addr) == NULL);

    conn = GC_NEW(Connection);
    assert(conn != NULL);

    conn->fd = fd;
    conn->type = type;
    conn->addr = addr;
    conn->maxSize = maxSize;
    conn->fid_vector = vector_create(FID_VECTOR_SIZE);
    conn->forward_vector = vector_create(FORWARD_VECTOR_SIZE);
    conn->tag_vector = vector_create(TAG_VECTOR_SIZE);
    conn->pending_writes = NULL;
    conn->notag_trans = NULL;

    vector_set(state->conn_vector, conn->fd, conn);
    hash_set(state->addr_2_conn, conn->addr, conn);

    return conn;
}

Connection *conn_new_unopened(enum conn_type type, Address *addr) {
    Connection *conn;

    assert(addr != NULL);
    assert(hash_get(state->addr_2_conn, addr) == NULL);

    conn = GC_NEW(Connection);
    assert(conn != NULL);

    conn->fd = -1;
    conn->type = type;
    conn->addr = addr;
    conn->maxSize = GLOBAL_MAX_SIZE;
    conn->fid_vector = vector_create(FID_VECTOR_SIZE);
    conn->forward_vector = vector_create(FORWARD_VECTOR_SIZE);
    conn->tag_vector = vector_create(TAG_VECTOR_SIZE);
    conn->pending_writes = NULL;
    conn->notag_trans = NULL;

    return conn;
}

Connection *conn_lookup_fd(int fd) {
    return vector_get(state->conn_vector, fd);
}

Connection *conn_lookup_addr(Address *addr) {
    return hash_get(state->addr_2_conn, addr);
}

Transaction *conn_get_pending_write(Connection *conn) {
    Transaction *res = NULL;

    assert(conn != NULL);

    if (!null(conn->pending_writes)) {
        List *elt = conn->pending_writes;
        res = car(elt);
        conn->pending_writes = cdr(elt);
        GC_free(elt);
    }

    return res;
}

int conn_has_pending_write(Connection *conn) {
    return !null(conn->pending_writes);
}

void conn_queue_write(Transaction *trans) {
    assert(trans != NULL);

    trans->conn->pending_writes =
        append_elt(trans->conn->pending_writes, trans);
}

void conn_remove(Connection *conn) {
    assert(conn != NULL);
    assert(vector_test(state->conn_vector, conn->fd));
    assert(hash_get(state->addr_2_conn, conn->addr) != NULL);

    vector_remove(state->conn_vector, conn->fd);
    hash_remove(state->addr_2_conn, conn->addr);
}
