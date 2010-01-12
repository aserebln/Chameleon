/*
 * Supplemental ramdisk functions for the multiboot ramdisk driver
 * Copyright 2009 Tamas Kosarszky. All rights reserved.
 *
 */

#ifndef __BOOT_RAMDISK_H
#define __BOOT_RAMDISK_H

#define RAMDISKCONFIG_FILENAME "rd(0,0)/RAMDisk.plist"

/* mboot.c */
extern struct multiboot_info *gMI;
extern int multibootRamdiskReadBytes( int biosdev, unsigned int blkno,
                      unsigned int byteoff,
                      unsigned int byteCount, void * buffer );
extern int multiboot_get_ramdisk_info(int biosdev, struct driveInfo *dip);
//

extern BVRef gRAMDiskVolume;
extern bool gRAMDiskBTAliased;

extern void setRAMDiskBTHook(bool mode);
extern int mountRAMDisk(const char * param);
extern void processRAMDiskCommand(char ** argPtr, const char * cmd);
extern int loadPrebootRAMDisk();

#endif /* !__BOOT_RAMDISK_H */
