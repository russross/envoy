#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <sys/stat.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "util.h"
#include "config.h"
#include "state.h"
#include "lru.h"
#include "oid.h"

static inline u64 objectdir_findstart(u64 oid) {
    return (oid >> BITS_PER_DIR_OBJECTS) << BITS_PER_DIR_OBJECTS;
}

static u32 u64_hash(const u64 *n) {
    return generic_hash(n, sizeof(u64), 13);
}

static int u64_cmp(const u64 *a, const u64 *b) {
    if (a == b)
        return 0;
    else if (a < b)
        return -1;
    else
        return 1;
}

static void close_dir(struct objectdir *od) {
    /* no cleanup is required */
    od->start = ~0LL;
    od->dirname = NULL;
    od->filenames = NULL;
}

static void close_fd_wrapper(struct fd_wrapper *wrapper) {
    if (--wrapper->refcount == 0) {
        close(wrapper->fd);
        wrapper->fd = -1;
    }
}

Lru *oid_init_dir_lru(void) {
    return lru_new(
            OBJECTDIR_CACHE_SIZE,
            (u32 (*)(const void *)) u64_hash,
            (int (*)(const void *, const void *)) u64_cmp,
            (void (*)(void *)) close_dir);
}

Lru *oid_init_fd_lru(void) {
    return lru_new(
            FD_CACHE_SIZE,
            (u32 (*)(const void *)) u64_hash,
            (int (*)(const void *, const void *)) u64_cmp,
            (void (*)(void *)) close_fd_wrapper);
}

struct fd_wrapper *oid_add_fd_wrapper(u64 oid, int fd) {
    struct fd_wrapper *wrapper;
    u64 *key;

    assert(fd >= 0);
    wrapper = GC_NEW_ATOMIC(struct fd_wrapper);
    assert(wrapper != NULL);
    key = GC_NEW_ATOMIC(u64);
    assert(key != NULL);

    wrapper->refcount = 0;
    wrapper->fd = fd;
    *key = oid;

    lru_add(state->oid_fd_lru, key, wrapper);

    return wrapper;
}

List *oid_to_path(u64 oid) {
    List *path = NULL;
    int bitsleft = OID_BITS;
    char buff[16];

    while (bitsleft > 0) {
        u64 part;
        int chunkpart =
            ((bitsleft - BITS_PER_DIR_OBJECTS) / BITS_PER_DIR_DIRS) *
            BITS_PER_DIR_DIRS;
        int bits;

        /* three cases:
         * 1) odd bits at the beginning
         * 2) odd bits at the end
         * 3) regular sized chunks in the middle */
        if (bitsleft > chunkpart + BITS_PER_DIR_OBJECTS)
            bits = bitsleft - (chunkpart + BITS_PER_DIR_OBJECTS);
        else if (bitsleft == BITS_PER_DIR_OBJECTS)
            break;
        else
            bits = BITS_PER_DIR_DIRS;

        bitsleft -= bits;
        part = oid >> bitsleft;
        oid &= (1LL << bitsleft) - 1LL;
        sprintf(buff, "%0*llx", (bits + 3) / 4, part);
        path = cons(stringcopy(buff), path);
    }

    return reverse(path);
}

u64 path_to_oid(List *path) {
    u64 oid = 0LL;
    int bitsleft = OID_BITS;

    while (bitsleft > 0) {
        int part;
        char *elt = car(path);
        int chunkpart =
            ((bitsleft - BITS_PER_DIR_OBJECTS) / BITS_PER_DIR_DIRS) *
            BITS_PER_DIR_DIRS;
        int bits;

        assert(sscanf(elt, "%x", &part) == 1);
        path = cdr(path);

        /* three cases:
         * 1) odd bits at the beginning
         * 2) odd bits at the end
         * 3) regular sized chunks in the middle */
        if (bitsleft > chunkpart + BITS_PER_DIR_OBJECTS)
            bits = bitsleft - (chunkpart + BITS_PER_DIR_OBJECTS);
        else if (bitsleft == BITS_PER_DIR_OBJECTS)
            break;
        else
            bits = BITS_PER_DIR_DIRS;

        bitsleft -= bits;

        oid |= ((u64) part) << bitsleft;
    }

    return oid;
}

