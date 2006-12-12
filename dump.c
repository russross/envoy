#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "vector.h"
#include "hashtable.h"
#include "connection.h"
#include "transaction.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "envoy.h"
#include "claim.h"
#include "lease.h"
#include "dump.h"

/*
 * Debugging functions
 */

/*
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
*/

/*****************************************************************************/

/* dump a lease as a dot graph */

static char *deslash(char *s) {
    char *res = stringcopy(s);
    int i;

    for (i = 0; res[i]; i++)
        if (res[i] == '/' || res[i] == '-' || res[i] == '.')
            res[i] = '_';

    return res;
}

void dump_dot(FILE *fp, Lease *lease) {
    int i;
    List *list;
    List *stack;
    double now = now_double();

    fprintf(fp, "\n/* lease root is %s */\n", lease->pathname);
    fprintf(fp, "digraph %s {\n", deslash(lease->pathname));

    /* the root */
    fprintf(fp, "  %s [shape=box];\n", deslash(lease->pathname));

    /* mark the exits */
    for (list = lease->wavefront; !null(list); list = cdr(list)) {
        Lease *exit = car(list);
        fprintf(fp, "  %s [shape=box,label=\"%s\"];\n", deslash(exit->pathname),
                filename(exit->pathname));
        fprintf(fp, "  %s -> %s [style=dotted];\n",
            deslash(dirname(exit->pathname)),
            deslash(exit->pathname));
    }

    /* walk the claim tree */
    stack = cons(lease->claim, NULL);
    while (!null(stack)) {
        Claim *claim = car(stack);
        stack = cdr(stack);

        /* update the counters (copied from claim.c) */
        if (claim->lastupdate < lease->lastchange) {
            for (i = 0; i < claim->urgencycount; i++)
                claim->urgency[i] = 0.0;
            claim->lastupdate = now;
        } else {
            double decay = pow(0.5, (now - claim->lastupdate) / ter_halflife);

            for (i = 0; i < claim->urgencycount; i++)
                claim->urgency[i] *= decay;
            claim->lastupdate = now;
        }

        /* add children to the stack */
        for (list = claim->children; !null(list); list = cdr(list))
            stack = cons(car(list), stack);

        /* link this claim to its parent */
        if (claim->parent != NULL) {
            fprintf(fp, "  %s -> %s;\n",
                    deslash(claim->parent->pathname),
                    deslash(claim->pathname));
        }

        fprintf(fp, "  %s [label=\"%s\"];\n", deslash(claim->pathname),
                filename(claim->pathname));
    }

    fprintf(fp, "}\n");
}

static int dot_counter = 0;

void dump_dot_all(FILE *fp) {
    List *leases = hash_tolist(lease_by_root_pathname);
    for ( ; !null(leases); leases = cdr(leases)) {
        Lease *lease = car(leases);
        if (!lease->isexit)
            dump_dot(fp, lease);
    }
}

void dump_conn_all_iter(FILE *fp, u32 i, Connection *conn) {
    if (conn == NULL || conn->addr == NULL)
        return;
    fprintf(fp, " *   %s ", addr_to_string(conn->addr));
    switch (conn->type) {
        case CONN_CLIENT_IN:    fprintf(fp, "CLIENT_IN");       break;
        case CONN_ENVOY_IN:     fprintf(fp, "ENVOY_IN");        break;
        case CONN_ENVOY_OUT:    fprintf(fp, "ENVOY_OUT");       break;
        case CONN_STORAGE_IN:   fprintf(fp, "STORAGE_IN");      break;
        case CONN_STORAGE_OUT:  fprintf(fp, "STORAGE_OUT");     break;
        case CONN_UNKNOWN_IN:   fprintf(fp, "UNKNOWN_IN");      break;
        default:
            assert(0);
    }
    fprintf(fp, ":\n");
    fprintf(fp, " *     messages/bytes in     : %d/%lld\n",
            conn->totalmessagesin, conn->totalbytesin);
    fprintf(fp, " *     messages/bytes out    : %d/%lld\n",
            conn->totalmessagesout, conn->totalbytesout);
    if (conn->prevbytesout != 0 || conn->prevbytesin != 0) {
        fprintf(fp, " *     new messages/bytes in : %d/%lld\n",
                conn->totalmessagesin - conn->prevmessagesin,
                conn->totalbytesin - conn->prevbytesin);
        fprintf(fp, " *     new messages/bytes out: %d/%lld\n",
                conn->totalmessagesout - conn->prevmessagesout,
                conn->totalbytesout - conn->prevbytesout);
    }
    conn->prevbytesin = conn->totalbytesin;
    conn->prevbytesout = conn->totalbytesout;
    conn->prevmessagesin = conn->totalmessagesin;
    conn->prevmessagesout = conn->totalmessagesout;
}

void dump_conn_all(FILE *fp) {
    fprintf(fp, "/* Connections:\n");
    vector_apply(conn_vector,
            (void (*)(void *, u32, void *)) dump_conn_all_iter, fp);
    fprintf(fp, " */\n");
}

void dump(char *name) {
    char filename[100];
    FILE *fp;
    time_t now;

    sprintf(filename, "/tmp/%s.%d.dot", name, ++dot_counter);

    fp = fopen(filename, "w");
    if (fp == NULL) {
        perror(filename);
        assert(0);
    }

    printf("dumping bytecounts and claim trees to %s\n", filename);

    time(&now);
    fprintf(fp, "/* Envoy connection bytecounts and claim trees\n"
                " * Host: %s\n"
                " * Time: %s"
                " */\n\n",
                addr_to_string(my_address),
                ctime(&now));

    dump_conn_all(fp);
    dump_dot_all(fp);
    fclose(fp);
}
