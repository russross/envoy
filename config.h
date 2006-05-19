#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <netinet/in.h>
#include "types.h"
#include "connection.h"

extern int GLOBAL_MAX_SIZE;
extern int GLOBAL_MIN_SIZE;
extern int BITS_PER_DIR_OBJECTS;
extern int BITS_PER_DIR_DIRS;
extern int BLOCK_SIZE;
extern char *rootdir;
extern char *objectroot;
extern int storage_server_count;
extern Connection **storage_servers;

#define ENVOY_PORT 9922
#define STORAGE_PORT 9923

#endif
