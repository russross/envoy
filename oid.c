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
#include "worker.h"
#include "lru.h"
#include "oid.h"

static inline char *make_filename(u64 oid, u32 mode, char *name, char *group) {
    char *filename = GC_MALLOC_ATOMIC(OBJECT_FILENAME_LENGTH + 1);
    assert(filename != NULL);
    sprintf(filename, "%0*x %8x %*.*s %*.*s",
            (BITS_PER_DIR_OBJECTS + 3) / 4,
            (unsigned) (oid & ((1 << BITS_PER_DIR_OBJECTS) - 1)),
            mode,
            MAX_UID_LENGTH, MAX_UID_LENGTH, name,
            MAX_GID_LENGTH, MAX_GID_LENGTH, group);
    return filename;
}

static inline int parse_filename(char *filename, unsigned int *id, u32 *mode,
        char **name, char **group)
{
    char eos;

    *name = GC_MALLOC_ATOMIC(MAX_UID_LENGTH + 1);
    assert(name != NULL);
    *group = GC_MALLOC_ATOMIC(MAX_GID_LENGTH + 1);
    assert(group != NULL);

    if (sscanf(filename, "%x %x %8s %8s%c",
                id, mode, *name, *group, &eos) != 5 ||
            name[0] == 0 || group[0] == 0)
    {
        return -1;
    }

    return 0;
}

static inline u64 oid_dir_findstart(u64 oid) {
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

static int resurrect_dir(struct oid_dir *od) {
    return od->wait != NULL;
}

static void close_dir(struct oid_dir *od) {
    assert(od->wait == NULL);

    /* no cleanup is required */
    od->start = ~0LL;
    od->dirname = NULL;
    od->filenames = NULL;
}

static int resurrect_oid_fd(struct oid_fd *wrapper) {
    return wrapper->wait != NULL;
}

static void close_oid_fd(struct oid_fd *wrapper) {
    assert(wrapper->wait == NULL);
    close(wrapper->fd);
    wrapper->fd = -1;
}

Lru *oid_init_dir_lru(void) {
    return lru_new(
            OBJECTDIR_CACHE_SIZE,
            (u32 (*)(const void *)) u64_hash,
            (int (*)(const void *, const void *)) u64_cmp,
            (int (*)(void *)) resurrect_dir,
            (void (*)(void *)) close_dir);
}

Lru *oid_init_fd_lru(void) {
    return lru_new(
            FD_CACHE_SIZE,
            (u32 (*)(const void *)) u64_hash,
            (int (*)(const void *, const void *)) u64_cmp,
            (int (*)(void *)) resurrect_oid_fd,
            (void (*)(void *)) close_oid_fd);
}

struct oid_fd *oid_add_oid_fd(u64 oid, int fd) {
    struct oid_fd *wrapper;
    u64 *key;

    assert(fd >= 0);
    wrapper = GC_NEW_ATOMIC(struct oid_fd);
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

int oid_reserve_block(u64 *oid, u32 *count) {
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
            if (mkdir(dir, OBJECT_DIR_MODE) < 0)
                return -1;
        } else if (!S_ISDIR(info.st_mode)) {
            return -1;
        }
    }

    return 0;
}

/* Read a directory of objects.  The argument should be a blank object that
 * is already indexed in the LRU and reserved. */
static void oid_read_dir(struct oid_dir *dir) {
    int i;
    const int size = 1 << BITS_PER_DIR_OBJECTS;
    char *dirname;
    DIR *dd;
    struct dirent *ent;
    List *path;

    dirname = objectroot;
    path = oid_to_path(dir->start);
    for (path = oid_to_path(dir->start); !null(path); path = cdr(path))
        dirname = concatname(dirname, (char *) car(path));

    dir->dirname = dirname;

    dir->filenames = GC_MALLOC(sizeof(char *) * size);
    assert(dir->filenames != NULL);
    for (i = 0; i < size; i++)
        result->filenames[i] = NULL;

    dd = opendir(dirname);
    if (dd == NULL)
        return;

    while ((ent = readdir(dd)) != NULL) {
        unsigned int id, mode;
        char *name, *group;
        if (parse_filename(ent->d_name, &id, &mode, &name, &group) == 0 &&
            id < size)
        {
            result->filenames[id] = stringcopy(ent->d_name);
        }
    }

    closedir(dd);
}

struct oid_dir *oid_dir_lookup(Worker *worker, u64 start) {
    struct oid_dir *result;

    worker_lock_acquire(LOCK_DIRECTORY);

    assert(start == oid_dir_findstart(start));

    result = lru_get(state->oid_dir_lru, &start);

