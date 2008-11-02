open Unix;;
open Unix.LargeFile;;
open State;;
open U9P2000u;;

let time2int32 = Int32.of_float

let stat2qid info =
  let mode = match info.st_kind with
             | S_DIR -> 0x80
             | S_LNK -> 0x02
             | _     -> 0x00
  in
  let version = Int32.logxor (Int32.of_float info.st_mtime) 
                             (Int32.shift_left (Int64.to_int32 info.st_size) 8)
  in
  let path = Int64.logor (Int64.shift_left (Int64.of_int info.st_dev) 32)
                         (Int64.of_int info.st_ino)
  in
  let i = ref 0 in
  let res = String.create 13 in
  ParseHelpers.packByte res i mode;
  ParseHelpers.packInt32 res i version;
  ParseHelpers.packInt64 res i path;
  res

let stat2stat info path =
  let qid = stat2qid info in
  let s = String.create (Int32.to_int !Config.maxSize) in
  let i = ref 2 in
  (* type *)
  ParseHelpers.packInt s i 0;
  (* device *)
  ParseHelpers.packInt32 s i 0l;
  (* qid *)
  ParseHelpers.packBytes s i qid (String.length qid);
  (* mode *)
  let mode = Int32.logor (Int32.of_int info.st_perm)
                         (match info.st_kind with
                         | S_DIR -> _DMDIR
                         | S_LNK -> 0x02000000l
                         | _     -> 0x00000000l)
  in
  ParseHelpers.packInt32 s i mode;
  (* atime *)
  ParseHelpers.packInt32 s i (time2int32 info.st_atime);
  (* mtime *)
  ParseHelpers.packInt32 s i (time2int32 info.st_mtime);
  (* length *)
  ParseHelpers.packInt64 s i info.st_size;
  (* name *)
  ParseHelpers.packString s i
      (if path = "" then "/" else Filename.basename path);
  (* uid *)
  ParseHelpers.packString s i (string_of_int info.st_uid);
  (* gid *)
  ParseHelpers.packString s i (string_of_int info.st_gid);
  (* muid *)
  ParseHelpers.packString s i (string_of_int info.st_uid);
  let len = !i in
  i := 0;
  ParseHelpers.packInt s i (len - 2);
  String.sub s 0 len

let mode2flags mode =
  let base = match mode land 3 with
             (* _OREAD *)
             | 0x00 -> O_RDONLY
             (* _OWRITE *)
             | 0x01 -> O_WRONLY
             (* _ORDWR *)
             | 0x02 -> O_RDWR
             (* _OEXEC *)
             | _ -> O_RDONLY
  in
  if mode land _OTRUNC = 0 then [base] else [O_TRUNC; base]

let error tag errno ename = 
    Rerror {
      rerror_ename = ename;
      rerror_errno = errno;
      rerror_tag = tag;
    }

let concatPath parent name =
  match name with
  | "." -> parent
  | ".." -> Filename.dirname parent
  | n -> Filename.concat parent name

let maxlinkdepth = 20
let resolvePathsImp followsym base ext =
  let rec f base ext depth =
    if depth < 0 then raise (Unix_error (ELOOP, "resolvePaths", ext)) else
    let i = ref 0 in 
    while (!i < String.length ext && ext.[!i] =  '/') do i := !i + 1 done;
    let j = ref !i in
    while (!j < String.length ext && ext.[!j] <> '/') do j := !j + 1 done;
    let k = ref !j in
    while (!k < String.length ext && ext.[!k] =  '/') do k := !k + 1 done;
    if !i = !j then base else
    let root = if !i = 0 then base else "/" in
    let newbase = match (String.sub ext !i (!j - !i)) with
                  | "."  -> root
                  | ".." -> Filename.dirname root
                  | name -> concatPath root name in
    let newext = String.sub ext !k ((String.length ext) - !k) in
    let info = lstat newbase in
    match info.st_kind with
    | S_DIR -> f newbase newext maxlinkdepth
    | S_LNK when followsym ->
               f root (concatPath (readlink newbase) newext) (depth - 1)
    | _     -> raise (Unix_error (ENOTDIR, "resolvePaths", ext))
  in f base ext maxlinkdepth
    
let resolvePaths = resolvePathsImp true
      
let resolvePathsNoSymlinks = resolvePathsImp false

(*****************************************************************************)

let respondTversion state fd { tversion_msize = msize;
                               tversion_version = version;
                               tversion_tag = tag; } =
    if version <> "9P2000.u"
      then error tag (-1) "Unknown protocol: I only understand 9P2000.u" else
    if msize < 1024l
      then error tag (-1) "Invalid maxSize: must be >1023" else
    Rversion {
      rversion_msize = min msize !Config.maxSize;
      rversion_version = version;
      rversion_tag = tag;
    }

let respondTauth state fd { tauth_afid = afid;
                            tauth_uname = uname;
                            tauth_aname = aname;
                            tauth_tag = tag; } =
    error tag (-1) "auth: Authentication not required"