u64 oid_find_next_available(void) {
    char *dir = objectroot;
    List *minpath_full = oid_to_path(0LL);
    List *minpath_lst = minpath_full;
    List *maxpath_full = oid_to_path(0xFFFFFFFFFFFFFFFFLL);
    List *maxpath_lst = maxpath_full;
    List *lastpath = NULL;

    while (!null(maxpath_lst)) {
        /* scan the current directory */
        char *maxpath = car(maxpath_lst);
        char *high = NULL;
        DIR *dd = opendir(dir);
        struct dirent *ent;
        assert(dd != NULL);

        while ((ent = readdir(dd)) != NULL) {
            int n;
            char c;

            /* test this filename:
             * 1) is it the right length?
             * 2) is it a valid hex number?
             * 3) is it within the acceptable range?
             * 4) does it beat our previous high value? */
            if (strlen(ent->d_name) != strlen(maxpath) ||
                    sscanf(ent->d_name, "%x%c", &n, &c) != 1 ||
                    strcmp(ent->d_name, maxpath) > 0 ||
                    (high != NULL && strcmp(ent->d_name, high) <= 0))
            {
                continue;
            }

            high = stringcopy(ent->d_name);
        }

        closedir(dd);

        if (high == NULL) {
            while (!null(minpath_lst)) {
                lastpath = cons(car(minpath_lst), lastpath);
                minpath_lst = cdr(minpath_lst);
            }
            return path_to_oid(reverse(lastpath));
        } else {
            maxpath_lst = cdr(maxpath_lst);
            minpath_lst = cdr(minpath_lst);
            lastpath = cons(high, lastpath);
            dir = concatname(dir, high);
        }
    }

    return path_to_oid(reverse(lastpath)) + (1 << BITS_PER_DIR_OBJECTS);
}

int oid_reserve_block(u64 *oid, int *count) {
    char *dir = objectroot;
    struct stat info;
    List *path;

    *oid = state->oid_next_available;
    *count = 1 << BITS_PER_DIR_OBJECTS;
    state->oid_next_available += (u64) *count;

    /* just in case 64 bits isn't enough... */
    if (state->oid_next_available < *oid)
        return -1;

    path = oid_to_path(*oid);

    while (!null(path) && !null(cdr(path))) {
        dir = concatname(dir, car(path));
        path = cdr(path);
        if (lstat(dir, &info) < 0) {
            if (mkdir(dir, 0755) < 0)
                return -1;
        } else if (!S_ISDIR(info.st_mode)) {
            return -1;
        }
    }

    return 0;
}

struct objectdir *oid_read_dir(u64 start) {
    int i;
    int size = 1 << BITS_PER_DIR_OBJECTS;
    struct objectdir *result;
    char *dirname;
    DIR *dd;
    struct dirent *ent;
    List *path;

    assert(start == objectdir_findstart(start));

    dirname = objectroot;
    path = oid_to_path(start);
    for (path = oid_to_path(start); !null(path); path = cdr(path))
        dirname = concatname(dirname, (char *) car(path));

    result = GC_NEW(struct objectdir);
    assert(result != NULL);
    result->dirname = dirname;
    result->filenames = GC_MALLOC(sizeof(char *) * size);
    assert(result->filenames != NULL);
    result->start = start;

    for (i = 0; i < size; i++)
        result->filenames[i] = NULL;

    dd = opendir(dirname);
    if (dd == NULL)
        return NULL;

    while ((ent = readdir(dd)) != NULL) {
        unsigned int id, mode;
        char type, eos;
        char name[9], group[9];
        if (sscanf(ent->d_name, "%x %c%o %8s %8s%c",
                    &id, &type, &mode, name, group, &eos) == 5 &&
                id < size &&
                (type == 'd' || type == 'f' || type == 'l') &&
                name[0] != 0 && group[0] != 0)
        {
            result->filenames[id] = stringcopy(ent->d_name);
        }
    }

    closedir(dd);

    return result;
}

struct objectdir *objectdir_lookup(u64 start) {
    struct objectdir *result;

    assert(start == objectdir_findstart(start));

