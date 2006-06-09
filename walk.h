#ifndef _WALK_H_
#define _WALK_H_

#include <stdlib.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "transaction.h"
#include "worker.h"
#include "lru.h"
#include "claim.h"

/* Walk requests can involve multiple hosts.  Since the client is pretty dumb
 * about caching walk requests, we do some caching here.  Walk entries that are
 * part of an active request have inflight > 0, others may be evicted from the
 * LRU cache at any time.
 *
 * Entries may exist with qid == NULL, in which case they are useful for
 * pointing out the appropriate remote envoy to contact.
 *
 * If addr == NULL then the object was owned locally.
 *
 * The walk cache is subject to a few rules to aim for reasonable consistency.
 * The end path of the walk is always queried for a fresh qid (and its cache
 * entry updated).
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
    char *pathname;
    List *users;
    struct qid *qid;
    Address *addr;
    int inflight;
};

extern Lru *walk_cache;

void walk_init(void);

/* note: does not lock the result */
Walk *walk_lookup(char *pathname, char *user);
void walk_release(Walk *walk);
/* modifies an existing one if it exists, else creates and caches a new entry */
Walk *walk_new(char *pathname, char *user, struct qid *qid, Address *addr);
/* create an entry (sans qid) if none exists, otherwise refresh the current
 * entry */
void walk_prime(char *pathname, char *user, Address *addr);
void walk_flush(void);
char *walk_pathname(char *pathname, char *name);

/*****************************************************************************/

enum walk_response_type {
    WALK_SUCCESS,
    WALK_ERROR,
    WALK_RACE,
};

struct walk_response {
    enum walk_response_type type;
    u32 errnum;

    /* names of remaining path elements */
    List *names;
    /* result list in reverse order */
    List *walks;

    /* returned from a local walk when the final target is found */
    Claim *claim;
};

struct walk_response *common_twalk(Worker *worker, Transaction *trans,
        int isclient, u32 newfid, List *names, char *pathname, char *user);

#endif
