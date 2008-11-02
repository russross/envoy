type semaphore = Mutex.t * Condition.t * int ref

let create n =
  if n < 1 then invalid_arg ("Semaphore.create called with (n = "^
                              (string_of_int n)^") < 1")
  else (Mutex.create (), Condition.create (), ref n)

let p (m, c, n) =
  Mutex.lock m;
  while !n <= 0 do
    Condition.wait c m
  done;
  decr n;
  Mutex.unlock m

let v (m, c, n) =
  Mutex.lock m;
  incr n;
  Condition.signal c;
  Mutex.unlock m
