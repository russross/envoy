#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "transaction.h"
#include "fid.h"
#include "util.h"
#include "state.h"
#include "object.h"
#include "envoy.h"
#include "remote.h"
#include "worker.h"
#include "lru.h"
#include "oid.h"
#include "claim.h"
#include "lease.h"
#include "walk.h"

Lru *walk_cache;

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
#define failwith(NUM) do { \
    res->errnum = NUM; \
    goto error; \
} while (0);

char *walk_pathname(char *pathname, char *name) {
    if (name == NULL || !strcmp(name, "."))
        return pathname;
    if (!strcmp(name, ".."))
        return dirname(pathname);
    return concatname(pathname, name);
}

static void common_twalk_local(Worker *worker, Transaction *trans,
        struct walk_response *res, char *pathname, char *user)
{
    struct p9stat *info;
    struct qid *qid;
    Walk *walk;
    char *next;
    Lease *lease;

    assert(res->claim != NULL);
    assert(!null(res->names));
    assert(!emptystring(pathname));
    assert(!emptystring(user));

    do {
        /* look at the current file/directory */
        info = object_stat(worker, res->claim->oid, filename(pathname));
        if (info == NULL)
            failwith(ENOENT);

        /* make the qid */
        qid = GC_NEW(struct qid);
        assert(qid != NULL);
        *qid = makeqid(info->mode, info->mtime, info->length, res->claim->oid);

        /* record the walk and step down the input list */
        walk = walk_new(pathname, user, qid, NULL);
        res->walks = cons(walk, res->walks);
        next = car(res->names);
        res->names = cdr(res->names);

        /* is this the target? */
        if (next == NULL) {
            /* the return includes the (locked) claim */
            res->type = WALK_SUCCESS;
            return;
        }

        /* we need to keep walking, so make sure it's a directory */
        if (!(info->mode & DMDIR))
            failwith(ENOTDIR);

        /* check that they have search (execute) permission for this dir */
        if (!has_permission(user, info, 0111))
            failwith(EPERM);

        /* process the next name, including special cases */
        if (!strcmp(next, ".")) {
            /* stay in the current directory */
        } else if (!strcmp(next, "..")) {
            /* go back a directory */
            res->claim = claim_get_parent(worker, res->claim);
            pathname = dirname(pathname);
        } else {
            /* walk to the next child */
            res->claim = claim_get_child(worker, res->claim, next);
            pathname = concatname(pathname, next);
        }
    } while (res->claim != NULL);

    /* prime the cache with the address for continuing */
    lease = lease_find_remote(pathname);
    assert(lease != NULL);

    walk_prime(pathname, user, lease->addr);

    res->type = WALK_SUCCESS;
    return;

