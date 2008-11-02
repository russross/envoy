open Unix
(*open Thread*)

let maxSize = 32768

let acceptConnections addr sockq maxServers f =
  print_endline "acceptConnections called";
  let sem = Semaphore.create maxServers in
  let sock = socket PF_INET SOCK_STREAM 0 in
  let _ = bind sock addr in
  let _ = listen sock sockq in
  let rec loop () =
    let conn = accept sock in
    print_endline "accepted connection";
    Semaphore.p sem;
    let service () =
      begin
        try f conn
        with exp -> prerr_endline ("acceptConnections: "^
                                    (Printexc.to_string exp))
      end;
      Semaphore.v sem;
      prerr_endline "connection closed"
    in
    (* ignore (Thread.create service ()); *)
    loop ()
  in
  loop ()

let getMessage sock =
  let sizeBuff = String.create 4 in
  let n = ref 0 in
  while !n < 4 do
    n := !n + recv sock sizeBuff !n (4 - !n) []
  done;
  let size = ((int_of_char (String.get sizeBuff 0)) lor
             ((int_of_char (String.get sizeBuff 1)) lsl 8) lor
             ((int_of_char (String.get sizeBuff 2)) lsl 16) lor
             ((int_of_char (String.get sizeBuff 3)) lsl 24)) - 4
  in
  if size + 4 > maxSize then raise ParseHelpers.ParseError else
  let msg = String.create size in
  let n = ref 0 in
  while !n < size do
    n := !n + recv sock msg !n (size - !n) []
  done;
  msg

let putMessage sock msg =
  if String.length msg > maxSize then raise ParseHelpers.ParseError else
  let n = ref 0 in
  while !n < String.length msg do
    n := !n + send sock msg !n (String.length msg - !n) []
  done

let messageServer f (sock, addr) =
  let rec loop () =
    let msg = getMessage sock in
    let (reply, close) = f msg in
    putMessage sock reply;
    if close then shutdown sock SHUTDOWN_ALL else loop ()
  in loop ()
