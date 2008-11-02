OCAMLC=ocamlc -g -warn-error Ae -thread
OCAMLOPT=ocamlopt -warn-error Ae -thread
OCAMLDEP=ocamldep
OCAMLMKTOP=ocamlmktop
OCAMLINCLUDE=`ocamlc -where`
CC= gcc
CCOPTS= -g

PACKAGES_BC= unix.cma threads/threads.cma
PACKAGES_OPT= unix.cmxa threads.cmxa

MODULES= config parseHelpers u9P2000u state transport responder fs debug

TARGETSOPT= envoy
SOURCESOPT= $(MODULES:=.ml) $(MODULES:=.mli) $(TARGETSOPT:=.ml)
OBJECTSOPT= $(MODULES:=.cmx) $(TARGETSOPT:=.cmx)
OBJECTSOPT_O= $(OBJECTSOPT:.cmx=.o)
#threads.cmxa

TARGETSBC= $(TARGETSOPT:=.bc)
SOURCESBC= $(SOURCESOPT)
OBJECTSBC= $(OBJECTSOPT:.cmx=.cmo)
#threads.cma

.PHONY: clean cleanall all opt native

all:	bc

opt:	$(TARGETSOPT)

bc:	$(TARGETSBC)
	rm -f envoy
	mv envoy.bc envoy

top:	$(MODULES:=.cmo)
	$(OCAMLMKTOP) -thread -o top $(PACKAGES_BC) $(MODULES:=.cmo)

envoy:	$(OBJECTSOPT) $(OBJECTSOPT_O)
	$(OCAMLOPT) -o $@ $(OBJECTSOPT) $(PACKAGES_OPT)

envoy.bc: $(OBJECTSBC)
	$(OCAMLC) -o $@ $(PACKAGES_BC) $(OBJECTSBC)

u9P2000u.ml u9P2000u.mli: u9P2000u.msg gen
	./gen u9P2000u.msg

gen:	gen.ml
	$(OCAMLC) -o gen gen.ml

%.cmi:	%.mli
	$(OCAMLC) -c $< $(PACKAGES_BC)

%.cmo:	%.ml
	$(OCAMLC) -c $< $(PACKAGES_BC)

%.cmx:	%.ml
	$(OCAMLOPT) -c $< $(PACKAGES_OPT)

%.o:	%.c
	$(CC) $(CCOPTS) -c $< -I $(OCAMLINCLUDE)

clean:
	rm -f $(TARGETSBC) $(TARGETSOPT) top *.cmo *.cmi *.cmx *.o

cleanall: clean
	rm -f u9P2000u.{ml,mli} gen

depend:
	$(OCAMLDEP) $(SOURCESBC) > depend

include depend
