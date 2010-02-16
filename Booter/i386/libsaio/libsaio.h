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
/* libsaio.h */

#ifndef __LIBSAIO_LIBSAIO_H
#define __LIBSAIO_LIBSAIO_H

#include "libsa.h"
#include "memory.h"
#include "io_inline.h"
#include "saio_types.h"
#include "saio_internal.h"

static inline uint16_t swap16(uint16_t x)
{
	return (((x & 0x00FF) << 8) | ((x & 0xFF00) >> 8));
}

static inline uint32_t swap32(uint32_t x)
{
	return ((x & 0x000000FF) << 24) | ((x & 0x0000FF00) << 8 ) | ((x & 0x00FF0000) >> 8 ) | ((x & 0xFF000000) >> 24);
}	

#endif /* !__LIBSAIO_LIBSAIO_H */
