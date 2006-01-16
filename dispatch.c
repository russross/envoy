#include <assert.h>
#include <pthread.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "transaction.h"
#include "state.h"
#include "transport.h"
#include "fs.h"
#include "storage.h"
#include "dispatch.h"
#include "worker.h"
#include "forward.h"

void send_request(Transaction *trans) {
    assert(trans->conn->type == CONN_ENVOY_OUT ||
           trans->conn->type == CONN_STORAGE_OUT);
    assert(trans->in == NULL);

    /* allocate a condition variable */
    assert(trans->wait == NULL);
    trans->wait = new_cond();

    trans_insert(trans);
    put_message(trans->conn, trans->out);
    cond_wait(trans->wait);

    /* we should have response when we wake up */
    assert(trans->in != NULL);
    trans->wait = NULL;
}

void send_requests(List *list) {
    Transaction *trans;
    List *ptr;
    pthread_cond_t *cond;
    int done;

    assert(!null(list));

    cond = new_cond();

    for (ptr = list; !null(ptr); ptr = cdr(ptr)) {
        trans = car(ptr);
        assert(trans->conn->type == CONN_ENVOY_OUT ||
                trans->conn->type == CONN_STORAGE_OUT);
        assert(trans->in == NULL);
        assert(trans->wait == NULL);

        trans->wait = cond;
        trans_insert(trans);
        put_message(trans->conn, trans->out);
    }

    /* We'll wake up every time a response comes in.  Since we may get
     * signalled multiple times before we are scheduled, we have to walk
     * the list to see if we have gathered all the responses. */
    
    done = 0;
    while (!done) {
        /* wait for at least one response */
        cond_wait(cond);

        /* check if we need to wait any longer */
        done = 1;
        for (ptr = list; !null(ptr); ptr = cdr(ptr)) {
            trans = car(ptr);
            if (trans->in == NULL) {
                done = 0;
                break;
            }
        }
    }
    
    /* clear the wait fields */
    for (ptr = list; !null(ptr); ptr = cdr(ptr)) {
        trans = car(ptr);
        trans->wait = NULL;
    }
}

void send_reply(Transaction *trans) {
    assert(trans->conn->type == CONN_ENVOY_IN ||
           trans->conn->type == CONN_CLIENT_IN ||
           trans->conn->type == CONN_STORAGE_IN ||
           trans->conn->type == CONN_UNKNOWN_IN);
    assert(trans->in != NULL);

    put_message(trans->conn, trans->out);
}

void handle_error(Worker *worker, Transaction *trans) {
    state->error_queue = append_elt(state->error_queue, trans);
}

void dispatch(Worker *worker, Transaction *trans) {
    assert(trans->conn->type == CONN_UNKNOWN_IN ||
            trans->conn->type == CONN_CLIENT_IN ||
            trans->conn->type == CONN_ENVOY_IN ||
            trans->conn->type == CONN_STORAGE_IN);
    assert(trans->out == NULL);

    trans->out = message_new();
    trans->out->tag = trans->in->tag;
    trans->out->id = trans->in->id + 1;

    if (trans->conn->type == CONN_UNKNOWN_IN) {
        switch (trans->in->id) {
            case TVERSION:  handle_tversion(worker, trans);             break;
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
                handle_error(worker, trans);
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
        HANDLER(worker, trans); \
    } else { \
        forward_to_envoy(worker, trans); \
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

            case TATTACH:   handle_tattach(worker, trans);              break;

            case TAUTH:     handle_tauth(worker, trans);                break;
            case TFLUSH:    handle_tflush(worker, trans);               break;
            case TWALK:     client_twalk(worker, trans);                break;

            case TVERSION:
            default:
                handle_error(worker, trans);
                printf("\nBad request from client\n");
        }
    } else if (trans->conn->type == CONN_ENVOY_IN) {
        // This request has been forwarded to us, so pass it directly to the
        // handler.  Behavior for objects we no longer own is
        // not yet implemented
        switch (trans->in->id) {
            case TATTACH:   handle_tattach(worker, trans);  break;
            case TOPEN:     handle_topen(worker, trans);    break;
            case TCREATE:   handle_tcreate(worker, trans);  break;
            case TREAD:     handle_tread(worker, trans);    break;
            case TWRITE:    handle_twrite(worker, trans);   break;
            case TCLUNK:    handle_tclunk(worker, trans);   break;
            case TREMOVE:   handle_tremove(worker, trans);  break;
            case TSTAT:     handle_tstat(worker, trans);    break;
            case TWSTAT:    handle_twstat(worker, trans);   break;

            case TAUTH:     handle_tauth(worker, trans);    break;
            case TFLUSH:    handle_tflush(worker, trans);   break;
            case TWALK:     envoy_twalk(worker, trans);     break;

            case TVERSION:
            default:
                handle_error(worker, trans);
                printf("\nBad request from envoy\n");
        }
    } else if (trans->conn->type == CONN_STORAGE_IN) {
        switch (trans->in->id) {
            case TSRESERVE: handle_tsreserve(worker, trans);    break;
            case TSCREATE:  handle_tscreate(worker, trans);     break;
            case TSCLONE:   handle_tsclone(worker, trans);      break;
            case TSREAD:    handle_tsread(worker, trans);       break;
            case TSWRITE:   handle_tswrite(worker, trans);      break;
            case TSSTAT:    handle_tsstat(worker, trans);       break;
            case TSWSTAT:   handle_tswstat(worker, trans);      break;

            default:
                handle_error(worker, trans);
                printf("\nBad storage request\n");
        }
    } else {
        assert(0);
    }
}
