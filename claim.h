#ifndef _CLAIM_H_
#define _CLAIM_H_

#include <foo.h>

struct claim {
    /* the storage system object ID */
    u64 oid;
    /* the lease under which this object falls */
    Lease *lease;
    /* the version number of the lease when last checked */
    u32 lease_version;
    /* the level of access we have to this object */
    enum fid_access access;
    /* the full system path of this object */
    char *pathname;
    /* number of clients for this file (fids or directory ops), -1 if it's exclusive */
    int refcount;
};

Claim *claim_new(u64 oid, Lease *lease, enum fid_access access, char *pathname);
Claim *claim_lookup(char *pathname);
void claim_remove(Claim *claim);

#endif
