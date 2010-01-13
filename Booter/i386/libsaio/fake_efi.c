/*
 * Copyright 2007 David F. Elliott.  All rights reserved.
 */

#include "libsaio.h"
#include "boot.h"
#include "bootstruct.h"
#include "efi.h"
#include "acpi.h"
#include "fake_efi.h"
#include "efi_tables.h"
#include "platform.h"
#include "dsdt_patcher.h"
#include "smbios_patcher.h"
#include "device_inject.h"
#include "pci.h"
#include "sl.h"

extern void setup_pci_devs(pci_dt_t *pci_dt);

/*
Modern Darwin kernels require some amount of EFI because Apple machines all
have EFI.  Modifying the kernel source to not require EFI is of course
possible but would have to be maintained as a separate patch because it is
unlikely that Apple wishes to add legacy support to their kernel.

As you can see from the Apple-supplied code in bootstruct.c, it seems that
the intention was clearly to modify this booter to provide EFI-like structures
to the kernel rather than modifying the kernel to handle non-EFI stuff.  This
makes a lot of sense from an engineering point of view as it means the kernel
for the as yet unreleased EFI-only Macs could still be booted by the non-EFI
DTK systems so long as the kernel checked to ensure the boot tables were
filled in appropriately.  Modern xnu requires a system table and a runtime
services table and performs no checks whatsoever to ensure the pointers to
these tables are non-NULL.  Therefore, any modern xnu kernel will page fault
early on in the boot process if the system table pointer is zero.

Even before that happens, the tsc_init function in modern xnu requires the FSB
Frequency to be a property in the /efi/platform node of the device tree or else
it panics the bootstrap process very early on.

As of this writing, the current implementation found here is good enough
to make the currently available xnu kernel boot without modification on a
system with an appropriate processor.  With a minor source modification to
the tsc_init function to remove the explicit check for Core or Core 2
processors the kernel can be made to boot on other processors so long as
the code can be executed by the processor and the machine contains the
necessary hardware.
*/


/*==========================================================================
 * Utility function to make a device tree string from an EFI_GUID
 */

static char *mallocStringForGuid(EFI_GUID const *pGuid)
{
	char *string = MALLOC(37);
	efi_guid_unparse_upper(pGuid, string);
	return string;
}


/*==========================================================================
 * Function to map 32 bit physical address to 64 bit virtual address
 */
static uint64_t ptov64(uint32_t addr)
{
	return ((uint64_t)addr | 0xFFFFFF8000000000ULL);
}


/*==========================================================================
 * Fake EFI implementation
 */

/* Identify ourselves as the EFI firmware vendor */
static EFI_CHAR16 const FIRMWARE_VENDOR[] = {'C','h','a','m','e','l','e','o','n','_','2','.','0', 0};
static EFI_UINT32 const FIRMWARE_REVISION = 132; /* FIXME: Find a constant for this. */

/* Just a ret instruction */
static uint8_t const VOIDRET_INSTRUCTIONS[] = {0xc3};
/* movl $0x80000003,%eax; ret */
static uint8_t const UNSUPPORTEDRET_INSTRUCTIONS[] = {0xb8, 0x03, 0x00, 0x00, 0x80, 0xc3};


/* We use the fake_efi_pages struct so that we only need to do one kernel
 * memory allocation for all needed EFI data.  Otherwise, small allocations
 * like the FIRMWARE_VENDOR string would take up an entire page.
 * NOTE WELL: Do NOT assume this struct has any particular layout within itself.
 * It is absolutely not intended to be publicly exposed anywhere
 * We say pages (plural) although right now we are well within the 1 page size
 * and probably will stay that way.
 */
