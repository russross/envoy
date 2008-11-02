open Unix;;
open U9P2000u;;
open State;;
open Config;;
open Transport;;

let get_my_addr () =
   ADDR_INET ((gethostbyname (gethostname())).h_addr_list.(0), 9922);;

let sock = socket PF_INET SOCK_STREAM 0;;
bind sock (get_my_addr ());;
listen sock 5;;
set_nonblock sock;;

let state = { listeners = [ sock ];
              conns = Hashtbl.create 10;
              fids = Hashtbl.create 1000; };;

let rec loop state =
  let (fd, msg) = Transport.getMessage state in
  let response = Debug.debugMessage state fd (unpackMessage msg) in
  Transport.putMessage state fd (packMessage response);
  loop state;;

loop state;;

(*
let handleMessage s =
  match respondMessage (unpackMessage s) with
  | Rerror m -> (packMessage (Rerror m), true)
  | m -> (packMessage m, false);;

acceptConnections (get_my_addr ()) 5 5 (messageServer handleMessage);;

print_string "done\n";;
*)
