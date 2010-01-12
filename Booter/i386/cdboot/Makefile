
DIR = cdboot
include ../MakePaths.dir

NASM = /Developer/usr/bin/nasm
INSTALLDIR = $(DSTROOT)/usr/standalone/i386
DIRS_NEEDED = $(SYMROOT)

all embedtheme: $(DIRS_NEEDED) cdboot

cdboot: cdboot.s $(SYMROOT)/boot Makefile $(NASM)
	$(NASM) cdboot.s -o $(SYMROOT)/cdboot
	dd if=$(SYMROOT)/boot of=$(SYMROOT)/cdboot conv=sync bs=2k seek=1

	# Update cdboot with boot file size info
	stat -f%z $(SYMROOT)/boot \
		| perl -ane "print pack('V',@F[0]);" \
		| dd of=$(SYMROOT)/cdboot bs=1 count=4 seek=2044 conv=notrunc

install_i386:: all $(INSTALLDIR)
	cp $(SYMROOT)/cdboot $(INSTALLDIR)
	cd $(INSTALLDIR); chmod u+w cdboot

include ../MakeInc.dir

#dependencies
