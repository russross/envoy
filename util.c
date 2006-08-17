#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <ctype.h>
#include <grp.h>
#include <pwd.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
#include "util.h"
#include "config.h"

Hashtable *group_info;
Hashtable *user_to_uid_table;
Hashtable *uid_to_user_table;
Hashtable *group_to_gid_table;
Hashtable *gid_to_group_table;

static List *raw_buffer;
static int raw_buffer_count;

/*****************************************************************************/

/* we know base to be well-formed, with a leading slash, no trailing slash */
char *resolvePath(char *base, char *ext, struct stat *info) {
    int first = 1;
    int linkdepth = 16;

    while (*ext) {
        int i, j, k;
        char *oldbase;

        for (i = 0; ext[i] == '/'; i++);
        for (j = i; ext[j] != 0 && ext[j] != '/'; j++);
        for (k = j; ext[k] == '/'; k++);

        /* empty string */
        if (i == j)
            break;

        /* starts with / */
        if (i != 0)
            base = "/";

        oldbase = base;

        if (j - i == 1 && ext[i] == '.') {
            /* nothing to do */
        } else if (j - i == 2 && ext[i] == '.' && ext[i+1] == '.') {
            /* back a directory */
            base = dirname(base);
        } else {
            /* copy over one directory part */
            base = concatname(base, substring(ext, i, j - i));
        }

        ext += k;

        if (lstat(base, info) < 0)
            return NULL;

        if (S_ISLNK(info->st_mode)) {
            char *lnk = GC_MALLOC_ATOMIC(info->st_size + strlen(ext) + 2);
            assert(lnk != NULL);

            if (linkdepth <= 0 || readlink(base, lnk, info->st_size) < 0)
                return NULL;

            lnk[info->st_size] = 0;
            strcat(lnk, "/");
            strcat(lnk, ext);
            ext = lnk;
            linkdepth--;
            base = oldbase;
        } else if (!S_ISDIR(info->st_mode)) {
            return NULL;
        }

        first = 0;
    }

    if (first) {
        if (lstat(base, info) < 0)
            return NULL;

        if (!S_ISDIR(info->st_mode))
            return NULL;
    }

    return base;
}

char *dirname(char *path) {
    char *slash, *base;
    int len;

    assert(path != NULL && path[0] == '/');

    if (!strcmp(path, "/"))
        return path;

    slash = strrchr(path, '/');
    assert(slash[1] != 0);
    if ((len = slash - path) == 0)
        return "/";
    base = GC_MALLOC_ATOMIC(len + 1);
    assert(base != NULL);
    strncpy(base, path, len);
    base[len] = 0;

    return base;
}

char *filename(char *path) {
    char *slash, *name;

    /* root directory is a special case */
    if (!strcmp(path, "/"))
        return path;

    slash = strrchr(path, '/');
    if (!slash)
        return path;
    ++slash;
    assert(*slash != 0);

    name = GC_MALLOC_ATOMIC(strlen(slash) + 1);
    strcpy(name, slash);

    return name;
}

char *concatname(char *path, char *name) {
    int pathlen = strlen(path);
    int namelen;
    char *res;

    if (emptystring(name))
        return path;

    namelen = strlen(name);
    res = GC_MALLOC_ATOMIC(pathlen + namelen + 2);
    assert(res != NULL);

    strcpy(res, path);
    assert(pathlen == 1 || res[pathlen-1] != '/');
    if (path[pathlen-1] != '/')
        strcat(res, "/");
    if (name[0] == '/')
        strcat(res, name + 1);
    else
        strcat(res, name);

    return res;
}

char *netaddr_to_string(struct sockaddr_in *netaddr) {
    struct hostent *host = gethostbyaddr(&netaddr->sin_addr,
            sizeof(netaddr->sin_addr), netaddr->sin_family);
    if (host == NULL || host->h_name == NULL) {
        Address *addr = netaddr_to_addr(netaddr);
        char *res = GC_MALLOC_ATOMIC(25);
        assert(res != NULL);
        sprintf(res, "%d.%d.%d.%d:%d",
                (addr->ip >> 24) & 0xff,
                (addr->ip >> 16) & 0xff,
                (addr->ip >>  8) & 0xff,
                addr->ip         & 0xff,
                addr->port);
        return res;
    } else {
        char *res = GC_MALLOC_ATOMIC(strlen(host->h_name) + 10);
        assert(res != NULL);
        sprintf(res, "%s:%d", host->h_name, ntohs(netaddr->sin_port));
        return res;
    }
}

