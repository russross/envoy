#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <sys/select.h>
#include "types.h"
#include "handles.h"

/*
 * Handle sets
 */

Handles *handles_new(void) {
    Handles *set = GC_NEW(Handles);
    assert(set != NULL);
    set->set = GC_NEW_ATOMIC(fd_set);
    assert(set != NULL);
    FD_ZERO(set->set);
    set->high = -1;

    return set;
}

void handles_add(Handles *set, int handle) {
    FD_SET(handle, set->set);
    set->high = handle > set->high ? handle : set->high;
}

void handles_remove(Handles *set, int handle) {
    FD_CLR(handle, set->set);
    if (handle >= set->high)
        while (set->high >= 0 && FD_ISSET(set->high, set->set))
            set->high--;
}

int handles_collect(Handles *set, fd_set *rset, int high) {
    int i;
    fd_mask *a = (fd_mask *) rset, *b = (fd_mask *) set->set;

    high = high > set->high ? high : set->high;

    for (i = 0; i * NFDBITS <= high; i++)
        a[i] |= b[i];

    return high;
}

int handles_member(Handles *set, fd_set *rset) {
    int i;

    for (i = 0; i <= set->high; i++)
        if (FD_ISSET(i, rset) && FD_ISSET(i, set->set))
            return i;

    return -1;
}
