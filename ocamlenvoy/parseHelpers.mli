exception ParseError

type qid = {
  q_type: int;
  q_version: int32;
  q_path: int64;
}

type p9stat = {
  s9_type: int;
  s9_dev: int32;
  s9_qid: qid;
  s9_mode: int32;
  s9_atime: int32;
  s9_mtime: int32;
  s9_length: int64;
  s9_name: string;
  s9_uid: string;
  s9_gid: string;
  s9_muid: string;
  s9_extension: string;
  s9_n_uid: int32;
  s9_n_gid: int32;
  s9_n_muid: int32;
}

(* unpackers *)
val unpackByte : string -> int -> int * int
val unpackInt : string -> int -> int * int
val unpackInt32 : string -> int -> int32 * int
val unpackInt64 : string -> int -> int64 * int
val unpackString : string -> int -> string * int
val unpackData : string -> int -> string * int
val unpackBytes : int -> string -> int -> string * int
val unpackRepeat : (string -> int -> 'a * int) -> string -> int -> 'a list * int
val unpackQid : string -> int -> qid * int
val unpackStat : string -> int -> p9stat * int

(* packers *)
val packByte : string -> int ref -> int -> unit
val packInt : string -> int ref -> int -> unit
val packInt32 : string -> int ref -> int32 -> unit
val packInt64 : string -> int ref -> int64 -> unit
val packBytes : string -> int ref -> string -> int -> unit
val packString : string -> int ref -> string -> unit
val packData : string -> int ref -> string -> unit
val packQid : string -> int ref -> qid -> unit
val packStat : string -> int ref -> p9stat -> unit

val makeStat : p9stat -> string