char *addr_to_string(Address *addr) {
    return netaddr_to_string(addr_to_netaddr(addr));
}

Address *get_my_address(void) {
    char *hostname = GC_MALLOC_ATOMIC(MAX_HOSTNAME + 1);
    assert(hostname != NULL);
    hostname[MAX_HOSTNAME] = 0;
    if (gethostname(hostname, MAX_HOSTNAME) < 0) {
        perror("can't find local hostname");
        exit(-1);
    }

    hostname = stringcopy(hostname);
    if (DEBUG_VERBOSE)
        printf("starting up on host %s\n", hostname);
    return make_address(hostname, PORT);
}

/*
 * Hash and comparison functions
 */

u32 generic_hash(const void *elt, int len, u32 hash) {
    int i;
    u8 *bytes = (u8 *) elt;

    for (i = 0; i < len; i++)
        hash = hash * 157 + *(bytes++);

    return hash;
}

u32 string_hash(const char *str) {
    u32 hash = 0;
    while (*str)
        hash = hash * 157 + *(str++);
    return hash;
}

/* convert a u32 to a string, allocating the necessary storage */
char *u32tostr(u32 n) {
    char *res = GC_MALLOC_ATOMIC(11);
    assert(res != NULL);

    sprintf(res, "%u", n);
    return res;
}

u32 u32_hash(const u32 *elt) {
    return generic_hash(elt, sizeof(u32), 0);
}

int u32_cmp(const u32 *a, const u32 *b) {
    if (*a > *b)
        return 1;
    else if (*a < *b)
        return -1;
    else
        return 0;
}

u32 u64_hash(const u64 *n) {
    return generic_hash(n, sizeof(u64), 13);
}

int u64_cmp(const u64 *a, const u64 *b) {
    if (*a == *b)
        return 0;
    else if (*a < *b)
        return -1;
    else
        return 1;
}

u32 now(void) {
    return (u32) time(NULL);
}

u32 addr_hash(const Address *addr) {
    return generic_hash(&addr->port, sizeof(u16),
            generic_hash(&addr->ip, sizeof(u32), 0));
}

int addr_cmp(const Address *a, const Address *b) {
    if (a == NULL || b == NULL)
        return a == b ? 0 : b == NULL;
    if (a->port != b->port)
        return a->port - b->port;
    return a->ip - b->ip;
}

List *parse_address_list(char *hosts, int defaultport) {
    List *res = NULL;
    do {
        char *comma = strchr(hosts, ',');
        char *host;
        if (comma == NULL)
            host = hosts;
        else
            host = substring(hosts, 0, comma++ - hosts);

        Address *addr = parse_address(host, defaultport);
        if (addr != NULL)
            res = cons(addr, res);
        hosts = comma;
    } while (!emptystring(hosts));

    return reverse(res);
}

Address *parse_address(char *host, int defaultport) {
    char *colon = strchr(host, ':');
    int port;
    if (colon == NULL)
        return make_address(host, defaultport);
    host = substring(host, 0, colon - host);
    port = atoi(++colon);
    return make_address(host, port);
}

Address *make_address(char *host, int port) {
    Address *addr = GC_NEW_ATOMIC(Address);
    struct hostent *ent = gethostbyname(host);

    assert(addr != NULL);
    assert(ent != NULL);
    assert(ent->h_addrtype == AF_INET && ent->h_length == 4);
    assert(ent->h_addr_list[0] != NULL);
    if (ent->h_addr_list[1] != NULL)
        fprintf(stderr, "warning: multiple ip addresses, using the first\n");

    addr->ip = ntohl(((struct in_addr *) ent->h_addr_list[0])->s_addr);
    addr->port = port;

    return addr;
}

Address *address_new(u32 ip, u16 port) {
    Address *addr = GC_NEW_ATOMIC(Address);
    assert(addr != NULL);
    addr->ip = ip;
    addr->port = port;
    return addr;
}

struct sockaddr_in *addr_to_netaddr(Address *addr) {
    struct sockaddr_in *netaddr = GC_NEW_ATOMIC(struct sockaddr_in);

    assert(netaddr != NULL);
    assert(addr != NULL);

    netaddr->sin_family = AF_INET;
    netaddr->sin_addr.s_addr = htonl(addr->ip);
    netaddr->sin_port = htons(addr->port);

    return netaddr;
}

