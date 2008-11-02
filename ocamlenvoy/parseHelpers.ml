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
let unpackByte s i =
  let res = int_of_char (String.get s i) in
  (res, i+1)

let unpackInt s i =
  let res = int_of_char (String.get s (i+1)) in
  let res = (res lsl 8) lor (int_of_char (String.get s i)) in
  (res, i+2)

let unpackInt32 s i =
  let res = Int32.of_int (int_of_char (String.get s (i+3))) in
  let res = Int32.logor (Int32.shift_left res 8)
                        (Int32.of_int (int_of_char (String.get s (i+2)))) in
  let res = Int32.logor (Int32.shift_left res 8)
                        (Int32.of_int (int_of_char (String.get s (i+1)))) in
  let res = Int32.logor (Int32.shift_left res 8)
                        (Int32.of_int (int_of_char (String.get s i))) in
  (res, i+4)

let unpackInt64 s i =
  let res = Int64.of_int (int_of_char (String.get s (i+7))) in
  let res = Int64.logor (Int64.shift_left res 8)
                        (Int64.of_int (int_of_char (String.get s (i+6)))) in
  let res = Int64.logor (Int64.shift_left res 8)
                        (Int64.of_int (int_of_char (String.get s (i+5)))) in
  let res = Int64.logor (Int64.shift_left res 8)
                        (Int64.of_int (int_of_char (String.get s (i+4)))) in
  let res = Int64.logor (Int64.shift_left res 8)
                        (Int64.of_int (int_of_char (String.get s (i+3)))) in
  let res = Int64.logor (Int64.shift_left res 8)
                        (Int64.of_int (int_of_char (String.get s (i+2)))) in
  let res = Int64.logor (Int64.shift_left res 8)
                        (Int64.of_int (int_of_char (String.get s (i+1)))) in
  let res = Int64.logor (Int64.shift_left res 8)
                        (Int64.of_int (int_of_char (String.get s i))) in
  (res, i+8)

let unpackString s i =
  let len = int_of_char (String.get s (i+1)) in
  let len = (len lsl 8) lor (int_of_char (String.get s i)) in
  (String.sub s (i+2) len, i+2+len)

let unpackData s i =
  let len = int_of_char (String.get s (i+3)) in
  if len > 0x7f then raise ParseError else
  let len = (len lsl 8) lor (int_of_char (String.get s (i+2))) in
  let len = (len lsl 8) lor (int_of_char (String.get s (i+1))) in
  let len = (len lsl 8) lor (int_of_char (String.get s i)) in
  (String.sub s (i+4) len, i+4+len)

let unpackBytes k s i =
  (String.sub s i k, i + k)

