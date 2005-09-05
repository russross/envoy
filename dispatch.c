#include <stdlib.h>
#include <assert.h>
#include "9p.h"
#include "state.h"
#include "map.h"
#include "transport.h"
#include "fs.h"

static int get_unknown_request_handler(struct transaction *trans) {
    switch (trans->in->id) {
        case TVERSION:
            trans->handler = unknown_tversion;
            break;
        case TAUTH:
            trans->handler = unknown_tauth;
            break;
        case TREAD:
            trans->handler = unknown_tread;
            break;
        case TWRITE:
            trans->handler = unknown_twrite;
            break;
        case TATTACH:
            /* client is the default connection type */
            trans->handler = client_tattach;
            break;
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
        case TAUTH:
        default:
            return -1;
    }

    return 0;
}

static int get_envoy_request_handler(struct transaction *trans) {
    switch (trans->in->id) {
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
        case TAUTH:
        default:
            return -1;
    }

    return 0;
}

void main_loop(struct map *root) {
    struct transaction *trans;

    for (;;) {
        trans = get_transaction();
        switch (trans->conn->type) {
            case CONN_UNKNOWN_IN:
                if (get_unknown_request_handler(trans) < 0) {
                    printf("\nBad request from unknown connection\n");
                    continue;
                }
                trans->out = message_new();
                trans->out->maxSize = trans->conn->maxSize;
                trans->out->tag = trans->in->tag;
                trans->out->id = trans->in->id + 1;
                break;

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
                if (get_envoy_request_handler(trans) < 0) {
                    printf("\nBad request from envoy\n");
                    continue;
                }
                trans->out = message_new();
                trans->out->maxSize = trans->conn->maxSize;
                trans->out->tag = trans->in->tag;
                trans->out->id = trans->in->id + 1;
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
