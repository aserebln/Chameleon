/*
 * Copyright 2008 mackerintel
 */

#ifndef __LIBSAIO_DSDT_PATCHER_H
#define __LIBSAIO_DSDT_PATCHER_H

#include "libsaio.h"

/* AsereBLN: this is bullsh*t... declaring vars in a header */
#if 0
uint64_t acpi10_p;
uint64_t acpi20_p;
uint64_t smbios_p;
#endif

extern int setupAcpi();
extern EFI_STATUS addConfigurationTable();
extern EFI_GUID gEfiAcpiTableGuid;
extern EFI_GUID gEfiAcpi20TableGuid;

#endif /* !__LIBSAIO_DSDT_PATCHER_H */
