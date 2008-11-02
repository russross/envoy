open Genlex;;
open Format;;

exception BadMessage

type field = FName of string
           | FInt1 of string
           | FInt2 of string
           | FInt4 of string
           | FInt8 of string
           | FBytes of string * int
           | FData of string
           | FString of string
           | FRepeat of string * field

let rec fieldName f =
  match f with
  | FName s -> raise BadMessage
  | FInt1 s -> s
  | FInt2 s -> s
  | FInt4 s -> s
  | FInt8 s -> s
  | FBytes (s, _) -> s
  | FData s -> s
  | FString s -> s
  | FRepeat (_, f) -> fieldName f

let rec fieldType f =
  match f with
  | FName _ -> raise BadMessage
  | FInt1 _ -> "int"
  | FInt2 _ -> "int"
  | FInt4 _ -> "int32"
  | FInt8 _ -> "int64"
  | FBytes _ -> "string"
  | FData _ -> "string"
  | FString _ -> "string"
  | FRepeat (_, f) -> (fieldType f)^" list"

let rec fieldWorker f =
  match f with
  | FName _ -> raise BadMessage
  | FInt1 _ -> "byte"
  | FInt2 _ -> "int"
  | FInt4 _ -> "int32"
  | FInt8 _ -> "int64"
  | FBytes _ -> "bytes"
  | FData _ -> "data"
  | FString _ -> "string"
  | FRepeat _ -> "repeat"

let lexer = make_lexer ["["; "]"; "("; ")"; "*"; "s" ; "n"]

(* Recognizes:
 *
 *   name[1]              -> FInt1 name
 *   name[2]              -> FInt2 name
 *   name[4]              -> FInt4 name
 *   name[8]              -> FInt8 name
 *   name[s]              -> FString name
 *   name[n]              -> FBytes (name, n)
 *   count[4] name[count] -> FData name
 *   name*(...)           -> FRepeat (name, field ...)
 *   name                 -> FName name
 *)

