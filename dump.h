#ifndef _STATE_H_
#define _STATE_H_

#include "types.h"
#include "lease.h"

void dump_dot(FILE *fp, Lease *lease);
void dump_dot_all(FILE *fp);
void dump_conn_all(FILE *fp);
void dump(char *name);

#endif
