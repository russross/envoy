open State
open Unix

exception ClosedSocket of file_descr
exception MessageTooBig of file_descr
exception FoundMessage of file_descr * string

let closedSocket state fd =
  Hashtbl.remove state.conns fd

let rec loopSockets state =
  let (ready, _, _) =
    let fds = State.getConnectionsList state in
    print_endline ("getConnectionsList returns "^
        (string_of_int (List.length fds))^" elements");
    Thread.select fds [] fds (-1.0) 
  in
  print_endline "loopSockets select returned";
  let f fd =
    if Hashtbl.mem state.conns fd then
      begin
        print_endline "loopSockets reading";
        let info = (Hashtbl.find state.conns fd).conn_read in
        if info.n < 4 then
          begin
            print_endline "loopSockets reading size";
            let n = recv fd info.sizeBuffer info.n (4 - info.n) [] in
            if n = 0 then raise (ClosedSocket fd) else
            info.n <- info.n + n;
            if info.n < 4 then () else (info.size <-
                ((int_of_char (String.get info.sizeBuffer 0)) lor
                ((int_of_char (String.get info.sizeBuffer 1)) lsl 8) lor
                ((int_of_char (String.get info.sizeBuffer 2)) lsl 16) lor
                ((int_of_char (String.get info.sizeBuffer 3)) lsl 24)) - 4;
                if info.size + 4 > Int32.to_int !Config.maxSize
                  then raise (MessageTooBig fd)
                  else ();
                info.msg <- String.create info.size)
          end
        else
          begin
            print_endline "loopSockets reading msg";
            let n = recv fd info.msg (info.n - 4) (info.size - info.n + 4) [] in
            if n = 0 then raise (ClosedSocket fd) else
            if info.n - 4 + n = info.size then
              begin
                Hashtbl.replace state.conns fd (State.create fd);
                raise (FoundMessage (fd, info.msg))
              end
            else info.n <- info.n + n
          end
      end
    else
      begin
        print_endline "loopSockets accepting connection";
        let (conn, addr) = accept fd in
        Unix.set_nonblock conn;
        Hashtbl.add state.conns conn (State.create conn)
      end
  in
  try List.iter f ready;
      loopSockets state
  with FoundMessage (fd, msg) -> (fd, msg)
     | ClosedSocket fd -> (closedSocket state fd; loopSockets state)
     | MessageTooBig fd -> (closedSocket state fd; loopSockets state)

let getMessage state =
  print_endline "getMessage called";
  print_endline "getMessage mutex acquired";
  let result = loopSockets state in
  print_endline "getMessage mutex released";
  result

let putMessage state sock msg =
  print_endline "putMessage called";
  if String.length msg > Int32.to_int !Config.maxSize
    then raise ParseHelpers.ParseError else
  let n = ref 0 in
  while !n < String.length msg do
    n := !n + send sock msg !n (String.length msg - !n) []
  done;
  print_endline "putMessage finished"
