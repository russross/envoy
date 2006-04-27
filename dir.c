#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "object.h"
#include "envoy.h"
#include "worker.h"
#include "dir.h"
#include "claim.h"
#include "lease.h"

/* Directories are stored in a series of BLOCK_SIZE length blocks, with
 * the last block possibly truncated.  Each block is structured as follows:
 *
 * u16: end-of-data offset in this block
 *
 * followed by a series of entries:
 * u64: object id of this entry
 * u8: copy-on-write flag
 * string: u16 length, file name in utf-8, (not null terminated)
 */

/* given a single block of directory data, return a list of entries */
static List *dir_unpack_entries(u32 count, u8 *data) {
    int size = (int) count;
    int offset;
    u16 end;
    struct direntry *elt;
    List *result = NULL;

    if (count < sizeof(u16))
        return NULL;

    offset = 0;
    end = unpackU16(data, size, &offset);

    if (offset < 0 || end > size)
        return NULL;

    while (offset >= 0 && offset < end) {
        elt = GC_NEW(struct direntry);
        assert(elt != NULL);
        elt->offset = offset;
        elt->oid = unpackU64(data, size, &offset);
        elt->cow = unpackU8(data, size, &offset);
        elt->filename = unpackString(data, size, &offset);

        if (offset < 0)
            return NULL;
        result = cons(elt, result);
    }

    if (offset != end)
        return NULL;

    return reverse(result);
}

static u32 dir_pack_entries(List *entries, u8 *data) {
    int i = 0;
    int start = 0;

    /* reserve space for the end marker */
    packU16(data, &i, 0);

    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        packU64(data, &i, elt->oid);
        packU8(data, &i, elt->cow);
        packString(data, &i, elt->filename);
    }

    /* pack the end-of-data offset at the beginning of the block */
    packU16(data, &start, (u16) i);
    return (u32) i;
}

/* prepare a directory block for a clone by setting all copy-on-write flags */
void dir_clone(u32 count, u8 *data) {
    List *entries = dir_unpack_entries(count, data);

    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        if (!elt->cow)
            data[elt->offset + DIR_COW_OFFSET] = 1;
    }
}

static struct p9stat *dir_read_next(Worker *worker, Fid *fid,
        struct p9stat *dirinfo, struct dir_read_env *env)
{
    struct direntry *elt;
    char *childpath;

    /* do we have a cached result? */
    if (env->next != NULL) {
        struct p9stat *res = env->next;
        env->next = NULL;
        return res;
    }

    /* do we need to read a new block?  loop because blocks can be empty */
    while (null(env->entries)) {
        u32 bytesread;
        void *block;

        /* have we already read the last block? */
        if (env->offset >= dirinfo->length)
            return NULL;

        /* read a block */
        block = object_read(worker, fid->claim->oid, now(),
                env->offset, BLOCK_SIZE, &bytesread);
        env->offset += BLOCK_SIZE;

        /* decode the entries in this block */
        env->entries = dir_unpack_entries(bytesread, block);
    }

    /* get stats for the next entry */
    elt = car(env->entries);
    env->entries = cdr(env->entries);
    childpath = concatname(fid->claim->pathname, elt->filename);

    /* can we get stats locally? */
    if (lease_find_root(childpath)) {
        /* TODO: lock the lease in local case */
        return object_stat(worker, elt->oid, elt->filename);
    } else {
        Lease *lease = lease_find_remote(childpath);
        assert(lease != NULL);
        return remote_stat(worker, lease->addr, childpath);
    }
}

