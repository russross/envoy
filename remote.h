#ifndef _REMOTE_H_
#define _REMOTE_H_

#include "types.h"
#include "9p.h"
#include "list.h"
#include "worker.h"
#include "lease.h"

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
void remote_grant_exits(Worker *worker, List *targets, Address *addr,
        enum grant_type type);
void remote_migrate(Worker *worker, List *groups);
void remote_revoke(Worker *worker, Address *target, enum grant_type type,
        char *pathname, Address *newaddress,
        enum grant_type *restype, struct leaserecord **root,
        List **exits, List **fids);
void remote_grant(Worker *worker, Address *target, enum grant_type type,
        struct leaserecord *root, Address *oldaddr, List *exits, List *fids);
void remote_nominate(Worker *worker, Address *target,
        char *pathname, Address *newaddr);

#endif
