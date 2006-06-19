#include <assert.h>
#include <pthread.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "transaction.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "state.h"
#include "transport.h"
#include "storage.h"
#include "envoy.h"
#include "dispatch.h"
#include "worker.h"
#include "claim.h"

static void rerror(Message *m, u16 errnum, int line) {
    m->id = RERROR;
    m->msg.rerror.errnum = errnum;
    m->msg.rerror.ename = stringcopy(strerror(errnum));
    fprintf(stderr, "error #%u: %s (%s line %d)\n",
            (u32) errnum, m->msg.rerror.ename, __FILE__, line);
}

#define failif(p,e) do { \
    if (p) { \
        rerror(trans->out, e, __LINE__); \
        send_reply(trans); \
        return; \
    } \
} while (0)

void send_request(Transaction *trans) {
    assert(trans->conn->type == CONN_ENVOY_OUT ||
           trans->conn->type == CONN_STORAGE_OUT);
    assert(trans->in == NULL);

    /* allocate a condition variable */
    assert(trans->wait == NULL);
    trans->wait = cond_new();

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

    cond = cond_new();

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

void dispatch_unknown(Worker *worker, Transaction *trans) {
    switch (trans->in->id) {
        case TVERSION:
            handle_tversion(worker, trans);
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
            handle_error(worker, trans);
            printf("\nBad request from unknown connection\n");
    }
}

void dispatch_storage(Worker *worker, Transaction *trans) {
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
}

void dispatch(Worker *worker, Transaction *trans) {
    u32 oldfid = NOFID;
    u32 newfid = NOFID;

    if (!state->isstorage && storage_server_count == 0) {
        storage_server_connection_init();
    }

    assert(trans->conn->type == CONN_UNKNOWN_IN ||
            trans->conn->type == CONN_CLIENT_IN ||
            trans->conn->type == CONN_ENVOY_IN ||
            trans->conn->type == CONN_STORAGE_IN);
    assert(trans->out == NULL);

    trans->out = message_new();
    trans->out->tag = trans->in->tag;
    trans->out->id = trans->in->id + 1;

    /* farm out handshakes and storage messages */
    if (trans->conn->type == CONN_UNKNOWN_IN) {
        dispatch_unknown(worker, trans);
        return;
    } else if (trans->conn->type == CONN_STORAGE_IN) {
        dispatch_storage(worker, trans);
        return;
    } else if (trans->conn->type != CONN_CLIENT_IN &&
            trans->conn->type != CONN_ENVOY_IN)
    {
        assert(0);
    }

    /* validate the fid(s) from the request */
#define new_fid(FIELD) do { \
    newfid = trans->in->msg.FIELD; \
    failif(newfid == NOFID, EBADF); \
} while(0)
#define old_fid(FIELD) do { \
    oldfid = trans->in->msg.FIELD; \
    failif(oldfid == NOFID, EBADF); \
} while(0)
    switch (trans->in->id) {
        case TAUTH:
        case TFLUSH:
            break;

        case TATTACH:   new_fid(tattach.fid);                   break;
        case TWALK:     old_fid(twalk.fid);
                        new_fid(twalk.newfid);                  break;
        case TOPEN:     old_fid(topen.fid);                     break;
        case TCREATE:   old_fid(tcreate.fid);                   break;
        case TREAD:     old_fid(tread.fid);                     break;
        case TWRITE:    old_fid(twrite.fid);                    break;
        case TCLUNK:    old_fid(tclunk.fid);                    break;
        case TREMOVE:   old_fid(tremove.fid);                   break;
        case TSTAT:     old_fid(tstat.fid);                     break;
        case TWSTAT:    old_fid(twstat.fid);                    break;

        case TVERSION:
        default:
            handle_error(worker, trans);
            printf("\nBad request from client or envoy\n");
    }
#undef old_fid
#undef new_fid

    /* if a new fid is required, make sure it isn't already in use */
    if (newfid != NOFID && newfid != oldfid &&
            fid_lookup(trans->conn, newfid) != NULL)
    {
        failif(fid_lookup(trans->conn, newfid) == NULL, EBADF);
    }

    /* check and possibly lock the fid */
    if (oldfid != NOFID) {
        Fid *fid = fid_lookup(trans->conn, oldfid);

        /* make sure it's a valid fid */
        failif(fid == NULL, EBADF);

        /* lock the objects this transaction will use */
        if (fid->isremote) {
            /* lock the fid only */
            reserve(worker, LOCK_FID, fid);
        } else {
            /* lock the lease first */
            lock_lease(worker, fid->claim->lease);

            /* next lock the fid */
            reserve(worker, LOCK_FID, fid);

            /* for write ops, make sure the object is writable */
            if (trans->in->id == TCREATE || trans->in->id == TWRITE ||
                    trans->in->id == TREMOVE || trans->in->id == TWSTAT ||
                    (trans->in->id == TOPEN &&
                     (trans->in->msg.topen.mode & OTRUNC)))
            {
                if (fid->claim->access == ACCESS_COW) {
                    claim_thaw(worker, fid->claim);
                } else if (fid->claim->access == ACCESS_READONLY) {
                    failif(1, EACCES);
                }
            }

            /* finally, lock the claim */
            /* reserve(worker, LOCK_CLAIM, fid->claim); */
        }
    }

    /* dispatch to the individual handlers */
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

        case TWALK:
                        if (trans->conn->type == CONN_CLIENT_IN)
                            client_twalk(worker, trans);
                        else
                            envoy_twalk(worker, trans);
                        break;

        case TVERSION:
        default:
                        handle_error(worker, trans);
                        printf("\nBad request from client\n");
    }
}
