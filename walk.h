#ifndef _WALK_H_
#define _WALK_H_

#include "foo.h"

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
 *
 * When an entry is found to be wrong (by a rejected remote request) or when
 * any lease update happens, the entire cache is flushed.
 */

struct walk {
    char *pathname;
    char *user;
    struct qid *qid;
    Address *addr;
    int inflight;
};

extern Lru *walk_cache;

void walk_init(void);

/* note: does not lock the result */
Walk *walk_lookup(char *pathname, char *user);
void walk_release(Walk *walk);
void walk_add(Walk *walk);
void walk_add_new(char *pathname, char *user, struct qid *qid, Address *addr);
void walk_flush(void);

/*****************************************************************************/

enum walk_request_type {
    WALK_CLIENT,
    WALK_ENVOY,
};

enum walk_response_type {
    WALK_SUCCESS,
    WALK_ERROR,
    WALK_RACE,
};

struct walk_response {
    enum walk_response_type type;
    u32 errnum;

    List *names;
    List *walks;

    /* returned from a local walk when the final target is found */
    Claim *claim;
};

struct walk_response *common_twalk(Worker *worker, Transaction *trans,
        enum walk_request_type type, u32 newfid, List *names,
        char *pathname, char *user);

#endif