    if (result == NULL) {
        /* create a new blank entry and reserve it */
        u64 *key = GC_NEW_ATOMIC(u64);
        assert(key != NULL);
        *key = start;

        result = GC_NEW(struct oid_dir);
        assert(result != NULL);
        result->wait = NULL;
        result->start = start;
        result->dirname = NULL;
        result->filenames = NULL;
        
        lru_add(state->oid_dir_lru, key, result);

        worker_reserve(worker, LOCK_DIRECTORY, result);
        worker_lock_release(LOCK_DIRECTORY);

        oid_read_dir(result);
    } else {
        worker_reserve(worker, LOCK_DIRECTORY, result);
        worker_lock_release(LOCK_DIRECTORY);
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

struct p9stat *oid_stat(Worker *worker, u64 oid) {
    /* get filename */
    u64 start = oid_dir_findstart(oid);
    struct oid_dir *dir;
    char *filename;
    struct stat info;
    struct p9stat *result;

    unsigned int id, mode;
    char *name, *group;

    if (    /* gather all the info we need */
            (dir = oid_dir_lookup(worker, start)) == NULL ||
            (filename = dir->filenames[oid - start]) == NULL ||
            parse_filename(filename, &id, &mode, &name, &group) < 0 ||
            id != oid - start ||
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
        struct oid_fd *wrapper = oid_get_open_fd(oid);
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
    u64 start = oid_dir_findstart(oid);
    struct oid_dir *dir;
    char *filename, *newfilename;
    char *pathname, *newpathname;

    unsigned int id;
    u32 mode;
    char *name, *group;

    if (    /* get the current info for the object */
            (dir = oid_dir_lookup(start)) == NULL ||
            (filename = dir->filenames[oid - start]) == NULL ||
            parse_filename(filename, &id, &mode, &name, &group) < 0 ||
            id != oid - start)
    {
        return -1;
    }
    pathname = concatname(dir->dirname, filename);

    /* make the requested changes */

    /* mtime */
    if (info->mtime != ~(u32) 0) {
        struct utimbuf buf;
        buf.actime = 0;
        buf.modtime = info->mtime;
        if (utime(pathname, &buf) < 0)
            return -1;
    }

    /* mode */
    if (info->mode != ~(u32) 0)
        mode = info->mode;

    /* gid */
    if (!emptystring(info->gid))
        group = info->gid;

    /* uid */
    if (!emptystring(info->uid))
        name = info->uid;

    /* extension */
    if (!emptystring(info->extension) &&
            (mode & (DMSYMLINK | DMDEVICE | DMNAMEDPIPE | DMSOCKET)))
    {
        struct oid_fd *wrapper = oid_get_open_fd(oid);
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
        if (lstat(pathname, &finfo) < 0)
            return -1;
        if (info->length != finfo.st_size) {
            if (truncate(pathname, info->length) < 0)
                return -1;
        }
    }

    newfilename = make_filename(oid, mode, name, group);

    /* see if there were any filename changes */
    if (!strcmp(filename, newfilename)) {
        newpathname = concatname(dir->dirname, newfilename);
        if (rename(pathname, newpathname) < 0)
            return -1;
        dir->filenames[oid - start] = newfilename;
    }

    return 0;
}

int oid_create(u64 oid, struct p9stat *info) {
    u64 start = oid_dir_findstart(oid);
    struct oid_dir *dir;
    struct utimbuf buf;
    char *filename;
    char *pathname;
    int fd;

    if (    /* find the place for this object and make sure it is new */
            (dir = oid_dir_lookup(start)) == NULL ||
            (filename = dir->filenames[oid - start]) != NULL)
    {
        return -1;
    }

    /* create the file and set filename-encoded stats */
    filename = make_filename(oid, info->mode, info->uid, info->gid);
    pathname = concatname(dir->dirname, filename);

    fd = creat(pathname, OBJECT_MODE);
    if (fd < 0)
        return -1;
    dir->filenames[oid - start] = filename;
    oid_add_oid_fd(oid, fd);

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

struct oid_fd *oid_get_open_fd(u64 oid) {
    struct oid_fd *wrapper;

    /* first check the cache */
    wrapper = lru_get(state->oid_fd_lru, &oid);

    /* open the file if necessary */
    if (wrapper == NULL) {
        u64 start = oid_dir_findstart(oid);
        struct oid_dir *dir;
        char *filename;
        int fd;
        if (    /* find the pathname and open the file */
                (dir = oid_dir_lookup(start)) == NULL ||
                (filename = dir->filenames[oid - start]) == NULL ||
                (fd = open(concatname(dir->dirname, filename), O_RDWR )) < 0)
        {
            return NULL;
        }
        wrapper = oid_add_oid_fd(oid, fd);
    }

    assert(wrapper->fd >= 0);

    wrapper->refcount++;

    return wrapper;
}

int oid_set_times(u64 oid, struct utimbuf *buf) {
    u64 start = oid_dir_findstart(oid);
    struct oid_dir *dir;
    char *filename;

    if ((dir = oid_dir_lookup(start)) == NULL ||
            (filename = dir->filenames[oid - start]) == NULL ||
            utime(concatname(dir->dirname, filename), buf) < 0)
    {
        return -1;
    }

    return 0;
}

int oid_clone(u64 oldoid, u64 newoid) {
    struct p9stat *info = oid_stat(oldoid);
    struct oid_fd *new_fd, *old_fd;
    void *buff = GC_MALLOC_ATOMIC(CLONE_BUFFER_SIZE);
    int count;
    struct utimbuf buf;

    assert(buff != NULL);

    if (oid_create(newoid, info) < 0)
        return -1;

    /* zero byte file? */
    if (info->length == 0LL)
        return 0;

    /* open both files and position at the start of the files */
    if ((old_fd = oid_get_open_fd(oldoid)) == NULL)
        return -1;
    if (lseek(old_fd->fd, 0, SEEK_SET) < 0 ||
            ((new_fd = oid_get_open_fd(newoid)) == NULL))
    {
        return -1;
    }
    if (lseek(new_fd->fd, 0, SEEK_SET) < 0) {
        return -1;
    }

    /* copy the file a chunk at a time */
    for (;;) {
        count = read(old_fd->fd, buff, CLONE_BUFFER_SIZE);
        if (count == 0)
            break;
        if (count < 0) {
            return -1;
        }
        if (write(new_fd->fd, buff, count) != count) {
            old_fd->refcount--;
            new_fd->refcount--;
            return -1;
        }
    }

    //old_fd->refcount--;
    //new_wrapper->refcount--;

    buf.actime = info->atime;
    buf.modtime = info->mtime;
    
    return oid_set_times(newoid, &buf);
}
