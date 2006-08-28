#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "hashtable.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "object.h"
#include "remote.h"
#include "worker.h"
#include "lru.h"
#include "dir.h"
#include "claim.h"
#include "lease.h"

int dir_block_cmp(const struct dir_block *a, const struct dir_block *b) {
    if (a->oid != b->oid)
        return a->oid - b->oid;
    return a->blocknum - b->blocknum;
}

u32 dir_block_hash(const struct dir_block *block) {
    return generic_hash(&block->oid, sizeof(u64),
            generic_hash(&block->blocknum, sizeof(u32), 0));
}

void dir_block_cache_set(Lease *lease, u64 oid, u32 blocknum, List *entries) {
    struct dir_block *block = GC_NEW(struct dir_block);
    assert(block != NULL);
    assert(lease != NULL);

    block->lease = lease;
    block->oid = oid;
    block->blocknum = blocknum;
    block->entries = entries;

    assert(null(entries) || car(entries) != NULL);

    /* note: the lru_add must come first because it may clear an old value */
    lru_add(dir_cache, block, block);
    hash_set(lease->dir_cache, block, block);
}

struct dir_block *dir_block_cache_lookup(u64 oid, u32 blocknum) {
    struct dir_block block = {
        .lease = NULL,
        .oid = oid,
        .blocknum = blocknum,
        .entries = NULL
    };

    return lru_get(dir_cache, &block);
}

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
    int cowoffset;
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

        /* CoW flag is the high bit of the string length field */
        elt->cow = unpackU8(data, size, &offset);
        cowoffset = --offset;
        if (cowoffset >= 0)
            data[cowoffset] = elt->cow & 0x7f;
        elt->filename = unpackString(data, size, &offset);
        if (cowoffset >= 0)
            data[cowoffset] = elt->cow;
        elt->cow = (elt->cow & 0x80) ? 1 : 0;

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
    int cowi;
    int start = 0;
    List *prev = NULL;

    /* reserve space for the end marker */
    packU16(data, &i, 0);

    /* pack the entries and clean up the list (including setting offsets)
     * so this list can go in the cache */
    while (!null(entries)) {
        struct direntry *elt = car(entries);

        prev = entries;
        entries = cdr(entries);

        assert(i > 0);
        elt->offset = (u32) i;

        packU64(data, &i, elt->oid);
        cowi = i;
        packString(data, &i, elt->filename);
        if (elt->cow)
            data[cowi] |= 0x80;
    }

    /* pack the end-of-data offset at the beginning of the block */
    packU16(data, &start, (u16) i);
    return (u32) i;
}

/* prime the claim cache for all the entries in the list */
static void dir_prime_claim_cache(Worker *worker, Claim *dir, List *entries,
        char *targetname)
{
    enum path_type type;

    if (emptystring(targetname))
        return;

    type = get_admin_path_type(dir->pathname);

    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        char *pathname;
        enum claim_access access;
        Claim *claim;

        if (strcmp(targetname, elt->filename))
            continue;

        pathname = concatname(dir->pathname, elt->filename);

        /* does this file exit the lease? */
        if (lease_get_remote(pathname) != NULL)
            continue;

        /* is it already in the cache? */
        if ((claim = claim_lookup_from_cache(dir->lease, pathname)) != NULL) {
            reserve(worker, LOCK_CLAIM, claim);
            continue;
        }

        /* create a new claim and add it to the cache */
        access = fid_access_child(dir->access, elt->cow);
        if (type == PATH_ADMIN && ispositiveint(elt->filename))
            access = ACCESS_READONLY;

        /* create the entry, but don't hold onto it */
        claim = claim_new(dir, elt->filename, access, elt->oid);

        reserve(worker, LOCK_CLAIM, claim);
    }
}

/* prepare a directory block for a clone by setting all copy-on-write flags */
void dir_clone(u32 count, u8 *data) {
    List *entries = dir_unpack_entries(count, data);

    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        if (!elt->cow)
            data[elt->offset + DIR_COW_OFFSET] |= 0x80;
    }
}

