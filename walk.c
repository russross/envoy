#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "transaction.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "object.h"
#include "envoy.h"
#include "remote.h"
#include "worker.h"
#include "lru.h"
#include "claim.h"
#include "lease.h"
#include "walk.h"

Lru *walk_cache;

#define failwith(NUM) do { \
    env->errnum = NUM; \
    goto error; \
} while (0);

#define require_info(_ptr) do { \
    if ((_ptr)->info == NULL) { \
        (_ptr)->info = \
            object_stat(worker, (_ptr)->oid, filename((_ptr)->pathname)); \
    } \
} while (0)

static char *walk_pathname(char *pathname, char *name) {
    if (name == NULL || !strcmp(name, "."))
        return pathname;
    if (!strcmp(name, ".."))
        return dirname(pathname);
    return concatname(pathname, name);
}

static void walk_build_qids(struct walk_env *env) {
    env->qids = NULL;
    while (!null(env->walks)) {
        Walk *walk = car(env->walks);
        struct qid *qid = walk->qid;
        if (null(env->qids) &&
                (env->result == WALK_COMPLETED_LOCAL ||
                 env->result == WALK_COMPLETED_REMOTE))
        {
            walk_remove(walk->pathname);
        } else {
            lru_add(walk_cache, walk->pathname, walk);
        }
        env->qids = cons(qid, env->qids);
        env->walks = cdr(env->walks);
    }
}

/* returns true if progress was made, false otherwise.
 * env->nextaddr will be valid in either case */
static int walk_from_cache(Worker *worker, Transaction *trans,
        struct walk_env *env)
{
    char *pathname = env->pathname;
    List *names = env->names;
    List *walks = NULL;
    Walk *walk;

    /* gather a chunk of cache entries */
    while (!null(names) &&
            (walk = walk_lookup(worker, pathname, env->user)) != NULL &&
            (!addr_cmp(walk->addr, env->nextaddr)))
    {
        /* force a lookup on the last element if it's local */
        if (car(names) == NULL && walk->addr == NULL) {
            walk_remove(walk->pathname);
            break;
        } else {
            walks = cons(walk, walks);
            if (car(names) != NULL)
                pathname = walk_pathname(pathname, car(names));
            names = cdr(names);
        }
    }

    /* quit if we didn't find anything */
    if (null(walks))
        return 0;

    /* if the chunk was local, return a successful chunk */
    if (env->nextaddr == NULL) {
        Lease *lease = lease_get_remote(pathname);

        /* figure out where to look next */
        env->lastaddr = env->nextaddr;
        if (lease == NULL)
            env->nextaddr = NULL;
        else
            env->nextaddr = lease->addr;

        env->names = names;
        env->walks = append_list(walks, env->walks);
        env->pathname = pathname;

        return 1;
    }

    /* chunk was remote */

    /* peek at the next cache entry */
    walk = walk_lookup(worker, pathname, env->user);

    /* decide if we should ignore the cached chunk we found:
     *    a) the request is not from a client
     *    b) it was the final chunk
     *    c) we don't know where to look after it
     *    d) the next step is the same remote address as this chunk */
    if (env->newrfid == NOFID)
        return 0;

    if (null(names) || walk == NULL || !addr_cmp(env->nextaddr, walk->addr)) {
        /* clear the cache for this chunk before repeating the lookup */
        for ( ; !null(walks); walks = cdr(walks)) {
            walk = car(walks);
            walk_remove(walk->pathname);
        }

        return 0;
    }

    /* successful chunk from the cache */
    env->names = names;
    env->walks = append_list(walks, env->walks);
    env->pathname = pathname;
    env->lastaddr = env->nextaddr;
    env->nextaddr = walk->addr;

    return 1;
}

/* Do a local walk.  Walks until the end of the list of names, or until a lease
 * boundary is reached.
 *
 * The return struct contains a list of new walk objects including the starting
 * point and all reached names, as well as a list of remaining names not
 * reached.
 *
 * Does not query or modify fids in any way. */
