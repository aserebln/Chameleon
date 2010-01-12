/*
 * Copyright 2010 AsereBLN. All rights reserved. <aserebln@googlemail.com>
 *
 * mem.c - obtain system memory information
 */

#include "libsaio.h"
#include "pci.h"
#include "platform.h"
#include "cpu.h"
#include "mem.h"

#ifndef DEBUG_MEM
#define DEBUG_MEM 0
#endif

#if DEBUG_MEM
#define DBG(x...)		printf(x)
#else
#define DBG(x...)
#endif

void scan_memory(PlatformInfo_t *p)
{
	/* NYI */
}