let respondTattach state fd { tattach_fid = fid;
                              tattach_afid = afid;
                              tattach_uname = uname;
                              tattach_aname = aname;
                              tattach_tag = tag; } =
    if afid <> _NOFID
      then error tag (-1) "attach: Authentication not required" else
    if Hashtbl.mem state.fids (fd, fid)
      then error tag (-1) "attach: fid already in use" else
    let path = resolvePaths (resolvePaths "/" !Config.root) aname in
    let newfid = { fid_self = (fd, fid);
                   fid_user = uname;
                   fid_fd = Closed;
                   fid_path = path;
                   fid_omode = 0; } in
    Hashtbl.add state.fids newfid.fid_self newfid;
    Rattach {
      rattach_qid = stat2qid (lstat path);
      rattach_tag = tag;
    }

let respondTflush state fd { tflush_oldtag = oldtag; tflush_tag = tag; } =
    Rflush {
      rflush_tag = tag;
    }

let respondTwalk state fd { twalk_fid = fid;
                            twalk_newfid = newfid;
                            twalk_wname = wname;
                            twalk_tag = tag; } =
    if fid != newfid && Hashtbl.mem state.fids (fd, newfid)
      then error tag (-1) "walk: fid already in use" else

    let elt = Hashtbl.find state.fids (fd, fid) in
    if elt.fid_fd <> Closed
      then error tag (-1) "walk: fid is for open file" else
    let info = lstat elt.fid_path in
    if wname = []
      then let elt' = { elt with fid_self = (fd, newfid); } in
           Hashtbl.replace state.fids elt'.fid_self elt';
           Rwalk { rwalk_wqid = []; rwalk_tag = tag; }
    else
    if info.st_kind <> S_DIR then error tag (-1) "walk: not a directory" else
    let rec walk prefix lst a =
      match lst with
      | [] -> (List.rev a, prefix)
      | x::xs ->
          try let prefix' = resolvePaths prefix x in
              let info' = lstat prefix' in
              walk prefix' xs (stat2qid info' :: a)
          with _ -> (List.rev a, prefix)
    in
    let (nwqid, newpath) = walk elt.fid_path wname [] in
    if nwqid = [] then error tag (-1) "walk: failed on first entry" else
    (if List.length nwqid = List.length wname
      then let elt' = { fid_self = (fd, newfid);
                        fid_user = elt.fid_user;
                        fid_fd = Closed;
                        fid_path = newpath;
                        fid_omode = 0; }
           in
           Hashtbl.replace state.fids elt'.fid_self elt'
      else ();
    Rwalk {
      rwalk_wqid = nwqid;
      rwalk_tag = tag;
    })

let respondTopen state fd { topen_fid = fid;
                            topen_mode = mode;
                            topen_tag = tag; } =
    let elt = Hashtbl.find state.fids (fd, fid) in
    if elt.fid_fd <> Closed then error tag (-1) "open: already open" else
    let info = lstat elt.fid_path in
    (* ignore permissions for now *)
    match (info.st_kind, mode) with
    (* (S_DIR, _OREAD) *)
    | (S_DIR, 0x00) ->
        elt.fid_fd <- Dh (opendir elt.fid_path, 0L, None);
        elt.fid_omode <- mode;
        Ropen {
          ropen_qid = stat2qid info;
          ropen_iounit = 0l;
          ropen_tag = tag;
        }
    | (S_DIR, _) ->
        error tag (-1) "open: directory can only be opened for reading"
    | (_, _) ->
        let flags = mode2flags mode in
        elt.fid_fd <- Fd (openfile elt.fid_path flags 0);
        elt.fid_omode <- mode;
        Ropen {
          ropen_qid = stat2qid info;
          ropen_iounit = 0l;
          ropen_tag = tag;
        }

let respondTcreate state fd { tcreate_fid = fid;
                              tcreate_name = name;
                              tcreate_perm = perm;
                              tcreate_mode = mode;
                              tcreate_tag = tag; } =
    let elt = Hashtbl.find state.fids (fd, fid) in
    if elt.fid_fd <> Closed then error tag (-1) "create: fid in use" else
    let dirinfo = lstat elt.fid_path in
    if dirinfo.st_kind <> S_DIR
        then error tag (-1) "create: not a directory" else
    let newpath = resolvePaths elt.fid_path name in
    (* ignore permissions for now *)
    match (Int32.logand perm 0x80000000l, mode) with
    | (0l, _) ->
        let flags = O_CREAT :: O_EXCL :: mode2flags mode in
        let newperm = (Int32.to_int perm) land ((lnot 0666) lor
                                                (dirinfo.st_perm land 0666))
        in
        elt.fid_fd <- Fd (openfile newpath flags newperm);
        elt.fid_omode <- mode;
        elt.fid_path <- newpath;
        Rcreate {
          rcreate_qid = stat2qid (lstat newpath);
          rcreate_iounit = 0l;
          rcreate_tag = tag;
        }
    (* (_, _OREAD) *)
    | (_, 0x00) ->
        let newperm = (Int32.to_int perm) land ((lnot 0666) lor
                                                (dirinfo.st_perm land 0666))
        in
        mkdir newpath newperm;
        elt.fid_fd <- Dh (opendir newpath, 0L, None);
        elt.fid_omode <- mode;
        elt.fid_path <- newpath;
        Rcreate {
          rcreate_qid = stat2qid (lstat newpath);
          rcreate_iounit = 0l;
          rcreate_tag = tag;
        }
    | (_, _) ->
        error tag (-1) "create: directory can only be opened for reading"