static void walk_local(Worker *worker, Transaction *trans,
        struct walk_env *env)
{
    struct p9stat *info;
    struct qid *qid;
    Walk *walk;
    char *name;
    Lease *lease;
    Claim *change = NULL;

    assert(!null(env->names));
    assert(!emptystring(env->pathname));
    assert(!emptystring(env->user));
    assert(env->nextaddr == NULL);

    if (env->claim == NULL)
        failwith(ENOENT);

    for (;;) {
        /* look at the current file/directory */
        require_info(env->claim);
        info = env->claim->info;

        /* make the qid */
        qid = GC_NEW(struct qid);
        assert(qid != NULL);
        *qid = makeqid(info->mode, info->mtime, info->length, env->claim->oid);

        /* record the walk and step down the input list */
        walk = walk_new(worker, env->pathname, env->user, qid, NULL);
        env->walks = cons(walk, env->walks);
        name = car(env->names);
        env->names = cdr(env->names);

        /* is this the target? */
        if (name == NULL) {
            /* the return includes the (locked) claim */
            env->result = WALK_COMPLETED_LOCAL;
            env->lastaddr = NULL;

            /* see if this walk triggers a territory migration */
            change = claim_update_territory_move(env->claim, trans->conn);
            if (transfer_territory(worker, trans->conn, change)) {
                worker_retry(worker);
                assert(0);
            }

            return;
        }

        /* we need to keep walking, so make sure it's a directory */
        if (!(info->mode & DMDIR))
            failwith(ENOTDIR);

        /* check that they have search (execute) permission for this dir */
        if (!has_permission(env->user, info, 0111))
            failwith(EPERM);

        /* process the next name, including special cases */
        if (!strcmp(name, ".")) {
            /* stay in the current directory */
        } else if (!strcmp(name, "..")) {
            /* go back a directory */
            char *oldpath = env->pathname;

            /* are we in a deleted directory? */
            if (env->claim->deleted)
                failwith(EEXIST);

            env->pathname = dirname(env->pathname);
            env->claim = claim_get_parent(worker, env->claim);

            /* did we step back out of a lease? */
            if (env->claim == NULL) {
                lease = lease_find_root(oldpath);
                assert(lease != NULL);

                env->nextaddr = lease->addr;
                env->result = WALK_PARTIAL;
                return;
            }
        } else {
            /* walk to the next child */
            Claim *child = claim_get_child(worker, env->claim, name);
            enum path_type parenttype = get_admin_path_type(env->pathname);

            if (child != NULL) {
                require_info(child);
                info = child->info;

                /* special case--for attach we handle symlinks to snapshots */
                if (env->isattach && (info->mode & DMSYMLINK) &&
                        get_admin_path_type(env->pathname) == PATH_ADMIN &&
                        !emptystring(info->extension) &&
                        ispositiveint(info->extension))
                {
                    name = info->extension;
                    child = claim_get_child(worker, env->claim, name);
                }
            }

            if (child == NULL) {
                /* see if this walk triggers a territory migration */
                change = claim_update_territory_move(env->claim, trans->conn);
                if (transfer_territory(worker, trans->conn, change)) {
                    worker_retry(worker);
                    assert(0);
                }
            }

            env->pathname = concatname(env->pathname, name);
            env->claim = child;

            /* did we step past the lease? */
            if (env->claim == NULL) {
                lease = lease_get_remote(env->pathname);

                /* not a lease exit, so the file must not exist */
                if (lease == NULL)
                    failwith(ENOENT);

                env->nextaddr = lease->addr;
                env->result = WALK_PARTIAL;
                return;
            }

            /* check if this is a remote attach and should trigger a grant:
             * 1) it's an attach request
             * 2) it's remote
             * 3) we are the root (implied by the others)
             * 4) we just crossed into the image
             * 5) the image has no fids or descendent leases */
            if (env->isattach && env->newrfid == NOFID &&
                    parenttype == PATH_ADMIN &&
                    get_admin_path_type(env->pathname) != PATH_ADMIN)
            {
                /* if there are no active fids, grant a lease to the remote
                 * envoy and start the request over */
                if (null(env->claim->children) && null(env->claim->fids)) {
                    Lease *lease = env->claim->lease;

                    if (DEBUG_VERBOSE) {
                        printf("lease split for attach: %s to %s\n",
                                env->pathname,
                                addr_to_string(trans->conn->addr));
                    }

                    worker_cleanup(worker);
                    lease_split(worker, lease, env->pathname,
                            trans->conn->addr);
                    worker_retry(worker);
                    assert(0);
                }
            }
        }
    }