struct fake_efi_pages
{
	EFI_SYSTEM_TABLE_64		efiSystemTable;
	EFI_RUNTIME_SERVICES_64		efiRuntimeServices;
	EFI_CONFIGURATION_TABLE_64	efiConfigurationTable[MAX_CONFIGURATION_TABLE_ENTRIES];
	EFI_CHAR16			firmwareVendor[sizeof(FIRMWARE_VENDOR)/sizeof(EFI_CHAR16)];
	uint8_t				voidret_instructions[sizeof(VOIDRET_INSTRUCTIONS)/sizeof(uint8_t)];
	uint8_t				unsupportedret_instructions[sizeof(UNSUPPORTEDRET_INSTRUCTIONS)/sizeof(uint8_t)];
};

EFI_SYSTEM_TABLE_64 *gST = NULL;
Node *gEfiConfigurationTableNode = NULL;

extern EFI_STATUS addConfigurationTable(EFI_GUID const *pGuid, void *table, char const *alias)
{
	EFI_UINTN i = gST->NumberOfTableEntries;

	/* We only do adds, not modifications and deletes like InstallConfigurationTable */
	if (i >= MAX_CONFIGURATION_TABLE_ENTRIES) {
		stop("Ran out of space for configuration tables. Increase the reserved size in the code.\n");
	}

	if (pGuid == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	if(table != NULL) {
		/* FIXME
		((EFI_CONFIGURATION_TABLE_64 *)gST->ConfigurationTable)[i].VendorGuid = *pGuid;
		((EFI_CONFIGURATION_TABLE_64 *)gST->ConfigurationTable)[i].VendorTable = (EFI_PTR64)table;

		++gST->NumberOfTableEntries; */
		Node *tableNode = DT__AddChild(gEfiConfigurationTableNode, mallocStringForGuid(pGuid));

		/* Use the pointer to the GUID we just stuffed into the system table */
		DT__AddProperty(tableNode, "guid", sizeof(EFI_GUID), (void *)pGuid);

		/* The "table" property is the 32-bit (in our implementation) physical address of the table */
		DT__AddProperty(tableNode, "table", sizeof(void *) * 2, table);

		/* Assume the alias pointer is a global or static piece of data */
		if (alias != NULL) {
			DT__AddProperty(tableNode, "alias", strlen(alias)+1, (void *)alias);
		}
		return EFI_SUCCESS;
    }
    return EFI_UNSUPPORTED;
}

static void fixupEfiSystemTableCRC32(EFI_SYSTEM_TABLE_64 *efiSystemTable)
{
	efiSystemTable->Hdr.CRC32 = 0;
	efiSystemTable->Hdr.CRC32 = crc32(0L, efiSystemTable, efiSystemTable->Hdr.HeaderSize);
}

/*
What we do here is simply allocate a fake EFI system table and a fake EFI
runtime services table.

Because we build against modern headers with kBootArgsRevision 4 we
also take care to set efiMode = 32.
*/
static void setupEfiTables(void)
{
    struct fake_efi_pages *fakeEfiPages= (struct fake_efi_pages*)AllocateKernelMemory(sizeof(struct fake_efi_pages));

    /* Zero out all the tables in case fields are added later */
    bzero(fakeEfiPages, sizeof(struct fake_efi_pages));

    /* --------------------------------------------------------------------
     * Initialize some machine code that will return EFI_UNSUPPORTED for
     * functions returning int and simply return for void functions.
     */
    memcpy(fakeEfiPages->voidret_instructions, VOIDRET_INSTRUCTIONS, sizeof(VOIDRET_INSTRUCTIONS));
    memcpy(fakeEfiPages->unsupportedret_instructions, UNSUPPORTEDRET_INSTRUCTIONS, sizeof(UNSUPPORTEDRET_INSTRUCTIONS));

    /* -------------------------------------------------------------------- */
    /* System table */
    EFI_SYSTEM_TABLE_64 *efiSystemTable = gST = &fakeEfiPages->efiSystemTable;
    efiSystemTable->Hdr.Signature = EFI_SYSTEM_TABLE_SIGNATURE;
    efiSystemTable->Hdr.Revision = EFI_SYSTEM_TABLE_REVISION;
    efiSystemTable->Hdr.HeaderSize = sizeof(EFI_SYSTEM_TABLE_64);
    efiSystemTable->Hdr.CRC32 = 0; /* Initialize to zero and then do CRC32 */
    efiSystemTable->Hdr.Reserved = 0;

    efiSystemTable->FirmwareVendor = (EFI_PTR32)&fakeEfiPages->firmwareVendor;
    memcpy(fakeEfiPages->firmwareVendor, FIRMWARE_VENDOR, sizeof(FIRMWARE_VENDOR));
    efiSystemTable->FirmwareRevision = FIRMWARE_REVISION;

    /* XXX: We may need to have basic implementations of ConIn/ConOut/StdErr */
    /* The EFI spec states that all handles are invalid after boot services have been
     * exited so we can probably get by with leaving the handles as zero. */
    efiSystemTable->ConsoleInHandle = 0;
    efiSystemTable->ConIn = 0;

    efiSystemTable->ConsoleOutHandle = 0;
    efiSystemTable->ConOut = 0;

    efiSystemTable->StandardErrorHandle = 0;
    efiSystemTable->StdErr = 0;

    efiSystemTable->RuntimeServices = ptov64((EFI_PTR32)&fakeEfiPages->efiRuntimeServices);
    /* According to the EFI spec, BootServices aren't valid after the
     * boot process is exited so we can probably do without it.
     * Apple didn't provide a definition for it in pexpert/i386/efi.h
     * so I'm guessing they don't use it.
    */
    efiSystemTable->BootServices = 0;

    efiSystemTable->NumberOfTableEntries = 0;
    efiSystemTable->ConfigurationTable = (EFI_PTR32)fakeEfiPages->efiConfigurationTable;


    /* We're done.  Now CRC32 the thing so the kernel will accept it */
    fixupEfiSystemTableCRC32(efiSystemTable);

    /* -------------------------------------------------------------------- */
    /* Runtime services */
    EFI_RUNTIME_SERVICES_64 *efiRuntimeServices = &fakeEfiPages->efiRuntimeServices;
    efiRuntimeServices->Hdr.Signature = EFI_RUNTIME_SERVICES_SIGNATURE;
    efiRuntimeServices->Hdr.Revision = EFI_RUNTIME_SERVICES_REVISION;
    efiRuntimeServices->Hdr.HeaderSize = sizeof(EFI_RUNTIME_SERVICES_64);
    efiRuntimeServices->Hdr.CRC32 = 0;
    efiRuntimeServices->Hdr.Reserved = 0;

    /* There are a number of function pointers in the efiRuntimeServices table.
     * These are the Foundation (e.g. core) services and are expected to be present on
     * all EFI-compliant machines.  Some kernel extensions (notably AppleEFIRuntime)
     * will call these without checking to see if they are null.
     *
     * We don't really feel like doing an EFI implementation in the bootloader
     * but it is nice if we can at least prevent a complete crash by
     * at least providing some sort of implementation until one can be provided
     * nicely in a kext.
     */
    void (*voidret_fp)() = (void*)fakeEfiPages->voidret_instructions;
    void (*unsupportedret_fp)() = (void*)fakeEfiPages->unsupportedret_instructions;
    efiRuntimeServices->GetTime = ptov64((EFI_PTR32)unsupportedret_fp);
    efiRuntimeServices->SetTime = ptov64((EFI_PTR32)unsupportedret_fp);
    efiRuntimeServices->GetWakeupTime = ptov64((EFI_PTR32)unsupportedret_fp);
    efiRuntimeServices->SetWakeupTime = ptov64((EFI_PTR32)unsupportedret_fp);
    efiRuntimeServices->SetVirtualAddressMap = ptov64((EFI_PTR32)unsupportedret_fp);
    efiRuntimeServices->ConvertPointer = ptov64((EFI_PTR32)unsupportedret_fp);
    efiRuntimeServices->GetVariable = ptov64((EFI_PTR32)unsupportedret_fp);
    efiRuntimeServices->GetNextVariableName = ptov64((EFI_PTR32)unsupportedret_fp);
    efiRuntimeServices->SetVariable = ptov64((EFI_PTR32)unsupportedret_fp);
    efiRuntimeServices->GetNextHighMonotonicCount = ptov64((EFI_PTR32)unsupportedret_fp);
    efiRuntimeServices->ResetSystem = ptov64((EFI_PTR32)voidret_fp);

    /* We're done.  Now CRC32 the thing so the kernel will accept it */
    efiRuntimeServices->Hdr.CRC32 = crc32(0L, efiRuntimeServices, efiRuntimeServices->Hdr.HeaderSize);


    /* -------------------------------------------------------------------- */
    /* Finish filling in the rest of the boot args that we need. */
    bootArgs->efiSystemTable = (uint32_t)efiSystemTable;
    bootArgs->efiMode = kBootArgsEfiMode64;

    /* The bootArgs structure as a whole is bzero'd so we don't need to fill in
     * things like efiRuntimeServices* and what not.
     *
     * In fact, the only code that seems to use that is the hibernate code so it
     * knows not to save the pages.  It even checks to make sure its nonzero.
     */
}

/*
In addition to the EFI tables there is also the EFI device tree node.
In particular, we need /efi/platform to have an FSBFrequency key. Without it,
the tsc_init function will panic very early on in kernel startup, before
the console is available.
*/

/*==========================================================================
 * FSB Frequency detection
 */

/* These should be const but DT__AddProperty takes char* */
static char TSC_Frequency_prop[] = "TSCFrequency";
static char FSB_Frequency_prop[] = "FSBFrequency";
static char CPU_Frequency_prop[] = "CPUFrequency";

/*==========================================================================
 * UUID & SystemSerial & SystemType
 */

#define SYSTEM_ID_DEFAULT \
  { \
	0x41,0x73,0x65,0x72,0x65,0x42,0x4c,0x4e,0x66,0x75,0x63,0x6b,0x45,0x46,0x49,0x58 \
  }

#define UUID_LEN	16
static EFI_CHAR8 SystemID[UUID_LEN] = SYSTEM_ID_DEFAULT;
static char SystemID_prop[] = "system-id";

#define MAX_SERIAL_LEN	32
static EFI_CHAR16 SystemSerial[MAX_SERIAL_LEN];
static int SystemSerialLength = 0;
static char SystemSerial_prop[] = "SystemSerialNumber";

#define MAX_MODEL_LEN	32
static EFI_CHAR16 Model[MAX_MODEL_LEN];
static int ModelLength = 0;
static char Model_prop[] = "Model";

static EFI_CHAR8 SystemType[1];
static char SystemType_prop[] = "system-type";

/*==========================================================================
 * SMBIOS
 */

/* From Foundation/Efi/Guid/Smbios/SmBios.h */
/* Modified to wrap Data4 array init with {} */
#define EFI_SMBIOS_TABLE_GUID \
  { \
    0xeb9d2d31, 0x2d88, 0x11d3, {0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} \
  }

/* From Foundation/Efi/Guid/Smbios/SmBios.c */
EFI_GUID const  gEfiSmbiosTableGuid = EFI_SMBIOS_TABLE_GUID;

#define SMBIOS_RANGE_START      0x000F0000
#define SMBIOS_RANGE_END        0x000FFFFF

/* '_SM_' in little endian: */
#define SMBIOS_ANCHOR_UINT32_LE 0x5f4d535f

#define EFI_ACPI_TABLE_GUID \
  { \
    0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } \
  }

