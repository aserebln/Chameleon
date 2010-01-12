/*
 *  usb.c
 *  
 *
 *  Created by mackerintel on 12/20/08.
 *  Copyright 2008 mackerintel. All rights reserved.
 *
 */

#include "libsaio.h"
#include "boot.h"
#include "bootstruct.h"
#include "pci.h"

#ifndef DEBUG_USB
#define DEBUG_USB 0
#endif

#if DEBUG_USB
#define DBG(x...)	printf(x)
#else
#define DBG(x...)
#endif

int ehci_acquire (pci_dt_t *pci_dev)
{
	int		j, k;
	uint32_t	base;
	uint8_t		eecp;
	uint8_t		legacy[8];
	bool		isOwnershipConflict;	
	bool		alwaysHardBIOSReset;

	alwaysHardBIOSReset = false;	
	if (!getBoolForKey(kEHCIhard, &alwaysHardBIOSReset, &bootInfo->bootConfig)) {
		alwaysHardBIOSReset = true;
	}

	pci_config_write16(pci_dev->dev.addr, 0x04, 0x0002);
	base = pci_config_read32(pci_dev->dev.addr, 0x10);

	verbose("EHCI controller [%04x:%04x] at %02x:%2x.%x DMA @%x\n", 
		pci_dev->vendor_id, pci_dev->device_id,
		pci_dev->dev.bits.bus, pci_dev->dev.bits.dev, pci_dev->dev.bits.func, 
		base);

	if (*((unsigned char*)base) < 0xc)
	{
		DBG("Config space too small: no legacy implementation\n");
		return 1;
	}
	eecp = *((unsigned char*)(base + 9));
	if (!eecp) {
		DBG("No extended capabilities: no legacy implementation\n");
		return 1;
	}

	DBG("eecp=%x\n",eecp);

	// bad way to do it
	// pci_conf_write(pci_dev->dev.addr, eecp, 4, 0x01000001);
	for (j = 0; j < 8; j++) {
		legacy[j] = pci_config_read8(pci_dev->dev.addr, eecp + j);
		DBG("%02x ", legacy[j]);
	}
	DBG("\n");

	//Real Job: based on orByte's AppleUSBEHCI.cpp
	//We try soft reset first - some systems hang on reboot with hard reset
	// Definitely needed during reboot on 10.4.6

	isOwnershipConflict = ((legacy[3] & 1 !=  0) && (legacy[2] & 1 !=  0));
	if (!alwaysHardBIOSReset && isOwnershipConflict) {
		DBG("EHCI - Ownership conflict - attempting soft reset ...\n");
		DBG("EHCI - toggle OS Ownership to 0\n");
		pci_config_write8(pci_dev->dev.addr, eecp + 3, 0);
		for (k = 0; k < 25; k++) {
			for (j = 0; j < 8; j++) {
				legacy[j] = pci_config_read8(pci_dev->dev.addr, eecp + j);
			}
			if (legacy[3] == 0) {
				break;
			}
			delay(10);
		}
	}	

	DBG("Found USBLEGSUP_ID - value %x:%x - writing OSOwned\n", legacy[3],legacy[2]);
	pci_config_write8(pci_dev->dev.addr, eecp + 3, 1);

	// wait for kEHCI_USBLEGSUP_BIOSOwned bit to clear
	for (k = 0; k < 25; k++) {
		for (j = 0;j < 8; j++) {
			legacy[j] = pci_config_read8(pci_dev->dev.addr, eecp + j);
		}
		DBG ("%x:%x,",legacy[3],legacy[2]);
		if (legacy[2] == 0) {
			break;
		}
		delay(10);
	}

	for (j = 0;j < 8; j++) {
		legacy[j] = pci_config_read8(pci_dev->dev.addr, eecp + j);
	}
	isOwnershipConflict = ((legacy[2]) != 0);
	if (isOwnershipConflict) {
		// Soft reset has failed. Assume SMI being ignored
		// Hard reset
		// Force Clear BIOS BIT
		DBG("EHCI - Ownership conflict - attempting hard reset ...\n");			
		DBG ("%x:%x\n",legacy[3],legacy[2]);
		DBG("EHCI - Force BIOS Ownership to 0\n");

		pci_config_write8(pci_dev->dev.addr, eecp + 2, 0);
		for (k = 0; k < 25; k++) {
			for (j = 0; j < 8; j++) {
				legacy[j] = pci_config_read8(pci_dev->dev.addr, eecp + j);
			}
			DBG ("%x:%x,",legacy[3],legacy[2]);

			if ((legacy[2]) == 0) {
				break;
			}
			delay(10);	
		}		
		// Disable further SMI events
		for (j = 4; j < 8; j++) {
			pci_config_write8(pci_dev->dev.addr, eecp + j, 0);
		}
	}

	for (j = 0; j < 8; j++) {
		legacy[j] = pci_config_read8(pci_dev->dev.addr, eecp + j);
	}

	DBG ("%x:%x\n",legacy[3],legacy[2]);

	// Final Ownership Resolution Check...
	if (legacy[2] & 1) {					
		DBG("EHCI controller unable to take control from BIOS\n");
		return 0;
	}

	DBG("EHCI Acquire OS Ownership done\n");	
	return 1;
}

int uhci_reset (pci_dt_t *pci_dev)
{
	uint32_t base, port_base;
	
	base = pci_config_read32(pci_dev->dev.addr, 0x20);
	port_base = (base >> 5) & 0x07ff;

	verbose("UHCI controller [%04x:%04x] at %02x:%2x.%x base %x(%x)\n", 
		pci_dev->vendor_id, pci_dev->device_id,
		pci_dev->dev.bits.bus, pci_dev->dev.bits.dev, pci_dev->dev.bits.func, 
		port_base, base);
	
	pci_config_write16(pci_dev->dev.addr, 0xc0, 0x8f00);

	outw (port_base, 0x0002);
	delay(10);
	outw (port_base+4,0);
	delay(10);
	outw (port_base,0);
	return 1;
}
