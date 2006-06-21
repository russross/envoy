#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
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
    env->errnum = NUM; \
    goto error; \
} while (0);

char *walk_pathname(char *pathname, char *name) {
    if (name == NULL || !strcmp(name, "."))
        return pathname;
    if (!strcmp(name, ".."))
        return dirname(pathname);
    return concatname(pathname, name);
}

/* returns true if progress was made, false otherwise.
 * the next walk will be in the cache in either case */
static int walk_from_cache(Worker *worker, Transaction *trans,
        struct walk_env *env)
{
    char *pathname = env->pathname;
    List *names = env->names;
    List *walks = NULL;
    Address *addr = NULL;
    Walk *walk;

    /* gather a chunk of cache entries */
    while (!null(names) &&
            (walk = walk_lookup(pathname, env->user)) != NULL &&
            walk->qid != NULL &&
            (null(walks) || !addr_cmp(addr, walk->addr)))
    {
        addr = walk->addr;

        /* force a lookup on the last element if it's local [*] */
        if (car(names) == NULL && addr == NULL) {
            walk->qid = NULL;
            break;
        } else {
            walks = cons(walk, walks);
            if (car(names) != NULL)
                pathname = walk_pathname(pathname, car(names));
            names = cdr(names);
        }
    }

    /* prime the cache if coming from a local hit */
    if (!null(names) && !null(walks) && addr == NULL &&
            walk_lookup(pathname, env->user) == NULL &&
            lease_get_remote(pathname) == NULL)
    {
        Lease *lease = lease_get_remote(pathname);
        if (lease == NULL)
            walk_prime(pathname, env->user, NULL);
        else
            walk_prime(pathname, env->user, lease->addr);
    }

    /* peek at the cache for the next entry */
    walk = walk_lookup(pathname, env->user);

    /* did we find nothing? */
    if (null(walks))
        return 0;

    /* decide if we should ignore the cached chunk we found:
     *    a) it's remote and the request is not from a client
     *    b) if it was the final chunk (this implies remote b/c of [*] above)
     *    c) if we don't know where to look after it
     *    d) the next step is the same remote address as this chunk */
    if (addr != NULL && env->newrfid == NOFID) {
        return 0;
    } else if (null(names) || walk == NULL) {
        /* clear the cached qids before we repeat the lookup, ending with
         * walk at the start of the chunk */
        for ( ; !null(walks); walks = cdr(walks)) {
            walk = car(walks);
            walk->qid = NULL;
        }
        return 0;
    } else if (!addr_cmp(addr, walk->addr)) {
        return 0;
    }

    /* successful chunk from the cache */
    env->names = names;
    env->walks = append_list(walks, env->walks);
    env->pathname = pathname;
    return 1;
}

static void walk_local(Worker *worker, Transaction *trans,
        struct walk_env *env)
{
    struct p9stat *info;
    struct qid *qid;
    Walk *walk;
    char *name;
    Lease *lease;

    assert(!null(env->names));
    assert(!emptystring(env->pathname));
    assert(!emptystring(env->user));

    if (env->claim == NULL)
        failwith(ENOENT);

    for (;;) {
        /* look at the current file/directory */
        if (env->claim->info == NULL) {
            env->claim->info =
                object_stat(worker, env->claim->oid, filename(env->pathname));
        }
        info = env->claim->info;

        /* make the qid */
        qid = GC_NEW(struct qid);
        assert(qid != NULL);
        *qid = makeqid(info->mode, info->mtime, info->length, env->claim->oid);

        /* record the walk and step down the input list */
        walk = walk_new(env->pathname, env->user, qid, NULL);
        env->walks = cons(walk, env->walks);
        name = car(env->names);
        env->names = cdr(env->names);

        /* is this the target? */
        if (name == NULL) {
            /* the return includes the (locked) claim */
            env->result = WALK_COMPLETED_LOCAL;
            env->addr = NULL;
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
            env->pathname = dirname(env->pathname);
            env->claim = claim_get_parent(worker, env->claim);

            /* did we step back out of a lease? */
            if (env->claim == NULL) {
                lease = lease_find_root(oldpath);
                assert(lease != NULL);

                walk_prime(env->pathname, env->user, lease->addr);
                env->result = WALK_PARTIAL;
                return;
            }
        } else {
            /* walk to the next child */
            env->pathname = concatname(env->pathname, name);
            env->claim = claim_get_child(worker, env->claim, name);

            /* did we step past the lease? */
            if (env->claim == NULL) {
                lease = lease_get_remote(env->pathname);

                /* not a lease exit, so the file must not exist */
                if (lease == NULL)
                    failwith(ENOENT);

                walk_prime(env->pathname, env->user, lease->addr);
                env->result = WALK_PARTIAL;
                return;
            }
        }
    }

    error:
    env->result = WALK_ERROR;
    if (env->claim != NULL) {
        claim_release(env->claim);
        env->claim = NULL;
    }
}

static void walk_remote(Worker *worker, Transaction *trans,
        struct walk_env *env)
{
    Address *addr = walk_lookup(env->pathname, env->user)->addr;
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

    /* prepare for a remote walk message */
    for (i = 0; !null(names); names = cdr(names), i++)
        wname[i] = car(names);

    if (!addr_cmp(env->oldaddr, addr))
        fid = env->oldrfid;
    else
        fid = NOFID;

    errnum = remote_walk(worker, addr, fid, env->newrfid, nwname, wname,
            env->user, env->pathname, &nwqid, &wqid, &next);

