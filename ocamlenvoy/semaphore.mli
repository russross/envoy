type semaphore

val create : int -> semaphore
val p : semaphore -> unit
val v : semaphore -> unit
