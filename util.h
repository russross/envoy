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
int ispathprefix(char *s, char *prefix);

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
Address *address_new(u32 ip, u16 port);
struct sockaddr_in *addr_to_netaddr(Address *addr);
Address *netaddr_to_addr(struct sockaddr_in *netattr);
char *netaddr_to_string(struct sockaddr_in *netaddr);
char *addr_to_string(Address *addr);
Address *get_my_address(void);

u32 generic_hash(const void *elt, int len, u32 hash);
u32 string_hash(const char *str);
u32 u32_hash(const u32 *elt);
int u32_cmp(const u32 *a, const u32 *b);
u32 u64_hash(const u64 *elt);
int u64_cmp(const u64 *a, const u64 *b);
char *u32tostr(u32 n);
int randInt(int range);
u32 now(void);
double now_double(void);
int p9stat_cmp(const struct p9stat *a, const struct p9stat *b);

int isgroupmember(char *user, char *group);
int isgroupleader(char *user, char *group);
u32 user_to_uid(char *user);
char *uid_to_user(u32 uid);
u32 group_to_gid(char *group);
char *gid_to_group(u32 gid);
int ispositiveint(char *s);

enum path_type {
    PATH_ADMIN,
    PATH_CURRENT,
    PATH_SNAPSHOT,
};

enum path_type get_admin_path_type(char *path);
struct qid makeqid(u32 mode, u32 mtime, u64 size, u64 oid);

void *raw_new(void);
void raw_delete(void *raw);

void util_state_init(void);

#endif
