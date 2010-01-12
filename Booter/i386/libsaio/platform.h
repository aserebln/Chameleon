/*
 *  platform.h
 *
 */

#ifndef __LIBSAIO_PLATFORM_H
#define __LIBSAIO_PLATFORM_H

#include "libsaio.h"

extern bool platformCPUFeature(uint32_t);
extern void scan_platform(void);

/* CPUID index into cpuid_raw */
#define CPUID_0				0
#define CPUID_1				1
#define CPUID_2				2
#define CPUID_3				3
#define CPUID_4				4
#define CPUID_80			5
#define CPUID_81			6
#define CPUID_MAX			7

/* CPU Features */
#define CPU_FEATURE_MMX			0x00000001		// MMX Instruction Set
#define CPU_FEATURE_SSE			0x00000002		// SSE Instruction Set
#define CPU_FEATURE_SSE2		0x00000004		// SSE2 Instruction Set
#define CPU_FEATURE_SSE3		0x00000008		// SSE3 Instruction Set
#define CPU_FEATURE_SSE41		0x00000010		// SSE41 Instruction Set
#define CPU_FEATURE_SSE42		0x00000020		// SSE42 Instruction Set
#define CPU_FEATURE_EM64T		0x00000040		// 64Bit Support
#define CPU_FEATURE_HTT			0x00000080		// HyperThreading
#define CPU_FEATURE_MOBILE		0x00000100		// Mobile CPU

/* SMBIOS Memory Types */ 
#define SMB_MEM_TYPE_UNDEFINED		0
#define SMB_MEM_TYPE_OTHER		1
#define SMB_MEM_TYPE_UNKNOWN		2
#define SMB_MEM_TYPE_DRAM		3
#define SMB_MEM_TYPE_EDRAM		4
#define SMB_MEM_TYPE_VRAM		5
#define SMB_MEM_TYPE_SRAM		6
#define SMB_MEM_TYPE_RAM		7
#define SMB_MEM_TYPE_ROM		8
#define SMB_MEM_TYPE_FLASH		9
#define SMB_MEM_TYPE_EEPROM		10
#define SMB_MEM_TYPE_FEPROM		11
#define SMB_MEM_TYPE_EPROM		12
#define SMB_MEM_TYPE_CDRAM		13
#define SMB_MEM_TYPE_3DRAM		14
#define SMB_MEM_TYPE_SDRAM		15
#define SMB_MEM_TYPE_SGRAM		16
#define SMB_MEM_TYPE_RDRAM		17
#define SMB_MEM_TYPE_DDR		18
#define SMB_MEM_TYPE_DDR2		19
#define SMB_MEM_TYPE_FBDIMM		20
#define SMB_MEM_TYPE_DDR3		24			// Supported in 10.5.6+ AppleSMBIOS

/* Memory Configuration Types */ 
#define SMB_MEM_CHANNEL_UNKNOWN		0
#define SMB_MEM_CHANNEL_SINGLE		1
#define SMB_MEM_CHANNEL_DUAL		2
#define SMB_MEM_CHANNEL_TRIPLE		3

/* Maximum number of ram slots */
#define MAX_RAM_SLOTS			8

/* Maximum number of SPD bytes */
#define MAX_SPD_SIZE			256

typedef struct _RamSlotInfo_t {
	bool		InUse;
	uint8_t		Type;
	char		Vendor[64];
	char		PartNo[64];
	char		SerialNo[16];
	uint8_t		spd[MAX_SPD_SIZE];
} RamSlotInfo_t;

typedef struct _PlatformInfo_t {
	struct CPU {
		uint32_t		Features;		// CPU Features like MMX, SSE2, VT, MobileCPU
		uint32_t		Vendor;			// Vendor
		uint32_t		Model;			// Model
		uint32_t		ExtModel;		// Extended Model
		uint32_t		Family;			// Family
		uint32_t		ExtFamily;		// Extended Family
		uint32_t		NoCores;		// No Cores per Package
		uint32_t		NoThreads;		// Threads per Package
		uint8_t			MaxCoef;		// Max Multiplier
		uint8_t			MaxDiv;
		uint8_t			CurrCoef;		// Current Multiplier
		uint8_t			CurrDiv;
		uint64_t		TSCFrequency;		// TSC Frequency Hz
		uint64_t		FSBFrequency;		// FSB Frequency Hz
		uint64_t		CPUFrequency;		// CPU Frequency Hz
		uint32_t		BrandString[16];	// 48 Byte Branding String
		uint32_t		CPUID[CPUID_MAX][4];	// CPUID 0..4, 80..81 Raw Values
	} CPU;
	struct RAM {
		RamSlotInfo_t		DIMM[MAX_RAM_SLOTS];	// Information about each slot
		uint64_t		Frequency;		// Ram Frequency
		//uint8_t			Type;			// Standard SMBIOS v2.5 Memory Type
	} RAM;
} PlatformInfo_t;

extern PlatformInfo_t Platform;

#endif /* !__LIBSAIO_PLATFORM_H */
