#include "9p.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <gc/gc.h>

/*
 * constructors
 */

struct message *message_new(void) {
    struct message *msg = GC_NEW(struct message);
    assert(msg != NULL);
    msg->raw = NULL;
    msg->tag = ALLOCTAG;
    return msg;
}

struct p9stat *p9stat_new(void) {
    struct p9stat *info = GC_NEW(struct p9stat);
    assert(info != NULL);

    /* set all fields to empty values */
    info->type = ~(u16) 0;
    info->dev = ~(u32) 0;
    info->qid.type = ~(u8) 0;
    info->qid.version = ~(u32) 0;
    info->qid.path = ~(u64) 0;
    info->mode = ~(u32) 0;
    info->atime = ~(u32) 0;
    info->mtime = ~(u32) 0;
    info->length = ~(u64) 0;
    info->name = NULL;
    info->uid = NULL;
    info->gid = NULL;
    info->muid = NULL;
    info->extension = NULL;
    info->n_uid = ~(u32) 0;
    info->n_gid = ~(u32) 0;
    info->n_muid = ~(u32) 0;

    return info;
}

/*
 * size helpers
 */

int statnsize(struct p9stat *elt) {
    return
        2 +     /* size[2] */
        2 +     /* type[2] */
        4 +     /* dev[4] */
        13 +    /* qid[13] */
        4 +     /* mode[4] */
        4 +     /* atime[4] */
        4 +     /* mtime[4] */
        8 +     /* length[8] */
        10 +    /* string lengths: name, uid, gid, muid, extension */
        4 +     /* n_uid[4] */
        4 +     /* n_gid[4] */
        4 +     /* n_muid[4] */
        safe_strlen(elt->name) +
        safe_strlen(elt->uid) +
        safe_strlen(elt->gid) +
        safe_strlen(elt->muid) +
        safe_strlen(elt->extension);
}

int stringlistsize(u16 len, char **elt) {
    int i;
    int size = len * 2;
    for (i = 0; i < len; i++)
        size += safe_strlen(elt[i]);
    return size;
}

int leaserecordsize(struct leaserecord *elt) {
    return
        2 +     /* length */
        2 +     /* pathname[s] length */
        1 +     /* readonly[1] */
        8 +     /* oid[8] */
        4 +     /* address[4] */
        2 +     /* port[2] */
        safe_strlen(elt->pathname);
}

int leaserecordlistsize(u16 len, struct leaserecord **elt) {
    int i;
    int size = 0;
    for (i = 0; i < len; i++)
        size += leaserecordsize(elt[i]);
    return size;
}

int fidrecordsize(struct fidrecord *elt) {
    return
        2 +     /* length */
        4 +     /* fid[4] */
        2 +     /* pathname[s] length */
        2 +     /* user[s] length */
        1 +     /* status[1] */
        4 +     /* omode[4] */
        8 +     /* readdir_cookie[8] */
        4 +     /* address[4] */
        2 +     /* port[2] */
        safe_strlen(elt->pathname) +
        safe_strlen(elt->user);
}

int fidrecordlistsize(u16 len, struct fidrecord **elt) {
    int i;
    int size = 0;
    for (i = 0; i < len; i++)
        size += fidrecordsize(elt[i]);
    return size;
}

/*
 * unpack helpers
 */

u8 unpackU8(u8 *raw, int size, int *i) {
    if (*i < 0) return (u8) 0;
    *i += 1;
    if (*i > size) {
        *i = -1;
        return (u8) 0;
    }
    return raw[*i - 1];
}

u16 unpackU16(u8 *raw, int size, int *i) {
    if (*i < 0) return (u16) 0;
    *i += 2;
    if (*i > size) {
        *i = -1;
        return (u16) 0;
    }
    return  (u16) raw[*i - 2] |
           ((u16) raw[*i - 1] << 8);
}