#define EFI_ACPI_20_TABLE_GUID \
  { \
    0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81 } \
  }

EFI_GUID gEfiAcpiTableGuid = EFI_ACPI_TABLE_GUID;
EFI_GUID gEfiAcpi20TableGuid = EFI_ACPI_20_TABLE_GUID;


/*==========================================================================
 * Fake EFI implementation
 */

/* These should be const but DT__AddProperty takes char* */
static char FIRMWARE_REVISION_PROP[]	= "firmware-revision";
static char FIRMWARE_ABI_PROP[]		= "firmware-abi";
static char FIRMWARE_VENDOR_PROP[]	= "firmware-vendor";
static char FIRMWARE_ABI_PROP_VALUE[]	= "EFI64";

static void setupEfiDeviceTree(void)
{
	Node		*node;
	const char	*value;
	int		len;
	bool		doit;

	if ((node = DT__FindNode("/", false)) == NULL) {
		stop("Couldn't find EFI root node");
	}

	/* Export system-type. Allowed values are:
	 * 0x01 for desktop computer (default)
	 * 0x02 for portable computers
	 */
	SystemType[0] = 1;
	if (getValueForKey(kSystemType, &value, &len, &bootInfo->bootConfig) && value != NULL) {
		SystemType[0] = (unsigned char) strtoul(value, NULL, 10);
		if (SystemType[0] != 1 && SystemType[0] != 2) {
			verbose("Error: system-type must be 1 (desktop) or 2 (portable). Defaulting to 1!\n");
			SystemType[0] = 1;
		}
	}
	DT__AddProperty(node, SystemType_prop, sizeof(SystemType), &SystemType);

	/* We could also just do DT__FindNode("/efi/platform", true)
	 * But I think eventually we want to fill stuff in the efi node
	 * too so we might as well create it so we have a pointer for it too.
	 */
	node = DT__AddChild(node, "efi");

	DT__AddProperty(node, FIRMWARE_REVISION_PROP, sizeof(FIRMWARE_REVISION), (EFI_UINT32*)&FIRMWARE_REVISION);
	DT__AddProperty(node, FIRMWARE_ABI_PROP, sizeof(FIRMWARE_ABI_PROP_VALUE), (char*)FIRMWARE_ABI_PROP_VALUE);
	DT__AddProperty(node, FIRMWARE_VENDOR_PROP, sizeof(FIRMWARE_VENDOR), (EFI_CHAR16*)FIRMWARE_VENDOR);

	/* TODO: Fill in other efi properties if necessary */

	/* Set up the /efi/runtime-services table node similar to the way a child node of configuration-table
	 * is set up.  That is, name and table properties
	 */
	Node *runtimeServicesNode = DT__AddChild(node, "runtime-services");

	/* The value of the table property is the 32-bit physical address for the RuntimeServices table.
	 * Sice the EFI system table already has a pointer to it, we simply use the address of that pointer
	 * for the pointer to the property data.  Warning.. DT finalization calls free on that but we're not
	 * the only thing to use a non-malloc'd pointer for something in the DT
	 */
	DT__AddProperty(runtimeServicesNode, "table", sizeof(uint64_t), &gST->RuntimeServices);

	/* Set up the /efi/configuration-table node which will eventually have several child nodes for
	 * all of the configuration tables needed by various kernel extensions.
	 */
	gEfiConfigurationTableNode = DT__AddChild(node, "configuration-table");

	/* Now fill in the /efi/platform Node */
	Node *efiPlatformNode = DT__AddChild(node, "platform");

	/* NOTE WELL: If you do add FSB Frequency detection, make sure to store
	 * the value in the fsbFrequency global and not an malloc'd pointer
	 * because the DT_AddProperty function does not copy its args.
	 */
	if(Platform.CPU.FSBFrequency != 0) {
		DT__AddProperty(efiPlatformNode, FSB_Frequency_prop, sizeof(uint64_t), &Platform.CPU.FSBFrequency);
	}

	/* Export TSC and CPU frequencies for use by the kernel or KEXTs */
	if(Platform.CPU.TSCFrequency != 0) {
		DT__AddProperty(efiPlatformNode, TSC_Frequency_prop, sizeof(uint64_t), &Platform.CPU.TSCFrequency);
	}
	if(Platform.CPU.CPUFrequency != 0) {
		DT__AddProperty(efiPlatformNode, CPU_Frequency_prop, sizeof(uint64_t), &Platform.CPU.CPUFrequency);
	}

	/* Export system-id. Can be disabled with system-id=No in com.apple.Boot.plist */
	doit = true;
	getBoolForKey(kSystemID, &doit, &bootInfo->bootConfig);
	if (doit) {
		DT__AddProperty(efiPlatformNode, SystemID_prop, sizeof(SystemID), &SystemID);
	}
	/* Export SystemSerialNumber if present */
	if (SystemSerialLength > 0) {
		DT__AddProperty(efiPlatformNode, SystemSerial_prop, SystemSerialLength, &SystemSerial);
	}
	/* Export Model if present */
	if (ModelLength > 0) {
		DT__AddProperty(efiPlatformNode, Model_prop, ModelLength, &Model);
	}

	/* Fill /efi/device-properties node */
	setupDeviceProperties(node);
}