static struct p9stat *dir_read_next(Worker *worker, Fid *fid,
        struct p9stat *dirinfo, struct dir_read_env *env)
{
    struct direntry *elt;
    char *childpath;
    Lease *remote;

    /* do we have a cached result? */
    if (env->next != NULL) {
        struct p9stat *res = env->next;
        env->next = NULL;
        return res;
    }

    /* do we need to read a new block?  loop because blocks can be empty */
    while (null(env->entries)) {
        u32 bytesread;
        u8 *block;
        void *raw;
        struct dir_block *elt;

        /* have we already read the last block? */
        if (env->offset >= dirinfo->length)
            return NULL;

        /* read a block */
        elt = dir_block_cache_lookup(fid->claim->oid, env->offset / BLOCK_SIZE);
        if (elt == NULL) {
            /* read a block and decode the entries */
            raw = object_read(worker, fid->claim->oid, now(),
                    env->offset, BLOCK_SIZE, &bytesread, &block);
            env->entries = dir_unpack_entries(bytesread, block);
            dir_block_cache_set(fid->claim->lease, fid->claim->oid,
                    env->offset / BLOCK_SIZE, env->entries);
            raw_delete(raw);
        } else {
            /* get the decoded entries from the cache */
            env->entries = elt->entries;
        }

        env->offset += BLOCK_SIZE;

        /* cache these entries */
        /*dir_prime_claim_cache(worker, fid->claim, env->entries, NULL);*/
    }

    /* get stats for the next entry */
    elt = car(env->entries);
    env->entries = cdr(env->entries);
    childpath = concatname(fid->claim->pathname, elt->filename);

    /* can we get stats locally? */
    if ((remote = lease_get_remote(childpath)) == NULL) {
        Claim *claim;
        struct p9stat *info;

        /* make sure this can be found in cache, otherwise we get attempts
         * to search the directory and deadlock results */
        dir_prime_claim_cache(worker, fid->claim, cons(elt, NULL),
                elt->filename);
        claim = claim_get_child(worker, fid->claim, elt->filename);

        if (claim->info == NULL)
            claim->info = object_stat(worker, claim->oid, elt->filename);
        info = claim->info;

        /* we don't want to hold locks on a bunch of claims */
        release(worker, LOCK_CLAIM, claim);
        claim_release(claim);

        return info;
    } else {
        return remote_stat(worker, remote->addr, childpath);
    }
}

