let _NOFID      = Int32.lognot 0l

let _OREAD      = 0x00
let _OWRITE     = 0x01
let _ORDWR      = 0x02
let _OEXEC      = 0x03
let _OTRUNC     = 0x10
let _ORCLOSE    = 0x40

let _DMDIR      = 0x80000000l
let _DMAPPEND   = 0x40000000l
let _DMEXCL     = 0x20000000l
let _DMTMP      = 0x04000000l

let minStatSize = 52

type socketReadBuffer = {
  mutable n: int;
  mutable size: int;
  mutable sizeBuffer: string;
  mutable msg: string;
}

type conn = {
  conn_self: Unix.file_descr;
  conn_read: socketReadBuffer;
  conn_maxSize: int;
}

type fd = Closed
        | Fd of Unix.file_descr
        | Dh of Unix.dir_handle * int64 * string option

type fid = {
  fid_self: Unix.file_descr * int32;
  fid_user: string;
  mutable fid_fd: fd;
  mutable fid_path: string;
  mutable fid_omode: int;
}

type t = {
  mutable listeners: Unix.file_descr list;
  conns: (Unix.file_descr, conn) Hashtbl.t;
  fids: (Unix.file_descr * int32, fid) Hashtbl.t;
}

let getConnectionsList state =
  Hashtbl.fold (fun k v lst -> k :: lst) state.conns state.listeners

let create fd =
  {
    conn_self = fd;
    conn_read = { n = 0; size = 0; sizeBuffer = String.create 4; msg = "" };
    conn_maxSize = Int32.to_int (!Config.maxSize);
  }