Address *netaddr_to_addr(struct sockaddr_in *netaddr) {
    Address *addr = GC_NEW_ATOMIC(Address);
    assert(addr != NULL);
    addr->ip = ntohl(netaddr->sin_addr.s_addr);
    addr->port = ntohs(netaddr->sin_port);
    return addr;
}

List *splitpath(char *path) {
    List *res = NULL;

    assert(path != NULL);

    while (*path) {
        int i, j, k;

        for (i = 0; path[i] == '/'; i++);
        for (j = i; path[j] != 0 && path[j] != '/'; j++);
        for (k = j; path[k] == '/'; k++);

        if (j - i > 0)
            res = cons(substring(path, i, j - i), res);
        else
            break;

        path = &path[k];
    }

    return reverse(res);
}

int min(int x, int y) {
    return x < y ? x : y;
}

int max(int x, int y) {
    return x > y ? x : y;
}

char *stringcopy(char *s) {
    char *res;
    assert(s != NULL);
    res = GC_MALLOC_ATOMIC(strlen(s) + 1);
    assert(res != NULL);
    strcpy(res, s);
    return res;
}

char *substring(char *s, int start, int len) {
    char *res;
    assert(s != NULL);
    if (len == 0)
        return "";
    res = GC_MALLOC_ATOMIC(len + 1);
    assert(res != NULL);
    strncpy(res, s + start, len);
    res[len] = 0;
    return res;
}

char *substring_rest(char *s, int start) {
    return substring(s, start, strlen(s) - start);
}

char *concatstrings(char *a, char *b) {
    char *res;
    assert(a != NULL && b != NULL);
    res = GC_MALLOC_ATOMIC(strlen(a) + strlen(b) + 1);
    assert(res != NULL);
    strcpy(res, a);
    strcat(res, b);
    return res;
}

int emptystring(char *s) {
    return s == NULL || s[0] == 0;
}

int startswith(char *s, char *sub) {
    int len = strlen(sub);
    return len <= strlen(s) && !strncmp(s, sub, len);
}

int ispathprefix(char *s, char *prefix) {
    int len = strlen(prefix);
    return len <= strlen(s) && !strncmp(s, prefix, len) &&
        (s[len] == '/' || s[len] == 0);
}

int randInt(int range) {
    static int first = 1;

    if (first) {
        srandom(time(NULL));
        first = 0;
    }

    return random() % range;
}

void util_state_init(void) {
    struct group *group;
    struct passwd *passwd;

    user_to_uid_table = hash_create(
            USER_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);

    uid_to_user_table = hash_create(
            USER_HASHTABLE_SIZE,
            (Hashfunc) u32_hash,
            (Cmpfunc) u32_cmp);

    gid_to_group_table = hash_create(
            GROUP_HASHTABLE_SIZE,
            (Hashfunc) u32_hash,
            (Cmpfunc) u32_cmp);

    group_to_gid_table = hash_create(
            GROUP_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);

    group_info = hash_create(
            GROUP_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);

    /* walk the system password file */
    setpwent();
    while ((passwd = getpwent()) != NULL) {
        u32 *uid = GC_NEW_ATOMIC(u32);
        char *user = stringcopy(passwd->pw_name);
        assert(uid != NULL);
        *uid = passwd->pw_uid;
        hash_set(user_to_uid_table, user, uid);
        hash_set(uid_to_user_table, uid, user);
    }
    endpwent();

    /* walk the system group table */
    setgrent();
    while ((group = getgrent()) != NULL) {
        int i;
        char *groupname = stringcopy(group->gr_name);
        u32 *gid = GC_NEW_ATOMIC(u32);
        assert(gid != NULL);
        *gid = group->gr_gid;
        hash_set(group_to_gid_table, groupname, gid);
        hash_set(gid_to_group_table, gid, groupname);
        for (i = 0; group->gr_mem[i] != NULL; i++) {
            Hashtable *user = hash_get(group_info, group->gr_mem[i]);
            if (user == NULL) {
                user = hash_create(
                        GROUP_HASHTABLE_SIZE,
                        (Hashfunc) string_hash,
                        (Cmpfunc) strcmp);
                hash_set(group_info, stringcopy(group->gr_mem[i]), user);
            }
            hash_set(user, groupname, groupname);
        }
    }
    endgrent();

    raw_buffer = NULL;
    raw_buffer_count = 0;
}

int isgroupmember(char *user, char *group) {
    Hashtable *groups = hash_get(group_info, user);
    if (groups == NULL)
        return 0;
    return hash_get(groups, group) != NULL;
}

