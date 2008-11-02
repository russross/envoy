let msg_version =
  Tversion {
    tversion_msize = 1024l;
    tversion_version = "9P2000.u";
    tversion_tag = 65535;
  }

let msg_sversion =
  Tversion {
    tversion_msize = 1024l;
    tversion_version = "9P2000.storage";
    tversion_tag = 65535;
  }

let msg_auth =
  Tauth {
    tauth_afid = 1l;
    tauth_uname = "russ";
    tauth_aname = "";
    tauth_tag = 2;
  }

let msg_attach =
  Tattach {
    tattach_fid = 1l;
    tattach_afid = Int32.lognot 0l;
    tattach_uname = "russ";
    tattach_aname = "";
    tattach_tag = 3;
  }

let msg_walk1 =
  Twalk {
    twalk_fid = 1l;
    twalk_newfid = 2l;
    twalk_wname = [];
    twalk_tag = 4;
  }

let msg_walk2 =
  Twalk {
    twalk_fid = 1l;
    twalk_newfid = 3l;
    twalk_wname = [];
    twalk_tag = 5;
  }

let msg_walk3 =
  Twalk {
    twalk_fid = 1l;
    twalk_newfid = 4l;
    twalk_wname = ["newfile"];
    twalk_tag = 6;
  }

let msg_create =
  Tcreate {
    tcreate_fid = 2l;
    tcreate_name = "newfile";
    tcreate_perm = 0o644l;
    tcreate_mode = 0;
    tcreate_tag = 6;
    tcreate_extension = "";
  }

let msg_create2 =
  Tcreate {
    tcreate_fid = 3l;
    tcreate_name = "newdir";
    tcreate_perm = 0o20000000755l;
    tcreate_mode = 0;
    tcreate_tag = 6;
    tcreate_extension = "";
  }

let msg_open =
  Topen {
    topen_fid = 3l;
    topen_mode = 0;
    topen_tag = 7;
  }

let msg_read =
  Tread {
    tread_fid = 3l;
    tread_offset = 0L;
    tread_count = 4096l;
    tread_tag = 8;
  }

let info = {
  s9_type = 0;
  s9_dev = 0l;
  s9_qid = { q_type = 0; q_version = 0l; q_path = 0L; };
  s9_mode = 0o644l;
  s9_atime = 0x12345678l;
  s9_mtime = 0x87654321l;
  s9_length = 1234567890L;
  s9_name = "newfile";
  s9_uid = "15";
  s9_gid = "51";
  s9_muid = "15";
  s9_extension = "";
  s9_n_uid = 15l;
  s9_n_gid = 51l;
  s9_n_muid = 15l;
}

let msg_screate =
  Tscreate {
    tscreate_oid = 0L;
    tscreate_mode = 0x800001edl;
    tscreate_time = 0x01234567l;
    tscreate_uid = "russ";
    tscreate_gid = "ross";
    tscreate_extension = "";
    tscreate_tag = 9;
  }
;;

(*send msg_version;;
send msg_attach;;
send msg_walk1;;
send msg_walk2;;
send msg_open;;
send msg_read;;
send msg_walk3;;*)

send msg_sversion;;
send msg_screate;;