    error:
    res->type = WALK_ERROR;
    if (res->claim != NULL) {
        claim_release(res->claim);
        res->claim = NULL;
    }
    return;
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
 *
 * A special NULL name must be present at the end of the names list.  This
 * simplifies bookkeeping: the first walk returned is for the given pathname,
 * and each successive walk returned is for the pathname plus a sequence of
 * names.  The length of the initial names list and the return walk list will
 * match if the walk is successful.
 */
/* TODO: lease locking, walk locking */
struct walk_response *common_twalk(Worker *worker, Transaction *trans,
        int isclient, u32 newfid, List *names, char *pathname, char *user)
{
    struct walk_response *res = GC_NEW(struct walk_response);
    assert(res != NULL);

    assert(!null(names));
    assert(!emptystring(pathname));
    assert(!emptystring(user));

    res->type = WALK_SUCCESS;
    res->names = names;
    res->walks = NULL;
    res->claim = NULL;

    while (!null(res->names)) {
        Walk *walk;
        List *chunk = NULL;
        List *chunknames = res->names;
        Address *addr = NULL;

        /* gather a chunk of cache entries */
        while (!null(chunknames) &&
                (walk = walk_lookup(pathname, user)) != NULL &&
                walk->qid != NULL &&
                (null(chunk) || !addr_cmp(addr, walk->addr)))
        {
            addr = walk->addr;

            /* force a lookup on the last element if it's local */
            if (car(chunknames) == NULL && addr == NULL) {
                walk->qid = NULL;
            } else {
                chunk = cons(walk, chunk);
                pathname = walk_pathname(pathname, car(chunknames));
                chunknames = cdr(chunknames);
            }
        }

        /* prime the cache if the next step is local->local */
        if (!null(chunknames) && !null(chunk) && addr == NULL &&
                walk_lookup(pathname, user) == NULL &&
                lease_find_remote(pathname) == NULL)
        {
            walk_prime(pathname, user, NULL);
        }

        /* peek at the cache for the next entry */
        walk = walk_lookup(pathname, user);

        /* decide if we should ignore the cached chunk we found:
         *    a) it's remote and the request is not from a client
         *    b) if it was the final chunk (this implies remote b/c of above)
         *    c) if we don't know where to look after it */
        if (!null(chunk) && addr != NULL && !isclient) {
            break;
        } else if (!null(chunk) && (null(chunknames) || walk == NULL)) {
            /* clear the cached qids before we repeat the lookup, ending with
             * walk at the start of the chunk */
            while (!null(chunk)) {
                walk = car(chunk);
                chunk = cdr(chunk);
                walk->qid = NULL;
            }
        } else if (!null(chunk)) {
            /* successful chunk from the cache */
            res->names = chunknames;
            res->walks = append_list(chunk, res->walks);
            continue;
        }

        assert(walk != NULL);

        if (walk->addr == NULL) {
            /* local chunk */
            res->claim = claim_get_pathname(worker, pathname);
            common_twalk_local(worker, trans, res, pathname, user);
            if (res->type == WALK_ERROR)
                failwith(res->errnum);
            if (res->claim != NULL) {
                Fid *fid;

                /* this must have finished the walk */
                assert(null(res->names));

                /* create the target fid */
                if ((fid = fid_lookup(trans->conn, newfid)) != NULL) {
                    /* update the fid to the new target */
                    claim_release(fid->claim);
                    fid->claim = res->claim;
                } else {
                    /* create the target fid */
                    fid_insert_local(trans->conn, newfid, user, res->claim);
                }

                res->claim = NULL;
            }
        } else if (isclient) {
            /* remote chunk */
            int i;
            List *iternames = res->names;
            u16 errnum = 0;
            u16 nwqid = 0;
            struct qid *wqid = NULL;
            u16 nwname = length(iternames);
            char **wname = GC_MALLOC(sizeof(char *) * nwname);
            assert(wname != NULL);

            for (i = 0; !null(iternames); iternames = cdr(iternames), i++)
                wname[i] = car(iternames);

            errnum = walkremote(worker, walk->addr, NOFID, newfid,
                    nwname, wname, user, pathname, &nwqid, &wqid, &addr);

            /* make sure we didn't get back too many results */
            if (nwqid > nwname)
                failwith(EMSGSIZE);

            /* fill in the result walks */
            for (i = 0; i < nwqid; i++) {
                struct qid *qid = GC_NEW(struct qid);
                Walk *w;
                assert(qid != NULL);
                *qid = wqid[i];
                w = walk_new(pathname, user, qid, walk->addr);
                res->walks = cons(res->walks, w);
                pathname = walk_pathname(pathname, car(res->names));
                res->names = cdr(res->names);
            }

            if (errnum != 0)
                failwith(errnum);

            /* prep a cache entry with the next address if there is one */
            if (!null(res->names))
                walk_prime(pathname, user, addr);
        } else {
            break;
        }
    }

    res->type = WALK_SUCCESS;
    return res;

    error:
    res->type = WALK_ERROR;
    if (res->claim != NULL) {
        claim_release(res->claim);
        res->claim = NULL;
    }
    return res;
}

void walk_flush(void) {
    lru_clear(walk_cache);
}

Walk *walk_new(char *pathname, char *user, struct qid *qid, Address *addr) {
    /* update an existing entry if one exists */
    Walk *walk = lru_get(walk_cache, pathname);

    if (walk == NULL) {
        /* create a new entry */
        walk = GC_NEW(Walk);
        assert(walk != NULL);

        walk->pathname = pathname;
        walk->users = cons(user, NULL);
        walk->qid = qid;
        walk->addr = addr;
        walk->inflight = 0;
        lru_add(walk_cache, pathname, walk);
    } else {
        /* if we don't have a qid, we can't assume all users have passed a
         * permission check so we clear the list */
        if (walk->qid == NULL)
            walk->users = cons(user, NULL);
        else if (!containsinorder((Cmpfunc) strcmp, walk->users, user))
            walk->users = insertinorder((Cmpfunc) strcmp, walk->users, user);

        walk->qid = qid;
        walk->addr = addr;
    }

    return walk;
}

/* this is similar to walk_new, with two differences:
 *   1) if the entry is new, it is created with a blank qid field
 *   2) if the user is new to this entry, the qid is cleared
 */
void walk_prime(char *pathname, char *user, Address *addr) {
    Walk *walk = lru_get(walk_cache, pathname);

    if (walk == NULL) {
        /* create a new entry that knows the address */
        walk = GC_NEW(Walk);
        assert(walk != NULL);

        walk->pathname = pathname;
        walk->users = cons(user, NULL);
        walk->qid = NULL;
        walk->addr = addr;
        walk->inflight = 0;
        lru_add(walk_cache, pathname, walk);
    } else {
        /* if this user hasn't seen this entry, clear the qid to force a lookup
         * and permissions check */
        if (!containsinorder((Cmpfunc) strcmp, walk->users, user)) {
            walk->users = insertinorder((Cmpfunc) strcmp, walk->users, user);
            walk->qid = NULL;
        }

        walk->addr = addr;
    }
}

Walk *walk_lookup(char *pathname, char *user) {
    Walk *walk = lru_get(walk_cache, pathname);

    if (walk != NULL && containsinorder((Cmpfunc) strcmp, walk->users, user))
        return walk;

    return NULL;
}

void walk_release(Walk *walk) {
    walk->inflight--;
}

int walk_resurrect(Walk *walk) {
    return walk->inflight > 0;
}

void walk_state_init(void) {
    walk_cache = lru_new(
            WALK_CACHE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp,
            (int (*)(void *)) walk_resurrect,
            NULL);
}
