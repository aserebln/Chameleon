
DIR = libsaio
include ../MakePaths.dir

UTILDIR = ../util
LIBSADIR = ../libsa
BOOT2DIR = ../boot2
INSTALLDIR = $(DSTROOT)/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders/standalone
#SYMROOT=

OPTIM = -Os -Oz
DEBUG = -DNOTHING
#DEBUG = -DDEBUG_CPU=1 -DDEBUG_MEM=1 -DDEBUG_SPD=1 -DDEBUG_PCI=1 -DDEBUG_SMBIOS=1
CFLAGS	= $(RC_CFLAGS) $(OPTIM) $(MORECPP) -arch i386 -g -Wmost \
    -D__ARCHITECTURE__=\"i386\" -DSAIO_INTERNAL_USER \
    -DRCZ_COMPRESSED_FILE_SUPPORT $(DEBUG) \
    -fno-builtin -static $(OMIT_FRAME_POINTER_CFLAG) \
    -mpreferred-stack-boundary=2 -fno-align-functions -fno-stack-protector \
    -march=pentium4 -msse2 -mfpmath=sse -msoft-float

DEFINES=
CONFIG = hd
INC = -I. -I$(SYMROOT) -I$(UTILDIR) -I$(LIBSADIR) -I$(BOOT2DIR)
ifneq "" "$(wildcard /bin/mkdirs)"
  MKDIRS = /bin/mkdirs
else
  MKDIRS = /bin/mkdir -p
endif
AS = as
LD = ld
# LIBS= -lc_static
LIBS=

VPATH = $(OBJROOT):$(SYMROOT)

SAIO_OBJS = table.o asm.o bios.o biosfn.o \
	disk.o sys.o cache.o bootstruct.o \
	stringTable.o load.o pci.o allocate.o misc.o \
	ufs.o ufs_byteorder.o \
	vbe.o hfs.o hfs_compare.o \
	xml.o ntfs.o msdos.o md5c.o device_tree.o \
	cpu.o platform.o dsdt_patcher.o \
	smbios_patcher.o fake_efi.o ext2fs.o \
	hpet.o spd.o usb.o pci_setup.o \
	device_inject.o nvidia.o ati.o pci_root.o mem.o


SAIO_EXTERN_OBJS = console.o

SFILES =
CFILES = 
HFILES = 
EXPORTED_HFILES =
INSTALLED_HFILES =
OTHERFILES = Makefile
ALLSRC =  $(SFILES) $(CFILES) \
	$(HFILES) $(OTHERFILES)
LIBS = libsaio.a
DIRS_NEEDED = $(OBJROOT) $(SYMROOT)
#GENFILES = $(SYMROOT)/saio_internal.h \
#	$(SYMROOT)/saio_external.h \
#	$(SYMROOT)/saio_defs.h \
#	$(SYMROOT)/saio_table.c

#SIG = $(SYMROOT)/sig

all embedtheme: $(DIRS_NEEDED) libsaio.h $(LIBS)

#libsaio_static.a: $(SAIO_OBJS)
#	rm -f $(SYMROOT)/$@
#	ar q $(SYMROOT)/$@ $(SAIO_OBJS)
#	ranlib $(SYMROOT)/$@

libsaio.a: $(SAIO_EXTERN_OBJS) $(SAIO_OBJS)
	rm -f $(SYMROOT)/$(@F)
	ar q $(SYMROOT)/$(@F) $^
	ranlib $(SYMROOT)/$(@F)

#saio_internal.h: saio_external.h
#saio_table.c: saio_external.h
#saio_defs.h: saio_external.h
#saio_external.h: saio.def
#	$(SIG) -d $(SYMROOT) -n saio saio.def

#installhdrs:: $(INSTALLDIR)
#	cp $(INSTALLED_HFILES) $(INSTALLDIR)

include ../MakeInc.dir

# dependencies
-include $(OBJROOT)/Makedep
