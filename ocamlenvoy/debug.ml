open U9P2000u;;
open Format;;

let printBytes k name data =
  if k = 13 then
    begin
      printf "%s[%d] = [" name k;
      (match (int_of_char (String.get data 0)) with
      | 0x80 -> printf "dir"
      | 0x02 -> printf "lnk"
      | _    -> printf "reg");
      let (version, _) = ParseHelpers.unpackInt32 data 1 in
      let (path, _) = ParseHelpers.unpackInt64 data 5 in
      printf ", ver=%ld, path=08x%0Lx]@," version path
    end
  else
    printf "%s[%d] = %S@," name k data

let printByte name data =
  printf "%s[1] = %d@," name data

let printInt name data =
  printf "%s[2] = %d@," name data

let printInt32 name data =
  printf "%s[4] = %ld@," name data 

let printInt64 name data =
  printf "%s[8] = %Ld@," name data

let printString name data =
  printf "%s[s] = %S@," name data

let printData name data =
  printf "%s[%d] = %S@," name (String.length data) data

let printRepeat f name data =
  printf "@[<v 4>%s * %d ->@," name (List.length data);
  let n = ref 0 in
  List.iter (fun elt -> f (string_of_int !n) elt; incr n) data;
  printf "@]@,"


let debugTversion state fd { tversion_msize = msize;
                             tversion_version = version;
                             tversion_tag = tag; } =
    printf "@[<v 4>Tversion:@,";
    printInt "tag" tag;
    printInt32 "msize" msize;
    printString "version" version;
    printf "@]@."

let debugRversion state fd { rversion_msize = msize;
                             rversion_version = version;
                             rversion_tag = tag; } =
    printf "@[<v 4>Rversion:@,";
    printInt "tag" tag;
    printInt32 "msize" msize;
    printString "version" version;
    printf "@]@."

let debugTauth state fd { tauth_afid = afid;
                          tauth_uname = uname;
                          tauth_aname = aname;
                          tauth_tag = tag; } =
    printf "@[<v 4>Tauth:@,";
    printInt "tag" tag;
    printInt32 "afid" afid;
    printString "uname" uname;
    printString "aname" aname;
    printf "@]@."

let debugRauth state fd { rauth_aqid = aqid; rauth_tag = tag; } =
    printf "@[<v 4>Rauth:@,";
    printInt "tag" tag;
    printBytes 13 "aqid" aqid;
    printf "@]@."

let debugRerror state fd { rerror_ename = ename;
                           rerror_errno = errno;
                           rerror_tag = tag; } =
    printf "@[<v 4>Rerror:@,";
    printInt "tag" tag;
    printString "ename" ename;
    printInt "errno" errno;
    printf "@]@."

let debugTflush state fd { tflush_oldtag = oldtag; tflush_tag = tag; } =
    printf "@[<v 4>Tflush:@,";
    printInt "tag" tag;
    printInt "oldtag" oldtag;
    printf "@]@."

let debugRflush state fd { rflush_tag = tag; } =
    printf "@[<v 4>Rflush:@,";
    printInt "tag" tag;
    printf "@]@."

let debugTattach state fd { tattach_fid = fid;
                            tattach_afid = afid;
                            tattach_uname = uname;
                            tattach_aname = aname;
                            tattach_tag = tag; } =
    printf "@[<v 4>Tattach:@,";
    printInt "tag" tag;
    printInt32 "fid" fid;
    printInt32 "afid" afid;
    printString "uname" uname;
    printString "aname" aname;
    printf "@]@."

let debugRattach state fd { rattach_qid = qid; rattach_tag = tag; } =
    printf "@[<v 4>Rattach:@,";
    printInt "tag" tag;
    printBytes 13 "qid" qid;
    printf "@]@."

let debugTwalk state fd { twalk_fid = fid;
                          twalk_newfid = newfid;
                          twalk_wname = wname;
                          twalk_tag = tag; } =
    printf "@[<v 4>Twalk:@,";
    printInt "tag" tag;
    printInt32 "fid" fid;
    printInt32 "newfid" newfid;
    printRepeat printString "wname" wname;
    printf "@]@."

let debugRwalk state fd { rwalk_wqid = wqid; rwalk_tag = tag; } =
    printf "@[<v 4>Rwalk:@,";
    printInt "tag" tag;
    printRepeat (printBytes 13) "wqid" wqid;
    printf "@]@."

let debugTopen state fd { topen_fid = fid;
                          topen_mode = mode;
                          topen_tag = tag; } =
    printf "@[<v 4>Topen:@,";
    printInt "tag" tag;
    printInt32 "fid" fid;
    printByte "mode" mode;
    printf "@]@."

let debugRopen state fd { ropen_qid = qid;
                          ropen_iounit = iounit;
                          ropen_tag = tag; } =
    printf "@[<v 4>Ropen:@,";
    printInt "tag" tag;
    printBytes 13 "qid" qid;
    printInt32 "iounit" iounit;
    printf "@]@."