u32 dir_read(Worker *worker, Fid *fid, u32 size, u8 *data) {
    struct p9stat *dirinfo = object_stat(worker, fid->claim->oid,
            filename(fid->pathname));
    struct p9stat *info = NULL;
    u32 count = 0;

    /* are we starting from scratch? */
    if (fid->readdir_env == NULL) {
        fid->readdir_env = GC_NEW(struct dir_read_env);
        assert(fid->readdir_env != NULL);

        fid->readdir_env->next = NULL;
        fid->readdir_env->offset = 0;
        fid->readdir_env->entries = NULL;

        /* do we need to catch up (after a fid migration)? */
        if (fid->readdir_cookie > 0) {
            /* scan ahead in the directory to find our current position */
            u64 bytes = 0;

            while (bytes < fid->readdir_cookie) {
                info = dir_read_next(worker, fid, dirinfo, fid->readdir_env);

                /* are we already past the end somehow? */
                if (info == NULL)
                    return 0;

                bytes += statsize(info);
            }
        }
    }

    for (;;) {
        info = dir_read_next(worker, fid, dirinfo, fid->readdir_env);
        if (info == NULL)
            break;

        if (count + statsize(info) > size) {
            /* push this entry back into the stream */
            fid->readdir_env->next = info;
            break;
        }

        packStat(data, (int *) &count, info);
    }

    return count;
}

enum dir_iter_action {
    DIR_ABORT,
    DIR_STOP,
    DIR_CONTINUE,
};

/* prime the claim cache for all the entries in the list */
static void dir_prime_claim_cache(Claim *dir, List *entries) {
    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        char *pathname = concatname(dir->pathname, elt->filename);
        List *children;

        /* does this entry exit the lease? */
        if (lease_check_for_lease_change(dir->lease, pathname) !=
                NULL)
        {
            continue;
        }

        /* is it an active claim? */
        for (children = dir->children; !null(children);
                children = cdr(children))
        {
            Claim *claim = car(children);
            if (!strcmp(pathname, claim->pathname))
                goto next;
        }

        /* is it already in the cache? */
        if (lease_lookup_claim_from_cache(dir->lease, pathname) == NULL) {
            /* create a new claim and add it to the cache */
            Claim *claim = claim_new(dir, elt->filename,
                    fid_access_child(dir->access, elt->cow), elt->oid);
            lease_add_claim_to_cache(claim);
        }

        next:
        continue;
    }
}

static int dir_iter(Worker *worker, Claim *claim, struct p9stat *dirinfo,
        enum dir_iter_action (f)(void *env, List *in, List **out),
        void *env)
{
    int num;
    List *changes = NULL;
    int stop = 0;

    for (num = 0; (u64) num * BLOCK_SIZE < dirinfo->length; num++) {
        u32 count;
        void *data;
        List *pre = NULL;
        List *post = NULL;

        if ((u64) num * BLOCK_SIZE >= dirinfo->length) {
            /* we're past the end of the directory */
            stop = 1;
        } else {
            /* read a block */
            data = object_read(worker, claim->oid, now(),
                    (u64) num * BLOCK_SIZE, BLOCK_SIZE, &count);
            assert(data != NULL);
            pre = dir_unpack_entries(count, data);
        }

        /* add all local entries to the claim cache */
        dir_prime_claim_cache(claim, pre);

        /* process the files in this block */
        switch (f(env, pre, &post)) {
            case DIR_ABORT:
                return -1;
            case DIR_STOP:
                stop = 1;
                /* fall through */
            case DIR_CONTINUE:
                /* store any requested changes */
                if (!null(post))
                    changes = cons(cons((void *) num, post), changes);
                break;
            default:
                assert(0);
        }
    }

    /* make any requested changes */
    for ( ; !null(changes); changes = cdr(changes)) {
        int num = (int) caar(changes);
        List *entries = cdar(changes);
        void *data = GC_MALLOC_ATOMIC(BLOCK_SIZE);
        u32 count;
        u64 offset;

        assert(data != NULL);

        count = dir_pack_entries(entries, data);

        /* make sure the directory has room for the block */
        offset = BLOCK_SIZE * num;
        if (offset > dirinfo->length) {
            /* extend the block */
            dirinfo->mode = ~(u32) 0;
            dirinfo->atime = ~(u32) 0;
            dirinfo->mtime = ~(u32) 0;
            dirinfo->length = offset + count;
            dirinfo->name = NULL;
            dirinfo->uid = NULL;
            dirinfo->gid = NULL;
            dirinfo->muid = NULL;
            dirinfo->extension = NULL;
            dirinfo->n_uid = 0;
            dirinfo->n_gid = 0;
            dirinfo->n_muid = 0;

            object_wstat(worker, claim->oid, dirinfo);
        }

        /* write the block */
        assert(object_write(worker, claim->oid, now(),
                    offset, count, data) == count);
    }

    return 0;
}

