#ifndef _REMOTE_H_
#define _REMOTE_H_

#include "hi.h"

/* stubs for remote envoy calls */

void walkremote(Worker *worker, Address *target,
        u32 fid, u32 newfid, u16 nwname, char **wname,
        char *uid, char *pathname,
        u16 *nwqid, struct qid **wqid, Address **address);

#endif
