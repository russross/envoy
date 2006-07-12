#ifndef _WALK_H_
#define _WALK_H_

#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "transaction.h"
#include "worker.h"
#include "lru.h"
#include "claim.h"

/* Walk requests can involve multiple hosts.  Since the client is pretty dumb
 * about caching walk requests, we do some caching here.
 *
 * If addr == NULL then the object was owned locally.
 *
 * The walk cache is subject to a few rules to aim for reasonable consistency.
 * The end path of the walk is always queried afresh (and its cache entry
 * updated).
 *
 * In addition, for remote items, the last chunk is re-queried (meaning the
 * entire final sequence with matching addresses that includes the end path).
 * Whenever an entry is queried from a remote host, the entire chunk leading up
 * to it (as part of the same remote host) is queried.
 *
 * When an entry is found to be wrong (by a rejected remote request) or when
 * any lease update happens, the entire cache is flushed.
 */

struct walk {
    Worker *lock;

    char *pathname;
    List *users;
    struct qid *qid;
    Address *addr;
};

extern Lru *walk_cache;

void walk_init(void);

Walk *walk_lookup(Worker *worker, char *pathname, char *user);
/* modifies an existing one if it exists, else creates and caches a new entry */
Walk *walk_new(Worker *worker, char *pathname, char *user, struct qid *qid,
        Address *addr);
void walk_remove(char *pathname);
void walk_flush(void);
void walk_state_init(void);

/*****************************************************************************/

struct walk_env {
    /* the arguments */
    char *pathname;
    char *user;
    List *names;

    Address *nextaddr;

    u32 oldfid;
    u32 oldrfid;
    u32 newfid;
    u32 newrfid;
    Address *oldaddr;

    /* the result */
    enum {
        WALK_ZERO,
        WALK_COMPLETED_REMOTE,
        WALK_COMPLETED_LOCAL,
        WALK_PARTIAL,
        WALK_ERROR,
    } result;
    u32 errnum;
    Claim *claim;
    List *walks;
    List *qids;
    /* the address of the last successful qid found */
    Address *lastaddr;
};

void walk_common(Worker *worker, Transaction *trans, struct walk_env *env);

#endif
