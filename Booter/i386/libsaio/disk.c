/*
 * Copyright (c) 1999-2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Portions Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 2.0 (the "License").  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON- INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/* 
 * Mach Operating System
 * Copyright (c) 1990 Carnegie-Mellon University
 * Copyright (c) 1989 Carnegie-Mellon University
 * All rights reserved.  The CMU software License Agreement specifies
 * the terms and conditions for use and redistribution.
 */

/*
 *          INTEL CORPORATION PROPRIETARY INFORMATION
 *
 *  This software is supplied under the terms of a license  agreement or 
 *  nondisclosure agreement with Intel Corporation and may not be copied 
 *  nor disclosed except in accordance with the terms of that agreement.
 *
 *  Copyright 1988, 1989 Intel Corporation
 */

/*
 * Copyright 1993 NeXT Computer, Inc.
 * All rights reserved.
 */

/*  Copyright 2007 VMware Inc.
    "Preboot" ramdisk support added by David Elliott
    GPT support added by David Elliott.  Based on IOGUIDPartitionScheme.cpp.
 */

// Allow UFS_SUPPORT to be overridden with preprocessor option.
#ifndef UFS_SUPPORT
// zef: Disabled UFS support
#define UFS_SUPPORT 0
#endif

#include "libsaio.h"
#include "boot.h"
#include "bootstruct.h"
#include "fdisk.h"
#if UFS_SUPPORT
#include "ufs.h"
#endif
#include "hfs.h"
#include "ntfs.h"
#include "msdos.h"
#include "ext2fs.h"

#include <limits.h>
#include <IOKit/storage/IOApplePartitionScheme.h>
#include <IOKit/storage/IOGUIDPartitionScheme.h>
typedef struct gpt_hdr gpt_hdr;
typedef struct gpt_ent gpt_ent;

// For EFI_GUID
#include "efi.h"
#include "efi_tables.h"

#define BPS              512     /* sector size of the device */
#define PROBEFS_SIZE     BPS * 4 /* buffer size for filesystem probe */
#define CD_BPS           2048    /* CD-ROM block size */
#define N_CACHE_SECS     (BIOS_LEN / BPS)  /* Must be a multiple of 4 for CD-ROMs */
#define UFS_FRONT_PORCH  0
#define kAPMSector       2       /* Sector number of Apple partition map */
#define kAPMCDSector     8       /* Translated sector of Apple partition map on a CD */

/*
 * IORound and IOTrunc convenience functions, in the spirit
 * of vm's round_page() and trunc_page().
 */
#define IORound(value,multiple) \
        ((((value) + (multiple) - 1) / (multiple)) * (multiple))

#define IOTrunc(value,multiple) \
        (((value) / (multiple)) * (multiple));

/*
 * trackbuf points to the start of the track cache. Biosread()
 * will store the sectors read from disk to this memory area.
 *
 * biosbuf points to a sector within the track cache, and is
 * updated by Biosread().
 */
static char * const trackbuf = (char *) ptov(BIOS_ADDR);
static char * biosbuf;

/*
 * Map a disk drive to bootable volumes contained within.
 */
struct DiskBVMap {
    int                biosdev;  // BIOS device number (unique)
    BVRef              bvr;      // chain of boot volumes on the disk
    int                bvrcnt;   // number of boot volumes
    struct DiskBVMap * next;     // linkage to next mapping
};

static struct DiskBVMap * gDiskBVMap  = NULL;
static struct disk_blk0 * gBootSector = NULL;

// Function pointers to be filled in if ramdisks are available:
int (*p_ramdiskReadBytes)( int biosdev, unsigned int blkno,
                      unsigned int byteoff,
                      unsigned int byteCount, void * buffer ) = NULL;
int (*p_get_ramdisk_info)(int biosdev, struct driveInfo *dip) = NULL;


extern void spinActivityIndicator(int sectors);

//==========================================================================

static int getDriveInfo( int biosdev,  struct driveInfo *dip )
{
    static struct driveInfo cached_di;
    int cc;
    
    // Real BIOS devices are 8-bit, so anything above that is for internal use.
    // Don't cache ramdisk drive info since it doesn't require several BIOS
    // calls and is thus not worth it.
    if(biosdev >= 0x100)
    {
        if(p_get_ramdisk_info != NULL)
            cc = (*p_get_ramdisk_info)(biosdev, dip);
        else
            cc = -1;
        if(cc < 0)
        {
            dip->valid = 0;
            return -1;
        }
        else
            return 0;
    }

    if ( !cached_di.valid || biosdev != cached_di.biosdev )
    {
	cc = get_drive_info(biosdev, &cached_di);
        if (cc < 0) {
	    cached_di.valid = 0;
            DEBUG_DISK(("get_drive_info returned error\n"));
	    return (-1); // BIOS call error
	}
    }

    bcopy(&cached_di, dip, sizeof(cached_di));

    return 0;
}

//==========================================================================
// Maps (E)BIOS return codes to message strings.

struct NamedValue {
    unsigned char value;
    const char *  name;
};

static const char * getNameForValue( const struct NamedValue * nameTable,
                                     unsigned char value )
{
    const struct NamedValue * np;

    for ( np = nameTable; np->value; np++)
        if (np->value == value)
            return np->name;

    return NULL;
}

#define ECC_CORRECTED_ERR 0x11

static const struct NamedValue bios_errors[] = {
    { 0x10, "Media error"                },
    { 0x11, "Corrected ECC error"        },
    { 0x20, "Controller or device error" },
    { 0x40, "Seek failed"                },
    { 0x80, "Device timeout"             },
    { 0xAA, "Drive not ready"            },
    { 0x00, 0                            }
};

static const char * bios_error(int errnum)
{
    static char  errorstr[] = "Error 0x00";
    const char * errname;

    errname = getNameForValue( bios_errors, errnum );
    if ( errname ) return errname;

    sprintf(errorstr, "Error 0x%02x", errnum);
    return errorstr;   // No string, print error code only
}

//==========================================================================
// Use BIOS INT13 calls to read the sector specified. This function will
// also perform read-ahead to cache a few subsequent sector to the sector
// cache.
// 
// Return:
//   0 on success, or an error code from INT13/F2 or INT13/F42 BIOS call.

static bool cache_valid = false;

