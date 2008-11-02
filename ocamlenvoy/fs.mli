val respondTversion :   State.t -> Unix.file_descr ->
  U9P2000u.tversion ->  U9P2000u.message
val respondTauth :      State.t -> Unix.file_descr ->
  U9P2000u.tauth ->     U9P2000u.message
val respondTattach :    State.t -> Unix.file_descr ->
  U9P2000u.tattach ->   U9P2000u.message
val respondTflush :     State.t -> Unix.file_descr ->
  U9P2000u.tflush ->    U9P2000u.message
val respondTwalk :      State.t -> Unix.file_descr ->
  U9P2000u.twalk ->     U9P2000u.message
val respondTopen :      State.t -> Unix.file_descr ->
  U9P2000u.topen ->     U9P2000u.message
val respondTcreate :    State.t -> Unix.file_descr ->
  U9P2000u.tcreate ->   U9P2000u.message
val respondTread :      State.t -> Unix.file_descr ->
  U9P2000u.tread ->     U9P2000u.message
val respondTwrite :     State.t -> Unix.file_descr ->
  U9P2000u.twrite ->    U9P2000u.message
val respondTclunk :     State.t -> Unix.file_descr ->
  U9P2000u.tclunk ->    U9P2000u.message
val respondTremove :    State.t -> Unix.file_descr ->
  U9P2000u.tremove ->   U9P2000u.message
val respondTstat :      State.t -> Unix.file_descr ->
  U9P2000u.tstat ->     U9P2000u.message
val respondTwstat :     State.t -> Unix.file_descr ->
  U9P2000u.twstat ->    U9P2000u.message
val respondMessage :    State.t -> Unix.file_descr ->
  U9P2000u.message ->   U9P2000u.message