(* Parse a message description into a list of message fields *)
let parseMessage s =
  let rec f lst =
    (match lst with
    | (Ident len1::Kwd "["::Int 2::Kwd "]"::
       Ident len2::Kwd "*"::Kwd "("::rest) when len1 = len2 ->
          (match f rest with
          | (inner, Kwd ")"::rest') -> (FRepeat (len1, inner), rest')
          | _ -> raise BadMessage)
    | (Ident len1::Kwd "["::Int 4::Kwd "]"::
       Ident name::Kwd "["::Ident len2::Kwd "]"::rest) when len1 = len2 ->
          (FData name, rest)
    | (Ident name::Kwd "["::Int 1::Kwd "]"::rest) -> (FInt1 name, rest)
    | (Ident name::Kwd "["::Int 2::Kwd "]"::rest) -> (FInt2 name, rest)
    | (Ident name::Kwd "["::Int 4::Kwd "]"::rest) -> (FInt4 name, rest)
    | (Ident name::Kwd "["::Int 8::Kwd "]"::rest) -> (FInt8 name, rest)
    | (Ident name::Kwd "["::Int l::Kwd "]"::rest) -> (FBytes (name, l), rest)
    | (Ident name::Kwd "["::Kwd "s"::Kwd "]"::rest) -> (FString name, rest)
    | (Ident name::Kwd "["::Kwd "n"::Kwd "]"::rest) -> (FString name, rest)
    | (Ident name::rest) -> (FName (String.uncapitalize name), rest)
    | _ -> raise BadMessage) in
  let rec gather lst =
    if lst = [] then []
    else let (x, lst') = f lst in x :: gather lst'
  in
    match (Stream.npeek 100 (lexer (Stream.of_string s))) with
    | Int id::rest -> (id, gather rest)
    | _ -> raise BadMessage

(* Print out a message type definition based on a list of message fields *)
let outputType out fields id =
  match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
    begin
      let outputLine elt =
        fprintf out "%s_%s: %s;@," msg (fieldName elt) (fieldType elt)
      in
      fprintf out "@[<v 4>type %s = {@," msg;
      List.iter outputLine rest;
      fprintf out "%s_tag: int;@]@\n}@\n" msg;
    end
  | _ -> raise BadMessage

(* Print out a message parser based on a list of message fields *)
let outputUnpacker out fields id =
  match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
    begin
      let parseLine elt =
        (match elt with
        | FBytes (name, k) ->
            fprintf out "let (%s, i) = unpackBytes %d s i in@," name k
        | FRepeat (name, field) ->
            fprintf out "let (%s, i) = unpackRepeat %s s i in@,"
                    (fieldName field)
                    (match field with
                    | FBytes (_, k) -> "(unpackBytes "^(string_of_int k)^")"
                    | FRepeat _ -> raise BadMessage
                    | f -> "unpack"^(String.capitalize (fieldWorker f)))
        | f ->
            fprintf out "let (%s, i) = unpack%s s i in@,"
                        (fieldName f) (String.capitalize (fieldWorker f)))
      in
      let assignLine elt =
        fprintf out "%s_%s = %s;@," msg (fieldName elt) (fieldName elt)
      in
      fprintf out "@[<v 4>let unpack%s s =@," (String.capitalize msg);
      fprintf out
        "if int_of_char (String.get s 0) != %d then raise ParseError else@," id;
      fprintf out "let (tag, i) = unpackInt s 1 in@,";
      List.iter parseLine rest;
      fprintf out "if i != String.length s then raise ParseError else@,";
      fprintf out "@[<v 4>{@,";
      List.iter assignLine rest;
      fprintf out "%s_tag = tag;@]@,}@]@," msg;
    end
  | _ -> raise BadMessage

(* Print out a message serializer based on a list of message fields *)
let outputPacker out fields id =
  match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
    begin
      let fieldLine elt =
        fprintf out "%s_%s = %s;@ " msg (fieldName elt) (fieldName elt)
      in
      let packLine elt =
        (match elt with
        | FRepeat (_, FBytes (name, k)) ->
            fprintf out "packInt s i (List.length %s);@," name;
            fprintf out "List.iter (fun elt -> packBytes s i elt %d) %s;@,"
                    k name
        | FRepeat (_, field) ->
            fprintf out "packInt s i (List.length %s);@," (fieldName field);
            fprintf out "List.iter (fun elt -> pack%s s i elt) %s;@,"
                    (String.capitalize (fieldWorker field)) (fieldName field)
        | FBytes (name, k) -> fprintf out "packBytes s i %s %d ;@," name k
        | f -> fprintf out "pack%s s i %s;@,"
                    (String.capitalize (fieldWorker f)) (fieldName f))
      in
      fprintf out "@[<v 4>let pack%s { @[<hv>" (String.capitalize msg);
      List.iter fieldLine rest;
      fprintf out "%s_tag = tag; } =@]@," msg;
      fprintf out "let size = @[<hov>";
      (* size[4] + msgId[1] + tag[2] *)
      let size = ref 7 in
      let compSize elt =
        (match elt with
        | FInt1 _ -> size := !size + 1
        | FInt2 _ -> size := !size + 2
        | FInt4 _ -> size := !size + 4
        | FInt8 _ -> size := !size + 8
        | FBytes (_, k) -> size := !size + k
        | FString name ->
            size := !size + 2;
            fprintf out "String.length %s +@ " name
        | FData name ->
            size := !size + 4;
            fprintf out "String.length %s +@ " name
        | FRepeat (_, FInt1 name) ->
            size := !size + 1;
            fprintf out "List.length %s +@ " name
        | FRepeat (_, FInt2 name) ->
            size := !size + 2;
            fprintf out "2 * List.length %s +@ " name
        | FRepeat (_, FInt4 name) ->
            size := !size + 2;
            fprintf out "4 * List.length %s +@ " name
        | FRepeat (_, FInt8 name) ->
            size := !size + 2;
            fprintf out "8 * List.length %s +@ " name
        | FRepeat (_, FBytes (name, k)) ->
            size := !size + 2;
            fprintf out "%d * List.length %s +@ " k name
        | FRepeat (_, FString name) ->
            size := !size + 2;
            fprintf out "List.fold_left ";
            fprintf out "(fun a s -> 2 + a + String.length s) 0 %s +@ " name
        | FRepeat (_, FData name) ->
            size := !size + 2;
            fprintf out "List.fold_left ";
            fprintf out "(fun a s -> 4 + a + String.length s) 0 %s +@ " name
        | _ -> raise BadMessage)
      in
      List.iter compSize rest;
      fprintf out "%d in@]@," !size;
      fprintf out "let s = String.create size in@,";
      fprintf out "let i = ref 0 in@,";
      fprintf out "packInt32 s i (Int32.of_int size);@,";
      fprintf out "packByte s i %d;@," id;
      fprintf out "packInt s i tag;@,";
      List.iter packLine rest;
      fprintf out "s@]@,"
    end
  | _ -> raise BadMessage

let getName msg =
  match msg with
  | (id, FInt4 "size"::FName msg::_) -> msg
  | _ -> raise BadMessage
  
let outputMessageType out m =
  fprintf out "type message @[<v>= ";
  match m with
  | x :: xs ->
      fprintf out "%s of %s@," (String.capitalize (getName x))
                               (getName x);
      List.iter (fun x -> fprintf out "| %s of %s@,"
                              (String.capitalize (getName x))
                              (getName x))
                xs;
      fprintf out "@]@,"
  | _ -> raise BadMessage

let outputUnpackSwitch out m =
  fprintf out "@[<v 4>let unpackMessage msg =@,";
  fprintf out "@[<v 4>try@,";
  fprintf out "match int_of_char (String.get msg 0) with@,";
  let matchLine elt =
    let name = String.capitalize (getName elt) in
    fprintf out "| %d -> %s (unpack%s msg)@," (fst elt) name name
  in
  List.iter matchLine m;
  fprintf out "| _ -> raise ParseError@]@,";
  fprintf out "@]with Invalid_argument _ -> raise ParseError@,"

let outputPackSwitch out m =
  fprintf out "@[<v 4>let packMessage msg =@,";
  fprintf out "match msg with@,";
  let matchLine elt =
    let name = String.capitalize (getName elt) in
    fprintf out "| %s msg -> pack%s msg@," name name
  in
  List.iter matchLine m;
  fprintf out "@]@,"

let outputGetTag out m =
  fprintf out "@[<v 4>let getTag msg =@,";
  fprintf out "match msg with@,";
  let matchLine elt =
    let name = getName elt in
    fprintf out "| %s msg -> msg.%s_tag@," (String.capitalize name) name
  in
  List.iter matchLine m;
  fprintf out "@]@,"

let printBytes k name data =
  printf "%s[%d] = [%s]@," name k data

let printByte name data =
  printf "%s[1] = %d@," name data

let printInt name data =
  printf "%s[2] = %d@," name data

let printInt4 name data =
  printf "%s[4] = %ld@," name data

let printInt8 name data =
  printf "%s[8] = %Ld@," name data

let printString name data =
  printf "%s[s] = [%s]@," name data

let printData name data =
  printf "%s = [%s]@," name data

let printRepeat f name data =
  printf "@[<v 4>%s * %d ->@," name (List.length data);
  List.iter f data;
  printf "@]@,"

let outputDebugStubs out fields =
  match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
    begin
      let fieldLine elt =
        fprintf out "%s_%s = %s;@ " msg (fieldName elt) (fieldName elt)
      in
      fprintf out "@[<v 4>let debug%s state fd { @[<hv>"
                  (String.capitalize msg);
      List.iter fieldLine rest;
      fprintf out "%s_tag = tag; } =@]@," msg;
      let printLine elt =
        (match elt with
        | FBytes (name, k) ->
            fprintf out "@,printBytes %d \"%s\" %s;" k name name
        | FRepeat (name, field) ->
            fprintf out "@,printRepeat %s \"%s\" %s;"
                (match field with
                | FBytes (_, k) -> "(printBytes "^(string_of_int k)^")"
                | FRepeat _ -> raise BadMessage
                | f -> "print"^(String.capitalize (fieldWorker f)))
              (fieldName field) (fieldName field)
        | f ->
            fprintf out "@,print%s \"%s\" %s;"
                (String.capitalize (fieldWorker f))
                (fieldName f)
                (fieldName f))
      in
      fprintf out "printf \"@@[<v 4>%s:@@,\";" (String.capitalize msg);
      List.iter printLine (FInt2 "tag" :: rest);
      fprintf out "@,printf \"@@]@@.\"@]@,"
    end
  | _ -> raise BadMessage

;;


let infile = ref "";;

Arg.parse [] (fun s -> infile := s) "usage: gen source.msg";;
if !infile = "" then (printf "usage: gen source.msg\n" ; exit 1);;

let go () =
  let stem = Filename.chop_extension !infile in
  let infp = open_in !infile in
  let mlfp = open_out (stem ^ ".ml") in
  let ml = formatter_of_out_channel mlfp in
  let mlifp = open_out (stem ^ ".mli") in
  let mli = formatter_of_out_channel mlifp in
  let rec readlines fp =
    try let line = input_line fp in
        if String.contains line '['
          then parseMessage line :: readlines fp
          else readlines fp
    with End_of_file -> []
  in
  let m = readlines infp in
  close_in infp;

  fprintf ml "open ParseHelpers;;@.@.";

  fprintf mli "(* Individual message type definitions *)@.@.";
  List.iter (fun (i,f) -> outputType mli f i; fprintf mli "@.") m;
  fprintf ml "(* Individual message type definitions *)@.@.";
  List.iter (fun (i,f) -> outputType ml f i; fprintf ml "@.") m;

  fprintf ml "@.(* Individual message unpacker functions *)@.@.";
  List.iter (fun (i,f) -> outputUnpacker ml f i; fprintf ml "@.") m;

  fprintf ml "@.(* Individual message packer functions *)@.@.";
  List.iter (fun (i,f) -> outputPacker ml f i; fprintf ml "@.") m;

  fprintf mli "@.(* Message type *)@.@.";
  outputMessageType mli m;
  fprintf ml "@.(* Message type *)@.@.";
  outputMessageType ml m;

  fprintf mli "@.(* Message unpacker function *)@.@.";
  fprintf mli "val unpackMessage : string -> message@.";
  fprintf ml "@.(* Message unpacker function *)@.@.";
  outputUnpackSwitch ml m;

  fprintf mli "@.(* Message packer function *)@.@.";
  fprintf mli "val packMessage : message -> string@.";
  fprintf ml "@.(* Message packer function *)@.@.";
  outputPackSwitch ml m;

  fprintf mli "@.(* Get tag function *)@.@.";
  fprintf mli "val getTag : message -> int@.";
  fprintf ml "@.(* Get tag function *)@.@.";
  outputGetTag ml m;

  fprintf mli "@?";
  fprintf ml "@?";

  close_out mlifp;
  close_out mlfp;;

(*
  let debugfp = open_out (stem ^ "_debug.ml") in
  let debug = formatter_of_out_channel debugfp in
  List.iter (fun (i,f) -> outputDebugStubs debug f; fprintf debug "@.") m;
  fprintf debug "@?";
  close_out debugfp;;
*)

go ();;
