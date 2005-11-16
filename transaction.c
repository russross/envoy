#include "transaction.h"
#include "vector.h"
#include "connection.h"

/*
 * Transaction pool state
 * Active transactions that are waiting for a response are indexed by tag.
 */

Transaction *trans_new(Connection *conn, Message *in, Message *out) {
    Transaction *trans = GC_NEW(Transaction);
    assert(trans != NULL);
    trans->wait = NULL;
    trans->conn = conn;
    trans->in = in;
    trans->out = out;
    return trans;
}

void trans_insert(Transaction *trans) {
    assert(trans != NULL);
    assert(trans->conn != NULL);
    assert(trans->in == NULL);
    assert(trans->out != NULL);

    /* NOTAG is a special case */
    if (trans->out->tag == NOTAG)
        trans->conn->notag_trans = trans;
    else if (trans->out->tag == ALLOCTAG)
        trans->out->tag = vector_alloc(trans->conn->tag_vector, trans);
    else
        vector_set(trans->conn->tag_vector, trans->out->tag, trans);
}

Transaction *trans_lookup_remove(Connection *conn, u16 tag) {
    assert(conn != NULL);

    if (tag == NOTAG) {
        Transaction *res = conn->notag_trans;
        conn->notag_trans = NULL;
        return res;
    }

    return (Transaction *) vector_get_remove(conn->tag_vector, tag);
}

