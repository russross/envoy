#include "claim.h"

Hashtable *path_2_claim;
Lru *claim_lru;

int claim_request(Claim *claim) {
    if (claim->refcount < 0)
        return -1;
    claim->refcount++;
    return 0;
}

void claim_release(Claim *claim) {
    if (claim->refcount == -1) {
        claim->refcount++;
    } else {
        assert(claim->refcount > 0);
        claim->refcount--;
    }

    /* should we close down this claim? */
    if (!lease_is_exit_point_parent(claim->pathname)) {
        Claim *elt = claim;

        /* delete up the tree as far as we can */
        while (elt->refcount == 0 && null(claim->children) && elt->parent != NULL) {
            List *list = claim->parent->children;
            assert(!null(list));

            /* remove this claim from the parent's children list */
            if (car(list) == elt) {
                claim->parent->children = cdr(list);
            } else {
                assert(!null(cdr(list)));
                while (cadr(list) != elt) {
                    list = cdr(list);
                    assert(!null(cdr(list)));
                }
                setcdr(list, cddr(list));
            }
        }
    }
}
