#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "transaction.h"
#include "util.h"
#include "storage.h"
#include "dispatch.h"
#include "oid.h"

/* generate an error response with Unix errno errnum */
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
} while(0)

#define guard(f) do { \
    if ((f) < 0) { \
        rerror(trans->out, errno, __LINE__); \
            send_reply(trans); \
            return; \
    } \
} while(0)

#define require_oid(ptr) do { \
    (ptr) = oid_lookup(req->oid); \
    if ((ptr) == NULL) { \
        rerror(trans->out, EBADF, __LINE__); \
            send_reply(trans); \
            return; \
    } \
} while(0)


void handle_tsreserve(Transaction *trans) {
    struct Rsreserve *res = &trans->out->msg.rsreserve;

    failif(oid_reserve_block(&res->firstoid, &res->count), ENOMEM);

    send_reply(trans);
}

void handle_tscreate(Transaction *trans) {
    struct Tscreate *req = &trans->in->msg.tscreate;
    struct Rscreate *res = &trans->out->msg.rscreate;
}

void handle_tsclone(Transaction *trans) {
    struct Tsclone *req = &trans->in->msg.tsclone;
    struct Rsclone *res = &trans->out->msg.rsclone;
}

void handle_tsread(Transaction *trans) {
    struct Tsread *req = &trans->in->msg.tsread;
    struct Rsread *res = &trans->out->msg.rsread;
    Oid *oid;

    //require_oid(oid);
}

void handle_tswrite(Transaction *trans) {
    struct Tswrite *req = &trans->in->msg.tswrite;
    struct Rswrite *res = &trans->out->msg.rswrite;
}

void handle_tsstat(Transaction *trans) {
    struct Tsstat *req = &trans->in->msg.tsstat;
    struct Rsstat *res = &trans->out->msg.rsstat;
}

void handle_tswstat(Transaction *trans) {
    struct Tswstat *req = &trans->in->msg.tswstat;
    struct Rswstat *res = &trans->out->msg.rswstat;
}