u32 unpackU32(u8 *raw, int size, int *i) {
    if (*i < 0) return (u32) 0;
    *i += 4;
    if (*i > size) {
        *i = -1;
        return (u32) 0;
    }
    return  (u32) raw[*i - 4] |
           ((u32) raw[*i - 3] << 8) |
           ((u32) raw[*i - 2] << 16) |
           ((u32) raw[*i - 1] << 24);
}

u64 unpackU64(u8 *raw, int size, int *i) {
    if (*i < 0) return (u64) 0;
    *i += 8;
    if (*i > size) {
        *i = -1;
        return (u64) 0;
    }
    return  (u64) raw[*i - 8] |
           ((u64) raw[*i - 7] << 8) |
           ((u64) raw[*i - 6] << 16) |
           ((u64) raw[*i - 5] << 24) |
           ((u64) raw[*i - 4] << 32) |
           ((u64) raw[*i - 3] << 40) |
           ((u64) raw[*i - 2] << 48) |
           ((u64) raw[*i - 1] << 56);
}

u32 *unpackU32list(u8 *raw, int size, int *i, u16 *n) {
    int x;
    u32 *elt = NULL;
    *n = unpackU16(raw, size, i);
    if (*i < 0) return NULL;
    if (*n == 0)
        return NULL;
    if (*n > MAXFELEM || (elt = GC_MALLOC_ATOMIC(sizeof(u32) * *n)) == NULL) {
        *i = -1;
        return NULL;
    }
    for (x = 0; x < *n; x++) {
        elt[x] = unpackU32(raw, size, i);
        if (*i < 0)
            return NULL;
    }
    return elt;
}

u64 *unpackU64list(u8 *raw, int size, int *i, u16 *n) {
    int x;
    u64 *elt = NULL;
    *n = unpackU16(raw, size, i);
    if (*i < 0) return NULL;
    if (*n == 0)
        return NULL;
    if (*n > MAXFELEM || (elt = GC_MALLOC_ATOMIC(sizeof(u64) * *n)) == NULL) {
        *i = -1;
        return NULL;
    }
    for (x = 0; x < *n; x++) {
        elt[x] = unpackU64(raw, size, i);
        if (*i < 0)
            return NULL;
    }
    return elt;
}

u8 *unpackData(u8 *raw, int size, int *i, u32 *len) {
    /* u8 *d = NULL; */
    *len = unpackU32(raw, size, i) & 0x00ffffff;
    if (*i < 0) return NULL;
    *i += *len;
    if (*len == 0)
      return NULL;
    return raw + *i - *len;
/*
    if (*i > size || (d = GC_MALLOC_ATOMIC(*len)) == NULL) {
        *i = -1;
        return NULL;
    }
    memcpy(d, raw + *i - *len, *len);
    return d;
*/
}

char *unpackString(u8 *raw, int size, int *i) {
    char *s = NULL;
    int len;

    /* workaround for bug in kernel module: accept a missing string at the end
     * of a record as an empty string */
    if (*i == size)
        return NULL;
    len = unpackU16(raw, size, i);
    if (*i < 0) return NULL;
    *i += len;
    if (len == 0)
        return NULL;
    if (*i > size || (s = GC_MALLOC_ATOMIC(len + 1)) == NULL) {
        *i = -1;
        return NULL;
    }
    memcpy(s, raw + *i - len, len);
    s[len] = 0;
    return s;
}

char **unpackStringlist(u8 *raw, int size, int *i, u16 *n) {
    int x;
    char **elt = NULL;
    *n = unpackU16(raw, size, i);
    if (*i < 0) return NULL;
    if (*n == 0)
        return NULL;
    if (*n > MAXWELEM || (elt = GC_MALLOC(sizeof(char *) * *n)) == NULL) {
        *i = -1;
        return NULL;
    }
    for (x = 0; x < *n; x++) {
        elt[x] = unpackString(raw, size, i);
        if (*i < 0)
            return NULL;
    }
    return elt;
}

