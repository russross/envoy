#include <assert.h>
#include <pthread.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "transaction.h"
#include "util.h"
#include "config.h"
#include "state.h"
#include "transport.h"
#include "fs.h"
#include "dispatch.h"
#include "worker.h"
#include "forward.h"

void send_request(Transaction *trans) {
    assert(trans->conn->type == CONN_ENVOY_OUT ||
           trans->conn->type == CONN_STORAGE_OUT);
    assert(trans->in == NULL);

    /* allocate a condition variable */
    assert(trans->wait == NULL);
    trans->wait = GC_NEW(pthread_cond_t);
    assert(trans->wait != NULL);
    pthread_cond_init(trans->wait, NULL);

    trans_insert(trans);
    put_message(trans->conn, trans->out);
    worker_wait(trans);

    /* we should have response when we wake up */
    assert(trans->in != NULL);
    GC_free(trans->wait);
    trans->wait = NULL;
}

void send_requests(List *list) {
    Transaction *trans;
    List *ptr;
    pthread_cond_t *wait;
    int count = 0, remaining = 0;

    assert(!null(list));

    wait = GC_NEW(pthread_cond_t);
    assert(wait != NULL);
    pthread_cond_init(wait, NULL);

    for (ptr = list; !null(ptr); ptr = cdr(ptr)) {
        count++;
        trans = car(ptr);
        assert(trans->conn->type == CONN_ENVOY_OUT ||
                trans->conn->type == CONN_STORAGE_OUT);
        assert(trans->in == NULL);
        assert(trans->wait == NULL);

        trans->wait = wait;
        trans_insert(trans);
        put_message(trans->conn, trans->out);
    }
    remaining = count;

    /* It's possible that multiple responses arrived and we were signalled
     * multiple times before we woke up.  In that case, active_worker_count
     * will be too high and the main select thread won't be scheduled.
     * We check how many responses have arrived since we were last called,
     * which is the number that have non-NULL wait and in fields.
     * We subtract that number (except for one corresponding to this thread)
     * from active_worker_count. */
    while (remaining > 0) {
        /* wait for at least one response */
        worker_wait_multiple(wait);

        /* this increment compensates for the extra decrement that will
         * happen in the loop below */
        state->active_worker_count++;

        /* see how many responses came in */
        for (ptr = list; !null(ptr); ptr = cdr(ptr)) {
            trans = car(ptr);
            if (trans->in != NULL && trans->wait != NULL) {
                if (--remaining == 0)
                    GC_free(trans->wait);

                trans->wait = NULL;
                state->active_worker_count--;
            }
        }
    }
}

void send_reply(Transaction *trans) {
    assert(trans->conn->type == CONN_ENVOY_IN ||
           trans->conn->type == CONN_CLIENT_IN ||
           trans->conn->type == CONN_UNKNOWN_IN);
    assert(trans->in != NULL);

    put_message(trans->conn, trans->out);
}

void handle_error(Transaction *trans) {
    state->error_queue = append_elt(state->error_queue, trans);
}

static void *dispatch(Transaction *trans) {
    assert(trans->conn->type == CONN_UNKNOWN_IN ||
            trans->conn->type == CONN_CLIENT_IN ||
            trans->conn->type == CONN_ENVOY_IN);
    assert(trans->out == NULL);

    trans->out = message_new();
    trans->out->tag = trans->in->tag;
    trans->out->id = trans->in->id + 1;

    if (trans->conn->type == CONN_UNKNOWN_IN) {
        switch (trans->in->id) {
            case TVERSION:  handle_tversion(trans);        break;
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

/*
 * This macro checks if the fid for a transaction has a forwarding address,
 * and if so calls forward_to_envoy with it.  Otherwise, it calls the normal
 * handler.
 */
#define forward_or_handle(MESSAGE,HANDLER) do { \
    if (forward_lookup(trans->conn, trans->in->msg.MESSAGE.fid) == NULL) { \
        HANDLER(trans); \
    } else { \
        forward_to_envoy(trans); \
    } \
} while(0);

        switch (trans->in->id) {
            case TOPEN:     forward_or_handle(topen, handle_topen);     break;
            case TCREATE:   forward_or_handle(tcreate, handle_tcreate); break;
            case TREAD:     forward_or_handle(tread, handle_tread);     break;
            case TWRITE:    forward_or_handle(twrite, handle_twrite);   break;
            case TCLUNK:    forward_or_handle(tclunk, handle_tclunk);   break;
            case TREMOVE:   forward_or_handle(tremove, handle_tremove); break;
            case TSTAT:     forward_or_handle(tstat, handle_tstat);     break;
            case TWSTAT:    forward_or_handle(twstat, handle_twstat);   break;

#undef forward_or_handle

            case TATTACH:   handle_tattach(trans);          break;

            case TAUTH:     handle_tauth(trans);            break;
            case TFLUSH:    handle_tflush(trans);           break;
            case TWALK:     client_twalk(trans);            break;

            case TVERSION:
            default:
                handle_error(trans);
                printf("\nBad request from client\n");
        }
    } else if (trans->conn->type == CONN_ENVOY_IN) {
        // This request has been forwarded to us, so pass it directly to the
        // handler.  Behavior for objects we no longer own is
        // not yet implemented
        switch (trans->in->id) {
            case TATTACH:   handle_tattach(trans);          break;
            case TOPEN:     handle_topen(trans);            break;
            case TCREATE:   handle_tcreate(trans);          break;
            case TREAD:     handle_tread(trans);            break;
            case TWRITE:    handle_twrite(trans);           break;
            case TCLUNK:    handle_tclunk(trans);           break;
            case TREMOVE:   handle_tremove(trans);          break;
            case TSTAT:     handle_tstat(trans);            break;
            case TWSTAT:    handle_twstat(trans);           break;

            case TAUTH:     handle_tauth(trans);            break;
            case TFLUSH:    handle_tflush(trans);           break;
            case TWALK:     envoy_twalk(trans);             break;

            case TVERSION:
            default:
                handle_error(trans);
                printf("\nBad request from envoy\n");
        }
    } else {
        assert(0);
    }

    return NULL;
}

void main_loop(void) {
    Transaction *trans;
    Message *msg;
    Connection *conn;

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

                trans = trans_new(conn, msg, NULL);

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

int connect_envoy(Connection *conn) {
    Transaction *trans;
    struct Rversion *res;

    /* prepare a Tversion message and package it in a transaction */
    trans = trans_new(conn, NULL, message_new());
    trans->out->tag = NOTAG;
    trans->out->id = TVERSION;
    set_tversion(trans->out, trans->conn->maxSize, "9P2000.envoy");

    send_request(trans);

    /* check Rversion results and prepare a Tauth message */
    res = &trans->in->msg.rversion;

    /* blow up if the reply wasn't what we were expecting */
    if (trans->in->id != RVERSION || strcmp(res->version, "9P2000.envoy")) {
        handle_error(trans);
        return -1;
    }

    conn->maxSize = max(min(GLOBAL_MAX_SIZE, res->msize), GLOBAL_MIN_SIZE);

    return 0;
}