/* Installs all the needed configuration table entries */
static void setupEfiConfigurationTable(void)
{
	smbios_p = (EFI_PTR32)getSmbios();
	addConfigurationTable(&gEfiSmbiosTableGuid, &smbios_p, NULL);

	// Setup ACPI with DSDT overrides (mackerintel's patch)
	setupAcpi();

	// We've obviously changed the count.. so fix up the CRC32
	fixupEfiSystemTableCRC32(gST);
}

static void setupEfiDevices(void)
{
	setup_pci_devs(root_pci_dev);
}

/*
 * AsereBLN: overides default values for: SystemSerialNumber, UUID, SYSTEMID
 */
static int stringToUUID(const char *source, unsigned char *uuid)
{
	char	*p = (char *)source;
	int	i;
	char	buf[3];

	buf[2] = '\0';
	for (i=0; i<UUID_LEN; i++) {
		if (p[0] == '\0' || p[1] == '\0' || !isxdigit(p[0]) || !isxdigit(p[1])) {
			printf("[ERROR] UUID='%s' syntax error\n", source);
			return -1;
		}
		buf[0] = *p++;
		buf[1] = *p++;
		uuid[i] = (unsigned char) strtoul(buf, NULL, 16);
		if (*p == '-' && (i % 2) == 1 && i < UUID_LEN - 1) {
			p++;
		}
	}

	if (*p != '\0') {
		printf("[ERROR] UUID='%s' syntax error\n", source);
		return -2;
	}

	return 0;
}