u32 dir_read(Worker *worker, Fid *fid, u32 size, u8 *data) {
    struct p9stat *dirinfo;
    struct p9stat *info;
    u32 count = 0;

    if (fid->claim->info == NULL) {
        fid->claim->info =
            object_stat(worker, fid->claim->oid, filename(fid->pathname));
    }
    dirinfo = fid->claim->info;

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

                bytes += statnsize(info);
            }
        }
    }

    for (;;) {
        info = dir_read_next(worker, fid, dirinfo, fid->readdir_env);
        if (info == NULL)
            break;

        if (count + statnsize(info) > size) {
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

static int dir_iter(Worker *worker, Claim *claim, char *targetname,
        enum dir_iter_action (f)(void *env, List *in, List **out, int extra),
        void *env)
{
    int num;
    List *changes = NULL;
    int stop = 0;
    struct p9stat *dirinfo;

    if (claim->info == NULL) {
        claim->info =
            object_stat(worker, claim->oid, filename(claim->pathname));
    }
    dirinfo = claim->info;

    for (num = 0; !stop; num++) {
        u32 count;
        u8 *data;
        List *pre = NULL;
        List *post = (List *) 1;
        void *raw;

        if ((u64) num * BLOCK_SIZE >= dirinfo->length) {
            /* we're past the end of the directory */
            stop = 1;
        } else {
            /* read a block */
            struct dir_block *elt = dir_block_cache_lookup(claim->oid, num);
            if (elt == NULL) {
                raw = object_read(worker, claim->oid, now(),
                        (u64) num * BLOCK_SIZE, BLOCK_SIZE, &count, &data);
                assert(data != NULL);
                pre = dir_unpack_entries(count, data);
                dir_block_cache_set(claim->lease, claim->oid, num, pre);
                raw_delete(raw);
            } else {
                pre = elt->entries;
            }
        }

        /* add target entry (if any) to the claim cache */
        dir_prime_claim_cache(worker, claim, pre, targetname);

        /* process the files in this block */
        switch (f(env, pre, &post, stop)) {
            case DIR_ABORT:
                return -1;
            case DIR_STOP:
                stop = 1;
                /* fall through */
            case DIR_CONTINUE:
                /* store any requested changes */
                if (post != (List *) 1)
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
        void *raw = raw_new();
        u8 *data = raw + TSWRITE_DATA_OFFSET;
        u32 count;
        u64 offset;

        count = dir_pack_entries(entries, data);
        dir_block_cache_set(claim->lease, claim->oid, num, entries);

        /* make sure the directory has room for the block */
        offset = BLOCK_SIZE * num;
        if (offset > dirinfo->length) {
            /* extend the block */
            struct p9stat *delta = p9stat_new();
            delta->length = offset + count;
            object_wstat(worker, claim->oid, delta);
        }

        /* write the block */
        assert(object_write(worker, claim->oid, now(),
                    offset, count, data, raw) == count);
        claim->info = NULL;
    }

    return 0;
}

struct dir_create_entry_env {
    struct direntry *newentry;
    int added;
};

static enum dir_iter_action dir_create_entry_iter(
        struct dir_create_entry_env *env, List *in, List **out, int extra)
{
    List *entries = in;
    u32 offset = sizeof(u16);

    /* scan to see if this file already exists and find the end of the block */
    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        if (!strcmp(elt->filename, env->newentry->filename))
            return DIR_ABORT;
        offset = elt->offset + DIR_END_OFFSET + strlen(elt->filename);
    }

    /* should we add it to this block? */
    if (!env->added && BLOCK_SIZE >=
            offset + strlen(env->newentry->filename) + DIR_END_OFFSET)
    {
        env->added = 1;
        *out = cons(env->newentry, in);
    }

    return DIR_CONTINUE;
}

int dir_create_entry(Worker *worker, Claim *dir, char *name, u64 oid, int cow) {
    struct dir_create_entry_env env;
    int result;

    env.added = 0;

    env.newentry = GC_NEW(struct direntry);
    assert(env.newentry != NULL);
    env.newentry->oid = oid;
    env.newentry->cow = cow;
    env.newentry->filename = name;

    result = dir_iter(worker, dir, NULL,
            (enum dir_iter_action (*)(void *, List *, List **, int))
            dir_create_entry_iter,
            &env);

    if (result < 0 || !env.added)
        return -1;
    return 0;
}

static enum dir_iter_action dir_remove_entry_iter(
        char *env, List *in, List **out, int extra)
{
    List *entries = in;

    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        if (!strcmp(elt->filename, env)) {
            /* delete the entry */
            *out = remove_elt(in, elt);

            return DIR_STOP;
        }
    }

    /* end of directory and we've done nothing? */
    if (extra)
        return DIR_ABORT;

    return DIR_CONTINUE;
}

int dir_remove_entry(Worker *worker, Claim *dir, char *name) {
    return dir_iter(worker, dir, NULL,
            (enum dir_iter_action (*)(void *, List *, List **, int))
            dir_remove_entry_iter,
            name);
}

struct dir_find_claim_env {
    char *pathname;
    Lease *lease;
    Claim *claim;
};

static enum dir_iter_action dir_find_claim_iter(
        struct dir_find_claim_env *env, List *in, List **out, int extra)
{
    /* dir iter adds it to the cache for us, so all we have to do is
     * check if our target has arrived in the cache yet */
    env->claim = claim_lookup_from_cache(env->lease, env->pathname);
    if (env->claim == NULL)
        return DIR_CONTINUE;
    else
        return DIR_STOP;
}

Claim *dir_find_claim(Worker *worker, Claim *dir, char *name) {
    struct dir_find_claim_env env;
    int result;

    env.pathname = concatname(dir->pathname, name);
    env.lease = dir->lease;
    env.claim = NULL;

    result = dir_iter(worker, dir, name,
            (enum dir_iter_action (*)(void *, List *, List **, int))
            dir_find_claim_iter,
            &env);

    if (result < 0)
        return NULL;

    return env.claim;
}

struct dir_is_empty_env {
    int isempty;
};

static enum dir_iter_action dir_is_empty_iter(
        struct dir_is_empty_env *env, List *in, List **out, int extra)
{
    if (!null(in)) {
        env->isempty = 0;
        return DIR_STOP;
    }

    return DIR_CONTINUE;
}

int dir_is_empty(Worker *worker, Claim *dir) {
    struct dir_is_empty_env env;

    env.isempty = 1;

    dir_iter(worker, dir, NULL,
            (enum dir_iter_action (*)(void *, List *, List **, int))
            dir_is_empty_iter,
            &env);

    return env.isempty;
}

struct dir_rename_env {
    char *oldname;
    struct direntry *newentry;
    int removed;
    int added;
};

static enum dir_iter_action dir_rename_iter(
        struct dir_rename_env *env, List *in, List **out, int extra)
{
    List *entries = in;
    List *changed = in;
    u32 offset = sizeof(u16);
    u32 deleted_offset = 0;

    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        if (!strcmp(elt->filename, env->newentry->filename)) {
            /* the new name already exists */
            changed = remove_elt(changed, elt);
            *out = changed;

            /* note how much space we opened up */
            deleted_offset += DIR_END_OFFSET + strlen(elt->filename);
        } else if (!strcmp(elt->filename, env->oldname)) {
            /* delete the old entry */
            changed = remove_elt(changed, elt);
            *out = changed;

            /* note how much space we opened up */
            deleted_offset += DIR_END_OFFSET + strlen(elt->filename);

            /* copy over the other data */
            env->newentry->oid = elt->oid;
            env->newentry->cow = elt->cow;
            env->removed = 1;
        } else {
            offset = elt->offset + DIR_END_OFFSET + strlen(elt->filename) -
                deleted_offset;
        }
    }

    /* should we add it to this block? */
    if (!env->added && BLOCK_SIZE >=
            offset + strlen(env->newentry->filename) + DIR_END_OFFSET)
    {
        env->added = 1;
        /* note: newentry may be updated later (when we find oid and cow) but
         * that's okay--the block pack doesn't happen until the end */
        changed = cons(env->newentry, changed);
        *out = changed;
    }

    if (env->removed && env->added)
        return DIR_STOP;
    else if (extra)
        return DIR_ABORT;
    else
        return DIR_CONTINUE;
}

