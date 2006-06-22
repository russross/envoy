#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "vector.h"
#include "hashtable.h"
#include "connection.h"
#include "transaction.h"
#include "fid.h"
#include "util.h"
#include "state.h"

/*
 * Debugging functions
 */

static void print_status(enum fid_status status) {
    printf("%s",
            status == STATUS_UNOPENNED ? "STATUS_UNOPENNED" :
            status == STATUS_OPEN_FILE ? "STATUS_OPEN_FILE" :
            status == STATUS_OPEN_DIR ? "STATUS_OPEN_DIR" :
            "(unknown status)");
}

static void print_fid(Fid *fid) {
    printf("    fid:%u path:%s uname:%s\n    ",
            fid->fid, fid->claim->pathname, fid->user);
    print_status(fid->status);
    printf(" cookie[%llu] omode[$%x]\n", fid->readdir_cookie, fid->omode);
    if (fid->readdir_env != NULL)
        printf("    readdir in progress:\n");
}

static void print_fid_fid(u32 n, Fid *fid) {
    printf("   %-2u:\n", n);
    print_fid(fid);
}

static void print_transaction(Transaction *trans) {
    if (trans->in != NULL) {
        printf("    Req: ");
        printMessage(stdout, trans->in);
    }
    if (trans->out != NULL) {
        printf("    Res: ");
        printMessage(stdout, trans->out);
    }
}

static void print_tag_trans(u32 tag, Transaction *trans) {
    printf("   %-2u:\n", tag);
    print_transaction(trans);
}

static void print_connection_type(enum conn_type type) {
    printf("%s",
            type == CONN_CLIENT_IN ? "CONN_CLIENT_IN" :
            type == CONN_ENVOY_IN ? "CONN_ENVOY_IN" :
            type == CONN_ENVOY_OUT ? "CONN_ENVOY_OUT" :
            type == CONN_STORAGE_OUT ? "CONN_STORAGE_OUT" :
            "(unknown state)");
}

static void print_connection(Connection *conn) {
    printf(" {%s} fd:%d ", address_to_string(conn->addr), conn->fd);
    print_connection_type(conn->type);
    printf("\n  fids:\n");
    vector_apply(conn->fid_vector, (void (*)(u32, void *)) print_fid_fid);
    printf("  transactions:\n");
    vector_apply(conn->tag_vector, (void (*)(u32, void *)) print_tag_trans);
}

static void print_addr_conn(Address *addr, Connection *conn) {
    print_connection(conn);
}

void state_dump(void) {
    printf("Hashtable: address -> connection\n");
    hash_apply(addr_2_conn, (void (*)(void *, void *)) print_addr_conn);
}