static int Biosread( int biosdev, unsigned long long secno )
{
    static int xbiosdev, xcyl, xhead;
    static unsigned int xsec, xnsecs;
    struct driveInfo di;

    int  rc = -1;
    int  cyl, head, sec;
    int  tries = 0;
    int bps, divisor;

    if (getDriveInfo(biosdev, &di) < 0) {
	return -1;
    }
    if (di.no_emulation) {
	/* Always assume 2k block size; BIOS may lie about geometry */
	bps = 2048;
    } else {
	bps = di.di.params.phys_nbps;
        if (bps == 0) {
            return -1;
        }
    }
    divisor = bps / BPS;

    DEBUG_DISK(("Biosread dev %x sec %d bps %d\n", biosdev, secno, bps));

    // To read the disk sectors, use EBIOS if we can. Otherwise,
    // revert to the standard BIOS calls.

    if ((biosdev >= kBIOSDevTypeHardDrive) &&
        (di.uses_ebios & EBIOS_FIXED_DISK_ACCESS))
    {
        if (cache_valid &&
            (biosdev == xbiosdev) &&
            (secno >= xsec) &&
            ((unsigned int)secno < (xsec + xnsecs)))
        {
            biosbuf = trackbuf + (BPS * (secno - xsec));
            return 0;
        }

        xnsecs = N_CACHE_SECS;
        xsec   = (secno / divisor) * divisor;
        cache_valid = false;

        while ((rc = ebiosread(biosdev, secno / divisor, xnsecs / divisor)) && (++tries < 5))
        {
            if (rc == ECC_CORRECTED_ERR) {
                /* Ignore corrected ECC errors */
                rc = 0;
                break;
            }
            error("  EBIOS read error: %s\n", bios_error(rc), rc);
            error("    Block 0x%x Sectors %d\n", secno, xnsecs);
            sleep(1);
        }
    }
    else
    {
	/* spc = spt * heads */
	int spc = (di.di.params.phys_spt * di.di.params.phys_heads);
        cyl  = secno / spc;
        head = (secno % spc) / di.di.params.phys_spt;
        sec  = secno % di.di.params.phys_spt;

        if (cache_valid &&
            (biosdev == xbiosdev) &&
            (cyl == xcyl) &&
            (head == xhead) &&
            ((unsigned int)sec >= xsec) &&
            ((unsigned int)sec < (xsec + xnsecs)))
        {
            // this sector is in trackbuf cache
            biosbuf = trackbuf + (BPS * (sec - xsec));
            return 0;
        }

        // Cache up to a track worth of sectors, but do not cross a
        // track boundary.

        xcyl   = cyl;
        xhead  = head;
        xsec   = sec;
        xnsecs = ((unsigned int)(sec + N_CACHE_SECS) > di.di.params.phys_spt) ? (di.di.params.phys_spt - sec) : N_CACHE_SECS;
        cache_valid = false;

        while ((rc = biosread(biosdev, cyl, head, sec, xnsecs)) &&
               (++tries < 5))
        {
            if (rc == ECC_CORRECTED_ERR) {
                /* Ignore corrected ECC errors */
                rc = 0;
                break;
            }
            error("  BIOS read error: %s\n", bios_error(rc), rc);
            error("    Block %d, Cyl %d Head %d Sector %d\n",
                  secno, cyl, head, sec);
            sleep(1);
        }
    }

    // If the BIOS reported success, mark the sector cache as valid.

    if (rc == 0) {
        cache_valid = true;
    }
    biosbuf  = trackbuf + (secno % divisor) * BPS;
    xbiosdev = biosdev;
    
    spinActivityIndicator(xnsecs);

    return rc;
}

//==========================================================================

int testBiosread( int biosdev, unsigned long long secno )
{
	return Biosread(biosdev, secno);
}

//==========================================================================

static int readBytes( int biosdev, unsigned long long blkno,
                      unsigned int byteoff,
                      unsigned int byteCount, void * buffer )
{
    // ramdisks require completely different code for reading.
    if(p_ramdiskReadBytes != NULL && biosdev >= 0x100)
        return (*p_ramdiskReadBytes)(biosdev, blkno, byteoff, byteCount, buffer);

    char * cbuf = (char *) buffer;
    int    error;
    int    copy_len;

    DEBUG_DISK(("%s: dev %x block %x [%d] -> 0x%x...", __FUNCTION__,
                biosdev, blkno, byteCount, (unsigned)cbuf));

    for ( ; byteCount; cbuf += copy_len, blkno++ )
    {
        error = Biosread( biosdev, blkno );
        if ( error )
        {
            DEBUG_DISK(("error\n"));
            return (-1);
        }

        copy_len = ((byteCount + byteoff) > BPS) ? (BPS - byteoff) : byteCount;
        bcopy( biosbuf + byteoff, cbuf, copy_len );
        byteCount -= copy_len;
        byteoff = 0;
    }

    DEBUG_DISK(("done\n"));

    return 0;    
}

//==========================================================================

static int isExtendedFDiskPartition( const struct fdisk_part * part )
{
    static unsigned char extParts[] =
    {
        0x05,   /* Extended */
        0x0f,   /* Win95 extended */
        0x85,   /* Linux extended */
    };

    unsigned int i;

    for (i = 0; i < sizeof(extParts)/sizeof(extParts[0]); i++)
    {
        if (extParts[i] == part->systid) return 1;
    }
    return 0;
}

//==========================================================================

static int getNextFDiskPartition( int biosdev, int * partno,
                                  const struct fdisk_part ** outPart )
{
    static int                 sBiosdev = -1;
    static int                 sNextPartNo;
    static unsigned int        sFirstBase;
    static unsigned int        sExtBase;
    static unsigned int        sExtDepth;
    static struct fdisk_part * sExtPart;
    struct fdisk_part *        part;

    if ( sBiosdev != biosdev || *partno < 0 )
    {
        // Fetch MBR.
        if ( readBootSector( biosdev, DISK_BLK0, 0 ) ) return 0;

        sBiosdev    = biosdev;
        sNextPartNo = 0;
        sFirstBase  = 0;
        sExtBase    = 0;
        sExtDepth   = 0;
        sExtPart    = NULL;
    }

    while (1)
    {
        part  = NULL;

        if ( sNextPartNo < FDISK_NPART )
        {
            part = (struct fdisk_part *) gBootSector->parts[sNextPartNo];
        }
        else if ( sExtPart )
        {
            unsigned int blkno = sExtPart->relsect + sFirstBase;

            // Save the block offset of the first extended partition.

            if (sExtDepth == 0) {
                sFirstBase = blkno;
            }
            sExtBase = blkno;

            // Load extended partition table.

            if ( readBootSector( biosdev, blkno, 0 ) == 0 )
            {
                sNextPartNo = 0;
                sExtDepth++;
                sExtPart = NULL;
                continue;
            }
            // Fall through to part == NULL
        }

        if ( part == NULL ) break;  // Reached end of partition chain.

        // Advance to next partition number.

        sNextPartNo++;

        if ( isExtendedFDiskPartition(part) )
        {
            sExtPart = part;
            continue;
        }

        // Skip empty slots.

        if ( part->systid == 0x00 )
        {
            continue;
        }

        // Change relative offset to an absolute offset.
        part->relsect += sExtBase;

        *outPart = part;
        *partno  = sExtDepth ? (int)(sExtDepth + FDISK_NPART) : sNextPartNo;

        break;
    }

    return (part != NULL);
}

//==========================================================================

