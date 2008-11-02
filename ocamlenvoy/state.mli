val _NOFID : int32

val _OREAD : int
val _OWRITE : int
val _ORDWR : int
val _OEXEC : int
val _OTRUNC : int
val _ORCLOSE : int

val _DMDIR : int32
val _DMAPPEND : int32
val _DMEXCL : int32
val _DMTMP : int32

val minStatSize : int

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

val getConnectionsList : t -> Unix.file_descr list
val create : Unix.file_descr -> conn