struct qid unpackQid(u8 *raw, int size, int *i) {
   struct qid qid;
   qid.type = unpackU8(raw, size, i);
   qid.version = unpackU32(raw, size, i);
   qid.path = unpackU64(raw, size, i);
   return qid;
}

struct qid *unpackQidlist(u8 *raw, int size, int *i, u16 *n) {
    int x;
    struct qid *elt = NULL;
    *n = unpackU16(raw, size, i);
    if (*i < 0) return NULL;
    if (*n == 0)
        return NULL;
    if (*n > MAXWELEM ||
            (elt = GC_MALLOC_ATOMIC(sizeof(struct qid) * *n)) == NULL)
    {
        *i = -1;
        return NULL;
    }
    for (x = 0; x < *n; x++) {
        elt[x] = unpackQid(raw, size, i);
        if (*i < 0)
            return NULL;
    }
    return elt;
}

struct p9stat *unpackStat(u8 *raw, int size, int *i) {
    u16 length;
    int starti = *i;
    struct p9stat *stat;
    if (*i < 0) return NULL;
    if ((stat = GC_NEW(struct p9stat)) == NULL) {
        *i = -1;
        return NULL;
    }
    length = unpackU16(raw, size, i);
    stat->type = unpackU16(raw, size, i);
    stat->dev = unpackU32(raw, size, i);
    stat->qid = unpackQid(raw, size, i);
    stat->mode = unpackU32(raw, size, i);
    stat->atime = unpackU32(raw, size, i);
    stat->mtime = unpackU32(raw, size, i);
    stat->length = unpackU64(raw, size, i);
    stat->name = unpackString(raw, size, i);
    stat->uid = unpackString(raw, size, i);
    stat->gid = unpackString(raw, size, i);
    stat->muid = unpackString(raw, size, i);
    stat->extension = unpackString(raw, size, i);
    stat->n_uid = unpackU32(raw, size, i);
    stat->n_gid = unpackU32(raw, size, i);
    stat->n_muid = unpackU32(raw, size, i);
    if (*i != length + starti + sizeof(u16))
        *i = -1;
    if (*i < 0)
        return NULL;
    return stat;
}

struct p9stat *unpackStatn(u8 *raw, int size, int *i) {
    u16 n = unpackU16(raw, size, i);
    int peek = *i;
    if (*i < 0 || unpackU16(raw, size, &peek) != n - 2 || peek < 0)
        return NULL;
    return unpackStat(raw, size, i);
}

struct leaserecord *unpackLeaserecord(u8 *raw, int size, int *i) {
    u16 length;
    int starti = *i;
    struct leaserecord *elt;
    if (*i < 0) return NULL;
    if ((elt = GC_NEW(struct leaserecord)) == NULL) {
        *i = -1;
        return NULL;
    }
    length = unpackU16(raw, size, i);
    elt->pathname = unpackString(raw, size, i);
    elt->readonly = unpackU8(raw, size, i);
    elt->oid = unpackU64(raw, size, i);
    elt->address = unpackU32(raw, size, i);
    elt->port = unpackU16(raw, size, i);
    if (*i != length + starti + sizeof(u16))
        *i = -1;
    if (*i < 0)
        return NULL;
    return elt;
}

struct leaserecord **unpackLeaserecordlist(u8 *raw, int size, int *i, u16 *n) {
    int x;
    struct leaserecord **elt = NULL;
    *n = unpackU16(raw, size, i);
    if (*i < 0) return NULL;
    if (*n == 0)
        return NULL;
    if ((elt = GC_MALLOC(sizeof(struct leaserecord *) * *n)) == NULL) {
        *i = -1;
        return NULL;
    }
    for (x = 0; x < *n; x++) {
        elt[x] = unpackLeaserecord(raw, size, i);
        if (*i < 0)
            return NULL;
    }
    return elt;
}

