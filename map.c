#include <gc/gc.h>
#include <string.h>
#include <assert.h>
#include "list.h"
#include "util.h"
#include "state.h"
#include "map.h"

void dumpMap(Map *root, char *indent) {
    char *s = NULL;
    List *elt;
    int i;

    for (elt = root->prefix; !null(elt); elt = cdr(elt)) {
        if (s == NULL)
            s = concatstrings("/", car(elt));
        else
            s = concatstrings(s, concatstrings("/", car(elt)));
    }

    printf("%s%s ", indent, s);
    print_address(root->addr);
    printf("\n");
    indent = concatstrings("    ", indent);
    for (i = 0; i < root->nchildren; i++)
        dumpMap(root->children[i], indent);
}

/* compare prefix with the prefix of path of the same length */
static int path_cmp(List *path, List *prefix) {
    while (!null(path) && !null(prefix)) {
        int test = strcmp(car(path), car(prefix));
        if (test != 0)
            return test;
        path = cdr(path);
        prefix = cdr(prefix);
    }

    /* is the path a parent of this node? */
    if (null(path) && !null(prefix))
        return -1;

    /* either an exact match, or the path is below this node */
    return 0;
}

static List *strip_prefix(List *full, List *prefix) {
    while (!null(full) && !null(prefix)) {
        assert(!strcmp(car(full), car(prefix)));
        full = cdr(full);
        prefix = cdr(prefix);
    }

    assert(null(prefix));

    return full;
}

List *map_lookup(Map *root, List *path) {
    List *result = NULL;

    assert(root != NULL);
    assert(null(root->prefix));

    while (!null(path)) {
        int top, bottom, hit, index, test;

        result = cons(root, result);

        /* strip the prefix found at this node */
        path = strip_prefix(path, root->prefix);

        /* find a child to follow */
        top = root->nchildren;
        bottom = 0;
        hit = 0;
        index = 0;

        while (top > bottom) {
            index = bottom + (top - bottom) / 2;
            test = path_cmp(path, root->children[index]->prefix);

            if (test < 0) {
                top = index;
            } else if (test > 0) {
                bottom = index + 1;
            } else {
                hit = 1;
                break;
            }
        }

        /* no matching child? */
        if (!hit)
            break;

        root = root->children[index];
    }

    /* reverse the order of the result list */
    return reverse(result);
}

void map_insert(Map *root, List *path, Address *addr) {
    List *ancestry; 
    Map *parent;
    int nsiblings = 0, nchildren = 0, i;
    Map **siblings, **children;
    Map *newnode;

    /* first find the node that will be the parent of the new node */
    ancestry = map_lookup(root, path);
    assert(!null(ancestry));

    do {
        /* consume some of our path */
        parent = (Map *) car(ancestry);
        path = strip_prefix(path, parent->prefix);
    } while (!null(ancestry = cdr(ancestry)));

    assert(!null(path));

    /* figure out which of the children will stay children of the parent and
       which will become children of the new node */
    for (i = 0; i < parent->nchildren; i++)
        if (path_cmp(parent->children[i]->prefix, path))
            nsiblings++;
        else
            nchildren++;

    /* now we know how big everything will be */
    siblings = GC_MALLOC(sizeof(Map *) * (nsiblings + 1));
    assert(siblings != NULL);
    if (nchildren > 0) {
        children = GC_MALLOC(sizeof(Map *) * nchildren);
        assert(children != NULL);
    } else {
        children = NULL;
    }
    newnode = GC_NEW(Map);
    assert(newnode != NULL);

    newnode->prefix = path;
    newnode->addr = addr;
    newnode->nchildren = nchildren;
    newnode->children = children;

    /* partition the siblings and children of the new node */
    nchildren = 0;
    nsiblings = 1;
    siblings[0] = newnode;
    for(i = 0; i < parent->nchildren; i++) {
        if (path_cmp(parent->children[i]->prefix, path)) {
            /* it's a sibling of the new node */
            siblings[nsiblings] = parent->children[i];

            /* bubble the new node into its place */
            if (path_cmp(siblings[nsiblings]->prefix,
                         siblings[nsiblings-1]->prefix) < 0)
            {
                Map *swap = siblings[nsiblings];
                siblings[nsiblings] = siblings[nsiblings-1];
                siblings[nsiblings-1] = swap;
            }
            nsiblings++;
        } else {
            /* it's a child of the new node */
            children[nchildren] = parent->children[i];
            children[nchildren]->prefix =
                strip_prefix(children[nchildren]->prefix, path);
            nchildren++;
        }
    }

    parent->nchildren = nsiblings;
    parent->children = siblings;
}

void map_delete(Map *root, List *path) {
}