static BVRef newFDiskBVRef( int biosdev, int partno, unsigned int blkoff,
                            const struct fdisk_part * part,
                            FSInit initFunc, FSLoadFile loadFunc,
                            FSReadFile readFunc,
                            FSGetDirEntry getdirFunc,
                            FSGetFileBlock getBlockFunc,
                            FSGetUUID getUUIDFunc,
                            BVGetDescription getDescriptionFunc,
                            BVFree bvFreeFunc,
                            int probe, int type, unsigned int bvrFlags )
{
    BVRef bvr = (BVRef) MALLOC( sizeof(*bvr) );
    if ( bvr )
    {
        bzero(bvr, sizeof(*bvr));

        bvr->biosdev        = biosdev;
        bvr->part_no        = partno;
        bvr->part_boff      = blkoff;
        bvr->part_type      = part->systid;
        bvr->fs_loadfile    = loadFunc;
        bvr->fs_readfile    = readFunc;
        bvr->fs_getdirentry = getdirFunc;
        bvr->fs_getfileblock= getBlockFunc;
        bvr->fs_getuuid     = getUUIDFunc;
        bvr->description    = getDescriptionFunc;
        bvr->type           = type;
        bvr->bv_free        = bvFreeFunc;

        if ((part->bootid & FDISK_ACTIVE) && (part->systid == FDISK_HFS))
            bvr->flags |= kBVFlagPrimary;

        // Probe the filesystem.

        if ( initFunc )
        {
            bvr->flags |= kBVFlagNativeBoot;

            if ( probe && initFunc( bvr ) != 0 )
            {
                // filesystem probe failed.

                DEBUG_DISK(("%s: failed probe on dev %x part %d\n",
                            __FUNCTION__, biosdev, partno));

                (*bvr->bv_free)(bvr);
                bvr = NULL;
            }
            if ( readBootSector( biosdev, blkoff, (void *)0x7e00 ) == 0 )
            {
            	bvr->flags |= kBVFlagBootable;
            }
        }
        else if ( readBootSector( biosdev, blkoff, (void *)0x7e00 ) == 0 )
        {
            bvr->flags |= kBVFlagForeignBoot;
        }
        else
        {
            (*bvr->bv_free)(bvr);
            bvr = NULL;
        }
    }
    if (bvr) bvr->flags |= bvrFlags;
    return bvr;
}

//==========================================================================

BVRef newAPMBVRef( int biosdev, int partno, unsigned int blkoff,
                   const DPME * part,
                   FSInit initFunc, FSLoadFile loadFunc,
                   FSReadFile readFunc,
                   FSGetDirEntry getdirFunc,
                   FSGetFileBlock getBlockFunc,
                   FSGetUUID getUUIDFunc,
                   BVGetDescription getDescriptionFunc,
                   BVFree bvFreeFunc,
                   int probe, int type, unsigned int bvrFlags )
{
    BVRef bvr = (BVRef) MALLOC( sizeof(*bvr) );
    if ( bvr )
    {
        bzero(bvr, sizeof(*bvr));

        bvr->biosdev        = biosdev;
        bvr->part_no        = partno;
        bvr->part_boff      = blkoff;
        bvr->fs_loadfile    = loadFunc;
        bvr->fs_readfile    = readFunc;
        bvr->fs_getdirentry = getdirFunc;
        bvr->fs_getfileblock= getBlockFunc;
        bvr->fs_getuuid     = getUUIDFunc;
        bvr->description    = getDescriptionFunc;
        bvr->type           = type;
        bvr->bv_free        = bvFreeFunc;
        strlcpy(bvr->name, part->dpme_name, DPISTRLEN);
        strlcpy(bvr->type_name, part->dpme_type, DPISTRLEN);

        /*
        if ( part->bootid & FDISK_ACTIVE )
            bvr->flags |= kBVFlagPrimary;
        */

        // Probe the filesystem.

        if ( initFunc )
        {
            bvr->flags |= kBVFlagNativeBoot | kBVFlagBootable | kBVFlagSystemVolume;

            if ( probe && initFunc( bvr ) != 0 )
            {
                // filesystem probe failed.

                DEBUG_DISK(("%s: failed probe on dev %x part %d\n",
                            __FUNCTION__, biosdev, partno));

                (*bvr->bv_free)(bvr);
                bvr = NULL;
            }
        }
        /*
        else if ( readBootSector( biosdev, blkoff, (void *)0x7e00 ) == 0 )
        {
            bvr->flags |= kBVFlagForeignBoot;
        }
        */
        else
        {
            (*bvr->bv_free)(bvr);
            bvr = NULL;
        }
    }
    if (bvr) bvr->flags |= bvrFlags;
    return bvr;
}

//==========================================================================

// HFS+ GUID in LE form
EFI_GUID const GPT_HFS_GUID	= { 0x48465300, 0x0000, 0x11AA, { 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } };
// turbo - also our booter partition
EFI_GUID const GPT_BOOT_GUID	= { 0x426F6F74, 0x0000, 0x11AA, { 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } };
// turbo - or an efi system partition
EFI_GUID const GPT_EFISYS_GUID	= { 0xC12A7328, 0xF81F, 0x11D2, { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B } };
// zef - basic data partition EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 for foreign OS support
EFI_GUID const GPT_BASICDATA_GUID = { 0xEBD0A0A2, 0xB9E5, 0x4433, { 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 } };
EFI_GUID const GPT_BASICDATA2_GUID = { 0xE3C9E316, 0x0B5C, 0x4DB8, { 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE } };


BVRef newGPTBVRef( int biosdev, int partno, unsigned int blkoff,
                   const gpt_ent * part,
                   FSInit initFunc, FSLoadFile loadFunc,
                   FSReadFile readFunc,
                   FSGetDirEntry getdirFunc,
                   FSGetFileBlock getBlockFunc,
                   FSGetUUID getUUIDFunc,
                   BVGetDescription getDescriptionFunc,
                   BVFree bvFreeFunc,
                   int probe, int type, unsigned int bvrFlags )
{
    BVRef bvr = (BVRef) MALLOC( sizeof(*bvr) );
    if ( bvr )
    {
        bzero(bvr, sizeof(*bvr));

        bvr->biosdev        = biosdev;
        bvr->part_no        = partno;
        bvr->part_boff      = blkoff;
        bvr->fs_loadfile    = loadFunc;
        bvr->fs_readfile    = readFunc;
        bvr->fs_getdirentry = getdirFunc;
        bvr->fs_getfileblock= getBlockFunc;
        bvr->fs_getuuid     = getUUIDFunc;
        bvr->description    = getDescriptionFunc;
      	bvr->type           = type;
        bvr->bv_free        = bvFreeFunc;
        // FIXME: UCS-2 -> UTF-8 the name
        strlcpy(bvr->name, "----", DPISTRLEN);
        if ( (efi_guid_compare(&GPT_BOOT_GUID, (EFI_GUID const*)part->ent_type) == 0) ||
          (efi_guid_compare(&GPT_HFS_GUID, (EFI_GUID const*)part->ent_type) == 0) )
          strlcpy(bvr->type_name, "GPT HFS+", DPISTRLEN);
        else
          strlcpy(bvr->type_name, "GPT Unknown", DPISTRLEN);

        /*
        if ( part->bootid & FDISK_ACTIVE )
            bvr->flags |= kBVFlagPrimary;
        */

        // Probe the filesystem.

        if ( initFunc )
        {
            bvr->flags |= kBVFlagNativeBoot;

            if ( probe && initFunc( bvr ) != 0 )
            {
                // filesystem probe failed.

                DEBUG_DISK(("%s: failed probe on dev %x part %d\n",
                            __FUNCTION__, biosdev, partno));

                (*bvr->bv_free)(bvr);
                bvr = NULL;
            }
            if ( readBootSector( biosdev, blkoff, (void *)0x7e00 ) == 0 )
            {
            	bvr->flags |= kBVFlagBootable;
            }
        }
        else if ( readBootSector( biosdev, blkoff, (void *)0x7e00 ) == 0 )
        {
            bvr->flags |= kBVFlagForeignBoot;
        }
        else
        {
            (*bvr->bv_free)(bvr);
            bvr = NULL;
        }
    }
    if (bvr) bvr->flags |= bvrFlags;
    return bvr;
}

