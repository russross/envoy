#ifndef _UTIL_H_
#define _UTIL_H_

#include <sys/stat.h>
#include <netinet/in.h>
#include "types.h"
#include "list.h"

/*
 * General utility functions
 */

int min(int x, int y);
int max(int x, int y);

char *stringcopy(char *s);
char *substring(char *s, int start, int len);
char *concatstrings(char *a, char *b);
int emptystring(char *s);

char *dirname(char *path);
char *filename(char *path);
char *concatname(char *path, char *name);
char *resolvePath(char *base, char *ext, struct stat *info);

Address *make_address(char *host, int port);
List *splitpath(char *path);

#endif
