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
#include "types.h"
#include "9p.h"
#include "list.h"
#include "util.h"
#include "config.h"

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
    int namelen = strlen(name);
    char *res = GC_MALLOC_ATOMIC(pathlen + namelen + 2);

    assert(res != NULL);

    strcpy(res, path);
    assert(pathlen == 1 || res[pathlen-1] != '/');
    if (path[pathlen-1] != '/')
        strcat(res, "/");
    strcat(res, name);

    return res;
}

char *address_to_string(Address *addr) {
    struct hostent *host = gethostbyaddr(&addr->sin_addr,
            sizeof(addr->sin_addr), addr->sin_family);
    if (host == NULL || host->h_name == NULL) {
        u32 address = addr_get_address(addr);
        char *res = GC_MALLOC_ATOMIC(25);
        assert(res != NULL);
        sprintf(res, "%d.%d.%d.%d:%d",
                (address >> 24) & 0xff,
                (address >> 16) & 0xff,
                (address >>  8) & 0xff,
                address         & 0xff,
                addr_get_port(addr));
        return res;
    } else {
        char *res = GC_MALLOC_ATOMIC(strlen(host->h_name) + 10);
        assert(res != NULL);
        sprintf(res, "%s:%d", host->h_name, ntohs(addr->sin_port));
        return res;
    }
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
    return generic_hash(str, strlen(str), 0);
}

u32 addr_hash(const Address *addr) {
    u32 hash = 0;
    hash = generic_hash(&addr->sin_family, sizeof(addr->sin_family), hash);
    hash = generic_hash(&addr->sin_port, sizeof(addr->sin_port), hash);
    hash = generic_hash(&addr->sin_addr, sizeof(addr->sin_addr), hash);
    return hash;
}

int addr_cmp(const Address *a, const Address *b) {
    int x;
    if (a == NULL || b == NULL)
        return a == b;
    if ((x = memcmp(&a->sin_family, &b->sin_family, sizeof(a->sin_family))))
        return x;
    if ((x = memcmp(&a->sin_port, &b->sin_port, sizeof(a->sin_port))))
        return x;
    return memcmp(&a->sin_addr, &b->sin_addr, sizeof(a->sin_addr));
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

    addr->sin_family = AF_INET;
    addr->sin_addr = *((struct in_addr *) ent->h_addr_list[0]);
    addr->sin_port = htons(port);

    return addr;
}

Address *addr_decode(u32 address, u16 port) {
    Address *addr = GC_NEW_ATOMIC(Address);
    assert(addr != NULL);
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = htonl(address);
    addr->sin_port = htons(port);

    return addr;
}

u32 addr_get_address(Address *addr) {
    return ntohl(addr->sin_addr.s_addr);
}

u16 addr_get_port(Address *addr) {
    return ntohs(addr->sin_port);
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

int randInt(int range) {
    static int first = 1;

    if (first) {
        srandom(time(NULL));
        first = 0;
    }

    return random() % range;
}