int isgroupleader(char *user, char *group) {
    return isgroupmember(user, group);
}

char *uid_to_user(u32 uid) {
    return hash_get(uid_to_user_table, &uid);
}

u32 user_to_uid(char *user) {
    u32 *uid = hash_get(user_to_uid_table, user);
    if (uid == NULL)
        return NOUID;
    return *uid;
}

char *gid_to_group(u32 gid) {
    return hash_get(gid_to_group_table, &gid);
}

u32 group_to_gid(char *group) {
    u32 *gid = hash_get(group_to_gid_table, group);
    if (gid == NULL)
        return NOGID;
    return *gid;
}

int ispositiveint(char *s) {
    int i;
    for (i = 0; s[i] != 0; i++)
        if (!isdigit(s[i]) || (i == 0 && s[i] == '0'))
            return 0;
    return 1;
}

#define CURRENT_LEN 7

enum path_type get_admin_path_type(char *path) {
    int i = 0, j, k;
    int len;
    char *name;

    while (path[i] != 0) {
        /* pick out the next path element */
        for ( ; path[i] == '/'; i++);
        for (j = i; path[j] != 0 && path[j] != '/'; j++);

        /* at the end? */
        if ((len = j - i) == 0)
            break;

        name = &path[i];
        i = j;

        if (len == CURRENT_LEN && !strncmp(name, "current", CURRENT_LEN))
            return PATH_CURRENT;

        /* is it a positive integer? */
        for (k = 0; k < len; k++)
            if (!isdigit(name[k]) || (k == 0 && name[k] == '0'))
                break;

        if (k == len)
            return PATH_SNAPSHOT;
    }

    return PATH_ADMIN;
}

struct qid makeqid(u32 mode, u32 mtime, u64 size, u64 oid) {
    struct qid qid;
    qid.type =
        (mode & DMDIR) ? QTDIR :
        (mode & DMSYMLINK) ? QTSLINK :
        (mode & DMDEVICE) ? QTTMP :
        QTFILE;
    qid.version = mtime ^ (size << 8);
    qid.path = oid;

    return qid;
}

void *raw_new(void) {
    void *raw;
    if (null(raw_buffer)) {
        raw = GC_MALLOC_ATOMIC(GLOBAL_MAX_SIZE);
        assert(raw != NULL);
        if (DEBUG_VERBOSE)
            printf("raw_buffer_count = %d\n", ++raw_buffer_count);
    } else {
        raw = car(raw_buffer);
        raw_buffer = cdr(raw_buffer);
    }
    return raw;
}

void raw_delete(void *raw) {
    if (raw == NULL)
        return;
    if (DEBUG_VERBOSE) {
        List *raws = raw_buffer;
        for ( ; !null(raws); raws = cdr(raws))
            assert(car(raws) != raw);
    }
    raw_buffer = cons(raw, raw_buffer);
}

int p9stat_cmp(const struct p9stat *a, const struct p9stat *b) {
    int cmp;
    if (a == b)
        return 0;
    if (a == NULL || b == NULL)
        return a == NULL ? -1 : 1;
    if (a->type != b->type)
        return a->type - b->type;
    if (a->dev != b->dev)
        return a->dev - b->dev;
    if (a->qid.type != b->qid.type)
        return a->qid.type - b->qid.type;
    if (a->qid.version != b->qid.version)
        return a->qid.version - b->qid.version;
    if (a->qid.path != b->qid.path)
        return a->qid.path - b->qid.path;
    if (a->mode != b->mode)
        return a->mode - b->mode;
    if (a->atime != b->atime)
        return a->atime - b->atime;
    if (a->mtime != b->mtime)
        return a->mtime - b->mtime;
    if (a->length != b->length)
        return a->length - b->length;
    if ((cmp = strcmp(a->name, b->name)) != 0)
        return cmp;
    if ((cmp = strcmp(a->uid, b->uid)) != 0)
        return cmp;
    if ((cmp = strcmp(a->gid, b->gid)) != 0)
        return cmp;
    if ((cmp = strcmp(a->muid, b->muid)) != 0)
        return cmp;
    if ((cmp = strcmp(a->extension, b->extension)) != 0)
        return cmp;
    if (a->n_uid != b->n_uid)
        return a->n_uid - b->n_uid;
    if (a->n_gid != b->n_gid)
        return a->n_gid - b->n_gid;
    if (a->n_muid != b->n_muid)
        return a->n_muid - b->n_muid;
    return 0;
}
