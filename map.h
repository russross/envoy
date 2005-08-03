#ifndef _MAP_H_
#define _MAP_H_

#include "util.h"

struct map {
    struct cons *prefix;
    struct sockaddr_in *addr;
    int nchildren;
    struct map **children;
};

struct cons *map_lookup(struct map *root, struct cons *path);
void map_insert(struct map *root, struct cons *path,
        struct sockaddr_in *addr);
void dumpMap(struct map *root, char *indent);

#endif
