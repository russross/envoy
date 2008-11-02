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

val unpackMessage : string -> message

(* Message packer function *)

val packMessage : message -> string

(* Get tag function *)

val getTag : message -> int