exception Stop of string

let respondTread state fd { tread_fid = fid;
                            tread_offset = offset;
                            tread_count = count;
                            tread_tag = tag; } =
    let elt = Hashtbl.find state.fids (fd, fid) in
    match elt.fid_fd with
    | Closed -> error tag (-1) "read: file not open"
    | Fd handle ->
        ignore (lseek handle offset SEEK_SET);
        let res = String.create (Int32.to_int count) in
        let len = read handle res 0 (String.length res) in
        if len = String.length res then
            Rread {
              rread_data = res;
              rread_tag = tag;
            }
        else if len > 0 then
            Rread {
              rread_data = String.sub res 0 len;
              rread_tag = tag;
            }
        else
            error tag (-1) "read: failed read request"
    | Dh (handle, oldoffset, next) ->  
      begin
        if offset = 0L && oldoffset <> 0L then rewinddir handle else ();
        if offset <> 0L && offset <> oldoffset
          then error tag (-1) "read: illegal directory seek" else
        let res = String.create (Int32.to_int count) in
        let i = ref 0 in
        let add s name =
            if !i + String.length s > String.length res
              then raise (Stop name)
              else (String.blit s 0 res !i (String.length s);
                   i := !i + String.length s)
        in
        (match next with
        | None -> ()
        | Some name -> add (stat2stat (lstat (concatPath elt.fid_path name))
                                      name)
                           name);
        let name' =
          begin
            try
              while String.length res - !i >= minStatSize do
                let name = readdir handle in
                let info = lstat (concatPath elt.fid_path name) in
                add (stat2stat info name) name
              done;
              None
            with Stop name -> Some name
               | End_of_file -> None
          end
        in
        elt.fid_fd <- Dh (handle, Int64.add oldoffset (Int64.of_int !i), name');
        Rread {
          rread_data =  if !i = String.length res then res
                        else String.sub res 0 !i;
          rread_tag = tag;
        }
      end

let respondTwrite state fd { twrite_fid = fid;
                             twrite_offset = offset;
                             twrite_data = data;
                             twrite_tag = tag; } =
    error tag (-1) "write: not implemented"

let respondTclunk state fd { tclunk_fid = fid; tclunk_tag = tag; } =
    let elt = Hashtbl.find state.fids (fd, fid) in
    (match elt.fid_fd with
    | Fd handle -> close handle
    | Dh (handle, _, _) -> closedir handle
    | Closed -> ());
    if elt.fid_omode land _ORCLOSE = _ORCLOSE then unlink elt.fid_path else ();
    Hashtbl.remove state.fids (fd, fid);
    Rclunk {
      rclunk_tag = tag;
    }

let respondTremove state fd { tremove_fid = fid; tremove_tag = tag; } =
    let elt = Hashtbl.find state.fids (fd, fid) in
    (match elt.fid_fd with
     | Fd handle -> close handle; unlink elt.fid_path
     | Dh (handle, _, _) -> closedir handle; rmdir elt.fid_path
     | Closed ->
        let info = lstat elt.fid_path in
        (match info.st_kind with
        | S_DIR -> rmdir elt.fid_path
        | _ -> unlink elt.fid_path));
    Hashtbl.remove state.fids (fd, fid);
    Rremove {
      rremove_tag = tag;
    }

let respondTstat state fd { tstat_fid = fid; tstat_tag = tag; } =
    let elt = Hashtbl.find state.fids (fd, fid) in
    let info = lstat elt.fid_path in
    Rstat {
      rstat_stat = stat2stat info elt.fid_path;
      rstat_tag = tag;
    }

let respondTwstat state fd { twstat_fid = fid;
                             twstat_stat = stat;
                             twstat_tag = tag; } =
    error tag (-1) "wstat: not implemented"

let respondMessage state fd msg =
  match msg with
  | Tversion msg -> respondTversion state fd msg
  | Tauth msg -> respondTauth state fd msg
  | Tflush msg -> respondTflush state fd msg
  | Tattach msg -> respondTattach state fd msg
  | Twalk msg -> respondTwalk state fd msg
  | Topen msg -> respondTopen state fd msg
  | Tcreate msg -> respondTcreate state fd msg
  | Tread msg -> respondTread state fd msg
  | Twrite msg -> respondTwrite state fd msg
  | Tclunk msg -> respondTclunk state fd msg
  | Tremove msg -> respondTremove state fd msg
  | Tstat msg -> respondTstat state fd msg
  | Twstat msg -> respondTwstat state fd msg
  | msg ->
      Rerror {
          rerror_tag = getTag msg;
          rerror_ename = "invalid message received";
          rerror_errno = 15;
      }
