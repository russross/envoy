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
char *substring_rest(char *s, int start);
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
List *parse_address_list(char *hosts, int defaultport);
Address *parse_address(char *host, int defaultport);
Address *make_address(char *host, int port);
Address *addr_decode(u32 address, u16 port);
u32 addr_get_ip(Address *addr);
u16 addr_get_port(Address *addr);
char *address_to_string(Address *addr);
Address *get_my_address(void);

u32 generic_hash(const void *elt, int len, u32 hash);
u32 string_hash(const char *str);
u32 u32_hash(const u32 *elt);
int u32_cmp(const u32 *a, const u32 *b);
char *u32tostr(u32 n);
int randInt(int range);
u32 now(void);

int isgroupmember(char *user, char *group);
int isgroupleader(char *user, char *group);
u32 user_to_uid(char *user);
char *uid_to_user(u32 uid);
u32 group_to_gid(char *group);
char *gid_to_group(u32 gid);
int ispositiveint(char *s);
int is_admin_directory(char *dir);

enum path_type {
    PATH_ADMIN,
    PATH_CURRENT,
    PATH_SNAPSHOT,
};

enum path_type get_admin_path_type(char *path);
struct qid makeqid(u32 mode, u32 mtime, u64 size, u64 oid);

void util_state_init(void);

#endif
