#ifndef _REMOTE_H_
#define _REMOTE_H_

#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "worker.h"

/* stubs for remote envoy calls */

u16 walkremote(Worker *worker, Address *target,
        u32 fid, u32 newfid, u16 nwname, char **wname,
        char *uid, char *pathname,
        u16 *nwqid, struct qid **wqid, Address **address);
/* note: pathname is also used to fill in the name field of the result */
struct p9stat *remote_stat(Worker *worker, Address *target,
        char *pathname);
void remote_rename(Worker *worker, Address *target,
        char *user, char *oldpath, char *newname);

#endif
