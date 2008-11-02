open ParseHelpers;;

(* Individual message type definitions *)

type tversion = {
    tversion_msize: int32;
    tversion_version: string;
    tversion_tag: int;
}

type rversion = {
    rversion_msize: int32;
    rversion_version: string;
    rversion_tag: int;
}

type tauth = {
    tauth_afid: int32;
    tauth_uname: string;
    tauth_aname: string;
    tauth_tag: int;
}

type rauth = {
    rauth_aqid: string;
    rauth_tag: int;
}

type rerror = {
    rerror_ename: string;
    rerror_errno: int;
    rerror_tag: int;
}

type tflush = {
    tflush_oldtag: int;
    tflush_tag: int;
}

type rflush = {
    rflush_tag: int;
}

type tattach = {
    tattach_fid: int32;
    tattach_afid: int32;
    tattach_uname: string;
    tattach_aname: string;
    tattach_tag: int;
}

type rattach = {
    rattach_qid: string;
    rattach_tag: int;
}

type twalk = {
    twalk_fid: int32;
    twalk_newfid: int32;
    twalk_wname: string list;
    twalk_tag: int;
}

type rwalk = {
    rwalk_wqid: string list;
    rwalk_tag: int;
}

type topen = {
    topen_fid: int32;
    topen_mode: int;
    topen_tag: int;
}

type ropen = {
    ropen_qid: string;
    ropen_iounit: int32;
    ropen_tag: int;
}

type tcreate = {
    tcreate_fid: int32;
    tcreate_name: string;
    tcreate_perm: int32;
    tcreate_mode: int;
    tcreate_extension: string;
    tcreate_tag: int;
}

type rcreate = {
    rcreate_qid: string;
    rcreate_iounit: int32;
    rcreate_tag: int;
}

type tread = {
    tread_fid: int32;
    tread_offset: int64;
    tread_count: int32;
    tread_tag: int;
}

type rread = {
    rread_data: string;
    rread_tag: int;
}

type twrite = {
    twrite_fid: int32;
    twrite_offset: int64;
    twrite_data: string;
    twrite_tag: int;
}

type rwrite = {
    rwrite_count: int32;
    rwrite_tag: int;
}

type tclunk = {
    tclunk_fid: int32;
    tclunk_tag: int;
}

type rclunk = {
    rclunk_tag: int;
}

type tremove = {
    tremove_fid: int32;
    tremove_tag: int;
}

type rremove = {
    rremove_tag: int;
}

type tstat = {
    tstat_fid: int32;
    tstat_tag: int;
}

type rstat = {
    rstat_stat: string;
    rstat_tag: int;
}

type twstat = {
    twstat_fid: int32;
    twstat_stat: string;
    twstat_tag: int;
}

type rwstat = {
    rwstat_tag: int;
}

type tesnapshot = {
    tesnapshot_tag: int;
}

type resnapshot = {
    resnapshot_tag: int;
}

type tegrant = {
    tegrant_gtype: int;
    tegrant_path: string;
    tegrant_data: string;
    tegrant_rfid: int32;
    tegrant_tag: int;
}

type regrant = {
    regrant_tag: int;
}

type terevoke = {
    terevoke_path: string;
    terevoke_tag: int;
}

type rerevoke = {
    rerevoke_tag: int;
}

type tenominate = {
    tenominate_path: string;
    tenominate_tag: int;
}

type renominate = {
    renominate_tag: int;
}

type tewalkremote = {
    tewalkremote_fid: int32;
    tewalkremote_newfid: int32;
    tewalkremote_wname: string list;
    tewalkremote_user: string;
    tewalkremote_path: string;
    tewalkremote_tag: int;
}

type rewalkremote = {
    rewalkremote_wqid: string list;
    rewalkremote_errnum: int;
    rewalkremote_address: int32;
    rewalkremote_port: int;
    rewalkremote_tag: int;
}

type testatremote = {
    testatremote_path: string;
    testatremote_tag: int;
}

type restatremote = {
    restatremote_stat: string;
    restatremote_tag: int;
}

type terenameremote = {
    terenameremote_user: string;
    terenameremote_oldpath: string;
    terenameremote_newname: string;
    terenameremote_tag: int;
}

type rerenameremote = {
    rerenameremote_tag: int;
}

type teclosefid = {
    teclosefid_fid: int32;
    teclosefid_tag: int;
}

type reclosefid = {
    reclosefid_tag: int;
}

type tsreserve = {
    tsreserve_tag: int;
}

type rsreserve = {
    rsreserve_firstoid: int64;
    rsreserve_count: int32;
    rsreserve_tag: int;
}

type tscreate = {
    tscreate_oid: int64;
    tscreate_mode: int32;
    tscreate_time: int32;
    tscreate_uid: string;
    tscreate_gid: string;
    tscreate_extension: string;
    tscreate_tag: int;
}

type rscreate = {
    rscreate_qid: string;
    rscreate_tag: int;
}

type tsclone = {
    tsclone_oid: int64;
    tsclone_newoid: int64;
    tsclone_tag: int;
}

type rsclone = {
    rsclone_tag: int;
}

type tsread = {
    tsread_oid: int64;
    tsread_time: int32;
    tsread_offset: int64;
    tsread_count: int32;
    tsread_tag: int;
}

type rsread = {
    rsread_data: string;
    rsread_tag: int;
}

