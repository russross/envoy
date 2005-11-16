#include <stdlib.h>
#include <netinet/in.h>
#include "config.h"

/* values that are configured at startup time */

int GLOBAL_MAX_SIZE = 4096;
int GLOBAL_MIN_SIZE = 256;
char *rootdir = NULL;
struct sockaddr_in *my_address = NULL;
