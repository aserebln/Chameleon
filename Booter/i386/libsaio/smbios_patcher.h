/*
 * Copyright 2008 mackerintel
 */

#ifndef __LIBSAIO_SMBIOS_PATCHER_H
#define __LIBSAIO_SMBIOS_PATCHER_H

#include "libsaio.h"
#include "SMBIOS.h"

extern EFI_GUID gEfiAcpiTableGuid;
extern EFI_GUID gEfiAcpi20TableGuid;

/* From Foundation/Efi/Guid/Smbios/SmBios.h */
/* Modified to wrap Data4 array init with {} */
#define EFI_SMBIOS_TABLE_GUID \
{ \
0xeb9d2d31, 0x2d88, 0x11d3, {0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} \
}

/* From Foundation/Efi/Guid/Smbios/SmBios.c */
//EFI_GUID const  gEfiSmbiosTableGuid = EFI_SMBIOS_TABLE_GUID;

#define SMBIOS_RANGE_START      0x000F0000
#define SMBIOS_RANGE_END        0x000FFFFF

#define SMBIOS_ORIGINAL		0
#define SMBIOS_PATCHED		1

struct smbios_table_header 
{
	uint8_t type;
	uint8_t length;
	uint16_t handle;
} __attribute__ ((packed));

struct smbios_property
{
	char *name;
	uint8_t table_type;
	enum {SMSTRING, SMWORD, SMBYTE, SMOWORD} value_type;
	int offset;
	int (*auto_int) (char *name, int table_num);
	char * (*auto_str) (char *name, int table_num);
	char * (*auto_oword) (char *name, int table_num);
};

struct smbios_table_description
{
	uint8_t type;
	int len;
	int (*numfunc)(int tablen);
};

extern struct SMBEntryPoint	*getSMBIOS(int);
#endif /* !__LIBSAIO_SMBIOS_PATCHER_H */
