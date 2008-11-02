open Unix
open Semaphore

(* acceptConnections addr sockq maxServers f
 * listens on addr with a queue of up to sockq incomming connections.
 * It calls f on each new connection in its own thread, up to maxServers
 * concurrent instances.
 * acceptConnections spawns a new thread for listening and returns a handle
 * to that thread.
 *)
val acceptConnections : sockaddr -> int -> int ->
    (file_descr * sockaddr -> unit) -> Thread.t

(* messageServer f (sock, addr)
 * receives a message from sock (and strips the size field), gives
 * it to f, and sends the reply that f returns.  If f also returns true,
 * the connection is closed (after sending the reply) and the server loop
 * terminates.
 *)
val messageServer : (string -> string * bool) -> file_descr * sockaddr -> unit

(* getMessage sock
 * waits for a message from sock. The first four bytes indicate the size
 * of the message, and getMessage returns a string containing the remaining
 * size-4 bytes of the message.
 *)
val getMessage : file_descr -> string

(* putMessage sock msg
 * sends a message over sock.
 *)
val putMessage : file_descr -> string -> unit
