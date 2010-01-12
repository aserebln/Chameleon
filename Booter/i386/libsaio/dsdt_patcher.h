/*
 * Copyright 2008 mackerintel
 */

#ifndef __LIBSAIO_DSDT_PATCHER_H
#define __LIBSAIO_DSDT_PATCHER_H

#include "libsaio.h"

uint64_t acpi10_p;
uint64_t acpi20_p;
uint64_t smbios_p;
extern int setupAcpi();

extern EFI_STATUS addConfigurationTable();

extern EFI_GUID gEfiAcpiTableGuid;
extern EFI_GUID gEfiAcpi20TableGuid;

#endif /* !__LIBSAIO_DSDT_PATCHER_H */