    result = lru_get(state->oid_dir_lru, &start);

    if (result == NULL) {
        result = oid_read_dir(start);

        if (result != NULL) {
            u64 *key = GC_NEW_ATOMIC(u64);
            assert(key != NULL);
            *key = start;
            lru_add(state->oid_dir_lru, key, result);
        }
    }

    return result;
}

static inline struct qid makeqid(u32 mode, u32 mtime, u64 size, u64 oid) {
    struct qid qid;
    qid.type =
        (mode & DMDIR) ? QTDIR :
        (mode & DMSYMLINK) ? QTLINK :
        0x00;
    qid.version = mtime ^ (size << 8);
    qid.path = oid;

    return qid;
}

struct p9stat *oid_stat(u64 oid) {
    /* get filename */
    u64 start = objectdir_findstart(oid);
    struct objectdir *dir;
    char *filename;
    struct stat info;
    struct p9stat *result;

    unsigned int id, mode;
    char type, eos;
    char name[9], group[9];

    if (    /* gather all the info we need */
            (dir = objectdir_lookup(start)) == NULL ||
            (filename = dir->filenames[oid - start]) == NULL ||
            sscanf(filename, "%x %c%o %8s %8s%c",
                &id, &type, &mode, name, group, &eos) != 5 ||
            id != oid - start ||
            (type != 'd' && type != 'f' && type != 'l') ||
            name[0] == 0 ||
            group[0] == 0 ||
            lstat(concatname(dir->dirname, filename), &info) < 0)
    {
        return NULL;
    }

    result = GC_NEW(struct p9stat);
    assert(result != NULL);

    /* convert everything to a 9p stat record */
    result->type = 0;
    result->dev = 0;
    result->qid = makeqid(mode, info.st_mtime, info.st_size, oid);
    result->mode = mode;
    result->atime = info.st_atime;
    result->mtime = info.st_mtime;
    result->length = info.st_size;
    result->name = NULL;
    result->uid = stringcopy(name);
    result->gid = stringcopy(group);
    result->muid = result->uid;
    result->extension = NULL;
    /* for special files, read the contents of the file into extension */
    if (mode & (DMSYMLINK | DMDEVICE | DMNAMEDPIPE | DMSOCKET) &&
            info.st_size > 0 && info.st_size < MAX_EXTENSION_LENGTH)
    {
        int i, size;
        struct fd_wrapper *wrapper = oid_get_open_fd_wrapper(oid);
        result->extension = GC_MALLOC_ATOMIC(info.st_size + 1);
        assert(result->extension != NULL);
        if (lseek(wrapper->fd, 0, SEEK_SET) < 0) {
            wrapper->refcount--;
            return NULL;
        }
        for (size = 0; size < info.st_size; ) {
            i = read(wrapper->fd, result->extension + size,
                    info.st_size - size);
            if (i <= 0) {
                wrapper->refcount--;
                return NULL;
            }
            size += i;
        }
        result->extension[size] = 0;
        wrapper->refcount--;
    }
    result->n_uid = atoi(name);
    result->n_gid = atoi(group);
    result->n_muid = atoi(name);

    return result;
}