struct fidrecord *unpackFidrecord(u8 *raw, int size, int *i) {
    u16 length;
    int starti = *i;
    struct fidrecord *elt;
    if (*i < 0) return NULL;
    if ((elt = GC_NEW(struct fidrecord)) == NULL) {
        *i = -1;
        return NULL;
    }
    length = unpackU16(raw, size, i);
    elt->fid = unpackU32(raw, size, i);
    elt->pathname = unpackString(raw, size, i);
    elt->user = unpackString(raw, size, i);
    elt->status = unpackU8(raw, size, i);
    elt->omode = unpackU32(raw, size, i);
    elt->readdir_cookie = unpackU64(raw, size, i);
    elt->address = unpackU32(raw, size, i);
    elt->port = unpackU16(raw, size, i);
    if (*i != length + starti + sizeof(u16))
        *i = -1;
    if (*i < 0)
        return NULL;
    return elt;
}

struct fidrecord **unpackFidrecordlist(u8 *raw, int size, int *i, u16 *n) {
    int x;
    struct fidrecord **elt = NULL;
    *n = unpackU16(raw, size, i);
    if (*i < 0) return NULL;
    if (*n == 0)
        return NULL;
    if ((elt = GC_MALLOC(sizeof(struct fidrecord *) * *n)) == NULL) {
        *i = -1;
        return NULL;
    }
    for (x = 0; x < *n; x++) {
        elt[x] = unpackFidrecord(raw, size, i);
        if (*i < 0)
            return NULL;
    }
    return elt;
}

/*
 * pack helpers
 */

void packU8(u8 *raw, int *i, u8 elt) {
    raw[(*i)++] = elt;
}

void packU16(u8 *raw, int *i, u16 elt) {
    raw[(*i)++] = (u8) ( elt       & 0xff);
    raw[(*i)++] = (u8) ((elt >> 8) & 0xff);
}

void packU32(u8 *raw, int *i, u32 elt) {
    raw[(*i)++] = (u8) ( elt        & 0xff);
    raw[(*i)++] = (u8) ((elt >> 8)  & 0xff);
    raw[(*i)++] = (u8) ((elt >> 16) & 0xff);
    raw[(*i)++] = (u8) ((elt >> 24) & 0xff);
}

void packU64(u8 *raw, int *i, u64 elt) {
    raw[(*i)++] = (u8) ( elt        & 0xff);
    raw[(*i)++] = (u8) ((elt >> 8)  & 0xff);
    raw[(*i)++] = (u8) ((elt >> 16) & 0xff);
    raw[(*i)++] = (u8) ((elt >> 24) & 0xff);
    raw[(*i)++] = (u8) ((elt >> 32) & 0xff);
    raw[(*i)++] = (u8) ((elt >> 40) & 0xff);
    raw[(*i)++] = (u8) ((elt >> 48) & 0xff);
    raw[(*i)++] = (u8) ((elt >> 56) & 0xff);
}

void packU32list(u8 *raw, int *i, u16 len, u32 *elt) {
    int x;
    packU16(raw, i, len);
    for (x = 0; x < len; x++)
        packU32(raw, i, elt[x]);
}

void packU64list(u8 *raw, int *i, u16 len, u64 *elt) {
    int x;
    packU16(raw, i, len);
    for (x = 0; x < len; x++)
        packU64(raw, i, elt[x]);
}

void packData(u8 *raw, int *i, u32 len, u8 *elt) {
    packU32(raw, i, len);
    /*
    if (len > 0)
        memcpy(raw + *i, elt, len);
    */
    if (len > 0)
        assert(elt == raw + *i);
    *i += len;
}

void packString(u8 *raw, int *i, char *elt) {
    int len = safe_strlen(elt);
    packU16(raw, i, (u16) len);
    if (len > 0)
        memcpy(raw + *i, elt, len);
    *i += len;
}