    /* make sure we didn't get back too many results */
    if (nwqid > nwname)
        failwith(EMSGSIZE);

    /* copy the result */
    for (i = 0; i < nwqid; i++) {
        struct qid *qid = GC_NEW(struct qid);
        assert(qid != NULL);
        *qid = wqid[i];
        env->walks = cons(walk_new(env->pathname, env->user, qid, addr),
                env->walks);
        env->pathname = walk_pathname(env->pathname, car(env->names));
        env->names = cdr(env->names);
    }

    if (errnum != 0)
        failwith(errnum);

    if (null(env->names)) {
        env->result = WALK_COMPLETED_REMOTE;
        env->addr = addr;
    } else {
        env->result = WALK_PARTIAL;
        env->addr = next;
    }

    return;

    error:
    env->result = WALK_ERROR;
    env->addr = NULL;
    return;
}

static void walk_build_qids(struct walk_env *env) {
    env->qids = NULL;
    while (!null(env->walks)) {
        Walk *walk = car(env->walks);
        struct qid *qid = walk->qid;
        if (null(env->qids) && env->result != WALK_ERROR)
            walk->qid = NULL;
        env->qids = cons(qid, env->qids);
        env->walks = cdr(env->walks);
    }
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
void common_walk(Worker *worker, Transaction *trans, struct walk_env *env) {
    assert(!null(env->names));
    assert(!emptystring(env->pathname));
    assert(!emptystring(env->user));

    env->walks = NULL;

    while (!null(env->names)) {
        Walk *walk;

        /* go as far as we can from the cache */
        while (walk_from_cache(worker, trans, env))
            continue;

        walk = walk_lookup(env->pathname, env->user);

        /* are we at a dead end? */
        if (walk == NULL) {
            fprintf(stderr, "walk error: don't know where to look\n");
            walk_flush();
            worker_retry(worker);
            assert(0);
        }

        if (walk->addr == NULL) {
            /* local chunk */
            env->claim = claim_find(worker, env->pathname);
            walk_local(worker, trans, env);

            /* did we reach the end? */
            if (env->result == WALK_COMPLETED_LOCAL) {
                assert(env->claim != NULL);
                assert(null(env->names));

                if (env->oldfid != NOFID && env->oldfid == env->newfid) {
                    Fid *fid = fid_lookup(trans->conn, env->newfid);
                    assert(fid != NULL);
                    fid_update_local(fid, env->claim);
                } else {
                    assert(fid_lookup(trans->conn, env->newfid) == NULL);
                    fid_insert_local(trans->conn, env->newfid, env->user,
                            env->claim);
                }

                /* the fid updates hold onto the claim locks */
                env->claim = NULL;

                if (env->oldrfid != NOFID) {
                    /* the walk has moved from a remote host to here */
                    assert(env->oldaddr != NULL);
                    remote_closefid(worker, env->oldaddr, env->oldrfid);
                    fid_release_remote(env->oldrfid);
                }
                if (env->newrfid != NOFID && env->newrfid != env->oldrfid)
                    fid_release_remote(env->newrfid);

                walk_build_qids(env);
                return;
            } else if (env->result == WALK_ERROR) {
                /* permissions, not found, etc. */
                assert(env->claim == NULL);
                if (env->newrfid != NOFID && env->newrfid != env->oldrfid)
                    fid_release_remote(env->newrfid);
                failwith(env->errnum);
            }
        } else if (env->newrfid == NOFID) {
            /* it's remote, but our client is also remote */
            env->result = WALK_PARTIAL;
            env->addr = walk->addr;
            return;
        } else {
            /* remote chunk */
            walk_remote(worker, trans, env);

            if (env->result == WALK_COMPLETED_REMOTE) {
                assert(null(env->names));

                if (env->oldfid != NOFID && env->oldfid == env->newfid) {
                    Fid *fid = fid_lookup(trans->conn, env->newfid);
                    assert(fid != NULL);
                    fid_update_remote(fid, env->pathname, env->addr,
                            env->newrfid);
                } else {
                    assert(fid_lookup(trans->conn, env->newfid) == NULL);
                    fid_insert_remote(trans->conn, env->newfid, env->pathname,
                            env->user, env->addr, env->newrfid);
                }

                /* was this a move from one remote host to another? */
                if (env->oldrfid == env->newrfid &&
                        addr_cmp(env->oldaddr, env->addr))
                {
                    remote_closefid(worker, env->oldaddr, env->oldrfid);
                }

                walk_build_qids(env);
                return;
            } else if (env->result == WALK_ERROR) {
                if (env->newrfid != NOFID && env->newrfid != env->oldrfid)
                    fid_release_remote(env->newrfid);
                failwith(env->errnum);
            }
            assert(!null(env->names));

            /* prep a cache entry with the next address */
            walk_prime(env->pathname, env->user, env->addr);
        }
    }

    error:
    env->result = WALK_ERROR;
    if (env->claim != NULL) {
        claim_release(env->claim);
        env->claim = NULL;
    }
    walk_build_qids(env);
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
        else if (findinorder((Cmpfunc) strcmp, walk->users, user) == NULL)
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
        if (findinorder((Cmpfunc) strcmp, walk->users, user) == NULL) {
            walk->users = insertinorder((Cmpfunc) strcmp, walk->users, user);
            walk->qid = NULL;
        }

        walk->addr = addr;
    }
}

Walk *walk_lookup(char *pathname, char *user) {
    Walk *walk = lru_get(walk_cache, pathname);

    if (walk != NULL && findinorder((Cmpfunc) strcmp, walk->users, user))
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
