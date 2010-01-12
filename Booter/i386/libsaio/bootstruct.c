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
 * Copyright 1993 NeXT, Inc.
 * All rights reserved.
 */

#include "libsaio.h"
#include "bootstruct.h"

/*==========================================================================
 * Initialize the structure of parameters passed to
 * the kernel by the booter.
 */

boot_args         *bootArgs;
PrivateBootInfo_t *bootInfo;
Node              *gMemoryMapNode;

static char platformName[64];

void initKernBootStruct( void )
{
    Node *node;
    int nameLen;
    static int init_done = 0;

    if ( !init_done )
    {
        bootArgs = (boot_args *)MALLOC(sizeof(boot_args));
        bootInfo = (PrivateBootInfo_t *)MALLOC(sizeof(PrivateBootInfo_t));
        if (bootArgs == 0 || bootInfo == 0)
            stop("Couldn't allocate boot info\n");

        bzero(bootArgs, sizeof(boot_args));
        bzero(bootInfo, sizeof(PrivateBootInfo_t));

        // Get system memory map. Also update the size of the
        // conventional/extended memory for backwards compatibility.

        bootInfo->memoryMapCount =
            getMemoryMap( bootInfo->memoryMap, kMemoryMapCountMax,
                          (unsigned long *) &bootInfo->convmem,
                          (unsigned long *) &bootInfo->extmem );

        if ( bootInfo->memoryMapCount == 0 )
        {
            // BIOS did not provide a memory map, systems with
            // discontiguous memory or unusual memory hole locations
            // may have problems.

            bootInfo->convmem = getConventionalMemorySize();
            bootInfo->extmem  = getExtendedMemorySize();
        }

        bootInfo->configEnd    = bootInfo->config;
        bootArgs->Video.v_display = VGA_TEXT_MODE;
        
        DT__Initialize();

        node = DT__FindNode("/", true);
        if (node == 0) {
            stop("Couldn't create root node");
        }
        getPlatformName(platformName);
        nameLen = strlen(platformName) + 1;
        DT__AddProperty(node, "compatible", nameLen, platformName);
        DT__AddProperty(node, "model", nameLen, platformName);

        gMemoryMapNode = DT__FindNode("/chosen/memory-map", true);

        bootArgs->Version  = kBootArgsVersion;
        bootArgs->Revision = 5;

        init_done = 1;
    }

}


/* Copy boot args after kernel and record address. */

void
reserveKernBootStruct(void)
{
    void *oldAddr = bootArgs;
    bootArgs = (boot_args *)AllocateKernelMemory(sizeof(boot_args));
    bcopy(oldAddr, bootArgs, sizeof(boot_args));
}

void
finalizeBootStruct(void)
{
    uint32_t size;
    void *addr;
    int i;
    EfiMemoryRange *memoryMap;
    MemoryRange *range;
    int memoryMapCount = bootInfo->memoryMapCount;

    if (memoryMapCount == 0) {
        // XXX could make a two-part map here
        stop("Unable to convert memory map into proper format\n");
    }

    // convert memory map to boot_args memory map
    memoryMap = (EfiMemoryRange *)AllocateKernelMemory(sizeof(EfiMemoryRange) * memoryMapCount);
    bootArgs->MemoryMap = (uint32_t)memoryMap;
    bootArgs->MemoryMapSize = sizeof(EfiMemoryRange) * memoryMapCount;
    bootArgs->MemoryMapDescriptorSize = sizeof(EfiMemoryRange);
    bootArgs->MemoryMapDescriptorVersion = 0;

    for (i=0; i<memoryMapCount; i++, memoryMap++) {
        range = &bootInfo->memoryMap[i];
        switch(range->type) {
			case kMemoryRangeACPI:
				memoryMap->Type = kEfiACPIReclaimMemory;
				break;
			case kMemoryRangeNVS:
				memoryMap->Type = kEfiACPIMemoryNVS;
				break;
			case kMemoryRangeUsable:
				memoryMap->Type = kEfiConventionalMemory;
				break;
			case kMemoryRangeReserved:
			default:
				memoryMap->Type = kEfiReservedMemoryType;
				break;
        }
        memoryMap->PhysicalStart = range->base;
        memoryMap->VirtualStart = range->base;
        memoryMap->NumberOfPages = range->length >> I386_PGSHIFT;
        memoryMap->Attribute = 0;
    }
    
    // copy bootFile into device tree
    // XXX

    // add PCI info somehow into device tree
    // XXX

    // Flatten device tree
    DT__FlattenDeviceTree(0, &size);
    addr = (void *)AllocateKernelMemory(size);
    if (addr == 0) {
        stop("Couldn't allocate device tree\n");
    }
    
    DT__FlattenDeviceTree((void **)&addr, &size);
    bootArgs->deviceTreeP = (uint32_t)addr;
    bootArgs->deviceTreeLength = size;
}
