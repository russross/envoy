#ifndef _MAP_H_
#define _MAP_H_

#include <netinet/in.h>
#include "types.h"
#include "list.h"

struct map {
    List *prefix;
    Address *addr;
    int nchildren;
    struct map **children;
};

List *map_lookup(Map *root, List *path);
void map_insert(Map *root, List *path, Address *addr);
void dumpMap(Map *root, char *indent);

#endif
