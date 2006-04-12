#include "foo.h"

/* Do a local walk.  Walks until the end of the list of names, or until a lease
 * boundary is reached.
 *
 * The return struct contains a list of new walk objects including the starting
 * point and all reached names, as well as a list of remaining names not
 * reached.
 *
 * Does not query or modify fids in any way.
 *
 * Does call claim_release on the input claim as well as any it finds, except
 * the one for the last entry found, which is returned in the result.  The
 * lease is not claimed or released. */
static void common_twalk_local(Worker *worker, Transaction *trans,
        struct walk_response *res, char *pathname, char *user);
{
    struct p9stat *info;
    struct qid *qid;

    /* note: names can be null */
    assert(res->claim != NULL);
    assert(!emptystring(pathname));
    assert(!emptystring(user));

    res->type = WALK_RACE;

    do {
        /* look at the current file/directory */
        info = object_stat(worker, res->claim->oid, filename(res->pathname));
        if (info == NULL) {
            res->type = WALK_ERROR;
            res->errnum = ENOENT;
            return res;
        }

        /* record the qid */
        qid = GC_NEW(struct qid);
        assert(qid != NULL);
        *qid = makeqid(info->mode, info->mtime, info->size, res->claim->oid);
        res->qids = append_elt(res->qids, qid);

        /* is this the target? */
        if (null(res->names)) {
            res->type = WALK_SUCCESS;
            return res;
        }

        /* we need to keep walking, so make sure it's a directory */
        if (!(info->mode & DMDIR)) {
            res->type = WALK_ERROR;
            res->errnum = ENOTDIR;
            return res;
        }

        /* check that they have search (execute) permission for this dir */
        if (!has_permission(req->user, info, 0111)) {
            res->type = WALK_ERROR;
            res->errnum = EPERM;
            return res;
        }

        /* process the next name, including special cases */
        if (!strcmp(car(res->names), ".")) {
            /* stay in the current directory */
        } else if (!strcmp(car(res->names), "..")) {
            /* go back a directory */
            res->claim = claim_get_parent(worker, res->claim);
            res->pathname = dirname(res->pathname);
        } else if (strchr(car(res->names), '/') != NULL) {
            /* illegal name */
            res->type = WALK_ERROR;
            res->errnum = EINVAL;
            return res;
        } else {
            /* walk to the next child */
            res->claim = claim_get_child(worker, res->claim, car(res->names));
            res->pathname = concatname(res->pathname, car(res->names));
        }

        res->names = cdr(res->names);
    } while (res->claim != NULL);

    return res;
}

/* The generic walk handler.  This takes input from local attach and walk
 * commands (with local or remote starting points) and remote walk commands
 * (with local starting points).
 *
 * Walking proceeds in lease-sized phases.  A locally leased region is walked
 * locally, and a remotely-owned region is forwarded to the appropriate envoy.
 * Forwarded requests return an error, a complete result (with remote fid set
 * up appropriately) or an incomplete result with an address for the process to
 * continue.  The original fid is unaffected, except when the entire walk was
 * successful, target fid == new fid, and the same envoy hosts the old and new
 * versions.
 *
 * Each chunk is treated like a transaction, with locks being released after
 * the chunk is finished.
 */
struct walk_response *common_twalk(Worker *worker, Transaction *trans,
        enum walk_request_type type, u32 newfid, List *names,
        char *pathname, char *user)
{
    struct walk_response *res = GC_NEW(struct walk_response);
    assert(res != NULL);

    assert(!emptystring(pathname));
    assert(!emptystring(user));

    res->type = WALK_SUCCESS;
    res->names = names;
    res->qids = NULL;
    res->addr = NULL;

    /* make sure we aren't given too much work */
    if (length(names) > MAXWELEM) {
        res->type = WALK_ERROR;
        res->errnum = EMSGSIZE;
        return res;
    }

    do {
        /* note: this locks the lease and the claim */
        Claim *claim = claim_find(work, pathname);

        if (claim == NULL && addr == NULL) {
            /* addr == NULL means this was supposed to be local... */
            res->type = WALK_RACE;
            return res;
        } else if (claim == NULL) {
            /* this chunk needs to be forwarded */
            if (req->type == WALK_ENVOY_START ||
                    req->type == WALK_ENVOY_CONTINUE)
            {

            }
        } else {
            /* this chunk is local */
            Lease *lease = claim->lease;
            struct walk_local_res *local;

            local = common_twalk_local(worker, trans, claim, res->names,
                    pathname, user);

            res->names = local->names;
            res->qids = append_list(res->qids, local->qids);
            pathname = local->pathname;
            claim = local->claim;

            if (local->type == WALK_ERROR) {
                lease_finish_transaction(lease);
                res->type = WALK_ERROR;
                res->errnum = local->errnum;
                res->addr = NULL;
                return res;
            }
            assert(local->type == WALK_SUCCESS);

            if (null(res->names) && claim != NULL) {
                /* finished, so create the target fid */
                Fid *fid;
                if ((fid = fid_lookup(trans->conn, req->newfid)) != NULL) {
                    /* update the fid to the new target */
                    claim_release(fid->claim);
                    fid->claim = claim;
                } else {
                    /* create the target fid */
                    fid_insert_new(trans->conn, req->newfid, req->user, claim);
                }

                lease_finish_transaction(lease);
                break;
            } else {
                /* find the address for the next chunk */
                lease_finish_transaction(lease);
                assert(claim == NULL);

                /* figure out where to look next */
                /* TODO: does this check belong with the claim lookup above? */
                lease = lease_find_root(pathname);
                if (lease == NULL) {
                    addr = NULL;
                    res->type = WALK_RACE;
                    return res;
                }
                if (lease->type == LEASE_REMOTE) {
                    addr = lease->addr;
                } else {
                    addr = NULL;
                }
            }
        }

        start = 0;
    } while (!null(names));
}
