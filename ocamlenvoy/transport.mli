val getMessage : State.t -> Unix.file_descr * string
val putMessage : State.t -> Unix.file_descr -> string -> unit