let unpackRepeat f s i =
  let len = int_of_char (String.get s (i+1)) in
  let len = (len lsl 8) lor (int_of_char (String.get s i)) in
  let rec gather n i res =
    if n = 0 then (List.rev res, i) else
    let (elt, i') = f s i in
    gather (n - 1) i' (elt :: res)
  in
  gather len (i+2) []

let unpackQid s i =
  let (q_type, i) = unpackByte s i in
  let (q_version, i) = unpackInt32 s i in
  let (q_path, i) = unpackInt64 s i in
  let res = {
    q_type = q_type;
    q_version = q_version;
    q_path = q_path;
  }
  in (res, i)

let unpackStat s i =
  let (s9_type, i) = unpackInt s i in
  let (s9_dev, i) = unpackInt32 s i in
  let (s9_qid, i) = unpackQid s i in
  let (s9_mode, i) = unpackInt32 s i in
  let (s9_atime, i) = unpackInt32 s i in
  let (s9_mtime, i) = unpackInt32 s i in
  let (s9_length, i) = unpackInt64 s i in
  let (s9_name, i) = unpackString s i in
  let (s9_uid, i) = unpackString s i in
  let (s9_gid, i) = unpackString s i in
  let (s9_muid, i) = unpackString s i in
  let (s9_extension, i) = unpackString s i in
  let (s9_n_uid, i) = unpackInt32 s i in
  let (s9_n_gid, i) = unpackInt32 s i in
  let (s9_n_muid, i) = unpackInt32 s i in
  let res = {
    s9_type = s9_type;
    s9_dev = s9_dev;
    s9_qid = s9_qid;
    s9_mode = s9_mode;
    s9_atime = s9_atime;
    s9_mtime = s9_mtime;
    s9_length = s9_length;
    s9_name = s9_name;
    s9_uid = s9_uid;
    s9_gid = s9_gid;
    s9_muid = s9_muid;
    s9_extension = s9_extension;
    s9_n_uid = s9_n_uid;
    s9_n_gid = s9_n_gid;
    s9_n_muid = s9_n_muid;
  }
  in (res, i)

(* packers *)
let packByte s i id =
  String.set s !i (char_of_int id);
  i := !i + 1

let packInt s i n =
  let b1 = char_of_int (n land 0xff) in
  let b2 = char_of_int ((n lsr 8) land 0xff) in
  String.set s !i b1;
  String.set s (!i + 1) b2;
  i := !i + 2

let packInt32 s i n =
  let b1 = char_of_int (Int32.to_int
      (Int32.logand 0xffl n)) in
  let b2 = char_of_int (Int32.to_int
      (Int32.logand 0xffl (Int32.shift_right n 8))) in
  let b3 = char_of_int (Int32.to_int
      (Int32.logand 0xffl (Int32.shift_right n 16))) in
  let b4 = char_of_int (Int32.to_int
      (Int32.logand 0xffl (Int32.shift_right n 24))) in
  String.set s !i b1;
  String.set s (!i + 1) b2;
  String.set s (!i + 2) b3;
  String.set s (!i + 3) b4;
  i := !i + 4

let packInt64 s i n =
  let b1 = char_of_int (Int64.to_int
      (Int64.logand 0xffL n)) in
  let b2 = char_of_int (Int64.to_int
      (Int64.logand 0xffL (Int64.shift_right n 8))) in
  let b3 = char_of_int (Int64.to_int
      (Int64.logand 0xffL (Int64.shift_right n 16))) in
  let b4 = char_of_int (Int64.to_int
      (Int64.logand 0xffL (Int64.shift_right n 24))) in
  let b5 = char_of_int (Int64.to_int
      (Int64.logand 0xffL (Int64.shift_right n 32))) in
  let b6 = char_of_int (Int64.to_int
      (Int64.logand 0xffL (Int64.shift_right n 40))) in
  let b7 = char_of_int (Int64.to_int
      (Int64.logand 0xffL (Int64.shift_right n 48))) in
  let b8 = char_of_int (Int64.to_int
      (Int64.logand 0xffL (Int64.shift_right n 56))) in
  String.set s !i b1;
  String.set s (!i + 1) b2;
  String.set s (!i + 2) b3;
  String.set s (!i + 3) b4;
  String.set s (!i + 4) b5;
  String.set s (!i + 5) b6;
  String.set s (!i + 6) b7;
  String.set s (!i + 7) b8;
  i := !i + 8

let packBytes s i str k =
  String.blit str 0 s !i k;
  i := !i + k

let packString s i str =
  let len = String.length str in
  packInt s i len;
  packBytes s i str len

let packData s i str =
  let len = String.length str in
  packInt32 s i (Int32.of_int len);
  packBytes s i str len

let packQid s i qid =
  packByte s i qid.q_type;
  packInt32 s i qid.q_version;
  packInt64 s i qid.q_path

let packStat s i info =
  packInt s i info.s9_type;
  packInt32 s i info.s9_dev;
  packQid s i info.s9_qid;
  packInt32 s i info.s9_mode;
  packInt32 s i info.s9_atime;
  packInt32 s i info.s9_mtime;
  packInt64 s i info.s9_length;
  packString s i info.s9_name;
  packString s i info.s9_uid;
  packString s i info.s9_gid;
  packString s i info.s9_muid;
  packString s i info.s9_extension;
  packInt32 s i info.s9_n_uid;
  packInt32 s i info.s9_n_gid;
  packInt32 s i info.s9_n_muid

let makeStat info =
  let size = 61 +
      String.length info.s9_name +
      String.length info.s9_uid +
      String.length info.s9_gid +
      String.length info.s9_muid +
      String.length info.s9_extension
  in
  let s = String.create (size + 2) in
  let i = ref 0 in
  packInt s i size;
  packStat s i info;
  s