type tswrite = {
    tswrite_oid: int64;
    tswrite_time: int32;
    tswrite_offset: int64;
    tswrite_data: string;
    tswrite_tag: int;
}

type rswrite = {
    rswrite_count: int32;
    rswrite_tag: int;
}

type tsstat = {
    tsstat_oid: int64;
    tsstat_tag: int;
}

type rsstat = {
    rsstat_stat: string;
    rsstat_tag: int;
}

type tswstat = {
    tswstat_oid: int64;
    tswstat_stat: string;
    tswstat_tag: int;
}

type rswstat = {
    rswstat_tag: int;
}


(* Individual message unpacker functions *)

let unpackTversion s =
    if int_of_char (String.get s 0) != 100 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (msize, i) = unpackInt32 s i in
    let (version, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        tversion_msize = msize;
        tversion_version = version;
        tversion_tag = tag;
    }

let unpackRversion s =
    if int_of_char (String.get s 0) != 101 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (msize, i) = unpackInt32 s i in
    let (version, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        rversion_msize = msize;
        rversion_version = version;
        rversion_tag = tag;
    }

let unpackTauth s =
    if int_of_char (String.get s 0) != 102 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (afid, i) = unpackInt32 s i in
    let (uname, i) = unpackString s i in
    let (aname, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        tauth_afid = afid;
        tauth_uname = uname;
        tauth_aname = aname;
        tauth_tag = tag;
    }

let unpackRauth s =
    if int_of_char (String.get s 0) != 103 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (aqid, i) = unpackBytes 13 s i in
    if i != String.length s then raise ParseError else
    {
        rauth_aqid = aqid;
        rauth_tag = tag;
    }

let unpackRerror s =
    if int_of_char (String.get s 0) != 107 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (ename, i) = unpackString s i in
    let (errno, i) = unpackInt s i in
    if i != String.length s then raise ParseError else
    {
        rerror_ename = ename;
        rerror_errno = errno;
        rerror_tag = tag;
    }

let unpackTflush s =
    if int_of_char (String.get s 0) != 108 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (oldtag, i) = unpackInt s i in
    if i != String.length s then raise ParseError else
    {
        tflush_oldtag = oldtag;
        tflush_tag = tag;
    }

let unpackRflush s =
    if int_of_char (String.get s 0) != 109 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        rflush_tag = tag;
    }

let unpackTattach s =
    if int_of_char (String.get s 0) != 104 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    let (afid, i) = unpackInt32 s i in
    let (uname, i) = unpackString s i in
    let (aname, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        tattach_fid = fid;
        tattach_afid = afid;
        tattach_uname = uname;
        tattach_aname = aname;
        tattach_tag = tag;
    }

let unpackRattach s =
    if int_of_char (String.get s 0) != 105 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (qid, i) = unpackBytes 13 s i in
    if i != String.length s then raise ParseError else
    {
        rattach_qid = qid;
        rattach_tag = tag;
    }

let unpackTwalk s =
    if int_of_char (String.get s 0) != 110 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    let (newfid, i) = unpackInt32 s i in
    let (wname, i) = unpackRepeat unpackString s i in
    if i != String.length s then raise ParseError else
    {
        twalk_fid = fid;
        twalk_newfid = newfid;
        twalk_wname = wname;
        twalk_tag = tag;
    }

let unpackRwalk s =
    if int_of_char (String.get s 0) != 111 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (wqid, i) = unpackRepeat (unpackBytes 13) s i in
    if i != String.length s then raise ParseError else
    {
        rwalk_wqid = wqid;
        rwalk_tag = tag;
    }

let unpackTopen s =
    if int_of_char (String.get s 0) != 112 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    let (mode, i) = unpackByte s i in
    if i != String.length s then raise ParseError else
    {
        topen_fid = fid;
        topen_mode = mode;
        topen_tag = tag;
    }

let unpackRopen s =
    if int_of_char (String.get s 0) != 113 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (qid, i) = unpackBytes 13 s i in
    let (iounit, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        ropen_qid = qid;
        ropen_iounit = iounit;
        ropen_tag = tag;
    }

let unpackTcreate s =
    if int_of_char (String.get s 0) != 114 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    let (name, i) = unpackString s i in
    let (perm, i) = unpackInt32 s i in
    let (mode, i) = unpackByte s i in
    let (extension, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        tcreate_fid = fid;
        tcreate_name = name;
        tcreate_perm = perm;
        tcreate_mode = mode;
        tcreate_extension = extension;
        tcreate_tag = tag;
    }

let unpackRcreate s =
    if int_of_char (String.get s 0) != 115 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (qid, i) = unpackBytes 13 s i in
    let (iounit, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        rcreate_qid = qid;
        rcreate_iounit = iounit;
        rcreate_tag = tag;
    }

let unpackTread s =
    if int_of_char (String.get s 0) != 116 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    let (offset, i) = unpackInt64 s i in
    let (count, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        tread_fid = fid;
        tread_offset = offset;
        tread_count = count;
        tread_tag = tag;
    }

let unpackRread s =
    if int_of_char (String.get s 0) != 117 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (data, i) = unpackData s i in
    if i != String.length s then raise ParseError else
    {
        rread_data = data;
        rread_tag = tag;
    }

let unpackTwrite s =
    if int_of_char (String.get s 0) != 118 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    let (offset, i) = unpackInt64 s i in
    let (data, i) = unpackData s i in
    if i != String.length s then raise ParseError else
    {
        twrite_fid = fid;
        twrite_offset = offset;
        twrite_data = data;
        twrite_tag = tag;
    }

