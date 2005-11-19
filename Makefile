OCAMLC=ocamlc
CC=gcc
# -D_FILE_OFFSET_BITS=64 comes from `getconf LFS_CFLAGS`
CCOPTS=-g -Wall -D_FILE_OFFSET_BITS=64 -D_REENTRANT -DGC_LINUX_THREADS
LINKOPTS=-lgc

SRCNOGEN=9pstatic.c 9p.c
SRCNOINC=main.c
SRCINC=list.c vector.c hashtable.c connection.c handles.c transaction.c fid.c util.c config.c state.c transport.c fs.c dispatch.c map.c worker.c forward.c heap.c lru.c
INCNOSRC=types.h

SRC=$(SRCNOGEN) $(SRCNOINC) $(SRCINC)
INC=$(INCNOSRC) $(SRCNOGEN:.c=.h) $(SRCINC:.c=.h)

OBJ=$(SRC:.c=.o)

all:	envoy

9p.h 9p.c: 9p.msg gen
	./gen 9p.msg

gen:	gen.ml
	$(OCAMLC) -o gen gen.ml
	rm -f gen.cmo gen.cmi

%.o:	%.c
	$(CC) $(CCOPTS) -c $<

envoy: $(OBJ)
	$(CC) $(CCOPTS) $(LINKOPTS) -o envoy $(OBJ)

clean:
	rm -f $(OBJ) gen.{cmo,cmi} envoy includes.dynamic

cleanall: clean
	rm -f 9p.{c,h} gen depend

depend: gen 9p.h
	$(CC) $(CCOPTS) -M $(SRC) > depend

includes.dynamic: extract-includes.pl includes.static $(INC)
	./extract-includes.pl includes.static $(INC) > includes.dynamic

includes: includes.dynamic
	./generate-includes.pl includes.dynamic $(SRCNOINC) $(SRCINC) $(SRCINC:.c=.h)

include depend
