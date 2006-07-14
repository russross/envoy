#ifndef _REMOTE_H_
#define _REMOTE_H_

#include "types.h"
#include "9p.h"
#include "list.h"
#include "worker.h"

/* stubs for remote envoy calls */

u16 remote_walk(Worker *worker, Address *target,
        u32 fid, u32 newfid, u16 nwname, char **wname,
        char *user, char *pathname,
        u16 *nwqid, struct qid **wqid, Address **address);
void remote_closefid(Worker *worker, Address *target, u32 fid);
/* note: pathname is also used to fill in the name field of the result */
struct p9stat *remote_stat(Worker *worker, Address *target, char *pathname);
void remote_rename(Worker *worker, Address *target,
        char *user, char *oldpath, char *newname);
/* tags a list of leases, returns a list of new oids in the same order */
List *remote_snapshot(Worker *worker, List *targets);
void remote_grant_exits(Worker *worker, List *targets, Address *addr);

#endif