let unpackRwrite s =
    if int_of_char (String.get s 0) != 119 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (count, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        rwrite_count = count;
        rwrite_tag = tag;
    }

let unpackTclunk s =
    if int_of_char (String.get s 0) != 120 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        tclunk_fid = fid;
        tclunk_tag = tag;
    }

let unpackRclunk s =
    if int_of_char (String.get s 0) != 121 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        rclunk_tag = tag;
    }

let unpackTremove s =
    if int_of_char (String.get s 0) != 122 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        tremove_fid = fid;
        tremove_tag = tag;
    }

let unpackRremove s =
    if int_of_char (String.get s 0) != 123 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        rremove_tag = tag;
    }

let unpackTstat s =
    if int_of_char (String.get s 0) != 124 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        tstat_fid = fid;
        tstat_tag = tag;
    }

let unpackRstat s =
    if int_of_char (String.get s 0) != 125 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (stat, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        rstat_stat = stat;
        rstat_tag = tag;
    }

let unpackTwstat s =
    if int_of_char (String.get s 0) != 126 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    let (stat, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        twstat_fid = fid;
        twstat_stat = stat;
        twstat_tag = tag;
    }

let unpackRwstat s =
    if int_of_char (String.get s 0) != 127 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        rwstat_tag = tag;
    }

let unpackTesnapshot s =
    if int_of_char (String.get s 0) != 150 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        tesnapshot_tag = tag;
    }

let unpackResnapshot s =
    if int_of_char (String.get s 0) != 151 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        resnapshot_tag = tag;
    }

let unpackTegrant s =
    if int_of_char (String.get s 0) != 152 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (gtype, i) = unpackByte s i in
    let (path, i) = unpackString s i in
    let (data, i) = unpackData s i in
    let (rfid, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        tegrant_gtype = gtype;
        tegrant_path = path;
        tegrant_data = data;
        tegrant_rfid = rfid;
        tegrant_tag = tag;
    }

let unpackRegrant s =
    if int_of_char (String.get s 0) != 153 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        regrant_tag = tag;
    }

let unpackTerevoke s =
    if int_of_char (String.get s 0) != 154 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (path, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        terevoke_path = path;
        terevoke_tag = tag;
    }

let unpackRerevoke s =
    if int_of_char (String.get s 0) != 155 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        rerevoke_tag = tag;
    }

let unpackTenominate s =
    if int_of_char (String.get s 0) != 156 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (path, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        tenominate_path = path;
        tenominate_tag = tag;
    }

let unpackRenominate s =
    if int_of_char (String.get s 0) != 157 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        renominate_tag = tag;
    }

let unpackTewalkremote s =
    if int_of_char (String.get s 0) != 158 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    let (newfid, i) = unpackInt32 s i in
    let (wname, i) = unpackRepeat unpackString s i in
    let (user, i) = unpackString s i in
    let (path, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        tewalkremote_fid = fid;
        tewalkremote_newfid = newfid;
        tewalkremote_wname = wname;
        tewalkremote_user = user;
        tewalkremote_path = path;
        tewalkremote_tag = tag;
    }

let unpackRewalkremote s =
    if int_of_char (String.get s 0) != 159 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (wqid, i) = unpackRepeat (unpackBytes 13) s i in
    let (errnum, i) = unpackInt s i in
    let (address, i) = unpackInt32 s i in
    let (port, i) = unpackInt s i in
    if i != String.length s then raise ParseError else
    {
        rewalkremote_wqid = wqid;
        rewalkremote_errnum = errnum;
        rewalkremote_address = address;
        rewalkremote_port = port;
        rewalkremote_tag = tag;
    }

let unpackTestatremote s =
    if int_of_char (String.get s 0) != 160 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (path, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        testatremote_path = path;
        testatremote_tag = tag;
    }

let unpackRestatremote s =
    if int_of_char (String.get s 0) != 161 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (stat, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        restatremote_stat = stat;
        restatremote_tag = tag;
    }

let unpackTerenameremote s =
    if int_of_char (String.get s 0) != 162 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (user, i) = unpackString s i in
    let (oldpath, i) = unpackString s i in
    let (newname, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        terenameremote_user = user;
        terenameremote_oldpath = oldpath;
        terenameremote_newname = newname;
        terenameremote_tag = tag;
    }

let unpackRerenameremote s =
    if int_of_char (String.get s 0) != 163 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        rerenameremote_tag = tag;
    }

let unpackTeclosefid s =
    if int_of_char (String.get s 0) != 164 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (fid, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        teclosefid_fid = fid;
        teclosefid_tag = tag;
    }

let unpackReclosefid s =
    if int_of_char (String.get s 0) != 165 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        reclosefid_tag = tag;
    }

let unpackTsreserve s =
    if int_of_char (String.get s 0) != 200 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        tsreserve_tag = tag;
    }

let unpackRsreserve s =
    if int_of_char (String.get s 0) != 201 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (firstoid, i) = unpackInt64 s i in
    let (count, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        rsreserve_firstoid = firstoid;
        rsreserve_count = count;
        rsreserve_tag = tag;
    }