//==========================================================================

/* A note on partition numbers:
 * IOKit makes the primary partitions numbers 1-4, and then
 * extended partitions are numbered consecutively 5 and up.
 * So, for example, if you have two primary partitions and
 * one extended partition they will be numbered 1, 2, 5.
 */

static BVRef diskScanFDiskBootVolumes( int biosdev, int * countPtr )
{
    const struct fdisk_part * part;
    struct DiskBVMap *        map;
    int                       partno  = -1;
    BVRef                     bvr;
#if UFS_SUPPORT
    BVRef                     booterUFS = NULL;
#endif
    int                       spc;
    struct driveInfo          di;
    boot_drive_info_t         *dp;

    /* Initialize disk info */
    if (getDriveInfo(biosdev, &di) != 0) {
	return NULL;
    }
    dp = &di.di;
    spc = (dp->params.phys_spt * dp->params.phys_heads);
    if (spc == 0) {
	/* This is probably a CD-ROM; punt on the geometry. */
	spc = 1;
    }

    do {
        // Create a new mapping.

        map = (struct DiskBVMap *) MALLOC( sizeof(*map) );
        if ( map )
        {
            map->biosdev = biosdev;
            map->bvr     = NULL;
            map->bvrcnt  = 0;
            map->next    = gDiskBVMap;
            gDiskBVMap   = map;

            // Create a record for each partition found on the disk.

            while ( getNextFDiskPartition( biosdev, &partno, &part ) )
            {
                DEBUG_DISK(("%s: part %d [%x]\n", __FUNCTION__,
                            partno, part->systid));
                bvr = 0;

                switch ( part->systid )
                {
#if UFS_SUPPORT
                    case FDISK_UFS:
                       bvr = newFDiskBVRef(
                                      biosdev, partno,
                                      part->relsect + UFS_FRONT_PORCH/BPS,
                                      part,
                                      UFSInitPartition,
                                      UFSLoadFile,
                                      UFSReadFile,
                                      UFSGetDirEntry,
                                      UFSGetFileBlock,
                                      UFSGetUUID,
                                      UFSGetDescription,
                                      UFSFree,
                                      0,
                                      kBIOSDevTypeHardDrive, 0);
                        break;
#endif

                    case FDISK_HFS:
                        bvr = newFDiskBVRef(
                                      biosdev, partno,
                                      part->relsect,
                                      part,
                                      HFSInitPartition,
                                      HFSLoadFile,
                                      HFSReadFile,
                                      HFSGetDirEntry,
                                      HFSGetFileBlock,
                                      HFSGetUUID,
                                      HFSGetDescription,
                                      HFSFree,
                                      0,
                                      kBIOSDevTypeHardDrive, 0);
                        break;

                    // turbo - we want the booter type scanned also
                    case FDISK_BOOTER:
                        if (part->bootid & FDISK_ACTIVE)
                        gBIOSBootVolume = newFDiskBVRef(
                                      biosdev, partno,
                                      part->relsect,
                                      part,
                                      HFSInitPartition,
                                      HFSLoadFile,
                                      HFSReadFile,
                                      HFSGetDirEntry,
                                      HFSGetFileBlock,
                                      HFSGetUUID,
                                      HFSGetDescription,
                                      HFSFree,
                                      0,
                                      kBIOSDevTypeHardDrive, 0);
                        break;

#if UFS_SUPPORT
                    case FDISK_BOOTER:
                        booterUFS = newFDiskBVRef(
                                      biosdev, partno,
                                      ((part->relsect + spc - 1) / spc) * spc,
                                      part,
                                      UFSInitPartition,
                                      UFSLoadFile,
                                      UFSReadFile,
                                      UFSGetDirEntry,
                                      UFSGetFileBlock,
                                      UFSGetUUID,
                                      UFSGetDescription,
                                      UFSFree,
                                      0,
                                      kBIOSDevTypeHardDrive, 0);
                        break;
#endif

                    case FDISK_FAT32:
                    case FDISK_DOS12:
                    case FDISK_DOS16S:
                    case FDISK_DOS16B:
                    case FDISK_SMALLFAT32:
                    case FDISK_DOS16SLBA:
                      bvr = newFDiskBVRef(
                            biosdev, partno,
                            part->relsect,
                            part,
                            MSDOSInitPartition,
                            MSDOSLoadFile,
                            MSDOSReadFile,
                            MSDOSGetDirEntry,
                            MSDOSGetFileBlock,
                            MSDOSGetUUID,
                            MSDOSGetDescription,
                            MSDOSFree,
                            0,
                            kBIOSDevTypeHardDrive, 0);
                    break;
						
                    case FDISK_NTFS:
                      bvr = newFDiskBVRef(
                                    biosdev, partno,
                                    part->relsect,
                                    part,
                                    0, 0, 0, 0, 0, 0,
                                    NTFSGetDescription,
                                    (BVFree)free,
                                    0, kBIOSDevTypeHardDrive, 0);
                    break;

                    case FDISK_LINUX:
                      bvr = newFDiskBVRef(
                      biosdev, partno,
                      part->relsect,
                      part,
                      0, 0, 0, 0, 0, 0,
                      EX2GetDescription,
                      (BVFree)free,
                      0, kBIOSDevTypeHardDrive, 0);
                    break;
				
                    default:
                        bvr = newFDiskBVRef(
                                      biosdev, partno,
                                      part->relsect,
                                      part,
                                      0, 0, 0, 0, 0, 0, 0,
                                      (BVFree)free,
                                      0,
                                      kBIOSDevTypeHardDrive, 0);
                    break;
                }

                if ( bvr )
                {
                    bvr->next = map->bvr;
                    map->bvr  = bvr;
                    map->bvrcnt++;
                }
            }

#if UFS_SUPPORT
            // Booting from a CD with an UFS filesystem embedded
            // in a booter partition.

            if ( booterUFS )
            {
                if ( map->bvrcnt == 0 )
                {
                    map->bvr = booterUFS;
                    map->bvrcnt++;
                }
                else free( booterUFS );
            }
#endif
        }
    } while (0);

    /*
     * If no FDisk partition, then we will check for
     * an Apple partition map elsewhere.
     */
#if UNUSED
    if (map->bvrcnt == 0) {
	static struct fdisk_part cdpart;
	cdpart.systid = 0xCD;

	/* Let's try assuming we are on a hybrid HFS/ISO9660 CD. */
	bvr = newFDiskBVRef(
			    biosdev, 0,
			    0,
			    &cdpart,
			    HFSInitPartition,
			    HFSLoadFile,
                            HFSReadFile,
			    HFSGetDirEntry,
                            HFSGetFileBlock,
                            HFSGetUUID,
			    0,
			    kBIOSDevTypeHardDrive);
	bvr->next = map->bvr;
	map->bvr = bvr;
	map->bvrcnt++;
    }
#endif
    // Actually this should always be true given the above code
    if(map == gDiskBVMap)
    {
        // Don't leave a null map in the chain
        if(map->bvrcnt == 0 && map->bvr == NULL)
        {
            gDiskBVMap = map->next;
            free(map);
            map = NULL;
        }
    }

    if (countPtr) *countPtr = map ? map->bvrcnt : 0;

    return map ? map->bvr : NULL;
}