void packStringlist(u8 *raw, int *i, u16 len, char **elt) {
    int x;
    packU16(raw, i, len);
    for (x = 0; x < len; x++)
        packString(raw, i, elt[x]);
}

void packQid(u8 *raw, int *i, struct qid elt) {
    packU8(raw, i, elt.type);
    packU32(raw, i, elt.version);
    packU64(raw, i, elt.path);
}

void packQidlist(u8 *raw, int *i, u16 len, struct qid *elt) {
    int x;
    packU16(raw, i, len);
    for (x = 0; x < len; x++)
        packQid(raw, i, elt[x]);
}

void packStat(u8 *raw, int *i, struct p9stat *elt) {
    u16 size = (u16) statnsize(elt) - sizeof(u16);
    packU16(raw, i, size);
    packU16(raw, i, elt->type);
    packU32(raw, i, elt->dev);
    packQid(raw, i, elt->qid);
    packU32(raw, i, elt->mode);
    packU32(raw, i, elt->atime);
    packU32(raw, i, elt->mtime);
    packU64(raw, i, elt->length);
    packString(raw, i, elt->name);
    packString(raw, i, elt->uid);
    packString(raw, i, elt->gid);
    packString(raw, i, elt->muid);
    packString(raw, i, elt->extension);
    packU32(raw, i, elt->n_uid);
    packU32(raw, i, elt->n_gid);
    packU32(raw, i, elt->n_muid);
}

void packStatn(u8 *raw, int *i, struct p9stat *elt) {
    int start = *i;
    packU16(raw, i, 0);
    packStat(raw, i, elt);
    packU16(raw, &start, (u16) (*i - start - sizeof(u16)));
}

void packLeaserecord(u8 *raw, int *i, struct leaserecord *elt) {
    u16 size = (u16) leaserecordsize(elt) - 2;
    packU16(raw, i, size);
    packString(raw, i, elt->pathname);
    packU8(raw, i, elt->readonly);
    packU64(raw, i, elt->oid);
    packU32(raw, i, elt->address);
    packU16(raw, i, elt->port);
}

void packLeaserecordlist(u8 *raw, int *i, u16 len, struct leaserecord **elt) {
    int x;
    packU16(raw, i, len);
    for (x = 0; x < len; x++)
        packLeaserecord(raw, i, elt[x]);
}

void packFidrecord(u8 *raw, int *i, struct fidrecord *elt) {
    u16 size = (u16) fidrecordsize(elt) - 2;
    packU16(raw, i, size);
    packU32(raw, i, elt->fid);
    packString(raw, i, elt->pathname);
    packString(raw, i, elt->user);
    packU8(raw, i, elt->status);
    packU32(raw, i, elt->omode);
    packU64(raw, i, elt->readdir_cookie);
    packU32(raw, i, elt->address);
    packU16(raw, i, elt->port);
}

void packFidrecordlist(u8 *raw, int *i, u16 len, struct fidrecord **elt) {
    int x;
    packU16(raw, i, len);
    for (x = 0; x < len; x++)
        packFidrecord(raw, i, elt[x]);
}

/*
 * printing helpers
 */

void dumpBytes(FILE *fp, char *prefix, u8 *buff, int size) {
    int i;
    char ch[18];
    ch[17] = 0;
    ch[8] = ' ';

    for (i = 0; i < size; i++) {
        fprintf(fp, "%s", i % 16 == 0 ? prefix : i % 8 == 0 ? " " : "");
        fprintf(fp, "%02x ", (u32) buff[i]);
        if ((buff[i] & 0x7f) < 0x20)
            ch[i % 16 + (i % 16) / 8] = '.';
        else
            ch[i % 16 + (i % 16) / 8] = buff[i];
        if (i + 1 == size) {
            int j;
            for (j = i % 16 + 1; j < 16; j++) {
                fprintf(fp, "   %s", j % 8 == 0 ? " " : "");
                ch[j + j / 8] = ' ';
            }
        }
        if (i % 16 == 15 || i + 1 == size)
            fprintf(fp, "[%s]\n", ch);
    }
}

