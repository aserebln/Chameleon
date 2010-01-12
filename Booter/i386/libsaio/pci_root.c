/*
 * Copyright 2009 netkas
 */

#include "libsaio.h"
#include "boot.h"
#include "bootstruct.h"

#ifndef DEBUG_PCIROOT
#define DEBUG_PCIROOT 0
#endif

#if DEBUG_PCIROOT
#define DBG(x...)  printf(x)
#else
#define DBG(x...)
#endif

static int rootuid = 10; //value means function wasnt ran yet

static unsigned int findrootuid(unsigned char * dsdt, int len)
{
	int i;
	for (i=0; i<64 && i<len-5; i++) //not far than 64 symbols from pci root 
	{
		if(dsdt[i] == '_' && dsdt[i+1] == 'U' && dsdt[i+2] == 'I' && dsdt[i+3] == 'D' && dsdt[i+5] == 0x08)
		{
			return dsdt[i+4];
		}
	}
	return 11;
}

static unsigned int findpciroot(unsigned char * dsdt,int len)
{
	int i;

	for (i=0; i<len-4; i++) {
		if(dsdt[i] == 'P' && dsdt[i+1] == 'C' && dsdt[i+2] == 'I' && (dsdt[i+3] == 0x08 || dsdt [i+4] == 0x08)) {
			return findrootuid(dsdt+i, len-i);
		}
	}
	return 10;
}

int getPciRootUID(void)
{
	void *new_dsdt;
	const char *dsdt_filename;
	const char *val;
	int fd;
	int dsdt_uid;
	int len,fsize;

	if (rootuid < 10) {
		return rootuid;
	}
	rootuid = 0;	/* default uid = 0 */

	if (getValueForKey(kPCIRootUID, &val, &len, &bootInfo->bootConfig)) {
		if (isdigit(val[0])) {
			rootuid = val[0] - '0';
		}
		goto out;
	}
#if 0
	/* Chameleon compatibility */
	if (getValueForKey("PciRoot", &val, &len, &bootInfo->bootConfig)) {
		if (isdigit(val[0])) {
			rootuid = val[0] - '0';
		}
		goto out;
	}

	/* PCEFI compatibility */
	if (getValueForKey("-pci0", &val, &len, &bootInfo->bootConfig)) {
		rootuid = 0;
		goto out;
	}
	if (getValueForKey("-pci1", &val, &len, &bootInfo->bootConfig)) {
		rootuid = 1;
		goto out;
	}
#endif
	if (!getValueForKey(kDSDT, &dsdt_filename, &len, &bootInfo->bootConfig)) {
		dsdt_filename="/Extra/DSDT.aml";
	}

	if ((fd = open_bvdev("bt(0,0)", dsdt_filename, 0)) < 0) {
		verbose("[WARNING] %s not found\n", dsdt_filename);
		goto out;
	}
	fsize = file_size(fd);

	if ((new_dsdt = MALLOC(fsize)) == NULL) {
		verbose("[ERROR] alloc DSDT memory failed\n");
		close (fd);
		goto out;
	}
	if (read (fd, new_dsdt, fsize) != fsize) {
		verbose("[ERROR] read %s failed\n", dsdt_filename);
		close (fd);
		goto out;
	}
	close (fd);

	dsdt_uid = findpciroot(new_dsdt, fsize);
	free(new_dsdt);

	if (dsdt_uid >= 0 && dsdt_uid <= 9) {
		rootuid = dsdt_uid;
	} else {
		verbose("Could not determine PCI-Root-UID value from DSDT!\n");
	}
out:
	verbose("Using PCI-Root-UID value %d\n", rootuid);
	return rootuid;
}