//==========================================================================

static BVRef diskScanAPMBootVolumes( int biosdev, int * countPtr )
{
    struct DiskBVMap *        map;
    struct Block0 *block0_p;
    unsigned int blksize;
    unsigned int factor;
    void *buffer = MALLOC(BPS);

    /* Check for alternate block size */
    if (readBytes( biosdev, 0, 0, BPS, buffer ) != 0) {
        return NULL;
    }
    block0_p = buffer;
    if (OSSwapBigToHostInt16(block0_p->sbSig) == BLOCK0_SIGNATURE) {
        blksize = OSSwapBigToHostInt16(block0_p->sbBlkSize);
        if (blksize != BPS) {
            free(buffer);
            buffer = MALLOC(blksize);
        }
        factor = blksize / BPS;
    } else {
        blksize = BPS;
        factor = 1;
    }
    
    do {
        // Create a new mapping.

        map = (struct DiskBVMap *) MALLOC( sizeof(*map) );
        if ( map )
        {
            int error;
            DPME *dpme_p = (DPME *)buffer;
            UInt32 i, npart = UINT_MAX;
            BVRef bvr;

            map->biosdev = biosdev;
            map->bvr     = NULL;
            map->bvrcnt  = 0;
            map->next    = gDiskBVMap;
            gDiskBVMap   = map;

            for (i=0; i<npart; i++) {
                error = readBytes( biosdev, (kAPMSector + i) * factor, 0, blksize, buffer );

                if (error || OSSwapBigToHostInt16(dpme_p->dpme_signature) != DPME_SIGNATURE) {
                    break;
                }

                if (i==0) {
                    npart = OSSwapBigToHostInt32(dpme_p->dpme_map_entries);
                }
                /*
                printf("name = %s, %s%s  %d -> %d [%d -> %d] {%d}\n",
                       dpme.dpme_name, dpme.dpme_type, (dpme.dpme_flags & DPME_FLAGS_BOOTABLE) ? "(bootable)" : "",
                       dpme.dpme_pblock_start, dpme.dpme_pblocks,
                       dpme.dpme_lblock_start, dpme.dpme_lblocks,
                       dpme.dpme_boot_block);
                */

                if (strcmp(dpme_p->dpme_type, "Apple_HFS") == 0) {
                    bvr = newAPMBVRef(biosdev,
                                      i,
                                      OSSwapBigToHostInt32(dpme_p->dpme_pblock_start) * factor,
                                      dpme_p,
                                      HFSInitPartition,
                                      HFSLoadFile,
                                      HFSReadFile,
                                      HFSGetDirEntry,
                                      HFSGetFileBlock,
                                      HFSGetUUID,
                                      HFSGetDescription,
                                      HFSFree,
                                      0,
                                      kBIOSDevTypeHardDrive, 0);
                    bvr->next = map->bvr;
                    map->bvr = bvr;
                    map->bvrcnt++;
                }
            }
        }
    } while (0);

    free(buffer);

    if (countPtr) *countPtr = map ? map->bvrcnt : 0;

    return map ? map->bvr : NULL;
}

//==========================================================================

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/*
 * Trying to figure out the filsystem type of a given partition.
 */
static int probeFileSystem(int biosdev, unsigned int blkoff)
{
  // detected filesystem type;
  int result = -1;
  int fatbits;

  // Allocating buffer for 4 sectors.
  const void * probeBuffer = MALLOC(PROBEFS_SIZE);
  if (probeBuffer == NULL)
    goto exit;

  // Reading first 4 sectors of current partition
  int error = readBytes(biosdev, blkoff, 0, PROBEFS_SIZE, (void *)probeBuffer);
  if (error)
    goto exit;

  if (HFSProbe(probeBuffer))
    result = FDISK_HFS;
  else if (EX2Probe(probeBuffer))
	  result = FDISK_LINUX;
  else if (NTFSProbe(probeBuffer))
    result = FDISK_NTFS;
  else if (fatbits=MSDOSProbe(probeBuffer))
  {
	  switch (fatbits)
	  {
		  case 32:
		  default:
			  result = FDISK_FAT32;
			  break;
		  case 16:
			  result = FDISK_DOS16B;
			  break;
		  case 12:
			  result = FDISK_DOS12;
			  break;			  
	  }
  }
  else
  // Couldn't detect filesystem type
    result = 0;
  
exit:
  if (probeBuffer != NULL) free((void *)probeBuffer);
  return result;
}