struct dir_add_entry_env {
    struct direntry *newentry;
    int added;
};

static enum dir_iter_action dir_create_entry_iter(struct dir_add_entry_env *env,
        List *in, List **out)
{
    List *entries;
    u32 offset = sizeof(u16);

    /* scan to see if this file already exists and find the end of the block */
    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        if (!strcmp(elt->filename, env->newentry->filename))
            return DIR_ABORT;
        offset = elt->offset + DIR_END_OFFSET + strlen(elt->filename);
    }

    /* should we add it to this block? */
    if (!env->added &&
            strlen(env->newentry->filename) + DIR_END_OFFSET <= BLOCK_SIZE)
    {
        env->added = 1;
        *out = cons(env->newentry, in);
    }

    return DIR_CONTINUE;
}

u64 dir_create_entry(Worker *worker, Fid *fid, struct p9stat *dirinfo,
        char *name)
{
    struct dir_add_entry_env env;
    int result;

    env.added = 0;

    env.newentry = GC_NEW(struct direntry);
    assert(env.newentry != NULL);
    env.newentry->oid = object_reserve_oid(worker);
    env.newentry->cow = 0;
    env.newentry->filename = name;

    result = dir_iter(worker, fid->claim, dirinfo,
            (enum dir_iter_action (*)(void *, List *, List **))
            dir_create_entry_iter,
            &env);

    if (result < 0 || !env.added)
        return NOOID;
    return env.newentry->oid;
}

struct dir_remove_entry_env {
    char *name;
    int removed;
};

enum dir_iter_action dir_remove_entry_iter(struct dir_remove_entry_env *env,
        List *in, List **out)
{
    List *entries = in;
    List *prev = NULL;

    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        if (!strcmp(elt->filename, env->name)) {
            /* delete the entry */
            if (null(prev)) {
                *out = cdr(entries);
            } else {
                setcdr(prev, cdr(entries));
                *out = in;
            }
            env->removed = 1;

            return DIR_STOP;
        }
        prev = entries;
    }

    return DIR_CONTINUE;
}

int dir_remove_entry(Worker *worker, Fid *fid, struct p9stat *dirinfo,
        char *name)
{
    struct dir_remove_entry_env env;
    int result;

    env.removed = 0;
    env.name = name;

    result = dir_iter(worker, fid->claim, dirinfo,
            (enum dir_iter_action (*)(void *, List *, List **))
            dir_remove_entry_iter,
            &env);

    if (result < 0 || !env.removed)
        return -1;
    return 0;
}

struct dir_find_claim_env {
    char *pathname;
    Lease *lease;
    Claim *claim;
};

enum dir_iter_action dir_find_claim_iter(struct dir_find_claim_env *env,
        List *in, List **out)
{
    env->claim = lease_lookup_claim_from_cache(env->lease, env->pathname);
    if (env->claim == NULL)
        return DIR_CONTINUE;
    else
        return DIR_STOP;
}

Claim *dir_find_claim(Worker *worker, Claim *dir, char *name) {
    struct dir_find_claim_env env;
    struct p9stat *dirinfo = object_stat(worker, dir->oid,
            filename(dir->pathname));
    int result;

    env.pathname = concatname(dir->pathname, name);
    env.lease = dir->lease;
    env.claim = NULL;

    result = dir_iter(worker, dir, dirinfo,
            (enum dir_iter_action (*)(void *, List *, List **))
            dir_find_claim_iter,
            &env);

    if (result < 0)
        return NULL;

    return env.claim;
}