    error:
    env->result = WALK_ERROR;
    env->claim = NULL;
}

static void walk_remote(Worker *worker, Transaction *trans,
        struct walk_env *env)
{
    Address *next;
    int i;
    u16 errnum;
    u16 nwqid;
    struct qid *wqid;
    List *names = env->names;
    u16 nwname = length(names);
    char **wname = GC_MALLOC(sizeof(char *) * nwname);
    assert(wname != NULL);
    u32 fid;

    assert(env->nextaddr != NULL);

    /* prepare for a remote walk message */
    for (i = 0; !null(names); names = cdr(names), i++)
        wname[i] = car(names);

    if (!addr_cmp(env->oldaddr, env->nextaddr))
        fid = env->oldrfid;
    else
        fid = NOFID;

    errnum = remote_walk(worker, env->nextaddr, fid, env->newrfid,
            nwname, wname, env->user, env->pathname, &nwqid, &wqid, &next);

    /* did we get a race condition? */
    if (fid != NOFID && errnum == EBADF) {
        if (DEBUG_VERBOSE)
            printf("walk: race condition detected\n");
        walk_flush();
        worker_retry(worker);
        assert(0);
    }

    /* make sure we didn't get back too many results */
    if (nwqid > nwname)
        failwith(EMSGSIZE);

    /* copy the result */
    for (i = 0; i < nwqid; i++) {
        struct qid *qid = GC_NEW(struct qid);
        assert(qid != NULL);
        *qid = wqid[i];
        env->walks = cons(
                walk_new(worker, env->pathname, env->user, qid, env->nextaddr),
                env->walks);
        env->pathname = walk_pathname(env->pathname, car(env->names));
        env->names = cdr(env->names);
    }

    if (errnum != 0) {
        if (nwqid > 0)
            env->lastaddr = env->nextaddr;
        failwith(errnum);
    }

    if (null(env->names)) {
        env->result = WALK_COMPLETED_REMOTE;
        env->lastaddr = env->nextaddr;
        env->nextaddr = NULL;
    } else {
        env->result = WALK_PARTIAL;
        env->lastaddr = env->nextaddr;
        if (!addr_cmp(next, my_address))
            env->nextaddr = NULL;
        else
            env->nextaddr = next;
    }

    return;

