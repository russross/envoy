open U9P2000u;;
open Unix;;

let maxSize = 32768;;

let sock = socket PF_INET SOCK_STREAM 0;;

connect sock
        (ADDR_INET ((gethostbyname (gethostname())).h_addr_list.(0), 9923));;

let putMessage sock msg =
  if String.length msg > maxSize then raise ParseHelpers.ParseError else
  let n = ref 0 in
  while !n < String.length msg do
    n := !n + send sock msg !n (String.length msg - !n) []
  done

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

let send msg = putMessage sock (packMessage msg);
               unpackMessage (getMessage sock)
  
;;


