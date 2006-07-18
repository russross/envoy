#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "transaction.h"
#include "util.h"
#include "remote.h"
#include "dispatch.h"
#include "worker.h"
#include "lease.h"

struct p9stat *remote_stat(Worker *worker, Address *target, char *pathname) {
    Transaction *trans;
    struct Restatremote *res;

    trans = trans_new(conn_get_envoy_out(worker, target), NULL, message_new());
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

    trans = trans_new(conn_get_envoy_out(worker, target), NULL, message_new());
    trans->out->tag = ALLOCTAG;
    trans->out->id = TEWALKREMOTE;
    set_tewalkremote(trans->out, fid, newfid, nwname, wname, user, pathname);

    send_request(trans);

    assert(trans->in != NULL);
    if (trans->in->id == REWALKREMOTE) {
        res = &trans->in->msg.rewalkremote;

        *nwqid = res->nwqid;
        *wqid = res->wqid;
        if (res->address == 0 && res->port == 0) {
            *address = NULL;
        } else {
            Address *addr = GC_NEW_ATOMIC(Address);
            assert(addr != NULL);
            addr->ip = res->address;
            addr->port = res->port;
            *address = addr;
        }
        return res->errnum;
    } else if (trans->in->id == RERROR) {
        *nwqid = 0;
        *wqid = NULL;
        *address = NULL;
        return trans->in->msg.rerror.errnum;
    }

    assert(trans->in->id == REWALKREMOTE || trans->in->id == RERROR);
    return ~(u16) 0;
}

void remote_closefid(Worker *worker, Address *target, u32 fid) {
    Transaction *trans;

    trans = trans_new(conn_get_envoy_out(worker, target), NULL, message_new());
    trans->out->tag = ALLOCTAG;
    trans->out->id = TECLOSEFID;
    set_teclosefid(trans->out, fid);

    send_request(trans);

    /* TODO: what if this fails?  migration during the middle of a walk, or
     * during a client shutdown */
    assert(trans->in != NULL && trans->in->id == RECLOSEFID);
}

List *remote_snapshot(Worker *worker, List *targets) {
    List *requests = NULL;
    List *results = NULL;
    Transaction *trans;

    /* build a (reversed) list of request transactions */
    for ( ; !null(targets); targets = cdr(targets)) {
        Lease *lease = car(targets);
        assert(lease->isexit);
        trans = trans_new(conn_get_envoy_out(worker, lease->addr),
                NULL, message_new());
        trans->out->tag = ALLOCTAG;
        trans->out->id = TESNAPSHOT;
        set_tesnapshot(trans->out, lease->pathname);
        requests = cons(trans, requests);
    }

    /* wait for all transactions to complete */
    send_requests(requests);

    /* build a (forward) list of new oids */
    for ( ; !null(requests); requests = cdr(requests)) {
        u64 *res = GC_NEW_ATOMIC(u64);
        assert(res != NULL);
        trans = car(requests);
        assert(trans->in->id == RESNAPSHOT);
        *res = trans->in->msg.resnapshot.newoid;
        results = cons(res, results);
    }

    return results;
}

/* targets is a list of exit leases, addr is the grant target */
void remote_grant_exits(Worker *worker, List *targets, Address *addr,
        enum grant_type type)
{
    List *requests = NULL;
    Transaction *trans;

    /* build a list of request transactions */
    for ( ; !null(targets); targets = cdr(targets)) {
        Lease *lease = car(targets);
        struct leaserecord *rec = GC_NEW(struct leaserecord);
        assert(rec != NULL);
        assert(lease->isexit);

        rec->pathname = lease->pathname;
        rec->readonly = lease->readonly ? 1 : 0;
        rec->oid = NOOID;
        rec->address = addr->ip;
        rec->port = addr->port;

        trans = trans_new(conn_get_envoy_out(worker, lease->addr),
                NULL, message_new());
        trans->out->tag = ALLOCTAG;
        trans->out->id = TEGRANT;
        set_tegrant(trans->out, type, rec, 0, NULL, 0, NULL);
        requests = cons(trans, requests);
    }

    /* wait for all transactions to complete */
    send_requests(requests);

    /* make sure they all succeeded */
    for ( ; !null(requests); requests = cdr(requests)) {
        trans = car(requests);
        assert(trans->in->id == REGRANT);
    }
}
