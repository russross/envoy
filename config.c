#include <stdlib.h>
#include <netinet/in.h>
#include "config.h"

/* values that are configured at startup time */

int GLOBAL_MAX_SIZE = 4096;
int GLOBAL_MIN_SIZE = 256;
int BITS_PER_DIR_OBJECTS = 6;
int BITS_PER_DIR_DIRS = 8;
char *rootdir = NULL;
char *objectroot = NULL;
struct sockaddr_in *my_address = NULL;
