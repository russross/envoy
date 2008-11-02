open U9P2000u;;
open Format;;

let printBytes k name data =
  printf "%s[%d] = [%s]@," name k data

let printByte name data =
  printf "%s[1] = %d@," name data

let printInt name data =
  printf "%s[2] = %d@," name data

let printInt32 name data =
  printf "%s[4] = %ld@," name data 

let printInt64 name data =
  printf "%s[8] = %Ld@," name data

let printString name data =
  printf "%s[s] = [%s]@," name data

let printData name data =
  printf "%s = [%s]@," name data

let printRepeat f name data =
  printf "@[<v 4>%s * %d ->@," name (List.length data);
  let n = ref 0 in
  List.iter (fun elt -> f (string_of_int !n) elt; incr n) data;
  printf "@]@,"



let respondTversion { tversion_msize = msize;
                      tversion_version = version;
                      tversion_tag = tag; } =
    printInt32 "msize" msize;
    printString "version" version;
    printInt "tag" tag;
    Rversion {
      rversion_msize = msize;
      rversion_version = version;
      rversion_tag = tag;
    }

let respondTauth { tauth_afid = afid;
                   tauth_uname = uname;
                   tauth_aname = aname;
                   tauth_tag = tag; } =
    printInt32 "afid" afid;
    printString "uname" uname;
    printString "aname" aname;
    printInt "tag" tag;
    Rauth {
      rauth_aqid = "0123456789012";
      rauth_tag = tag;
    }

let respondTflush { tflush_oldtag = oldtag; tflush_tag = tag; } =
    printInt "oldtag" oldtag;
    printInt "tag" tag;
    Rflush {
      rflush_tag = tag;
    }

let respondTattach { tattach_fid = fid;
                     tattach_afid = afid;
                     tattach_uname = uname;
                     tattach_aname = aname;
                     tattach_tag = tag; } =
    printInt32 "fid" fid;
    printInt32 "afid" afid;
    printString "uname" uname;
    printString "aname" aname;
    printInt "tag" tag;
    Rattach {
      rattach_qid = "0123456789012";
      rattach_tag = tag;
    }

let respondTwalk { twalk_fid = fid;
                   twalk_newfid = newfid;
                   twalk_wname = wname;
                   twalk_tag = tag; } =
    printInt32 "fid" fid;
    printInt32 "newfid" newfid;
    printRepeat printString "wname" wname;
    printInt "tag" tag;
    Rwalk {
      rwalk_wqid = List.map (fun _ -> "0123456789012") wname;
      rwalk_tag = tag;
    }

let respondTopen { topen_fid = fid; topen_mode = mode; topen_tag = tag; } =
    printInt32 "fid" fid;
    printByte "mode" mode;
    printInt "tag" tag;
    Ropen {
      ropen_qid = "0123456789012";
      ropen_iounit = 54321l;
      ropen_tag = tag;
    }

let respondTcreate { tcreate_fid = fid;
                     tcreate_name = name;
                     tcreate_perm = perm;
                     tcreate_mode = mode;
                     tcreate_tag = tag; } =
    printInt32 "fid" fid;
    printString "name" name;
    printInt32 "perm" perm;
    printByte "mode" mode;
    printInt "tag" tag;
    Rcreate {
      rcreate_qid = "0123456789012";
      rcreate_iounit = 54321l;
      rcreate_tag = tag;
    }

let respondTread { tread_fid = fid;
                   tread_offset = offset;
                   tread_count = count;
                   tread_tag = tag; } =
    printInt32 "fid" fid;
    printInt64 "offset" offset;
    printInt32 "count" count;
    printInt "tag" tag;
    Rread {
      rread_data = "successful read operation";
      rread_tag = tag;
    }

let respondTwrite { twrite_fid = fid;
                    twrite_offset = offset;
                    twrite_data = data;
                    twrite_tag = tag; } =
    printInt32 "fid" fid;
    printInt64 "offset" offset;
    printData "data" data;
    printInt "tag" tag;
    Rwrite {
      rwrite_count = Int32.of_int (String.length data);
      rwrite_tag = tag;
    }

let respondTclunk { tclunk_fid = fid; tclunk_tag = tag; } =
    printInt32 "fid" fid;
    printInt "tag" tag;
    Rclunk {
      rclunk_tag = tag;
    }

let respondTremove { tremove_fid = fid; tremove_tag = tag; } =
    printInt32 "fid" fid;
    printInt "tag" tag;
    Rremove {
      rremove_tag = tag;
    }

let respondTstat { tstat_fid = fid; tstat_tag = tag; } =
    printInt32 "fid" fid;
    printInt "tag" tag;
    Rstat {
      rstat_stat = "stat stuff";
      rstat_tag = tag;
    }

let respondTwstat { twstat_fid = fid;
                    twstat_stat = stat;
                    twstat_tag = tag; } =
    printInt32 "fid" fid;
    printString "stat" stat;
    printInt "tag" tag;
    Rwstat {
      rwstat_tag = tag;
    }

let respondMessage msg =
  printf "@[<v 4>Message contents:@,";
  let res =
    match msg with
    | Tversion msg -> respondTversion msg
    | Tauth msg -> respondTauth msg
    | Tflush msg -> respondTflush msg
    | Tattach msg -> respondTattach msg
    | Twalk msg -> respondTwalk msg
    | Topen msg -> respondTopen msg
    | Tcreate msg -> respondTcreate msg
    | Tread msg -> respondTread msg
    | Twrite msg -> respondTwrite msg
    | Tclunk msg -> respondTclunk msg
    | Tremove msg -> respondTremove msg
    | Tstat msg -> respondTstat msg
    | Twstat msg -> respondTwstat msg
    | msg ->
        Rerror {
            rerror_tag = getTag msg;
            rerror_ename = "invalid message received";
            rerror_errno = 15;
        }
  in
    printf "@]@.";
    res
