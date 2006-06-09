#ifndef _UTIL_H_
#define _UTIL_H_

#include <sys/stat.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
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
int startswith(char *s, char *sub);

char *dirname(char *path);
char *filename(char *path);
char *concatname(char *path, char *name);
char *resolvePath(char *base, char *ext, struct stat *info);
List *splitpath(char *path);

u32 addr_hash(const Address *addr);
int addr_cmp(const Address *a, const Address *b);
Address *make_address(char *host, int port);
Address *addr_decode(u32 address, u16 port);
u32 addr_get_address(Address *addr);
u16 addr_get_port(Address *addr);
u32 generic_hash(const void *elt, int len, u32 hash);
u32 string_hash(const char *str);
int randInt(int range);

#endif
