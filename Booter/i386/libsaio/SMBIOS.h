/*
 * Copyright (c) 1998-2006 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#ifndef _LIBSAIO_SMBIOS_H
#define _LIBSAIO_SMBIOS_H

/*
 * Based on System Management BIOS Reference Specification v2.5
 */

typedef uint8_t		SMBString;
typedef uint8_t		SMBByte;
typedef uint16_t	SMBWord;
typedef uint32_t	SMBDWord;
typedef uint64_t	SMBQWord;

struct DMIHeader {
	SMBByte			type;
	SMBByte			length;
	SMBWord			handle;
} __attribute__((packed));

struct DMIEntryPoint {
	SMBByte			anchor[5];
	SMBByte			checksum;
	SMBWord			tableLength;
	SMBDWord		tableAddress;
	SMBWord			structureCount;
	SMBByte			bcdRevision;
} __attribute__((packed));

struct SMBEntryPoint {
	SMBByte			anchor[4];
	SMBByte			checksum;
	SMBByte			entryPointLength;
	SMBByte			majorVersion;
	SMBByte			minorVersion;
	SMBWord			maxStructureSize;
	SMBByte			entryPointRevision;
	SMBByte			formattedArea[5];
	struct DMIEntryPoint	dmi;
} __attribute__((packed));

struct DMIMemoryControllerInfo {/* 3.3.6 Memory Controller Information (Type 5) */
	struct DMIHeader	dmiHeader;
	SMBByte			errorDetectingMethod;
	SMBByte			errorCorrectingCapability;
	SMBByte			supportedInterleave;
	SMBByte			currentInterleave;
	SMBByte			maxMemoryModuleSize;
	SMBWord			supportedSpeeds;
	SMBWord			supportedMemoryTypes;
	SMBByte			memoryModuleVoltage;
	SMBByte			numberOfMemorySlots;
} __attribute__((packed));

struct DMIMemoryModuleInfo {	/* 3.3.7 Memory Module Information (Type 6) */
	struct DMIHeader	dmiHeader;
	SMBByte			socketDesignation;
	SMBByte			bankConnections;
	SMBByte			currentSpeed;
	SMBWord			currentMemoryType;
	SMBByte			installedSize;
	SMBByte			enabledSize;
	SMBByte			errorStatus;
} __attribute__((packed));

struct DMIPhysicalMemoryArray {	/* 3.3.17 Physical Memory Array (Type 16) */
	struct DMIHeader	dmiHeader;
	SMBByte			location;
	SMBByte			use;
	SMBByte			memoryCorrectionError;
	SMBDWord		maximumCapacity;
	SMBWord			memoryErrorInformationHandle;
	SMBWord			numberOfMemoryDevices;
} __attribute__((packed));

struct DMIMemoryDevice {	/* 3.3.18 Memory Device (Type 17) */
	struct DMIHeader	dmiHeader;
	SMBWord			physicalMemoryArrayHandle;
	SMBWord			memoryErrorInformationHandle;
	SMBWord			totalWidth;
	SMBWord			dataWidth;
	SMBWord			size;
	SMBByte			formFactor;
	SMBByte			deviceSet;
	SMBByte			deviceLocator;
	SMBByte			bankLocator;
	SMBByte			memoryType;
	SMBWord			typeDetail;
} __attribute__((packed));

#endif /* !_LIBSAIO_SMBIOS_H */
