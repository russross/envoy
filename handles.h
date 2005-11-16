#ifndef _HANDLES_H_
#define _HANDLES_H_

#include <sys/select.h>
#include "types.h"

/* handle sets */
struct handles {
    fd_set *set;
    int high;
};

void handles_add(Handles *set, int handle);
void handles_remove(Handles *set, int handle);
int handles_collect(Handles *set, fd_set *rset, int high);
int handles_member(Handles *set, fd_set *rset);
Handles *handles_new(void);

#endif
