#include <assert.h>
#include <stdlib.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "connection.h"
#include "transaction.h"
#include "util.h"
#include "remote.h"
#include "dispatch.h"
#include "worker.h"

struct p9stat *remote_stat(Worker *worker, Address *target, char *pathname) {
    Transaction *trans;
    struct Restatremote *res;

    trans = trans_new(conn_get_from_addr(worker, target), NULL, message_new());
    trans->out->tag = ALLOCTAG;
    trans->out->id = TESTATREMOTE;
    set_testatremote(trans->out, pathname);

    send_request(trans);

    assert(trans->in != NULL && trans->in->id == RESTATREMOTE);
    res = &trans->in->msg.restatremote;

    /* insert the filename supplied by the caller */
    res->stat->name = filename(pathname);

    return res->stat;
}

u16 remote_walk(Worker *worker, Address *target,
        u32 fid, u32 newfid, u16 nwname, char **wname,
        char *user, char *pathname,
        u16 *nwqid, struct qid **wqid, Address **address)
{
    Transaction *trans;
    struct Rewalkremote *res;

    trans = trans_new(conn_get_from_addr(worker, target), NULL, message_new());
    trans->out->tag = ALLOCTAG;
    trans->out->id = TEWALKREMOTE;
    set_tewalkremote(trans->out, fid, newfid, nwname, wname, user, pathname);

    send_request(trans);

    assert(trans->in != NULL && trans->in->id == REWALKREMOTE);
    res = &trans->in->msg.rewalkremote;

    *nwqid = res->nwqid;
    *wqid = res->wqid;
    *address = addr_decode(res->address, res->port);

    return res->errnum;
}

void remote_closefid(Worker *worker, Address *target, u32 fid) {
    Transaction *trans;

    trans = trans_new(conn_get_from_addr(worker, target), NULL, message_new());
    trans->out->tag = ALLOCTAG;
    trans->out->id = TECLOSEFID;
    set_teclosefid(trans->out, fid);

    send_request(trans);

    assert(trans->in != NULL && trans->in->id == RECLOSEFID);
}