static int getSMBIOSUUID(unsigned char *uuid)
{
	struct SMBEntryPoint	*smbios;
	struct DMIHeader	*dmihdr;
	SMBByte			*p;
	int			found;
	int			i;
	int			isZero;
	int			isOnes;
	
	smbios = getAddressOfSmbiosTable();	/* checks for _SM_ anchor and table header checksum */
	if (memcmp( &smbios->dmi.anchor[0], "_DMI_", 5) != 0) {
		return 0;
	}
	//verbose(">>> SMBIOSAddr=0x%08x\n", smbios);
	//verbose(">>> DMI: addr=0x%08x, len=0x%d, count=%d\n", smbios->dmi.tableAddress, smbios->dmi.tableLength, smbios->dmi.structureCount);
	i = 0;
	found = 0;
	p = (SMBByte *) smbios->dmi.tableAddress;
	while (i < smbios->dmi.structureCount && p + 4 <= (SMBByte *)smbios->dmi.tableAddress + smbios->dmi.tableLength) {
		dmihdr = (struct DMIHeader *) p;
		//verbose(">>>>>> DMI(%d): type=0x%02x, len=0x%d\n",i,dmihdr->type,dmihdr->length);
		if (dmihdr->length < 4) {
			break;
		}
		if (dmihdr->type == 127) {		/* end of table */
			break;
		}
		if (dmihdr->type == 1) {		/* 3.3.2 System Information */
			if (dmihdr->length >= 0x19) {
				found = 1;
			}
			break;
		}
		p = p + dmihdr->length;
		while ((p - (SMBByte *)smbios->dmi.tableAddress + 1 < smbios->dmi.tableLength) && (p[0] != 0x00 || p[1] != 0x00)) {
			p++;
		}
		p += 2;
		i++;
	}

	if (!found) {
		return 0;
	}

	verbose("Found SMBIOS System Information Table 1\n");
	p += 8;

	for (i=0, isZero=1, isOnes=1; i<UUID_LEN; i++) {
		if (p[i] != 0x00) isZero = 0;
		if (p[i] != 0xff) isOnes = 0;
	}
	if (isZero || isOnes) {	/* empty or setable means: no uuid present */
		verbose("No UUID present in SMBIOS System Information Table\n");
		return 0;
	}

	memcpy(uuid, p, UUID_LEN);

	return 1;
}