let unpackTscreate s =
    if int_of_char (String.get s 0) != 202 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (oid, i) = unpackInt64 s i in
    let (mode, i) = unpackInt32 s i in
    let (time, i) = unpackInt32 s i in
    let (uid, i) = unpackString s i in
    let (gid, i) = unpackString s i in
    let (extension, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        tscreate_oid = oid;
        tscreate_mode = mode;
        tscreate_time = time;
        tscreate_uid = uid;
        tscreate_gid = gid;
        tscreate_extension = extension;
        tscreate_tag = tag;
    }

let unpackRscreate s =
    if int_of_char (String.get s 0) != 203 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (qid, i) = unpackBytes 13 s i in
    if i != String.length s then raise ParseError else
    {
        rscreate_qid = qid;
        rscreate_tag = tag;
    }

let unpackTsclone s =
    if int_of_char (String.get s 0) != 204 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (oid, i) = unpackInt64 s i in
    let (newoid, i) = unpackInt64 s i in
    if i != String.length s then raise ParseError else
    {
        tsclone_oid = oid;
        tsclone_newoid = newoid;
        tsclone_tag = tag;
    }

let unpackRsclone s =
    if int_of_char (String.get s 0) != 205 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        rsclone_tag = tag;
    }

let unpackTsread s =
    if int_of_char (String.get s 0) != 206 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (oid, i) = unpackInt64 s i in
    let (time, i) = unpackInt32 s i in
    let (offset, i) = unpackInt64 s i in
    let (count, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        tsread_oid = oid;
        tsread_time = time;
        tsread_offset = offset;
        tsread_count = count;
        tsread_tag = tag;
    }

let unpackRsread s =
    if int_of_char (String.get s 0) != 207 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (data, i) = unpackData s i in
    if i != String.length s then raise ParseError else
    {
        rsread_data = data;
        rsread_tag = tag;
    }

let unpackTswrite s =
    if int_of_char (String.get s 0) != 208 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (oid, i) = unpackInt64 s i in
    let (time, i) = unpackInt32 s i in
    let (offset, i) = unpackInt64 s i in
    let (data, i) = unpackData s i in
    if i != String.length s then raise ParseError else
    {
        tswrite_oid = oid;
        tswrite_time = time;
        tswrite_offset = offset;
        tswrite_data = data;
        tswrite_tag = tag;
    }

let unpackRswrite s =
    if int_of_char (String.get s 0) != 209 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (count, i) = unpackInt32 s i in
    if i != String.length s then raise ParseError else
    {
        rswrite_count = count;
        rswrite_tag = tag;
    }

let unpackTsstat s =
    if int_of_char (String.get s 0) != 210 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (oid, i) = unpackInt64 s i in
    if i != String.length s then raise ParseError else
    {
        tsstat_oid = oid;
        tsstat_tag = tag;
    }

let unpackRsstat s =
    if int_of_char (String.get s 0) != 211 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (stat, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        rsstat_stat = stat;
        rsstat_tag = tag;
    }

let unpackTswstat s =
    if int_of_char (String.get s 0) != 212 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    let (oid, i) = unpackInt64 s i in
    let (stat, i) = unpackString s i in
    if i != String.length s then raise ParseError else
    {
        tswstat_oid = oid;
        tswstat_stat = stat;
        tswstat_tag = tag;
    }

let unpackRswstat s =
    if int_of_char (String.get s 0) != 213 then raise ParseError else
    let (tag, i) = unpackInt s 1 in
    if i != String.length s then raise ParseError else
    {
        rswstat_tag = tag;
    }


(* Individual message packer functions *)

let packTversion { tversion_msize = msize;
                   tversion_version = version;
                   tversion_tag = tag; } =
    let size = String.length version + 13 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 100;
    packInt s i tag;
    packInt32 s i msize;
    packString s i version;
    s

let packRversion { rversion_msize = msize;
                   rversion_version = version;
                   rversion_tag = tag; } =
    let size = String.length version + 13 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 101;
    packInt s i tag;
    packInt32 s i msize;
    packString s i version;
    s

let packTauth { tauth_afid = afid;
                tauth_uname = uname;
                tauth_aname = aname;
                tauth_tag = tag; } =
    let size = String.length uname + String.length aname + 15 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 102;
    packInt s i tag;
    packInt32 s i afid;
    packString s i uname;
    packString s i aname;
    s

let packRauth { rauth_aqid = aqid; rauth_tag = tag; } =
    let size = 20 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 103;
    packInt s i tag;
    packBytes s i aqid 13 ;
    s

let packRerror { rerror_ename = ename;
                 rerror_errno = errno;
                 rerror_tag = tag; } =
    let size = String.length ename + 11 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 107;
    packInt s i tag;
    packString s i ename;
    packInt s i errno;
    s

let packTflush { tflush_oldtag = oldtag; tflush_tag = tag; } =
    let size = 9 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 108;
    packInt s i tag;
    packInt s i oldtag;
    s

