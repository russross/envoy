#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <unistd.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "util.h"
#include "config.h"
#include "dir.h"

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
List *dir_get_entries(u32 count, u8 *data) {
    int size = (int) count;
    int offset;
    u16 end;
    struct direntry *elt;
    List *result = NULL;

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

/* prepare a directory block for a clone by setting all copy-on-write flags */
void dir_clone(u32 count, u8 *data) {
    List *entries = dir_get_entries(count, data);

    for ( ; !null(entries); entries = cdr(entries)) {
        struct direntry *elt = car(entries);
        if (!elt->cow)
            data[elt->offset + DIR_COW_OFFSET] = 1;
    }
}