int oid_wstat(u64 oid, struct p9stat *info) {
    u64 start = objectdir_findstart(oid);
    struct objectdir *dir;
    char *filename, *newfilename;

    unsigned int id, mode;
    char type, eos;
    char name[9], group[9];

    if (    /* get the current info for the object */
            (dir = objectdir_lookup(start)) == NULL ||
            (filename = dir->filenames[oid - start]) == NULL ||
            sscanf(filename, "%x %c%o %8s %8s%c",
                &id, &type, &mode, name, group, &eos) != 5 ||
            id != oid - start ||
            (type != 'd' && type != 'f' && type != 'l') ||
            name[0] == 0 ||
            group[0] == 0)
    {
        return -1;
    }

    /* make the requested changes */

    /* mtime */
    if (info->mtime != ~(u32) 0) {
        struct utimbuf buf;
        buf.actime = 0;
        buf.modtime = info->mtime;
        if (utime(filename, &buf) < 0)
            return -1;
    }

    /* mode */
    if (info->mode != ~(u32) 0)
        mode = info->mode;

    /* gid */
    if (!emptystring(info->gid)) {
        strncpy(group, info->gid, 8);
        name[8] = 0;
    }

    /* uid */
    if (!emptystring(info->uid)) {
        strncpy(name, info->uid, 8);
        name[8] = 0;
    }

    /* extension */
    if (!emptystring(info->extension) &&
            (mode & (DMSYMLINK | DMDEVICE | DMNAMEDPIPE | DMSOCKET)))
    {
        struct fd_wrapper *wrapper = oid_get_open_fd_wrapper(oid);
        int len = strlen(info->extension);
        if (ftruncate(wrapper->fd, 0) < 0 ||
                lseek(wrapper->fd, 0, SEEK_SET) < 0 ||
                write(wrapper->fd, info->extension, len) != len)
        {
            wrapper->refcount--;
            return -1;
        }
        wrapper->refcount--;
    } else if (info->length != ~(u64) 0) {
        /* truncate */
        struct stat finfo;
        if (lstat(filename, &finfo) < 0)
            return -1;
        if (info->length != finfo.st_size) {
            if (truncate(filename, info->length) < 0)
                return -1;
        }
    }

    /* see if there were any filename changes */
    newfilename = GC_MALLOC_ATOMIC(OBJECT_FILENAME_LENGTH + 1);
    assert(newfilename != NULL);
    sprintf(newfilename, "%0*x %08x %8s %8s",
            (BITS_PER_DIR_OBJECTS + 3) / 4, (unsigned) (oid - start),
            mode,
            name,
            group);
    if (!strcmp(filename, newfilename)) {
        if (rename(filename, newfilename) < 0)
            return -1;
        dir->filenames[oid - start] = newfilename;
    }

    return 0;
}

int oid_create(u64 oid, struct p9stat *info) {
    u64 start = objectdir_findstart(oid);
    struct objectdir *dir;
    struct utimbuf buf;
    char *filename;
    char *pathname;
    int fd;

    if (    /* find the place for this object and make sure it is new */
            (dir = objectdir_lookup(start)) == NULL ||
            (filename = dir->filenames[oid - start]) != NULL)
    {
        return -1;
    }

    /* create the file and set filename-encoded stats */
    filename = GC_MALLOC_ATOMIC(OBJECT_FILENAME_LENGTH + 1);
    assert(filename != NULL);
    sprintf(filename, "%0*x %08x %8s %8s",
            (BITS_PER_DIR_OBJECTS + 3) / 4, (unsigned) (oid - start),
            info->mode,
            info->uid,
            info->gid);
    pathname = concatname(dir->dirname, filename);

    fd = creat(pathname, info->mode & 07777);
    if (fd < 0)
        return -1;
    dir->filenames[oid - start] = filename;
    oid_add_fd_wrapper(oid, fd);

    /* store metadata for special file types as file contents */
    if (!emptystring(info->extension) &&
            (info->mode & (DMSYMLINK | DMDEVICE | DMNAMEDPIPE | DMSOCKET)) &&
            strlen(info->extension) !=
            write(fd, info->extension, strlen(info->extension)))
    {
        return -1;
    }

    /* set the times */
    buf.actime = info->atime;
    buf.modtime = info->mtime;
    if (utime(pathname, &buf) < 0)
        return -1;

    return 0;
}

struct fd_wrapper *oid_get_open_fd_wrapper(u64 oid) {
    struct fd_wrapper *wrapper;

    /* first check the cache */
    wrapper = lru_get(state->oid_fd_lru, &oid);

    /* open the file if necessary */
    if (wrapper == NULL) {
        u64 start = objectdir_findstart(oid);
        struct objectdir *dir;
        char *filename;
        int fd;
        if (    /* find the pathname and open the file */
                (dir = objectdir_lookup(start)) == NULL ||
                (filename = dir->filenames[oid - start]) == NULL ||
                (fd = open(concatname(dir->dirname, filename), O_RDWR )) < 0)
        {
            return NULL;
        }
        wrapper = oid_add_fd_wrapper(oid, fd);
    }

    assert(wrapper->fd >= 0);

    wrapper->refcount++;

    return wrapper;
}
