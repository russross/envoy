#include <stdlib.h>
#include <assert.h>
#include <gc/gc.h>
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
    assert(trans->in == NULL);

    put_message(trans);
    worker_wait(trans);
}

void send_reply(struct transaction *trans) {
    assert(trans->conn->type == CONN_ENVOY_IN ||
           trans->conn->type == CONN_CLIENT_IN ||
           trans->conn->type == CONN_UNKNOWN_IN);
    assert(trans->in != NULL);

    put_message(trans);
}

void handle_error(struct transaction *trans) {
    state->error_queue = append_elt(state->error_queue, trans);
}

static void *dispatch(struct transaction *trans) {
    /* we're a new thread, so acquire the big lock */
    worker_start();

    assert(trans->conn->type == CONN_UNKNOWN_IN ||
            trans->conn->type == CONN_CLIENT_IN ||
            trans->conn->type == CONN_ENVOY_IN);
    assert(trans->out == NULL);

    trans->out = message_new();
    trans->out->maxSize = trans->conn->maxSize;
    trans->out->tag = trans->in->tag;
    trans->out->id = trans->in->id + 1;

    if (trans->conn->type == CONN_UNKNOWN_IN) {
        switch (trans->in->id) {
            case TVERSION:  unknown_tversion(trans);        break;
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
                handle_error(trans);
                printf("\nBad request from unknown connection\n");
        }
    } else if (trans->conn->type == CONN_CLIENT_IN) {
        switch (trans->in->id) {
            case TAUTH:     client_tauth(trans);            break;
            case TFLUSH:    client_tflush(trans);           break;
            case TATTACH:   client_tattach(trans);          break;
            case TWALK:     client_twalk(trans);            break;
            case TOPEN:     client_topen(trans);            break;
            case TCREATE:   client_tcreate(trans);          break;
            case TREAD:     client_tread(trans);            break;
            case TWRITE:    client_twrite(trans);           break;
            case TCLUNK:    client_tclunk(trans);           break;
            case TREMOVE:   client_tremove(trans);          break;
            case TSTAT:     client_tstat(trans);            break;
            case TWSTAT:    client_twstat(trans);           break;
            case TVERSION:
            default:
                handle_error(trans);
                printf("\nBad request from client\n");
        }
    } else if (trans->conn->type == CONN_ENVOY_IN) {
        switch (trans->in->id) {
            case TAUTH:     envoy_tauth(trans);             break;
            case TFLUSH:    envoy_tflush(trans);            break;
            case TATTACH:   envoy_tattach(trans);           break;
            case TWALK:     envoy_twalk(trans);             break;
            case TOPEN:     envoy_topen(trans);             break;
            case TCREATE:   envoy_tcreate(trans);           break;
            case TREAD:     envoy_tread(trans);             break;
            case TWRITE:    envoy_twrite(trans);            break;
            case TCLUNK:    envoy_tclunk(trans);            break;
            case TREMOVE:   envoy_tremove(trans);           break;
            case TSTAT:     envoy_tstat(trans);             break;
            case TWSTAT:    envoy_twstat(trans);            break;
            case TVERSION:
            default:
                handle_error(trans);
                printf("\nBad request from envoy\n");
        }
    } else {
        assert(0);
    }

    worker_finish();
    return NULL;
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

                worker_create((void * (*)(void *)) dispatch, (void *) trans);
                break;

            case CONN_ENVOY_OUT:
                /* this is a reply to a request we made */
                assert(trans != NULL);

                trans->in = msg;

                /* wake up the handler that is waiting for this message */
                worker_wakeup(trans);
                break;
                
            case CONN_STORAGE_OUT:
            default:
                assert(0);
        }
    }
}

struct transaction *connect_envoy(struct connection *conn) {
    /* prepare a Tversion message and package it in a transaction */
    struct transaction *trans = transaction_new();

    trans->conn = conn;
    trans->in = NULL;
    trans->out = message_new();
    trans->out->maxSize = trans->conn->maxSize;
    trans->out->tag = NOTAG;
    trans->out->id = TVERSION;
    set_tversion(trans->out, trans->conn->maxSize, "9P2000.envoy");

    send_request(trans);

    /* check Rversion results and prepare a Tauth message */
    struct Rversion *res = &trans->in->msg.rversion;

    /* blow up if the reply wasn't what we were expecting */
    if (trans->in->id != RVERSION || strcmp(res->version, "9P2000.envoy")) {
        handle_error(trans);
        return NULL;
    }

    trans->conn->maxSize =
        max(min(GLOBAL_MAX_SIZE, res->msize), GLOBAL_MIN_SIZE);

    return trans;
}
