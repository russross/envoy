#include <stdio.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef int (*Cmpfunc)(const void *, const void *);
typedef u32 (*Hashfunc)(const void *);

extern int DEBUG_VERBOSE;
extern int GLOBAL_MAX_SIZE;

#define OID_BITS 64

#define MAXWELEM 16
#define MAXFELEM 64
#define MAX_EXTENSION_LENGTH 1000

#define NOTAG 0xffff
#define ALLOCTAG 0xfffe
#define NOFID (~(u32) 0)
#define NOOID (~(u64) 0)
#define NOUID (~(u32) 0)
#define NOGID (~(u32) 0)

enum openmodes {
    OREAD =             0x00,
    OWRITE =            0x01,
    ORDWR =             0x02,
    OEXEC =             0x03,
    OTRUNC =            0x10,
    ORCLOSE =           0x40,
};

#define OMASK 3

enum filemodes {
    DMDIR =             0x80000000,
    DMAPPEND =          0x40000000,
    DMEXCL =            0x20000000,
    DMMOUNT =           0x10000000,
    DMAUTH =            0x08000000,
    DMTMP =             0x04000000,
    DMSYMLINK =         0x02000000,
    DMLINK =            0x01000000,
    DMDEVICE =          0x00800000,
    DMNAMEDPIPE =       0x00200000,
    DMSOCKET =          0x00100000,
    DMSETUID =          0x00080000,
    DMSETGID =          0x00040000,
};

#define DMMASK 0xfff00000

enum qidtypes {
    QTDIR =             0x80,
    QTAPPEND =          0x40,
    QTEXCL =            0x20,
    QTMOUNT =           0x10,
    QTAUTH =            0x08,
    QTTMP =             0x04,
    QTSLINK =           0x02,
    QTLINK =            0x01,
    QTFILE =            0x00,
};

/* non-data size of read messages */
#define RREAD_HEADER 11
#define RSREAD_HEADER 11
#define RREAD_DATA_OFFSET 11
#define RSREAD_DATA_OFFSET 11
#define TWRITE_DATA_OFFSET 23
#define TSWRITE_DATA_OFFSET 23
#define REREVOKE_SIZE_FIXED 12
#define TEGRANT_SIZE_FIXED 12
#define TEMIGRATE_SIZE_FIXED 9
#define TERENAMETREE_SIZE_FIXED 13

/* size difference between storage and client read/write messages */
#define STORAGE_SLUSH 8

/* string length that handles NULL strings */
#define safe_strlen(_s) ((_s) == NULL ? 0 : strlen(_s))

struct qid {
    u8 type;
    u32 version;
    u64 path;
};

struct p9stat {
    u16 type;
    u32 dev;
    struct qid qid;
    u32 mode;
    u32 atime;
    u32 mtime;
    u64 length;
    char *name;
    char *uid;
    char *gid;
    char *muid;
    char *extension;
    u32 n_uid;
    u32 n_gid;
    u32 n_muid;
};

struct leaserecord {
    char *pathname;
    u8 readonly;
    u64 oid;
    u32 address;
    u16 port;
};

struct fidrecord {
    u32 fid;
    char *pathname;
    char *user;
    u8 status;
    u32 omode;
    u64 readdir_cookie;
    u32 address;
    u16 port;
};

struct message *message_new(void);
struct p9stat *p9stat_new(void);

int statnsize(struct p9stat *elt);
int stringlistsize(u16 len, char **elt);
int leaserecordsize(struct leaserecord *elt);
int leaserecordlistsize(u16 len, struct leaserecord **elt);
int fidrecordsize(struct fidrecord *elt);
int fidrecordlistsize(u16 len, struct fidrecord **elt);

void dumpBytes(FILE *fp, char *prefix, u8 *buff, int size);
void dumpData(FILE *fp, char *prefix, u8 *buff, int size);
void dumpStat(FILE *fp, char *prefix, struct p9stat *stat);
void dumpLeaserecord(FILE *fp, char *prefix, struct leaserecord *elt);
void dumpFidrecord(FILE *fp, char *prefix, struct fidrecord *elt);

u8 unpackU8(u8 *raw, int size, int *i);
u16 unpackU16(u8 *raw, int size, int *i);
u32 unpackU32(u8 *raw, int size, int *i);
u64 unpackU64(u8 *raw, int size, int *i);
u32 *unpackU32list(u8 *raw, int size, int *i, u16 *n);
u64 *unpackU64list(u8 *raw, int size, int *i, u16 *n);
u8 *unpackData(u8 *raw, int size, int *i, u32 *len);
char *unpackString(u8 *raw, int size, int *i);
char **unpackStringlist(u8 *raw, int size, int *i, u16 *n);
struct qid unpackQid(u8 *raw, int size, int *i);
struct qid *unpackQidlist(u8 *raw, int size, int *i, u16 *n);
struct p9stat *unpackStat(u8 *raw, int size, int *i);
struct p9stat *unpackStatn(u8 *raw, int size, int *i);
struct leaserecord *unpackLeaserecord(u8 *raw, int size, int *i);
struct leaserecord **unpackLeaserecordlist(u8 *raw, int size, int *i, u16 *n);
struct fidrecord *unpackFidrecord(u8 *raw, int size, int *i);
struct fidrecord **unpackFidrecordlist(u8 *raw, int size, int *i, u16 *n);

void packU8(u8 *raw, int *i, u8 elt);
void packU16(u8 *raw, int *i, u16 elt);
void packU32(u8 *raw, int *i, u32 elt);
void packU64(u8 *raw, int *i, u64 elt);
void packU32list(u8 *raw, int *i, u16 len, u32 *elt);
void packU64list(u8 *raw, int *i, u16 len, u64 *elt);
void packData(u8 *raw, int *i, u32 len, u8 *elt);
void packString(u8 *raw, int *i, char *elt);
void packStringlist(u8 *raw, int *i, u16 len, char **elt);
void packQid(u8 *raw, int *i, struct qid elt);
void packQidlist(u8 *raw, int *i, u16 len, struct qid *elt);
void packStat(u8 *raw, int *i, struct p9stat *elt);
void packStatn(u8 *raw, int *i, struct p9stat *elt);
void packLeaserecord(u8 *raw, int *i, struct leaserecord *elt);
void packFidrecord(u8 *raw, int *i, struct fidrecord *elt);
void packLeaserecordlist(u8 *raw, int *i, u16 len, struct leaserecord **elt);
void packFidrecordlist(u8 *raw, int *i, u16 len, struct fidrecord **elt);
