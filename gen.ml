open Genlex;;
open Format;;

exception BadMessage

type field = FName of string
           | FInt1 of string
           | FInt2 of string
           | FInt4 of string
           | FInt8 of string
           | FString of string
           | FQid of string
           | FStat of string
           | FData of string * string
           | FStringlist of string * string
           | FQidlist of string * string
           | FInt8list of string * string

let rec fieldName f =
  match f with
  | FName s -> raise BadMessage
  | FInt1 s -> s
  | FInt2 s -> s
  | FInt4 s -> s
  | FInt8 s -> s
  | FData (_, s) -> s
  | FString s -> s
  | FStringlist (_, s) -> s
  | FQid s -> s
  | FQidlist (_, s) -> s
  | FInt8list (_, s) -> s
  | FStat s -> s

let rec getType f =
  match f with
  | FName _ -> raise BadMessage
  | FInt1 _ -> "u8 "
  | FInt2 _ -> "u16 "
  | FInt4 _ -> "u32 "
  | FInt8 _ -> "u64 "
  | FInt8list _ -> "u64 *"
  | FQid _ -> "struct qid "
  | FQidlist _ -> "struct qid *"
  | FData _ -> "u8 *"
  | FString _ -> "char *"
  | FStringlist _ -> "char **"
  | FStat _ -> "struct p9stat *"

let rec fieldWorker f =
  match f with
  | FName _ -> "u8"
  | FInt1 _ -> "u8"
  | FInt2 _ -> "u16"
  | FInt4 _ -> "u32"
  | FInt8 _ -> "u64"
  | FInt8list _ -> "u64list"
  | FQid _ -> "qid"
  | FQidlist _ -> "qidlist"
  | FData _ -> "data"
  | FString _ -> "string"
  | FStringlist _ -> "stringlist"
  | FStat _ -> "statn"

let getName msg =
  match msg with
  | (id, FInt4 "size"::FName msg::_) -> msg
  | _ -> raise BadMessage

let getId = fst

let lexer = make_lexer ["["; "]"; "("; ")"; "*"; "s" ; "n"]

(* Recognizes:
 *
 *   name[1]               -> FInt1 name
 *   name[2]               -> FInt2 name
 *   name[4]               -> FInt4 name
 *   name[8]               -> FInt8 name
 *   name[s]               -> FString name
 *   count[4] name[count]  -> FData name
 *   len[2] len*(name[13]) -> FQidlist name
 *   len[2] len*(name[s])  -> FStringlist name
 *   len[2] len*(name[4])  -> FInt8list name
 *   name[n]               -> FStat name
 *   name                  -> FName name
 *)

