/* Transaction dependencies are tracked using the children and parent fields.
 * When a message is received, the following rules are considered:
 *
 * 1. If the message is a new request, a handler is found based on the
 *    connection and message type.
 * 2. Else if handler and parent are both null, assert a failure.
 * 3. Else if handler is !null, handler is called.
 * 4. Else parent's children list is checked; if all transactions are
 *    completed, parent is processed starting at step 2.
 *
 * A handler returns a single transaction, but it may also make other changes
 * to transaction structures.  The returned transaction may have either a
 * message to be sent (out) or a list of children transactions with out fields
 * to be sent.
 */

#include <stdlib.h>
#include <assert.h>
#include "9p.h"
#include "state.h"
#include "map.h"
#include "transport.h"
#include "fs.h"
#include "list.h"
#include "config.h"

void send_request(struct transaction *trans) {
    assert(trans->conn->type == CONN_ENVOY_OUT ||
           trans->conn->type == CONN_STORAGE_OUT);
    assert(trans->handler != NULL || trans->parent != NULL);
    assert(trans->in == NULL);

    put_message(trans);

    /* index this transaction so we can match the reply when it comes */
    trans_insert(trans);
}

void send_reply(struct transaction *trans) {
    assert(trans->conn->type == CONN_ENVOY_IN ||
           trans->conn->type == CONN_CLIENT_IN ||
           trans->conn->type == CONN_UNKNOWN_IN);
    assert(trans->in != NULL);

    put_message(trans);
}

void queue_transaction(struct transaction *trans) {
    state->transaction_queue = append_elt(state->transaction_queue, trans);
}

void handle_error(struct transaction *trans) {
    state->error_queue = append_elt(state->error_queue, trans);
}

static int get_unknown_request_handler(struct transaction *trans) {
    switch (trans->in->id) {
        case TVERSION:
            trans->handler = unknown_tversion;
            break;
        case TAUTH:
        case TREAD:
        case TWRITE:
        case TATTACH:
        case TFLUSH:
        case TWALK:
        case TOPEN:
        case TCREATE:
        case TCLUNK:
        case TREMOVE:
        case TSTAT:
        case TWSTAT:
        default:
            return -1;
    }

    return 0;
}

static int get_client_request_handler(struct transaction *trans) {
    switch (trans->in->id) {
        case TAUTH:
            trans->handler = client_tauth;
            break;
        case TFLUSH:
            trans->handler = client_tflush;
            break;
        case TATTACH:
            trans->handler = client_tattach;
            break;
        case TWALK:
            trans->handler = client_twalk;
            break;
        case TOPEN:
            trans->handler = client_topen;
            break;
        case TCREATE:
            trans->handler = client_tcreate;
            break;
        case TREAD:
            trans->handler = client_tread;
            break;
        case TWRITE:
            trans->handler = client_twrite;
            break;
        case TCLUNK:
            trans->handler = client_tclunk;
            break;
        case TREMOVE:
            trans->handler = client_tremove;
            break;
        case TSTAT:
            trans->handler = client_tstat;
            break;
        case TWSTAT:
            trans->handler = client_twstat;
            break;
        case TVERSION:
        default:
            return -1;
    }

    return 0;
}

static int get_envoy_request_handler(struct transaction *trans) {
    switch (trans->in->id) {
        case TAUTH:
            trans->handler = envoy_tauth;
            break;
        case TFLUSH:
            trans->handler = envoy_tflush;
            break;
        case TATTACH:
            trans->handler = envoy_tattach;
            break;
        case TWALK:
            trans->handler = envoy_twalk;
            break;
        case TOPEN:
            trans->handler = envoy_topen;
            break;
        case TCREATE:
            trans->handler = envoy_tcreate;
            break;
        case TREAD:
            trans->handler = envoy_tread;
            break;
        case TWRITE:
            trans->handler = envoy_twrite;
            break;
        case TCLUNK:
            trans->handler = envoy_tclunk;
            break;
        case TREMOVE:
            trans->handler = envoy_tremove;
            break;
        case TSTAT:
            trans->handler = envoy_tstat;
            break;
        case TWSTAT:
            trans->handler = envoy_twstat;
            break;
        case TVERSION:
        default:
            return -1;
    }

    return 0;
}

