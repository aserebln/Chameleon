/*
 *  platform.c
 *
 * AsereBLN: cleanup
 */

#include "libsaio.h"
#include "boot.h"
#include "bootstruct.h"
#include "pci.h"
#include "platform.h"
#include "cpu.h"
#include "mem.h"
#include "spd.h"

#ifndef DEBUG_PLATFORM
#define DEBUG_PLATFORM 0
#endif

#if DEBUG_PLATFORM
#define DBG(x...)	printf(x)
#else
#define DBG(x...)
#endif

PlatformInfo_t    Platform;

bool platformCPUFeature(uint32_t feature)
{
	if (Platform.CPU.Features & feature) {
		return true;
	} else {
		return false;
	}
}

void scan_platform(void)
{
	const char	*value;
	int		len;

	memset(&Platform, 0, sizeof(Platform));
	build_pci_dt();
	scan_cpu(&Platform);
	scan_memory(&Platform);
	scan_spd(&Platform);

	Platform.Type = 1;		/* Desktop */
	if (getValueForKey(kSystemType, &value, &len, &bootInfo->bootConfig) && value != NULL) {
		Platform.Type = (unsigned char) strtoul(value, NULL, 10);
		if (Platform.Type > 6) {
			verbose("Error: system-type must be 0..6. Defaulting to 1!\n");
			Platform.Type = 1;
		}
	}
}
