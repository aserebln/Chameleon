/*
 * Copyright 2010 AsereBLN. All rights reserved. <aserebln@googlemail.com>
 *
 * spd.c - obtain serial presene detect memory information
 */

#include "libsaio.h"
#include "pci.h"
#include "platform.h"
#include "spd.h"

#ifndef DEBUG_SPD
#define DEBUG_SPD 0
#endif

#if DEBUG_SPD
#define DBG(x...)	printf(x)
#else
#define DBG(x...)
#endif

void scan_spd(PlatformInfo_t *p)
{
	/* NYI */
}