let packRflush { rflush_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 109;
    packInt s i tag;
    s

let packTattach { tattach_fid = fid;
                  tattach_afid = afid;
                  tattach_uname = uname;
                  tattach_aname = aname;
                  tattach_tag = tag; } =
    let size = String.length uname + String.length aname + 19 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 104;
    packInt s i tag;
    packInt32 s i fid;
    packInt32 s i afid;
    packString s i uname;
    packString s i aname;
    s

let packRattach { rattach_qid = qid; rattach_tag = tag; } =
    let size = 20 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 105;
    packInt s i tag;
    packBytes s i qid 13 ;
    s

let packTwalk { twalk_fid = fid;
                twalk_newfid = newfid;
                twalk_wname = wname;
                twalk_tag = tag; } =
    let size = List.fold_left (fun a s -> 2 + a + String.length s) 0 wname +
               17 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 110;
    packInt s i tag;
    packInt32 s i fid;
    packInt32 s i newfid;
    packInt s i (List.length wname);
    List.iter (fun elt -> packString s i elt) wname;
    s

let packRwalk { rwalk_wqid = wqid; rwalk_tag = tag; } =
    let size = 13 * List.length wqid + 9 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 111;
    packInt s i tag;
    packInt s i (List.length wqid);
    List.iter (fun elt -> packBytes s i elt 13) wqid;
    s

let packTopen { topen_fid = fid; topen_mode = mode; topen_tag = tag; } =
    let size = 12 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 112;
    packInt s i tag;
    packInt32 s i fid;
    packByte s i mode;
    s

let packRopen { ropen_qid = qid; ropen_iounit = iounit; ropen_tag = tag; } =
    let size = 24 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 113;
    packInt s i tag;
    packBytes s i qid 13 ;
    packInt32 s i iounit;
    s

let packTcreate { tcreate_fid = fid;
                  tcreate_name = name;
                  tcreate_perm = perm;
                  tcreate_mode = mode;
                  tcreate_extension = extension;
                  tcreate_tag = tag; } =
    let size = String.length name + String.length extension + 20 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 114;
    packInt s i tag;
    packInt32 s i fid;
    packString s i name;
    packInt32 s i perm;
    packByte s i mode;
    packString s i extension;
    s

let packRcreate { rcreate_qid = qid;
                  rcreate_iounit = iounit;
                  rcreate_tag = tag; } =
    let size = 24 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 115;
    packInt s i tag;
    packBytes s i qid 13 ;
    packInt32 s i iounit;
    s

let packTread { tread_fid = fid;
                tread_offset = offset;
                tread_count = count;
                tread_tag = tag; } =
    let size = 23 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 116;
    packInt s i tag;
    packInt32 s i fid;
    packInt64 s i offset;
    packInt32 s i count;
    s

let packRread { rread_data = data; rread_tag = tag; } =
    let size = String.length data + 11 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 117;
    packInt s i tag;
    packData s i data;
    s

let packTwrite { twrite_fid = fid;
                 twrite_offset = offset;
                 twrite_data = data;
                 twrite_tag = tag; } =
    let size = String.length data + 23 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 118;
    packInt s i tag;
    packInt32 s i fid;
    packInt64 s i offset;
    packData s i data;
    s

let packRwrite { rwrite_count = count; rwrite_tag = tag; } =
    let size = 11 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 119;
    packInt s i tag;
    packInt32 s i count;
    s

let packTclunk { tclunk_fid = fid; tclunk_tag = tag; } =
    let size = 11 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 120;
    packInt s i tag;
    packInt32 s i fid;
    s

let packRclunk { rclunk_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 121;
    packInt s i tag;
    s

let packTremove { tremove_fid = fid; tremove_tag = tag; } =
    let size = 11 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 122;
    packInt s i tag;
    packInt32 s i fid;
    s

let packRremove { rremove_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 123;
    packInt s i tag;
    s

let packTstat { tstat_fid = fid; tstat_tag = tag; } =
    let size = 11 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 124;
    packInt s i tag;
    packInt32 s i fid;
    s

let packRstat { rstat_stat = stat; rstat_tag = tag; } =
    let size = String.length stat + 9 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 125;
    packInt s i tag;
    packString s i stat;
    s

let packTwstat { twstat_fid = fid; twstat_stat = stat; twstat_tag = tag; } =
    let size = String.length stat + 13 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 126;
    packInt s i tag;
    packInt32 s i fid;
    packString s i stat;
    s

let packRwstat { rwstat_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 127;
    packInt s i tag;
    s

let packTesnapshot { tesnapshot_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 150;
    packInt s i tag;
    s

let packResnapshot { resnapshot_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 151;
    packInt s i tag;
    s

let packTegrant { tegrant_gtype = gtype;
                  tegrant_path = path;
                  tegrant_data = data;
                  tegrant_rfid = rfid;
                  tegrant_tag = tag; } =
    let size = String.length path + String.length data + 18 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 152;
    packInt s i tag;
    packByte s i gtype;
    packString s i path;
    packData s i data;
    packInt32 s i rfid;
    s

let packRegrant { regrant_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 153;
    packInt s i tag;
    s

let packTerevoke { terevoke_path = path; terevoke_tag = tag; } =
    let size = String.length path + 9 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 154;
    packInt s i tag;
    packString s i path;
    s

let packRerevoke { rerevoke_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 155;
    packInt s i tag;
    s

let packTenominate { tenominate_path = path; tenominate_tag = tag; } =
    let size = String.length path + 9 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 156;
    packInt s i tag;
    packString s i path;
    s

let packRenominate { renominate_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 157;
    packInt s i tag;
    s

let packTewalkremote { tewalkremote_fid = fid;
                       tewalkremote_newfid = newfid;
                       tewalkremote_wname = wname;
                       tewalkremote_user = user;
                       tewalkremote_path = path;
                       tewalkremote_tag = tag; } =
    let size = List.fold_left (fun a s -> 2 + a + String.length s) 0 wname +
               String.length user + String.length path + 21 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 158;
    packInt s i tag;
    packInt32 s i fid;
    packInt32 s i newfid;
    packInt s i (List.length wname);
    List.iter (fun elt -> packString s i elt) wname;
    packString s i user;
    packString s i path;
    s

let packRewalkremote { rewalkremote_wqid = wqid;
                       rewalkremote_errnum = errnum;
                       rewalkremote_address = address;
                       rewalkremote_port = port;
                       rewalkremote_tag = tag; } =
    let size = 13 * List.length wqid + 17 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 159;
    packInt s i tag;
    packInt s i (List.length wqid);
    List.iter (fun elt -> packBytes s i elt 13) wqid;
    packInt s i errnum;
    packInt32 s i address;
    packInt s i port;
    s

let packTestatremote { testatremote_path = path; testatremote_tag = tag; } =
    let size = String.length path + 9 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 160;
    packInt s i tag;
    packString s i path;
    s

let packRestatremote { restatremote_stat = stat; restatremote_tag = tag; } =
    let size = String.length stat + 9 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 161;
    packInt s i tag;
    packString s i stat;
    s

let packTerenameremote { terenameremote_user = user;
                         terenameremote_oldpath = oldpath;
                         terenameremote_newname = newname;
                         terenameremote_tag = tag; } =
    let size = String.length user + String.length oldpath +
               String.length newname + 13 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 162;
    packInt s i tag;
    packString s i user;
    packString s i oldpath;
    packString s i newname;
    s

let packRerenameremote { rerenameremote_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 163;
    packInt s i tag;
    s

let packTeclosefid { teclosefid_fid = fid; teclosefid_tag = tag; } =
    let size = 11 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 164;
    packInt s i tag;
    packInt32 s i fid;
    s

let packReclosefid { reclosefid_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 165;
    packInt s i tag;
    s

let packTsreserve { tsreserve_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 200;
    packInt s i tag;
    s

let packRsreserve { rsreserve_firstoid = firstoid;
                    rsreserve_count = count;
                    rsreserve_tag = tag; } =
    let size = 19 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 201;
    packInt s i tag;
    packInt64 s i firstoid;
    packInt32 s i count;
    s

let packTscreate { tscreate_oid = oid;
                   tscreate_mode = mode;
                   tscreate_time = time;
                   tscreate_uid = uid;
                   tscreate_gid = gid;
                   tscreate_extension = extension;
                   tscreate_tag = tag; } =
    let size = String.length uid + String.length gid +
               String.length extension + 29 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 202;
    packInt s i tag;
    packInt64 s i oid;
    packInt32 s i mode;
    packInt32 s i time;
    packString s i uid;
    packString s i gid;
    packString s i extension;
    s

let packRscreate { rscreate_qid = qid; rscreate_tag = tag; } =
    let size = 20 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 203;
    packInt s i tag;
    packBytes s i qid 13 ;
    s

let packTsclone { tsclone_oid = oid;
                  tsclone_newoid = newoid;
                  tsclone_tag = tag; } =
    let size = 23 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 204;
    packInt s i tag;
    packInt64 s i oid;
    packInt64 s i newoid;
    s

let packRsclone { rsclone_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 205;
    packInt s i tag;
    s

let packTsread { tsread_oid = oid;
                 tsread_time = time;
                 tsread_offset = offset;
                 tsread_count = count;
                 tsread_tag = tag; } =
    let size = 31 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 206;
    packInt s i tag;
    packInt64 s i oid;
    packInt32 s i time;
    packInt64 s i offset;
    packInt32 s i count;
    s

let packRsread { rsread_data = data; rsread_tag = tag; } =
    let size = String.length data + 11 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 207;
    packInt s i tag;
    packData s i data;
    s

let packTswrite { tswrite_oid = oid;
                  tswrite_time = time;
                  tswrite_offset = offset;
                  tswrite_data = data;
                  tswrite_tag = tag; } =
    let size = String.length data + 31 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 208;
    packInt s i tag;
    packInt64 s i oid;
    packInt32 s i time;
    packInt64 s i offset;
    packData s i data;
    s

let packRswrite { rswrite_count = count; rswrite_tag = tag; } =
    let size = 11 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 209;
    packInt s i tag;
    packInt32 s i count;
    s

let packTsstat { tsstat_oid = oid; tsstat_tag = tag; } =
    let size = 15 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 210;
    packInt s i tag;
    packInt64 s i oid;
    s

let packRsstat { rsstat_stat = stat; rsstat_tag = tag; } =
    let size = String.length stat + 9 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 211;
    packInt s i tag;
    packString s i stat;
    s

let packTswstat { tswstat_oid = oid;
                  tswstat_stat = stat;
                  tswstat_tag = tag; } =
    let size = String.length stat + 17 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 212;
    packInt s i tag;
    packInt64 s i oid;
    packString s i stat;
    s

let packRswstat { rswstat_tag = tag; } =
    let size = 7 in
    let s = String.create size in
    let i = ref 0 in
    packInt32 s i (Int32.of_int size);
    packByte s i 213;
    packInt s i tag;
    s


(* Message type *)

type message = Tversion of tversion
             | Rversion of rversion
             | Tauth of tauth
             | Rauth of rauth
             | Rerror of rerror
             | Tflush of tflush
             | Rflush of rflush
             | Tattach of tattach
             | Rattach of rattach
             | Twalk of twalk
             | Rwalk of rwalk
             | Topen of topen
             | Ropen of ropen
             | Tcreate of tcreate
             | Rcreate of rcreate
             | Tread of tread
             | Rread of rread
             | Twrite of twrite
             | Rwrite of rwrite
             | Tclunk of tclunk
             | Rclunk of rclunk
             | Tremove of tremove
             | Rremove of rremove
             | Tstat of tstat
             | Rstat of rstat
             | Twstat of twstat
             | Rwstat of rwstat
             | Tesnapshot of tesnapshot
             | Resnapshot of resnapshot
             | Tegrant of tegrant
             | Regrant of regrant
             | Terevoke of terevoke
             | Rerevoke of rerevoke
             | Tenominate of tenominate
             | Renominate of renominate
             | Tewalkremote of tewalkremote
             | Rewalkremote of rewalkremote
             | Testatremote of testatremote
             | Restatremote of restatremote
             | Terenameremote of terenameremote
             | Rerenameremote of rerenameremote
             | Teclosefid of teclosefid
             | Reclosefid of reclosefid
             | Tsreserve of tsreserve
             | Rsreserve of rsreserve
             | Tscreate of tscreate
             | Rscreate of rscreate
             | Tsclone of tsclone
             | Rsclone of rsclone
             | Tsread of tsread
             | Rsread of rsread
             | Tswrite of tswrite
             | Rswrite of rswrite
             | Tsstat of tsstat
             | Rsstat of rsstat
             | Tswstat of tswstat
             | Rswstat of rswstat
             

(* Message unpacker function *)

let unpackMessage msg =
    try
        match int_of_char (String.get msg 0) with
        | 100 -> Tversion (unpackTversion msg)
        | 101 -> Rversion (unpackRversion msg)
        | 102 -> Tauth (unpackTauth msg)
        | 103 -> Rauth (unpackRauth msg)
        | 107 -> Rerror (unpackRerror msg)
        | 108 -> Tflush (unpackTflush msg)
        | 109 -> Rflush (unpackRflush msg)
        | 104 -> Tattach (unpackTattach msg)
        | 105 -> Rattach (unpackRattach msg)
        | 110 -> Twalk (unpackTwalk msg)
        | 111 -> Rwalk (unpackRwalk msg)
        | 112 -> Topen (unpackTopen msg)
        | 113 -> Ropen (unpackRopen msg)
        | 114 -> Tcreate (unpackTcreate msg)
        | 115 -> Rcreate (unpackRcreate msg)
        | 116 -> Tread (unpackTread msg)
        | 117 -> Rread (unpackRread msg)
        | 118 -> Twrite (unpackTwrite msg)
        | 119 -> Rwrite (unpackRwrite msg)
        | 120 -> Tclunk (unpackTclunk msg)
        | 121 -> Rclunk (unpackRclunk msg)
        | 122 -> Tremove (unpackTremove msg)
        | 123 -> Rremove (unpackRremove msg)
        | 124 -> Tstat (unpackTstat msg)
        | 125 -> Rstat (unpackRstat msg)
        | 126 -> Twstat (unpackTwstat msg)
        | 127 -> Rwstat (unpackRwstat msg)
        | 150 -> Tesnapshot (unpackTesnapshot msg)
        | 151 -> Resnapshot (unpackResnapshot msg)
        | 152 -> Tegrant (unpackTegrant msg)
        | 153 -> Regrant (unpackRegrant msg)
        | 154 -> Terevoke (unpackTerevoke msg)
        | 155 -> Rerevoke (unpackRerevoke msg)
        | 156 -> Tenominate (unpackTenominate msg)
        | 157 -> Renominate (unpackRenominate msg)
        | 158 -> Tewalkremote (unpackTewalkremote msg)
        | 159 -> Rewalkremote (unpackRewalkremote msg)
        | 160 -> Testatremote (unpackTestatremote msg)
        | 161 -> Restatremote (unpackRestatremote msg)
        | 162 -> Terenameremote (unpackTerenameremote msg)
        | 163 -> Rerenameremote (unpackRerenameremote msg)
        | 164 -> Teclosefid (unpackTeclosefid msg)
        | 165 -> Reclosefid (unpackReclosefid msg)
        | 200 -> Tsreserve (unpackTsreserve msg)
        | 201 -> Rsreserve (unpackRsreserve msg)
        | 202 -> Tscreate (unpackTscreate msg)
        | 203 -> Rscreate (unpackRscreate msg)
        | 204 -> Tsclone (unpackTsclone msg)
        | 205 -> Rsclone (unpackRsclone msg)
        | 206 -> Tsread (unpackTsread msg)
        | 207 -> Rsread (unpackRsread msg)
        | 208 -> Tswrite (unpackTswrite msg)
        | 209 -> Rswrite (unpackRswrite msg)
        | 210 -> Tsstat (unpackTsstat msg)
        | 211 -> Rsstat (unpackRsstat msg)
        | 212 -> Tswstat (unpackTswstat msg)
        | 213 -> Rswstat (unpackRswstat msg)
        | _ -> raise ParseError
    with Invalid_argument _ -> raise ParseError

(* Message packer function *)

let packMessage msg =
    match msg with
    | Tversion msg -> packTversion msg
    | Rversion msg -> packRversion msg
    | Tauth msg -> packTauth msg
    | Rauth msg -> packRauth msg
    | Rerror msg -> packRerror msg
    | Tflush msg -> packTflush msg
    | Rflush msg -> packRflush msg
    | Tattach msg -> packTattach msg
    | Rattach msg -> packRattach msg
    | Twalk msg -> packTwalk msg
    | Rwalk msg -> packRwalk msg
    | Topen msg -> packTopen msg
    | Ropen msg -> packRopen msg
    | Tcreate msg -> packTcreate msg
    | Rcreate msg -> packRcreate msg
    | Tread msg -> packTread msg
    | Rread msg -> packRread msg
    | Twrite msg -> packTwrite msg
    | Rwrite msg -> packRwrite msg
    | Tclunk msg -> packTclunk msg
    | Rclunk msg -> packRclunk msg
    | Tremove msg -> packTremove msg
    | Rremove msg -> packRremove msg
    | Tstat msg -> packTstat msg
    | Rstat msg -> packRstat msg
    | Twstat msg -> packTwstat msg
    | Rwstat msg -> packRwstat msg
    | Tesnapshot msg -> packTesnapshot msg
    | Resnapshot msg -> packResnapshot msg
    | Tegrant msg -> packTegrant msg
    | Regrant msg -> packRegrant msg
    | Terevoke msg -> packTerevoke msg
    | Rerevoke msg -> packRerevoke msg
    | Tenominate msg -> packTenominate msg
    | Renominate msg -> packRenominate msg
    | Tewalkremote msg -> packTewalkremote msg
    | Rewalkremote msg -> packRewalkremote msg
    | Testatremote msg -> packTestatremote msg
    | Restatremote msg -> packRestatremote msg
    | Terenameremote msg -> packTerenameremote msg
    | Rerenameremote msg -> packRerenameremote msg
    | Teclosefid msg -> packTeclosefid msg
    | Reclosefid msg -> packReclosefid msg
    | Tsreserve msg -> packTsreserve msg
    | Rsreserve msg -> packRsreserve msg
    | Tscreate msg -> packTscreate msg
    | Rscreate msg -> packRscreate msg
    | Tsclone msg -> packTsclone msg
    | Rsclone msg -> packRsclone msg
    | Tsread msg -> packTsread msg
    | Rsread msg -> packRsread msg
    | Tswrite msg -> packTswrite msg
    | Rswrite msg -> packRswrite msg
    | Tsstat msg -> packTsstat msg
    | Rsstat msg -> packRsstat msg
    | Tswstat msg -> packTswstat msg
    | Rswstat msg -> packRswstat msg
    

(* Get tag function *)

let getTag msg =
    match msg with
    | Tversion msg -> msg.tversion_tag
    | Rversion msg -> msg.rversion_tag
    | Tauth msg -> msg.tauth_tag
    | Rauth msg -> msg.rauth_tag
    | Rerror msg -> msg.rerror_tag
    | Tflush msg -> msg.tflush_tag
    | Rflush msg -> msg.rflush_tag
    | Tattach msg -> msg.tattach_tag
    | Rattach msg -> msg.rattach_tag
    | Twalk msg -> msg.twalk_tag
    | Rwalk msg -> msg.rwalk_tag
    | Topen msg -> msg.topen_tag
    | Ropen msg -> msg.ropen_tag
    | Tcreate msg -> msg.tcreate_tag
    | Rcreate msg -> msg.rcreate_tag
    | Tread msg -> msg.tread_tag
    | Rread msg -> msg.rread_tag
    | Twrite msg -> msg.twrite_tag
    | Rwrite msg -> msg.rwrite_tag
    | Tclunk msg -> msg.tclunk_tag
    | Rclunk msg -> msg.rclunk_tag
    | Tremove msg -> msg.tremove_tag
    | Rremove msg -> msg.rremove_tag
    | Tstat msg -> msg.tstat_tag
    | Rstat msg -> msg.rstat_tag
    | Twstat msg -> msg.twstat_tag
    | Rwstat msg -> msg.rwstat_tag
    | Tesnapshot msg -> msg.tesnapshot_tag
    | Resnapshot msg -> msg.resnapshot_tag
    | Tegrant msg -> msg.tegrant_tag
    | Regrant msg -> msg.regrant_tag
    | Terevoke msg -> msg.terevoke_tag
    | Rerevoke msg -> msg.rerevoke_tag
    | Tenominate msg -> msg.tenominate_tag
    | Renominate msg -> msg.renominate_tag
    | Tewalkremote msg -> msg.tewalkremote_tag
    | Rewalkremote msg -> msg.rewalkremote_tag
    | Testatremote msg -> msg.testatremote_tag
    | Restatremote msg -> msg.restatremote_tag
    | Terenameremote msg -> msg.terenameremote_tag
    | Rerenameremote msg -> msg.rerenameremote_tag
    | Teclosefid msg -> msg.teclosefid_tag
    | Reclosefid msg -> msg.reclosefid_tag
    | Tsreserve msg -> msg.tsreserve_tag
    | Rsreserve msg -> msg.rsreserve_tag
    | Tscreate msg -> msg.tscreate_tag
    | Rscreate msg -> msg.rscreate_tag
    | Tsclone msg -> msg.tsclone_tag
    | Rsclone msg -> msg.rsclone_tag
    | Tsread msg -> msg.tsread_tag
    | Rsread msg -> msg.rsread_tag
    | Tswrite msg -> msg.tswrite_tag
    | Rswrite msg -> msg.rswrite_tag
    | Tsstat msg -> msg.tsstat_tag
    | Rsstat msg -> msg.rsstat_tag
    | Tswstat msg -> msg.tswstat_tag
    | Rswstat msg -> msg.rswstat_tag
    