    error:
    env->result = WALK_ERROR;
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
 * A special NULL name must be present at the end of the names list.  This
 * simplifies bookkeeping: the first walk returned is for the given pathname,
 * and each successive walk returned is for the pathname plus a sequence of
 * names.  The length of the initial names list and the return walk list will
 * match if the walk is successful.
 */
void walk_common(Worker *worker, Transaction *trans, struct walk_env *env) {
    assert(!null(env->names));
    assert(!emptystring(env->pathname));
    assert(!emptystring(env->user));

    env->walks = NULL;
    env->result = WALK_ZERO;

    while (!null(env->names)) {
        if (walk_from_cache(worker, trans, env)) {
            /* cache chunk */
            continue;
        } else if (env->nextaddr == NULL) {
            /* local chunk */
            env->claim = claim_find(worker, env->pathname);
            walk_local(worker, trans, env);

            /* did we reach the end? */
            if (env->result == WALK_COMPLETED_LOCAL) {
                assert(env->claim != NULL);
                assert(null(env->names));

                /* close the remote fid */
                if (env->oldrfid != NOFID && env->oldrfid == env->newrfid) {
                    /* the walk has moved from a remote host to here */
                    assert(env->oldaddr != NULL);
                    if (remote_closefid(worker, env->oldaddr, env->oldrfid) < 0)
                    {
                        /* race condition--restart the walk */
                        walk_flush();
                        worker_retry(worker);
                        assert(0);
                    }
                    fid_release_remote(env->oldrfid);
                }

                /* create/update the fid */
                if (env->oldfid != NOFID && env->oldfid == env->newfid) {
                    Fid *fid = fid_lookup(trans->conn, env->newfid);
                    assert(fid != NULL);
                    fid_update_local(fid, env->claim);
                } else {
                    assert(fid_lookup(trans->conn, env->newfid) == NULL);
                    fid_insert_local(trans->conn, env->newfid, env->user,
                            env->claim);
                }

                env->claim = NULL;

                if (env->newrfid != NOFID && env->newrfid != env->oldrfid)
                    fid_release_remote(env->newrfid);

                goto finish;
            } else if (env->result == WALK_ERROR) {
                /* permissions, not found, etc. */
                assert(env->claim == NULL);

                /* we didn't use the remote fid, so cancel the reservation */
                if (env->newrfid != NOFID && env->newrfid != env->oldrfid)
                    fid_release_remote(env->newrfid);

                failwith(env->errnum);
            }
        } else if (env->newrfid == NOFID) {
            /* it's remote, but our client is also remote */
            env->result = WALK_PARTIAL;
            goto finish;
        } else {
            /* remote chunk */
            walk_remote(worker, trans, env);

            if (env->result == WALK_COMPLETED_REMOTE) {
                assert(null(env->names));

                /* was this a move from one remote host to another? */
                if (env->oldrfid == env->newrfid &&
                        addr_cmp(env->oldaddr, env->lastaddr))
                {
                    if (remote_closefid(worker, env->oldaddr, env->oldrfid) < 0)
                    {
                        /* race condition--restart the walk */
                        walk_flush();
                        worker_retry(worker);
                        assert(0);
                    }
                }

                /* create/update the fid */
                if (env->oldfid != NOFID && env->oldfid == env->newfid) {
                    Fid *fid = fid_lookup(trans->conn, env->newfid);
                    assert(fid != NULL);
                    fid_update_remote(fid, env->pathname, env->lastaddr,
                            env->newrfid);
                } else {
                    assert(fid_lookup(trans->conn, env->newfid) == NULL);
                    fid_insert_remote(trans->conn, env->newfid, env->pathname,
                            env->user, env->lastaddr, env->newrfid);
                }

                goto finish;
            } else if (env->result == WALK_ERROR) {
                /* cancel the remote fid reservation */
                if (env->newrfid != NOFID && env->newrfid != env->oldrfid)
                    fid_release_remote(env->newrfid);

                failwith(env->errnum);
            }
            assert(!null(env->names));
        }
    }

    error:
    env->result = WALK_ERROR;

    finish:
    env->claim = NULL;
    walk_build_qids(env);
}

void walk_flush(void) {
    lru_clear(walk_cache);
}

Walk *walk_new(Worker *worker, char *pathname, char *user, struct qid *qid,
        Address *addr)
{
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
        /* mark the new walk as being in use */
        reserve(worker, LOCK_WALK, walk);
    } else {
        reserve(worker, LOCK_WALK, walk);

        if (findinorder((Cmpfunc) strcmp, walk->users, user) == NULL)
            walk->users = insertinorder((Cmpfunc) strcmp, walk->users, user);

        walk->qid = qid;
        walk->addr = addr;
    }

    return walk;
}

void walk_remove(char *pathname) {
    lru_remove(walk_cache, pathname);
}

Walk *walk_lookup(Worker *worker, char *pathname, char *user) {
    Walk *walk = lru_get(walk_cache, pathname);

    if (walk != NULL && findinorder((Cmpfunc) strcmp, walk->users, user)) {
        reserve(worker, LOCK_WALK, walk);
        return walk;
    }

    return NULL;
}

int walk_resurrect(Walk *walk) {
    return walk->lock != NULL;
}

void walk_state_init(void) {
    walk_cache = lru_new(
            WALK_CACHE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp,
            (int (*)(void *)) walk_resurrect,
            NULL);
}