static bool isPartitionUsed(gpt_ent * partition)
{
    //
    // Ask whether the given partition is used.
    //

    return efi_guid_is_null((EFI_GUID const*)partition->ent_type) ? false : true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static BVRef diskScanGPTBootVolumes( int biosdev, int * countPtr )
{
    struct DiskBVMap *        map = NULL;
    void *buffer = MALLOC(BPS);
    int error;
    if ( error = readBytes( biosdev, /*secno*/0, 0, BPS, buffer ) != 0) {
        verbose("Failed to read boot sector from BIOS device %02xh. Error=%d\n", biosdev, error);
        goto scanErr;
    }
    struct REAL_disk_blk0 *fdiskMap = buffer;
    if ( OSSwapLittleToHostInt16(fdiskMap->signature) != DISK_SIGNATURE )
    {
        verbose("Failed to find boot signature on BIOS device %02xh\n", biosdev);
        goto scanErr;
    }

    int fdiskID = 0;
    unsigned index;
    for ( index = 0; index < FDISK_NPART; index++ )
    {
        if ( fdiskMap->parts[index].systid )
        {
            if ( fdiskMap->parts[index].systid == 0xEE )
            {
                // Fail if two 0xEE partitions are present which
                // means the FDISK code will wind up parsing it.
                if ( fdiskID )  goto scanErr;

                fdiskID = index + 1;
            }
        }
    }

    if ( fdiskID == 0 )  goto scanErr;
    verbose("Attempting to read GPT\n");

    if(readBytes(biosdev, 1, 0, BPS, buffer) != 0)
        goto scanErr;
    
    gpt_hdr *headerMap = buffer;

    // Determine whether the partition header signature is present.

    if ( memcmp(headerMap->hdr_sig, GPT_HDR_SIG, strlen(GPT_HDR_SIG)) )
    {
        goto scanErr;
    }

    // Determine whether the partition header size is valid.

    UInt32 headerCheck = OSSwapLittleToHostInt32(headerMap->hdr_crc_self);
    UInt32 headerSize  = OSSwapLittleToHostInt32(headerMap->hdr_size);

    if ( headerSize < offsetof(gpt_hdr, padding) )
    {
        goto scanErr;
    }

    if ( headerSize > BPS )
    {
        goto scanErr;
    }

    // Determine whether the partition header checksum is valid.

    headerMap->hdr_crc_self = 0;

    if ( crc32(0, headerMap, headerSize) != headerCheck )
    {
        goto scanErr;
    }

    // Determine whether the partition entry size is valid.

    UInt64                     gptBlock       = 0;
    UInt32                     gptCheck       = 0;
    UInt32                     gptCount       = 0;
    UInt32                     gptID          = 0;
    gpt_ent *                  gptMap         = 0;
    UInt32                     gptSize        = 0;

    gptBlock = OSSwapLittleToHostInt64(headerMap->hdr_lba_table);
    gptCheck = OSSwapLittleToHostInt32(headerMap->hdr_crc_table);
    gptCount = OSSwapLittleToHostInt32(headerMap->hdr_entries);
    gptSize  = OSSwapLittleToHostInt32(headerMap->hdr_entsz);

    if ( gptSize < sizeof(gpt_ent) )
    {
        goto scanErr;
    }

    // Allocate a buffer large enough to hold one map, rounded to a media block.
    free(buffer);
    buffer = NULL;

    UInt32 bufferSize = IORound(gptCount * gptSize, BPS);
    if(bufferSize == 0)
        goto scanErr;
    buffer = MALLOC(bufferSize);

    if(readBytes(biosdev, gptBlock, 0, bufferSize, buffer) != 0)
        goto scanErr;

    verbose("Read GPT\n");

    // Allocate a new map for this BIOS device and insert it into the chain
    map = MALLOC(sizeof(*map));
    map->biosdev = biosdev;
    map->bvr = NULL;
    map->bvrcnt = 0;
    map->next = gDiskBVMap;
    gDiskBVMap = map;

    // fdisk like partition type id.
    int fsType = 0;
    
    for(gptID = 1; gptID <= gptCount; ++gptID)
    {
				BVRef bvr = NULL;
				unsigned int bvrFlags = 0;
    
        // size on disk can be larger than sizeof(gpt_ent)
        gptMap = (gpt_ent *) ( buffer + ( (gptID - 1) * gptSize)  );

        // NOTE: EFI_GUID's are in LE and we know we're on an x86.
        // The IOGUIDPartitionScheme.cpp code uses byte-based UUIDs, we don't.

        if(isPartitionUsed(gptMap))
        {
            char stringuuid[100];
            efi_guid_unparse_upper((EFI_GUID*)gptMap->ent_type, stringuuid);
            verbose("Reading GPT partition %d, type %s\n", gptID, stringuuid);

            // Getting fdisk like partition type.
            fsType = probeFileSystem(biosdev, gptMap->ent_lba_start);

            if ( (efi_guid_compare(&GPT_BOOT_GUID, (EFI_GUID const*)gptMap->ent_type) == 0) ||
                 (efi_guid_compare(&GPT_HFS_GUID, (EFI_GUID const*)gptMap->ent_type) == 0) )
            {
                bvrFlags = (efi_guid_compare(&GPT_BOOT_GUID, (EFI_GUID const*)gptMap->ent_type) == 0) ? kBVFlagBooter : 0;
                bvr = newGPTBVRef(biosdev,
                                      gptID,
                                      gptMap->ent_lba_start,
                                      gptMap,
                                      HFSInitPartition,
                                      HFSLoadFile,
                                      HFSReadFile,
                                      HFSGetDirEntry,
                                      HFSGetFileBlock,
                                      HFSGetUUID,
                                      HFSGetDescription,
                                      HFSFree,
                                      0,
                                      kBIOSDevTypeHardDrive, bvrFlags);
            }

						// zef - foreign OS support
            if ( (efi_guid_compare(&GPT_BASICDATA_GUID, (EFI_GUID const*)gptMap->ent_type) == 0) ||
                 (efi_guid_compare(&GPT_BASICDATA2_GUID, (EFI_GUID const*)gptMap->ent_type) == 0) )
            {
							switch (fsType)
              {
							  case FDISK_NTFS:
							    bvr = newGPTBVRef(biosdev, gptID, gptMap->ent_lba_start, gptMap,
                	  								0, 0, 0, 0, 0, 0, NTFSGetDescription,
                		  							(BVFree)free, 0, kBIOSDevTypeHardDrive, 0);
								break;

                default:
                  bvr = newGPTBVRef(biosdev, gptID, gptMap->ent_lba_start, gptMap,
                                    0, 0, 0, 0, 0, 0, 0,
                                    (BVFree)free, 0, kBIOSDevTypeHardDrive, 0);
                break;
              }
							  
            }

            // turbo - save our booter partition
            // zef - only on original boot device
            if ( (efi_guid_compare(&GPT_EFISYS_GUID, (EFI_GUID const*)gptMap->ent_type) == 0) )
            {
							switch (fsType)
              {
                case FDISK_HFS:
                  if (readBootSector( biosdev, gptMap->ent_lba_start, (void *)0x7e00 ) == 0)
                  {
                    bvr = newGPTBVRef(biosdev, gptID, gptMap->ent_lba_start, gptMap,
                          HFSInitPartition,
                          HFSLoadFile,
                          HFSReadFile,
                          HFSGetDirEntry,
                          HFSGetFileBlock,
                          HFSGetUUID,
                          HFSGetDescription,
                          HFSFree,
                          0, kBIOSDevTypeHardDrive, kBVFlagEFISystem);
                  }
                break;

                case FDISK_FAT32:
                  if (testFAT32EFIBootSector( biosdev, gptMap->ent_lba_start, (void *)0x7e00 ) == 0)
                  {
                    bvr = newGPTBVRef(biosdev, gptID, gptMap->ent_lba_start, gptMap,
                          MSDOSInitPartition,
                          MSDOSLoadFile,
                          MSDOSReadFile,
                          MSDOSGetDirEntry,
                          MSDOSGetFileBlock,
                          MSDOSGetUUID,
                          MSDOSGetDescription,
                          MSDOSFree,
                          0, kBIOSDevTypeHardDrive, kBVFlagEFISystem);
                  }
                break;

                if (biosdev == gBIOSDev)
                  gBIOSBootVolume = bvr;
              }
            }            

						if (bvr)
						{
              // Fixup bvr with the fake fdisk partition type.
              if (fsType > 0) bvr->part_type = fsType;

							bvr->next = map->bvr;
							map->bvr = bvr;
							++map->bvrcnt;
						}

        }
    }

scanErr:
    free(buffer);

    if(map)
    {
        if(countPtr) *countPtr = map->bvrcnt;
        return map->bvr;
    }
    else
    {
        if(countPtr) *countPtr = 0;
        return NULL;
    }
}

//==========================================================================

static void scanFSLevelBVRSettings(BVRef chain)
{
  BVRef bvr;
  char  dirSpec[512], fileSpec[512];
  char  label[BVSTRLEN];
  int   ret;
  long  flags, time;
  int   fh, fileSize, error;

  for (bvr = chain; bvr; bvr = bvr->next)
  {
    ret = -1;
    error = 0;
    
    //
    // Check for alternate volume label on boot helper partitions.
    //
    if (bvr->flags & kBVFlagBooter)
    {
      sprintf(dirSpec, "hd(%d,%d)/System/Library/CoreServices/", BIOS_DEV_UNIT(bvr), bvr->part_no);
      strcpy(fileSpec, ".disk_label.contentDetails");
      ret = GetFileInfo(dirSpec, fileSpec, &flags, &time);
      if (!ret)
      {
        fh = open(strcat(dirSpec, fileSpec), 0);
        fileSize = file_size(fh);
        if (fileSize > 0 && fileSize < BVSTRLEN)
        {
          if (read(fh, label, fileSize) != fileSize)
            error = -1;
        }
        else
          error = -1;

        close(fh);

        if (!error)
        {
          label[fileSize] = '\0';
          strcpy(bvr->altlabel, label);
        }
      }
    }

    //
    // Check for SystemVersion.plist or ServerVersion.plist
    // to determine if a volume hosts an installed system.
    //
    if (bvr->flags & kBVFlagNativeBoot)
    {
      sprintf(dirSpec, "hd(%d,%d)/System/Library/CoreServices/", BIOS_DEV_UNIT(bvr), bvr->part_no);
      strcpy(fileSpec, "SystemVersion.plist");
      ret = GetFileInfo(dirSpec, fileSpec, &flags, &time);

      if (ret == -1)
      {
        strcpy(fileSpec, "ServerVersion.plist");
        ret = GetFileInfo(dirSpec, fileSpec, &flags, &time);
      }

      if (!ret)
        bvr->flags |= kBVFlagSystemVolume;
    }

  }
}

void rescanBIOSDevice(int biosdev)
{
  struct DiskBVMap *oldMap = diskResetBootVolumes(biosdev);
  CacheReset();
  diskFreeMap(oldMap);
  oldMap = NULL;
  
  scanBootVolumes(biosdev, 0);
}

struct DiskBVMap* diskResetBootVolumes(int biosdev)
{
    struct DiskBVMap *        map;
    struct DiskBVMap *prevMap = NULL;
    for ( map = gDiskBVMap; map; prevMap = map, map = map->next ) {
        if ( biosdev == map->biosdev ) {
            break;
        }
    }
    if(map != NULL)
    {
        verbose("Resetting BIOS device %xh\n", biosdev);
        // Reset the biosbuf cache
        cache_valid = false;
        if(map == gDiskBVMap)
            gDiskBVMap = map->next;
        else if(prevMap != NULL)
            prevMap->next = map->next;
        else
            stop("");
    }
    // Return the old map, either to be freed, or reinserted later
    return map;
}

// Frees a DiskBVMap and all of its BootVolume's
void diskFreeMap(struct DiskBVMap *map)
{
    if(map != NULL)
    {
        while(map->bvr != NULL)
        {
            BVRef bvr = map->bvr;
            map->bvr = bvr->next;
            (*bvr->bv_free)(bvr);
        }
        free(map);
    }
}

BVRef diskScanBootVolumes( int biosdev, int * countPtr )
{
    struct DiskBVMap *        map;
    BVRef bvr;
    int count = 0;

    // Find an existing mapping for this device.

    for ( map = gDiskBVMap; map; map = map->next ) {
        if ( biosdev == map->biosdev ) {
            count = map->bvrcnt;
            break;
        }
    }

    if (map == NULL) {
        bvr = diskScanGPTBootVolumes(biosdev, &count);
        if (bvr == NULL) {
          bvr = diskScanFDiskBootVolumes(biosdev, &count);
        }
        if (bvr == NULL) {
          bvr = diskScanAPMBootVolumes(biosdev, &count);
        }
        if (bvr)
        {
          scanFSLevelBVRSettings(bvr);
        }
    } else {
        bvr = map->bvr;
    }
    if (countPtr)  *countPtr += count;
    return bvr;
}

BVRef getBVChainForBIOSDev(int biosdev)
{
  BVRef chain = NULL;
  struct DiskBVMap * map = NULL;

  for (map = gDiskBVMap; map; map = map->next)
  {
    if (map->biosdev == biosdev)
    {
      chain = map->bvr;
      break;
    }
  }
  
  return chain;
}

BVRef newFilteredBVChain(int minBIOSDev, int maxBIOSDev, unsigned int allowFlags, unsigned int denyFlags, int *count)
{
  BVRef chain = NULL;
  BVRef bvr = NULL;
  BVRef newBVR = NULL;
  BVRef prevBVR = NULL;

  struct DiskBVMap * map = NULL;
  int bvCount = 0;

  const char *val;
  char devsw[12];
  int len;

  /*
   * Traverse gDISKBVmap to get references for
   * individual bvr chains of each drive.
   */
  for (map = gDiskBVMap; map; map = map->next)
  {
    for (bvr = map->bvr; bvr; bvr = bvr->next)
    {
      /*
       * Save the last bvr.
       */
      if (newBVR) prevBVR = newBVR;

      /*
       * Allocate and copy the matched bvr entry into a new one.
       */
      newBVR = (BVRef) MALLOC(sizeof(*newBVR));
      bcopy(bvr, newBVR, sizeof(*newBVR));

      /*
       * Adjust the new bvr's fields.
       */
      newBVR->next = NULL;
      newBVR->filtered = true;

      if ( (!allowFlags || newBVR->flags & allowFlags)
          && (!denyFlags || !(newBVR->flags & denyFlags) )
          && (newBVR->biosdev >= minBIOSDev && newBVR->biosdev <= maxBIOSDev)
         )
        newBVR->visible = true;
      
      /*
       * Looking for "Hide Partition" entries in "hd(x,y) hd(n,m)" format
       * to be able to hide foreign partitions from the boot menu.
       */
			if ( (newBVR->flags & kBVFlagForeignBoot)
					&& getValueForKey(kHidePartition, &val, &len, &bootInfo->bootConfig)
				 )
    	{
    	  sprintf(devsw, "hd(%d,%d)", BIOS_DEV_UNIT(newBVR), newBVR->part_no);
    	  if (strstr(val, devsw) != NULL)
          newBVR->visible = false;
    	}

      /*
       * Use the first bvr entry as the starting chain pointer.
       */
      if (!chain)
        chain = newBVR;

      /*
       * Update the previous bvr's link pointer to use the new memory area.
       */
      if (prevBVR)
        prevBVR->next = newBVR;
        
      if (newBVR->visible)
        bvCount++;
    }
  }

#if DEBUG
  for (bvr = chain; bvr; bvr = bvr->next)
  {
    printf(" bvr: %d, dev: %d, part: %d, flags: %d, vis: %d\n", bvr, bvr->biosdev, bvr->part_no, bvr->flags, bvr->visible);
  }
  printf("count: %d\n", bvCount);
  getc();
#endif

  *count = bvCount;
  return chain;
}

int freeFilteredBVChain(const BVRef chain)
{
  int ret = 1;
  BVRef bvr = chain;
  BVRef nextBVR = NULL;
  
  while (bvr)
  {
    nextBVR = bvr->next;

    if (bvr->filtered)
    {
      free(bvr);
    }
    else
    {
      ret = 0;
      break;
    }

    bvr = nextBVR;
  }
  
  return ret;
}

//==========================================================================

static const struct NamedValue fdiskTypes[] =
{
    { FDISK_NTFS,   "Windows NTFS"   },
	{ FDISK_DOS12,  "Windows FAT12"  },
	{ FDISK_DOS16B, "Windows FAT16"  },
	{ FDISK_DOS16S, "Windows FAT16"  },
	{ FDISK_DOS16SLBA, "Windows FAT16"  },
	{ FDISK_SMALLFAT32,  "Windows FAT32"  },
	{ FDISK_FAT32,  "Windows FAT32"  },
    { FDISK_LINUX,  "Linux"          },
    { FDISK_UFS,    "Apple UFS"      },
    { FDISK_HFS,    "Apple HFS"      },
    { FDISK_BOOTER, "Apple Boot/UFS" },
    { 0xCD,         "CD-ROM"         },
    { 0x00,         0                }  /* must be last */
};

//==========================================================================

void getBootVolumeDescription( BVRef bvr, char * str, long strMaxLen, bool verbose )
{
    unsigned char type = (unsigned char) bvr->part_type;
    char *p;
	
    p = str;
    if (verbose) {
      sprintf( str, "hd(%d,%d) ", BIOS_DEV_UNIT(bvr), bvr->part_no);
      for (; strMaxLen > 0 && *p != '\0'; p++, strMaxLen--);
    }

    //
    // Get the volume label using filesystem specific functions
    // or use the alternate volume label if available.
    //
	  if (*bvr->altlabel == '\0')
	  {
      if (bvr->description)
        bvr->description(bvr, p, strMaxLen);
    }
    else
	    strncpy(p, bvr->altlabel, strMaxLen);

    if (*p == '\0') {
      const char * name = getNameForValue( fdiskTypes, type );
      if (name == NULL) {
          name = bvr->type_name;
      }
      if (name == NULL) {
          sprintf(p, "TYPE %02x", type);
      } else {
          strncpy(p, name, strMaxLen);
      }
    }

    // Set the devices label
    sprintf(bvr->label, p);
}

//==========================================================================

int readBootSector( int biosdev, unsigned int secno, void * buffer )
{
    struct disk_blk0 * bootSector = (struct disk_blk0 *) buffer;
    int                error;

    if ( bootSector == NULL )
    {
        if ( gBootSector == NULL )
        {
            gBootSector = (struct disk_blk0 *) MALLOC(sizeof(*gBootSector));
            if ( gBootSector == NULL ) return -1;
        }
        bootSector = gBootSector;
    }

    error = readBytes( biosdev, secno, 0, BPS, bootSector );
    if ( error || bootSector->signature != DISK_SIGNATURE )
        return -1;

    return 0;
}

/*
 * Format of boot1f32 block.
 */
 
#define BOOT1F32_MAGIC      "BOOT       "
#define BOOT1F32_MAGICLEN   11

struct disk_boot1f32_blk {
    unsigned char    init[3];
    unsigned char    fsheader[87];
    unsigned char    magic[BOOT1F32_MAGICLEN];
    unsigned char    bootcode[409];
    unsigned short   signature;
};

int testFAT32EFIBootSector( int biosdev, unsigned int secno, void * buffer )
{
    struct disk_boot1f32_blk * bootSector = (struct disk_boot1f32_blk *) buffer;
    int                error;

    if ( bootSector == NULL )
    {
        if ( gBootSector == NULL )
        {
            gBootSector = (struct disk_blk0 *) MALLOC(sizeof(*gBootSector));
            if ( gBootSector == NULL ) return -1;
        }
        bootSector = (struct disk_boot1f32_blk *) gBootSector;
    }

    error = readBytes( biosdev, secno, 0, BPS, bootSector );
    if ( error || bootSector->signature != DISK_SIGNATURE
         || strncmp((const char *)bootSector->magic, BOOT1F32_MAGIC, BOOT1F32_MAGICLEN) )
      return -1;

    return 0;
}

//==========================================================================
// Handle seek request from filesystem modules.

void diskSeek( BVRef bvr, long long position )
{
    bvr->fs_boff = position / BPS;
    bvr->fs_byteoff = position % BPS;
}

//==========================================================================
// Handle read request from filesystem modules.

int diskRead( BVRef bvr, long addr, long length )
{
    return readBytes( bvr->biosdev,
                      bvr->fs_boff + bvr->part_boff,
                      bvr->fs_byteoff,
                      length,
                      (void *) addr );
}

int rawDiskRead( BVRef bvr, unsigned int secno, void *buffer, unsigned int len )
{
    int secs;
    unsigned char *cbuf = (unsigned char *)buffer;
    unsigned int copy_len;
    int rc;

    if ((len & (BPS-1)) != 0) {
        error("raw disk read not sector aligned");
        return -1;
    }
    secno += bvr->part_boff;

    cache_valid = false;

    while (len > 0) {
        secs = len / BPS;
        if (secs > N_CACHE_SECS) secs = N_CACHE_SECS;
        copy_len = secs * BPS;

        //printf("rdr: ebiosread(%d, %d, %d)\n", bvr->biosdev, secno, secs);
        if ((rc = ebiosread(bvr->biosdev, secno, secs)) != 0) {
            /* Ignore corrected ECC errors */
            if (rc != ECC_CORRECTED_ERR) {
                error("  EBIOS read error: %s\n", bios_error(rc), rc);
                error("    Block %d Sectors %d\n", secno, secs);
                return rc;
            }
        }
        bcopy( trackbuf, cbuf, copy_len );
        len -= copy_len;
        cbuf += copy_len;
        secno += secs;
        spinActivityIndicator(secs);
    }

    return 0;
}

int rawDiskWrite( BVRef bvr, unsigned int secno, void *buffer, unsigned int len )
{
    int secs;
    unsigned char *cbuf = (unsigned char *)buffer;
    unsigned int copy_len;
    int rc;

    if ((len & (BPS-1)) != 0) {
        error("raw disk write not sector aligned");
        return -1;
    }
    secno += bvr->part_boff;

    cache_valid = false;

    while (len > 0) {
        secs = len / BPS;
        if (secs > N_CACHE_SECS) secs = N_CACHE_SECS;
        copy_len = secs * BPS;

        bcopy( cbuf, trackbuf, copy_len );
        //printf("rdr: ebioswrite(%d, %d, %d)\n", bvr->biosdev, secno, secs);
        if ((rc = ebioswrite(bvr->biosdev, secno, secs)) != 0) {
            error("  EBIOS write error: %s\n", bios_error(rc), rc);
            error("    Block %d Sectors %d\n", secno, secs);
            return rc;
        }
        len -= copy_len;
        cbuf += copy_len;
        secno += secs;
        spinActivityIndicator(secs);
    }

    return 0;
}


int diskIsCDROM(BVRef bvr)
{
    struct driveInfo          di;

    if (getDriveInfo(bvr->biosdev, &di) == 0 && di.no_emulation) {
	return 1;
    }
    return 0;
}

int biosDevIsCDROM(int biosdev)
{
    struct driveInfo          di;

    if (getDriveInfo(biosdev, &di) == 0 && di.no_emulation)
    {
    	return 1;
    }
    return 0;
}
