#include <stdlib.h>
#include <assert.h>
#include "9p.h"
#include "state.h"
#include "transport.h"
#include "fs.h"

static int get_client_request_handler(struct transaction *trans) {
    switch (trans->in->id) {
        case TVERSION:
            trans->handler = client_tversion;
            break;
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
        default:
            return -1;
    }

    return 0;
}

void main_loop(void) {
    struct transaction *trans;

    for (;;) {
        trans = get_transaction();
        switch (trans->conn->type) {
            case CONN_CLIENT_IN:
                if (get_client_request_handler(trans) < 0) {
                    printf("\nBad request from client\n");
                    continue;
                }
                trans->out = message_new();
                trans->out->maxSize = trans->conn->maxSize;
                trans->out->tag = trans->in->tag;
                trans->out->id = trans->in->id + 1;
                break;

            case CONN_ENVOY_IN:
                if (1) {
                    printf("\nBad request from envoy\n");
                    continue;
                }
                break;

            case CONN_ENVOY_OUT:
            case CONN_STORAGE_OUT:
                if (trans->handler == NULL) {
                    printf("\nResponse found transaction with no handler\n");
                    continue;
                }
                break;

            default:
                assert(0);
        }

        trans = trans->handler(trans);

        put_transaction(trans);
        
        /*state_dump();*/
    }
}