static void dispatch(struct transaction *trans) {
    switch (trans->conn->type) {
        case CONN_UNKNOWN_IN:
        case CONN_CLIENT_IN:
        case CONN_ENVOY_IN:
            assert(trans->out == NULL);

            switch (trans->conn->type) {
                case CONN_UNKNOWN_IN:
                    if (get_unknown_request_handler(trans) < 0) {
                        handle_error(trans);
                        printf("\nBad request from unknown connection\n");
                        return;
                    }
                    break;
                case CONN_CLIENT_IN:
                    if (get_client_request_handler(trans) < 0) {
                        handle_error(trans);
                        printf("\nBad request from client\n");
                        return;
                    }
                    break;
                case CONN_ENVOY_IN:
                    if (get_envoy_request_handler(trans) < 0) {
                        handle_error(trans);
                        printf("\nBad request from envoy\n");
                        return;
                    }
                    break;
                default:
                    assert(0);
            }
            trans->out = message_new();
            trans->out->maxSize = trans->conn->maxSize;
            trans->out->tag = trans->in->tag;
            trans->out->id = trans->in->id + 1;

            trans->handler(trans);
            break;

        case CONN_ENVOY_OUT:
        case CONN_STORAGE_OUT:
            assert(trans->handler != NULL || trans->parent != NULL);

            /* first check if there are any children waiting */
            if (trans->children != NULL) {
                struct cons *list = trans->children;
                struct transaction *elt;

                while (!null(list)) {
                    elt = car(list);
                    list = cdr(list);

                    /* nothing to do--one of the children is still pending */
                    if (elt->in == NULL)
                        return;
                }
            }

            if (trans->handler != NULL)
                trans->handler(trans);

            break;

        default:
            assert(0);
    }
}

void main_loop(void) {
    struct transaction *trans;
    struct message *msg;
    struct connection *conn;

    for (;;) {
        /* handle any pending errors */
        if (!null(state->error_queue)) {
            printf("PANIC! Unhandled error\n");
            state->error_queue = cdr(state->error_queue);
            continue;
        }

        /* handle any pending transactions */
        if (!null(state->transaction_queue)) {
            trans = car(state->transaction_queue);
            state->transaction_queue = cdr(state->transaction_queue);
            dispatch(trans);
            continue;
        }

        /* receive a new message while trying to write outgoing messages */
        msg = get_message(&conn);
        trans = trans_lookup_remove(conn, msg->tag);

        /* what kind of request/response is this? */
        switch (conn->type) {
            case CONN_UNKNOWN_IN:
            case CONN_CLIENT_IN:
            case CONN_ENVOY_IN:
                /* this is a new transaction */
                assert(trans == NULL);
                trans = transaction_new();
                trans->conn = conn;
                trans->in = msg;

                queue_transaction(trans);

                break;

            case CONN_ENVOY_OUT:
                /* this is a reply to a request we made */
                assert(trans != NULL);

                queue_transaction(trans);

                break;
                
            case CONN_STORAGE_OUT:
            default:
                assert(0);
        }
        state_dump();
    }
}

void connect_envoy(struct transaction *parent, struct connection *conn);
static void connect_envoy_ii(struct transaction *trans);

void connect_envoy(struct transaction *parent, struct connection *conn) {
    /* prepare a Tversion message and package it in a transaction */
    struct transaction *trans = transaction_new();

    trans->handler = connect_envoy_ii;
    trans->conn = conn;
    trans->in = NULL;
    trans->out = message_new();
    trans->out->maxSize = trans->conn->maxSize;
    trans->out->tag = NOTAG;
    trans->out->id = TVERSION;
    trans->out->msg.tversion.msize = trans->conn->maxSize;
    trans->out->msg.tversion.version = "9P2000.envoy";
    trans->children = NULL;
    trans->parent = parent;
    parent->children = append_elt(parent->children, trans);

    send_request(trans);
}

static void connect_envoy_ii(struct transaction *trans) {
    /* check Rversion results and prepare a Tauth message */
    struct Rversion *res = &trans->in->msg.rversion;

    /* blow up if the reply wasn't what we were expecting */
    if (trans->in->id != RVERSION || strcmp(res->version, "9P2000.envoy")) {
        handle_error(trans);
        return;
    }

    trans->conn->maxSize =
        max(min(GLOBAL_MAX_SIZE, res->msize), GLOBAL_MIN_SIZE);

    assert(trans->parent != NULL);

    /* try again on the parent transaction to initiate an attach */
    queue_transaction(trans->parent);
}