int dir_rename(Worker *worker, Claim *dir, char *oldname, char *newname) {
    struct dir_rename_env env;

    env.oldname = oldname;

    env.newentry = GC_NEW(struct direntry);
    assert(env.newentry != NULL);
    env.newentry->oid = NOOID;
    env.newentry->cow = -1;
    env.newentry->filename = newname;

    env.added = 0;
    env.removed = 0;

    return dir_iter(worker, dir, NULL,
            (enum dir_iter_action (*)(void *, List *, List **, int))
            dir_rename_iter,
            &env);
}

struct dir_change_oid_env {
    char *name;
    u64 oid;
    int cow;
    u64 oldoid;
};

static enum dir_iter_action dir_change_oid_iter(
        struct dir_change_oid_env *env, List *in, List **out, int extra)
{
    List *entries = in;

    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        if (!strcmp(elt->filename, env->name)) {
            /* update the entry */
            env->oldoid = elt->oid;
            elt->oid = env->oid;
            elt->cow = env->cow;
            *out = in;

            return DIR_STOP;
        }
    }

    /* end of direntry and we've done nothing? */
    if (extra)
        return DIR_ABORT;

    return DIR_CONTINUE;
}

u64 dir_change_oid(Worker *worker, Claim *dir, char *name,
        u64 oid, int cow)
{
    struct dir_change_oid_env env;

    env.name = name;
    env.oid = oid;
    env.cow = cow;

    if (dir_iter(worker, dir, NULL,
            (enum dir_iter_action (*)(void *, List *, List **, int))
            dir_change_oid_iter,
            &env) < 0)
    {
        return NOOID;
    } else {
        return env.oldoid;
    }
}
