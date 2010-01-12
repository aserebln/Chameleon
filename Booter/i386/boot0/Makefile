
DIR = boot0
include ../MakePaths.dir

NASM = /Developer/usr/bin/nasm
INSTALLDIR = $(DSTROOT)/usr/standalone/i386
DIRS_NEEDED = $(SYMROOT)

all embedtheme: $(DIRS_NEEDED) boot0 chain0

boot0: boot0.s Makefile $(NASM)
	$(NASM) boot0.s -o $(SYMROOT)/$@

chain0: chain0.s Makefile $(NASM)
	$(NASM) chain0.s -o $(SYMROOT)/$@

install_i386:: all $(INSTALLDIR)
	cp $(SYMROOT)/boot0 $(SYMROOT)/chain0 $(INSTALLDIR)
	cd $(INSTALLDIR); chmod u+w boot0

include ../MakeInc.dir

#dependencies