let debugTcreate state fd { tcreate_fid = fid;
                            tcreate_name = name;
                            tcreate_perm = perm;
                            tcreate_mode = mode;
                            tcreate_tag = tag; } =
    printf "@[<v 4>Tcreate:@,";
    printInt "tag" tag;
    printInt32 "fid" fid;
    printString "name" name;
    printInt32 "perm" perm;
    printByte "mode" mode;
    printf "@]@."

let debugRcreate state fd { rcreate_qid = qid;
                            rcreate_iounit = iounit;
                            rcreate_tag = tag; } =
    printf "@[<v 4>Rcreate:@,";
    printInt "tag" tag;
    printBytes 13 "qid" qid;
    printInt32 "iounit" iounit;
    printf "@]@."

let debugTread state fd { tread_fid = fid;
                          tread_offset = offset;
                          tread_count = count;
                          tread_tag = tag; } =
    printf "@[<v 4>Tread:@,";
    printInt "tag" tag;
    printInt32 "fid" fid;
    printInt64 "offset" offset;
    printInt32 "count" count;
    printf "@]@."

let debugRread state fd { rread_data = data; rread_tag = tag; } =
    printf "@[<v 4>Rread:@,";
    printInt "tag" tag;
    printData "data" data;
    printf "@]@."

let debugTwrite state fd { twrite_fid = fid;
                           twrite_offset = offset;
                           twrite_data = data;
                           twrite_tag = tag; } =
    printf "@[<v 4>Twrite:@,";
    printInt "tag" tag;
    printInt32 "fid" fid;
    printInt64 "offset" offset;
    printData "data" data;
    printf "@]@."

let debugRwrite state fd { rwrite_count = count; rwrite_tag = tag; } =
    printf "@[<v 4>Rwrite:@,";
    printInt "tag" tag;
    printInt32 "count" count;
    printf "@]@."

let debugTclunk state fd { tclunk_fid = fid; tclunk_tag = tag; } =
    printf "@[<v 4>Tclunk:@,";
    printInt "tag" tag;
    printInt32 "fid" fid;
    printf "@]@."

let debugRclunk state fd { rclunk_tag = tag; } =
    printf "@[<v 4>Rclunk:@,";
    printInt "tag" tag;
    printf "@]@."

let debugTremove state fd { tremove_fid = fid; tremove_tag = tag; } =
    printf "@[<v 4>Tremove:@,";
    printInt "tag" tag;
    printInt32 "fid" fid;
    printf "@]@."

let debugRremove state fd { rremove_tag = tag; } =
    printf "@[<v 4>Rremove:@,";
    printInt "tag" tag;
    printf "@]@."

let debugTstat state fd { tstat_fid = fid; tstat_tag = tag; } =
    printf "@[<v 4>Tstat:@,";
    printInt "tag" tag;
    printInt32 "fid" fid;
    printf "@]@."

let debugRstat state fd { rstat_stat = stat; rstat_tag = tag; } =
    printf "@[<v 4>Rstat:@,";
    printInt "tag" tag;
    printString "stat" stat;
    printf "@]@."

let debugTwstat state fd { twstat_fid = fid;
                           twstat_stat = stat;
                           twstat_tag = tag; } =
    printf "@[<v 4>Twstat:@,";
    printInt "tag" tag;
    printInt32 "fid" fid;
    printString "stat" stat;
    printf "@]@."

let debugRwstat state fd { rwstat_tag = tag; } =
    printf "@[<v 4>Rwstat:@,";
    printInt "tag" tag;
    printf "@]@."


let debugMessage state fd msg =
  begin
    match msg with
    | Tversion msg -> debugTversion state fd msg
    | Tauth msg -> debugTauth state fd msg
    | Tflush msg -> debugTflush state fd msg
    | Tattach msg -> debugTattach state fd msg
    | Twalk msg -> debugTwalk state fd msg
    | Topen msg -> debugTopen state fd msg
    | Tcreate msg -> debugTcreate state fd msg
    | Tread msg -> debugTread state fd msg
    | Twrite msg -> debugTwrite state fd msg
    | Tclunk msg -> debugTclunk state fd msg
    | Tremove msg -> debugTremove state fd msg
    | Tstat msg -> debugTstat state fd msg
    | Twstat msg -> debugTwstat state fd msg
    | msg -> ()
  end;
  let res = Fs.respondMessage state fd msg in
  begin
    match res with
    | Rversion msg -> debugRversion state fd msg
    | Rauth msg -> debugRauth state fd msg
    | Rflush msg -> debugRflush state fd msg
    | Rattach msg -> debugRattach state fd msg
    | Rwalk msg -> debugRwalk state fd msg
    | Ropen msg -> debugRopen state fd msg
    | Rcreate msg -> debugRcreate state fd msg
    | Rread msg -> debugRread state fd msg
    | Rwrite msg -> debugRwrite state fd msg
    | Rclunk msg -> debugRclunk state fd msg
    | Rremove msg -> debugRremove state fd msg
    | Rstat msg -> debugRstat state fd msg
    | Rwstat msg -> debugRwstat state fd msg
    | Rerror msg -> debugRerror state fd msg
    | msg -> ()
  end;
  printf "@.";
  res