void dumpStat(FILE *fp, char *prefix, struct p9stat *stat) {
    char buff[80];
    fprintf(fp, "%sname[%s] length[%lld]\n", prefix, stat->name, stat->length);
    fprintf(fp, "%suid[%s] gid[%s] muid[%s]\n", prefix, stat->uid, stat->gid,
            stat->muid);
    fprintf(fp, "%sn_uid[%d] n_gid[%d] n_muid[%d]\n", prefix, stat->n_uid,
            stat->n_gid, stat->n_muid);
    ctime_r((const time_t *) &stat->atime, buff);
    if (buff[strlen(buff) - 1] == '\n')
        buff[strlen(buff) - 1] = 0;
    fprintf(fp, "%satime[$%x] (%s)\n", prefix, stat->atime, buff);
    ctime_r((const time_t *) &stat->mtime, buff);
    if (buff[strlen(buff) - 1] == '\n')
        buff[strlen(buff) - 1] = 0;
    fprintf(fp, "%smtime[$%x] (%s)\n", prefix, stat->mtime, buff);
    fprintf(fp, "%smode[%04o] type[$%x] dev[$%x]\n", prefix, stat->mode,
            (u32) stat->type, stat->dev);
    fprintf(fp, "%sqid: { type[$%x] version[$%x] path[$%llx] }\n", prefix,
            (u32) stat->qid.type, stat->qid.version, stat->qid.path);
    fprintf(fp, "%sextension[%s]\n", prefix, stat->extension);
}

void dumpData(FILE *fp, char *prefix, u8 *data, int size) {
    int i = 0, count = 0, n;
    struct p9stat *stat;
    char *pre;

    while (i >= 0 && i < size) {
        count++;
        unpackStat(data, size, &i);
    }
    if (DEBUG_VERBOSE) {
        if (i < 0)
            dumpBytes(fp, prefix, data, size);
        else {
            i = 0;
            pre = GC_MALLOC_ATOMIC(strlen(prefix) + 5);
            strcpy(pre, prefix);
            strcat(pre, "    ");
            for (n = 0; n < count; n++) {
                stat = unpackStat(data, size, &i);
                fprintf(fp, "%s%2d:\n", prefix, n);
                dumpStat(fp, pre, stat);
            }
        }
    } else {
        if (i < 0 || count == 0) {
            fprintf(fp, "%s[%d bytes of data]\n", prefix, size);
        } else {
            fprintf(fp, "%s[%d stat records/%d bytes of data]\n", prefix, count,
                    size);
        }
    }
}

void dumpLeaserecord(FILE *fp, char *prefix, struct leaserecord *elt) {
    fprintf(fp, "[%s,r%c,$%llx,%u.%u.%u.%u:%u]", elt->pathname,
            elt->readonly ? 'o' : 'w', elt->oid,
            (u32) (elt->address >> 24) & 0xff,
            (u32) (elt->address >> 16) & 0xff,
            (u32) (elt->address >>  8) & 0xff,
            (u32)  elt->address        & 0xff,
            (u32) elt->port);
}

void dumpFidrecord(FILE *fp, char *prefix, struct fidrecord *elt) {
    fprintf(fp, "{ fid[%u] pathname[%s] user[%s]\n", elt->fid, elt->pathname,
            elt->user);
    fprintf(fp, "%s  status[%u] omode[%04o] cookie[$%llx]\n", prefix,
            elt->status, elt->omode, (u64) elt->readdir_cookie);
    fprintf(fp, "%s  %u.%u.%u.%u:%u }", prefix,
            (u32) (elt->address >> 24) & 0xff,
            (u32) (elt->address >> 16) & 0xff,
            (u32) (elt->address >>  8) & 0xff,
            (u32)  elt->address        & 0xff,
            (u32) elt->port);
}
