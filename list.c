#include <gc.h>
#include "list.h"

struct cons *append_elt(struct cons *list, void *elt) {
    struct cons *res = list;

    if (res == NULL)
        return cons(elt, NULL);

    while (!null(cdr(list)))
        list = cdr(list);
    setcdr(list, cons(elt, NULL));

    return res;
}