static void setupEfiGetOverrideConfig( void )
{
	const char	*value;
	int		len;
	int		i;
	unsigned char	uuid[UUID_LEN];

	if (!getValueForKey(kSMBIOS, &value, &len, &bootInfo->bootConfig)) {
		value = "/Extra/smbios.plist";
	}

	if (loadConfigFile(value, &bootInfo->smbiosConfig) == -1) {
		verbose("No SMBIOS replacement found\n");
	}
	if (getValueForKey("SMUUID", &value, &len, &bootInfo->smbiosConfig) && value != NULL && stringToUUID(value, uuid) == 0) {
		verbose("Using SMUUID='%s' from smbios.plist as System-ID\n", value);
		memcpy(SystemID, uuid, UUID_LEN);
	} else if (getValueForKey(kSystemID, &value, &len, &bootInfo->bootConfig) && value != NULL && value[0] != 'N' && value[0] != 'n' && stringToUUID(value, uuid) == 0) {
		verbose("Using SystemID='%s' from com.apple.Boot.plist as System-ID\n", value);
		memcpy(SystemID, uuid, UUID_LEN);
	} else if (getSMBIOSUUID(uuid)) {
		verbose("Using original SMBIOS UUID='%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x' as System-ID\n",
			uuid[0],uuid[1],uuid[2],uuid[3],uuid[4],uuid[5],uuid[6],uuid[7],
			uuid[8],uuid[9],uuid[10],uuid[11],uuid[12],uuid[13],uuid[14],uuid[15]);
		memcpy(SystemID, uuid, UUID_LEN);
	} else {
		verbose("Using builtin default UUID as System-ID\n");
	}
	if (getValueForKey("SMserial", &value, &len, &bootInfo->smbiosConfig)) {
		if (len < MAX_SERIAL_LEN) {
			for (i=0; i<len; i++) {
				SystemSerial[i] = value[i];
			}
			SystemSerial[len] = '\0';
			SystemSerialLength = (len + 1) * 2;
		}
	}
	if (getValueForKey("SMproductname", &value, &len, &bootInfo->smbiosConfig)) {
		if (len < MAX_MODEL_LEN) {
			for (i=0; i<len; i++) {
				Model[i] = value[i];
			}
			Model[len] = '\0';
			ModelLength = (len + 1) * 2;
		}
	}
}

/* Entrypoint from boot.c */
void setupFakeEfi(void)
{
	setupEfiGetOverrideConfig();
	// Generate efi device strings 
	setupEfiDevices();
	// Initialize the base table
	setupEfiTables();
	// Initialize the device tree
	setupEfiDeviceTree();
	// Add configuration table entries to both the services table and the device tree
	setupEfiConfigurationTable();
}
