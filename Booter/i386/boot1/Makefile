
DIR = boot1
include ../MakePaths.dir

INSTALLDIR = $(DSTROOT)/usr/standalone/i386
DIRS_NEEDED = $(OBJROOT) $(SYMROOT)

NASM = /Developer/usr/bin/nasm

VERSIONED_FILES = boot1h

VERS = `vers_string -f 5.0 | tr - .`
NEW_VERS = Darwin boot1h v$(VERS)
ifneq "" "$(wildcard /bin/mkdirs)"
  MKDIRS = /bin/mkdirs
else
  MKDIRS = /bin/mkdir -p
endif

all embedtheme: $(DIRS_NEEDED) $(VERSIONED_FILES)

boot1h: boot1.s Makefile
	$(NASM) boot1.s -o $(SYMROOT)/boot1h
	$(NASM) boot1hp.s -o $(SYMROOT)/boot1hp
	$(NASM) boot1he.s -o $(SYMROOT)/boot1he
	$(NASM) boot1f32.s -o $(SYMROOT)/boot1f32

install_i386:: all $(INSTALLDIR)
	cp $(SYMROOT)/boot1h $(INSTALLDIR)/
	cd $(INSTALLDIR); chmod u+w $(VERSIONED_FILES) 

include ../MakeInc.dir