(* Parse a message description into a list of message fields *)
let parseMessage s =
  let rec f lst =
    (match lst with
    | (Ident "errno"::rest) -> f (Ident "errnum" :: rest)
    | (Ident len1::Kwd "["::Int 2::Kwd "]"::
       Ident len2::Kwd "*"::Kwd "("::rest) when len1 = len2 ->
          (match f rest with
          | (FString name, Kwd ")"::rest') -> (FStringlist (len1, name), rest')
          | (FQid name, Kwd ")"::rest') -> (FQidlist (len1, name), rest')
          | (FInt8 name, Kwd ")"::rest') -> (FInt8list (len1, name), rest')
          | _ -> raise BadMessage)
    | (Ident len1::Kwd "["::Int 4::Kwd "]"::
       Ident name::Kwd "["::Ident len2::Kwd "]"::rest) when len1 = len2 ->
          (FData (len1, name), rest)
    | (Ident name::Kwd "["::Int 1::Kwd "]"::rest) -> (FInt1 name, rest)
    | (Ident name::Kwd "["::Int 2::Kwd "]"::rest) -> (FInt2 name, rest)
    | (Ident name::Kwd "["::Int 4::Kwd "]"::rest) -> (FInt4 name, rest)
    | (Ident name::Kwd "["::Int 8::Kwd "]"::rest) -> (FInt8 name, rest)
    | (Ident name::Kwd "["::Int 13::Kwd "]"::rest) -> (FQid name, rest)
    | (Ident name::Kwd "["::Kwd "s"::Kwd "]"::rest) -> (FString name, rest)
    | (Ident name::Kwd "["::Kwd "n"::Kwd "]"::rest) -> (FStat name, rest)
    | (Ident name::rest) -> (FName (String.uncapitalize name), rest)
    | _ -> raise BadMessage)
  in
  let rec gather lst =
    if lst = [] then []
    else let (x, lst') = f lst in x :: gather lst'
  in
    match (Stream.npeek 100 (lexer (Stream.of_string s))) with
    | Int id::rest -> (id, gather rest)
    | _ -> raise BadMessage

let outputStructs out fields id =
  match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
    begin
      let fieldLine elt =
        match elt with
        | FName s
        | FInt1 s
        | FInt2 s
        | FInt4 s
        | FInt8 s
        | FString s
        | FQid s
        | FStat s ->
              fprintf out "@,%s%s;" (getType elt) s
        | FData (l, s) ->
              fprintf out "@,u32 %s;" l;
              fprintf out "@,%s%s;" (getType elt) s
        | FQidlist (l, s)
        | FStringlist (l, s)
        | FInt8list (l, s) ->
              fprintf out "@,u16 %s;" l;
              fprintf out "@,%s%s;" (getType elt) s
      in
      fprintf out "@[<v 4>struct %s {" (String.capitalize msg);
      List.iter fieldLine rest;
      fprintf out "@]@,};@.@."
    end
  | _ -> raise BadMessage

let outputMessageStruct out m =
  fprintf out "@[<v 4>struct message {";
  fprintf out "@,u8 *raw;";
  fprintf out "@,u32 size;";
  fprintf out "@,";
  fprintf out "@,u8 id;";
  fprintf out "@,u16 tag;";
  fprintf out "@,";
  fprintf out "@,@[<v 4>union {";
  let structLine elt =
    let name = getName elt in
    fprintf out "@,struct %s %s;" (String.capitalize name) name
  in
  List.iter structLine m;
  fprintf out "@]@,} msg;";
  fprintf out "@]@,};@."

let outputSetters out fields id =
  match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
    begin
      let fieldLine elt =
        let name = fieldName elt in
        (match elt with
          FData (name, _)
        | FStringlist (name, _)
        | FQidlist (name, _)
        | FInt8list (name, _) ->
            fprintf out "@,(m)->msg.%s.%s = (%s_v); \\" msg name name
        | _ -> ());
        fprintf out "@,(m)->msg.%s.%s = (%s_v); \\" msg name name
      in
      let fieldArg elt =
        (match elt with
          FData (name, _)
        | FStringlist (name, _)
        | FQidlist (name, _)
        | FInt8list (name, _) ->
            fprintf out ", %s_v" name
        | _ -> ());
        fprintf out ", %s_v" (fieldName elt)
      in
      fprintf out "@[<v 4>#define set_%s(m" msg;
      List.iter fieldArg rest;
      fprintf out ") do { \\";
      List.iter fieldLine rest;
      fprintf out "@]@,} while(0)@.@.";
    end
  | _ -> raise BadMessage

let outputUnpacker out m =
  let msg (id, fields) =
    match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
      begin
        fprintf out "@,@[<v 4>case %s:" (String.uppercase msg);
        let fieldLine elt =
          match elt with
          | FName name
          | FInt1 name
          | FInt2 name
          | FInt4 name
          | FInt8 name
          | FString name
          | FQid name
          | FStat name ->
              fprintf out "@,@[<hv 8>m->msg.%s.%s =@ " msg name;
              fprintf out "unpack%s(m->raw, (int) m->size, &i);@]"
                  (String.capitalize (fieldWorker elt))
          | FData (len, name)
          | FStringlist (len, name)
          | FQidlist (len, name)
          | FInt8list (len, name) ->
              fprintf out "@,m->msg.%s.%s = " msg name;
              fprintf out "unpack%s(@[<v>m->raw, (int) m->size, &i,@ "
                  (String.capitalize (fieldWorker elt));
              fprintf out "&m->msg.%s.%s);@]" msg len
        in
        List.iter fieldLine rest;
        fprintf out "@,break;";
        fprintf out "@]@,"
      end
    | _ -> raise BadMessage
  in
  fprintf out "@[<v 4>int unpackMessage(struct message *m) {";
  fprintf out "@,int i = 0;";
  fprintf out "@,@[<v 4>if (m->size < 7 || ";
  fprintf out "unpackU32(m->raw, (int) m->size, &i) != m->size)";
  fprintf out "@,return -1;";
  fprintf out "@]@,m->id = unpackU8(m->raw, (int) m->size, &i);";
  fprintf out "@,m->tag = unpackU16(m->raw, (int) m->size, &i);";
  fprintf out "@,@,@[<v 4>switch (m->id) {";
  List.iter msg m;
  fprintf out "@,@[<v 4>default:";
  fprintf out "@,return -1;";
  fprintf out "@]";
  fprintf out "@]@,}";
  fprintf out "@,@,@[<v 4>if (i != (int) m->size)";
  fprintf out "@,return -1;";
  fprintf out "@]@,@,return 0;";
  fprintf out "@]@,}@."

(*
let outputFreer out m =
  let msg (id, fields) =
    match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
      begin
        fprintf out "@,@[<v 4>case %s:" (String.uppercase msg);
        let fieldLine elt =
          match elt with
          | FName _
          | FInt1 _
          | FInt2 _
          | FInt4 _
          | FInt8 _
          | FQid _ -> ()
          | FStat name ->
              fprintf out "@,@[<v 4>if (m->msg.%s.%s != NULL)" msg name;
              fprintf out "@,freeStat(m->msg.%s.%s);@]" msg name
          | FQidlist (_, name)
          | FData (_, name)
          | FInt8list (_, name)
          | FString name ->
              fprintf out "@,@[<v 4>if (m->msg.%s.%s != NULL)" msg name;
              fprintf out "@,free(m->msg.%s.%s);@]" msg name
          | FStringlist (len, name) ->
              fprintf out "@,@[<v 4>if (m->msg.%s.%s != NULL) {" msg name;
              fprintf out "@,int i = (int) m->msg.%s.%s;" msg len;
              fprintf out "@,@[<v 4>while (i-- > 0)";
              fprintf out "@,@[<v 4>if (m->msg.%s.%s[i] != NULL)" msg name;
              fprintf out "@,free(m->msg.%s.%s[i]);" msg name;
              fprintf out "@]@]@,free(m->msg.%s.%s);" msg name;
              fprintf out "@]@,}"
        in
        List.iter fieldLine rest;
        fprintf out "@,break;";
        fprintf out "@]@,"
      end
    | _ -> raise BadMessage
  in
  fprintf out "@[<v 4>void freeMessage(struct message *m) {";
  fprintf out "@,@[<v 4>switch (m->id) {";
  List.iter msg m;
  fprintf out "@,@[<v 4>default:";
  fprintf out "@,return;";
  fprintf out "@]";
  fprintf out "@]@,}";
  fprintf out "@,@,bzero(&m->msg, sizeof(m->msg));";
  fprintf out "@]@,}@."
*)

let outputPacker out m =
  let msgSize (id, fields) =
    match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
      begin
        (* size[4] + msgId[1] + tag[2] *)
        let size = ref 7 in
        let compSize elt =
          match elt with
          | FName _
          | FInt1 _ -> size := !size + 1
          | FInt2 _ -> size := !size + 2
          | FInt4 _ -> size := !size + 4
          | FInt8 _ -> size := !size + 8
          | FString name ->
              size := !size + 2;
              fprintf out "safe_strlen(m->msg.%s.%s) +@ " msg name
          | FQid _ -> size := !size + 13
          | FStat name ->
              size := !size + 2;
              fprintf out "statsize(m->msg.%s.%s) +@ " msg name
          | FData (len, name) ->
              size := !size + 4;
              fprintf out "m->msg.%s.%s +@ " msg len
          | FStringlist (len, name) ->
              size := !size + 2;
              fprintf out "%ssize(@[<hov>m->msg.%s.%s,@ m->msg.%s.%s)@] +@ "
                  (fieldWorker elt) msg len msg name
          | FQidlist (len, name) ->
              size := !size + 2;
              fprintf out "(13 * m->msg.%s.%s) +@ " msg len
          | FInt8list (len, name) ->
              size := !size + 2;
              fprintf out "(8 * m->msg.%s.%s) +@ " msg len
        in
        fprintf out "@,@[<v 4>case %s:" (String.uppercase msg);
        fprintf out "@,m->size = @[<hov>(u32) ";
        List.iter compSize rest;
        fprintf out "%d;@]" !size;
        fprintf out "@,break;";
        fprintf out "@]@,"
      end
    | _ -> raise BadMessage
  in
  let msgPack (id, fields) =
    match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
      begin
        let fieldLine elt =
          match elt with
          | FName name
          | FInt1 name
          | FInt2 name
          | FInt4 name
          | FInt8 name
          | FString name
          | FQid name
          | FStat name ->
              fprintf out "@,pack%s(@[<hov>m->raw, &i,@ m->msg.%s.%s);@]"
                  (String.capitalize (fieldWorker elt)) msg name
          | FData (len, name)
          | FStringlist (len, name)
          | FQidlist (len, name)
          | FInt8list (len, name) ->
              fprintf out "@,pack%s(@[<hov>m->raw, &i,@ m->msg.%s.%s,@ "
                  (String.capitalize (fieldWorker elt)) msg len;
              fprintf out "m->msg.%s.%s);@]" msg name
        in
        fprintf out "@,@[<v 4>case %s:" (String.uppercase msg);
        List.iter fieldLine rest;
        fprintf out "@,break;";
        fprintf out "@]@,"
      end
    | _ -> raise BadMessage
  in
  fprintf out "@[<v 4>int packMessage(struct message *m, u32 maxSize) {";
  fprintf out "@,int i = 0;";
  fprintf out "@,@,@[<v 4>switch (m->id) {";
  List.iter msgSize m;
  fprintf out "@,@[<v 4>default:";
  fprintf out "@,return -1;@]";
  fprintf out "@]@,}";
  fprintf out "@,@,@[<v 4>if (m->size > maxSize)";
  fprintf out "@,return -1;@]";
  fprintf out "@,@,packU32(m->raw, &i, m->size);";
  fprintf out "@,packU8(m->raw, &i, m->id);";
  fprintf out "@,packU16(m->raw, &i, m->tag);";
  fprintf out "@,@,@[<v 4>switch (m->id) {";
  List.iter msgPack m;
  fprintf out "@,@[<v 4>default:";
  fprintf out "@,return -1;@]";
  fprintf out "@]@,}";
  fprintf out "@,@,return 0;";
  fprintf out "@]@,}@."

let outputPrinter out m =
  let msg (id, fields) =
    match fields with (FInt4 "size"::FName msg::FInt2 "tag"::rest) ->
      begin
        let newline = ref false in
        let fieldLine elt =
          newline := false;
          match elt with
          | FName name -> raise BadMessage
          | FInt1 name ->
              fprintf out "@,fprintf(fp, \" %s[$%%x]\", (u32) m->msg.%s.%s);"
                  name msg name
          | FInt2 name ->
              fprintf out "@,fprintf(fp, \" %s[$%%x]\", (u32) m->msg.%s.%s);"
                  name msg name
          | FInt4 name ->
              fprintf out "@,fprintf(fp, \" %s[$%%x]\", m->msg.%s.%s);"
                  name msg name
          | FInt8 name ->
              fprintf out "@,fprintf(fp, \" %s[$%%llx]\", " name;
              fprintf out "m->msg.%s.%s);" msg name
          | FString name ->
              fprintf out "@,fprintf(fp, \" %s[%%s]\", m->msg.%s.%s);"
                  name msg name
          | FQid name ->
              fprintf out "@,fprintf(fp, @[<hv>";
              fprintf out "\" %s[$%%x,$%%x,$%%llx]\",@ " name;
              fprintf out "(u32) m->msg.%s.%s.type,@ " msg name;
              fprintf out "m->msg.%s.%s.version,@ " msg name;
              fprintf out "m->msg.%s.%s.path);@]" msg name
          | FStat name ->
              fprintf out "@,fprintf(fp, \" %s->\\n\");" name;
              fprintf out "@,dumpStat(fp, \"    \", m->msg.%s.%s);" msg name;
              newline := true
          | FData (len, name) ->
              fprintf out "@,fprintf(fp, \" %s[%%d]->\\n\", m->msg.%s.%s);"
                  name msg len;
              fprintf out "@,dumpData(fp, \"    \", ";
              fprintf out "m->msg.%s.%s, m->msg.%s.%s);" msg name msg len;
              newline := true
          | FStringlist (len, name) ->
              fprintf out "@,fprintf(fp, \" %s * %%d:\", " name;
              fprintf out "(u32) m->msg.%s.%s);" msg len;
              fprintf out "@,@[<v 4>for (i = 0; i < (int) m->msg.%s.%s; i++)"
                  msg len;
              fprintf out "@,fprintf(fp, \"\\n    ";
              fprintf out "%%2d: %s[%%s]\", i, " name;
              fprintf out "m->msg.%s.%s[i]);@]" msg name
          | FQidlist (len, name) ->
              fprintf out "@,fprintf(fp, \" %s * %%d:\", " name;
              fprintf out "(u32) m->msg.%s.%s);" msg len;
              fprintf out "@,@[<v 4>for (i = 0; i < (int) m->msg.%s.%s; i++)"
                  msg len;
              fprintf out "@,fprintf(fp, @[<hv>\"\\n    ";
              fprintf out "%%2d: %s[$%%x,$%%x,$%%llx]\", i,@ " name;
              fprintf out "(u32) m->msg.%s.%s[i].type,@ " msg name;
              fprintf out "m->msg.%s.%s[i].version,@ " msg name;
              fprintf out "m->msg.%s.%s[i].path);@]" msg name;
              fprintf out "@]"
          | FInt8list (len, name) ->
              fprintf out "@,fprintf(fp, \" %s * %%d:\", " name;
              fprintf out "(u32) m->msg.%s.%s);" msg len;
              fprintf out "@,@[<v 4>for (i = 0; i < (int) m->msg.%s.%s; i++)"
                  msg len;
              fprintf out "@,fprintf(fp, \"\\n    ";
              fprintf out "%%2d: %s[$%%llx]\", i, " name;
              fprintf out "m->msg.%s.%s[i]);@]" msg name
        in
        fprintf out "@,@[<v 4>case %s:" (String.uppercase msg);
        fprintf out "@,fprintf(fp, \"%s[%%d]:\", m->tag);"
            (String.capitalize msg);
        List.iter fieldLine rest;
        if !newline then () else
          fprintf out "@,fprintf(fp, \"\\n\");";
        fprintf out "@,break;";
        fprintf out "@]@,"
      end
    | _ -> raise BadMessage
  in
  fprintf out "@[<v 4>void printMessage(FILE *fp, struct message *m) {";
  fprintf out "@,int i;";
  fprintf out "@,@,@[<v 4>switch (m->id) {";
  List.iter msg m;
  fprintf out "@,@[<v 4>default:";
  fprintf out "@,fprintf(fp, \"error: unknown message with id %%d\", m->id);";
  fprintf out "@,break;";
  fprintf out "@]";
  fprintf out "@]@,}";
  fprintf out "@]@,}@."


(*****************************************************************************)

let infile = ref "";;

Arg.parse [] (fun s -> infile := s) "usage: gen source.msg";;
if !infile = "" then (printf "usage: gen source.msg\n" ; exit 1);;

let go () =
  let stem = Filename.chop_extension !infile in
  let infp = open_in !infile in
  let rec readlines fp =
    try let line = input_line fp in
        if String.contains line '['
          then parseMessage line :: readlines fp
          else readlines fp
    with End_of_file -> []
  in
  let m = readlines infp in
  close_in infp;

  let hfp = open_out (stem ^ ".h") in
  let h = formatter_of_out_channel hfp in
  let hdef = "_" ^ (String.uppercase stem) ^ "_H_" in
  fprintf h "/*";
  fprintf h "@. * Generated file--do not edit!";
  fprintf h "@. */";
  fprintf h "@.@.#ifndef %s" hdef;
  fprintf h "@.#define %s" hdef;
  fprintf h "@.@.#include \"%sstatic.h\"@.@." stem;
  List.iter (fun (i,f) ->
      fprintf h "#define %s %d@." (String.uppercase (getName (i,f))) i) m;
  fprintf h "@.";
  List.iter (fun (i,f) -> outputStructs h f i) m;
  outputMessageStruct h m;
  fprintf h "@.";
  List.iter (fun (i,f) -> outputSetters h f i) m;
  fprintf h "int unpackMessage(struct message *);";
  fprintf h "@.int packMessage(struct message *, u32);";
  fprintf h "@.void printMessage(FILE *fp, struct message *m);";
  fprintf h "@.";
  fprintf h "@.#endif@.";
  fprintf h "@?";
  close_out hfp;

  let cfp = open_out (stem ^ ".c") in
  let c = formatter_of_out_channel cfp in
  fprintf c "/*";
  fprintf c "@. * Generated file--do not edit!";
  fprintf c "@. */";
  fprintf c "@.@.#include \"9p.h\"";
  fprintf c "@.#include <stdlib.h>";
  fprintf c "@.#include <stdio.h>";
  fprintf c "@.#include <string.h>@.@.";
  outputUnpacker c m;
  fprintf c "@.";
  outputPacker c m;
  fprintf c "@.";
  outputPrinter c m;
  fprintf c "@?";
  close_out cfp;;

go ();;
