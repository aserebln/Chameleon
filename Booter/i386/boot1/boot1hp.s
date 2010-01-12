; Copyright (c) 1999-2003 Apple Computer, Inc. All rights reserved.
;
; @APPLE_LICENSE_HEADER_START@
; 
; Portions Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights
; Reserved.  This file contains Original Code and/or Modifications of
; Original Code as defined in and that are subject to the Apple Public
; Source License Version 2.0 (the "License").  You may not use this file
; except in compliance with the License.  Please obtain a copy of the
; License at http://www.apple.com/publicsource and read it before using
; this file.
; 
; The Original Code and all software distributed under the License are
; distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
; EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
; INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE OR NON- INFRINGEMENT.  Please see the
; License for the specific language governing rights and limitations
; under the License.
; 
; @APPLE_LICENSE_HEADER_END@
;
; Partition Boot Preloader: boot1hp
;
; This program is designed to reside in sector 0 of an HFS+ partition.
; It expects that the MBR has left the drive number in DL
; and a pointer to the partition entry in SI.
; 
; This version requires a BIOS with EBIOS (LBA) support.
;
; This code is written for the NASM assembler.
;   nasm boot1hp.s -o boot1hp

;
; This version of boot1hp tries to find a boot1h extended loader.
; If it fails then uses the traditional method by loading the startupfile.
;
; Written by zef @ 2008-04-17
;

;
; Set to 1 to enable obscure debug messages.
;
DEBUG				EQU		0

;
; Set to 1 to enable unused code.
;
UNUSED				EQU		0

;
; Set to 1 to enable verbose mode.
;
VERBOSE				EQU		1

;
; Various constants.
;
NULL		   		EQU		0
CR					EQU		0x0D
LF					EQU		0x0A

maxSectorCount		EQU		64									; maximum sector count for read_sectors

kSectorBytes		EQU		512									; sector size in bytes
kBootSignature		EQU		0xAA55								; boot sector signature

kBoot1StackAddress	EQU		0xFFF0								; boot1 stack pointer
kBoot1LoadAddr		EQU		0x7C00								; boot1 load address
kBoot1RelocAddr		EQU		0xE000								; boot1 relocated address
kHFSPlusBuffer		EQU		kBoot1RelocAddr + kSectorBytes		; HFS+ Volume Header address

kBoot1ExtSector		EQU		40
kBoot1ExtSignature	EQU		'b1he'

kBoot2Sectors		EQU		126									; sectors to load for boot2
kBoot2Segment		EQU		0x2000								; boot2 load segment
kBoot2Address		EQU		kSectorBytes						; boot2 load address

;
; Format of fdisk partition entry.
;
; The symbol 'part_size' is automatically defined as an `EQU'
; giving the size of the structure.
;
			struc part
.bootid		resb 1		; bootable or not 
.head		resb 1		; starting head, sector, cylinder
.sect		resb 1		;
.cyl		resb 1		;
.type		resb 1		; partition type
.endhead	resb 1		; ending head, sector, cylinder
.endsect	resb 1		;
.endcyl		resb 1		;
.lba		resd 1		; starting lba
.sectors	resd 1		; size in sectors
			endstruc

;-------------------------------------------------------------------------
; HFS+ related structures and constants
;
kHFSPlusSignature		EQU		'H+'		; HFS+ volume signature
kHFSPlusCaseSignature	EQU		'HX'		; HFS+ volume case-sensitive signature
kHFSPlusCaseSigX		EQU		'X'			; upper byte of HFS+ volume case-sensitive signature
kHFSPlusExtentDensity	EQU		8			; 8 extent descriptors / extent record

;
; HFSPlusExtentDescriptor
;
					struc	HFSPlusExtentDescriptor
.startBlock			resd	1
.blockCount			resd	1
					endstruc

;
; HFSPlusForkData
;
					struc	HFSPlusForkData
.logicalSize		resq	1
.clumpSize			resd	1
.totalBlocks		resd	1
.extents			resb	kHFSPlusExtentDensity * HFSPlusExtentDescriptor_size
					endstruc

;
; HFSPlusVolumeHeader
;
					struc	HFSPlusVolumeHeader
.signature			resw	1
.version			resw	1
.attributes			resd	1
.lastMountedVersion resd	1
.journalInfoBlock	resd	1
.createDate			resd	1
.modifyDate			resd	1
.backupDate			resd	1
.checkedDate		resd	1
.fileCount			resd	1
.folderCount		resd	1
.blockSize			resd	1
.totalBlocks		resd	1
.freeBlocks			resd	1
.nextAllocation		resd	1
.rsrcClumpSize		resd	1
.dataClumpSize		resd	1
.nextCatalogID		resd	1
.writeCount			resd	1
.encodingsBitmap	resq	1
.finderInfo			resd	8
.allocationFile		resb	HFSPlusForkData_size
.extentsFile		resb	HFSPlusForkData_size
.catalogFile		resb	HFSPlusForkData_size
.attributesFile		resb	HFSPlusForkData_size
.startupFile		resb	HFSPlusForkData_size
					endstruc

;
; Macros.
;
%macro jmpabs 1
	push	WORD %1
	ret
%endmacro

%macro DebugCharMacro 1
	pushad
	mov		al, %1
	call	print_char
	call	getc
	popad
%endmacro

%macro PrintCharMacro 1
	pushad
	mov		al, %1
	call	print_char
	popad
%endmacro

%macro PutCharMacro 1
	call	print_char
%endmacro

%macro PrintHexMacro 1
	call	print_hex
%endmacro

%macro PrintString 1
	mov		si, %1
	call	print_string
%endmacro
        
%macro LogString 1
	mov		di, %1
	call	log_string
%endmacro

%if DEBUG
  %define DebugChar(x) DebugCharMacro x
  %define PrintChar(x) PrintCharMacro x
  %define PutChar(x) PutCharMacro
  %define PrintHex(x) PrintHexMacro x
%else
  %define DebugChar(x)
  %define PrintChar(x)
  %define PutChar(x)
  %define PrintHex(x)
%endif
	
;--------------------------------------------------------------------------
; Start of text segment.

    SEGMENT .text

	ORG		kBoot1RelocAddr

;--------------------------------------------------------------------------
; Boot code is loaded at 0:7C00h.
;
start:
    ;
    ; Set up the stack to grow down from kBoot1StackSegment:kBoot1StackAddress.
    ; Interrupts should be off while the stack is being manipulated.
    ;
    cli                             ; interrupts off
    xor		ax, ax                  ; zero ax
    mov		ss, ax                  ; ss <- 0
    mov     sp, kBoot1StackAddress  ; sp <- top of stack
    sti                             ; reenable interrupts

    mov     ds, ax                  ; ds <- 0
    mov     es, ax                  ; es <- 0

    ;
    ; Relocate boot1 code.
    ;
    push	si
    mov		si, kBoot1LoadAddr		; si <- source
    mov		di, kBoot1RelocAddr		; di <- destination
    cld								; auto-increment SI and/or DI registers
    mov		cx, kSectorBytes		; copy 256 words
    rep		movsb					; repeat string move (word) operation
    pop		si
    
    ;
    ; Code relocated, jump to startReloc in relocated location.
    ;
	; FIXME: Is there any way to instruct NASM to compile a near jump
	; using absolute address instead of relative displacement?
	;
	jmpabs	startReloc

;--------------------------------------------------------------------------
; Start execution from the relocated location.
;
startReloc:

    ;
    ; Initializing global variables.
    ;
    mov     eax, [si + part.lba]
    mov     [gPartLBA], eax					; save the current partition LBA offset
    mov     [gBIOSDriveNumber], dl			; save BIOS drive number

    ;
    ; Loading HFS+ Volume Header.
    ;
    mov	    ecx, 2							; sector 2 of current partition
    mov     al, 1							; read HFS+ Volume Header
    mov     edx, kHFSPlusBuffer
    call    readLBA
	jc		NEAR bios_read_error

	;
	; Looking for HFSPlus ('H+') or HFSPlus case-sensitive ('HX') signature.
	;
	mov		ax, [kHFSPlusBuffer + HFSPlusVolumeHeader.signature]
	cmp		ax, kHFSPlusCaseSignature
	je		.foundHFSPlus
    cmp     ax, kHFSPlusSignature
    jne     NEAR hfsp_error

.foundHFSPlus:
    ;
    ; Loading first sector of boot1h extended code.
    ;
	mov		eax, [gPartLBA]					; save starting LBA of current partition
	push	eax
	xor		eax, eax
	mov		[gPartLBA], eax					; will be read sectors from the beginning of the disk
    mov	    ecx, kBoot1ExtSector			; sector 1 of boot1h extended code
    mov     al, 1							; read HFS+ Volume Header
    mov     edx, kBoot1LoadAddr
    call    readLBA
	jc		bios_read_error
	pop		eax
	mov		[gPartLBA], eax					; restore starting LBA of current partition

	;
	; Looking for boot1h extended code signature.
	;
    cmp     WORD [kBoot1LoadAddr + kSectorBytes - 2], kBootSignature
	jne		boot1h_ext_not_found
    cmp     DWORD [kBoot1LoadAddr + kSectorBytes - 6], kBoot1ExtSignature
	jne		boot1h_ext_not_found

%if VERBOSE
    LogString(boot1he_start_str)
%endif

    mov     dl, [gBIOSDriveNumber]			; load BIOS drive number
    jmpabs	kBoot1LoadAddr

boot1h_ext_not_found:

%if VERBOSE
    LogString(boot1he_error_str)
%endif

    ;
    ; Initializing more global variables.
    ;
	mov		eax, [kHFSPlusBuffer + HFSPlusVolumeHeader.blockSize]
	bswap	eax								; convert to little-endian
	shr		eax, 9							; convert to sector unit
	mov		[gBlockSize], eax				; save blockSize as little-endian sector unit!

;--------------------------------------------------------------------------
; findStartup - Find HFS+ startup file in a partition.
;
findStartup:
	mov     edx, [kHFSPlusBuffer + HFSPlusVolumeHeader.startupFile + HFSPlusForkData.extents]
	call	blockToSector					; result in ECX
	or		ecx, ecx
	je		startupfile_error

    mov     al, kBoot2Sectors
    mov     edx, (kBoot2Segment << 4) + kBoot2Address
    call    readLBA
	jc		bios_read_error

%if VERBOSE
    LogString(startupfile_str)
%endif

    ;
    ; Jump to boot2.
    ;
boot2:

%if DEBUG
	DebugChar ('!')
%endif

	call	getc

    mov     dl, [gBIOSDriveNumber]			; load BIOS drive number
    jmp     kBoot2Segment:kBoot2Address

bios_read_error:

%if VERBOSE
    LogString(bios_error_str)
%endif

	jmp		hang

startupfile_error:

%if VERBOSE
    LogString(startupfile_err_str)
%endif

	jmp		hang

hfsp_error:

%if VERBOSE
    LogString(hfsp_error_str)
%endif

hang:
    hlt
    jmp     hang

%if UNUSED

;--------------------------------------------------------------------------
; readSectors - Reads more than 127 sectors using LBA addressing.
;
; Arguments:
;   AX = number of 512-byte sectors to read (valid from 1-1280).
;   EDX = pointer to where the sectors should be stored.
;   ECX = sector offset in partition 
;
; Returns:
;   CF = 0  success
;        1 error
;
readSectors:
	pushad
	mov		bx, ax

.loop:
	xor		eax, eax						; EAX = 0
	mov		al, bl							; assume we reached the last block.
	cmp		bx, maxSectorCount				; check if we really reached the last block
	jb		.readBlock						; yes, BX < MaxSectorCount
	mov		al, maxSectorCount				; no, read MaxSectorCount

.readBlock:
	call	readLBA
	jc		bios_read_error
	sub		bx, ax							; decrease remaning sectors with the read amount
	jz		.exit							; exit if no more sectors left to be loaded
	add		ecx, eax						; adjust LBA sector offset
	shl		ax, 9							; convert sectors to bytes
	add		edx, eax						; adjust target memory location
	jmp		.loop							; read remaining sectors

.exit:
	popad
	ret

%endif ; UNUSED

;--------------------------------------------------------------------------
; readLBA - Read sectors from a partition using LBA addressing.
;
; Arguments:
;   AL = number of 512-byte sectors to read (valid from 1-127).
;   EDX = pointer to where the sectors should be stored.
;   ECX = sector offset in partition 
;   [bios_drive_number] = drive number (0x80 + unit number)
;
; Returns:
;   CF = 0  success
;        1 error
;
readLBA:
    pushad                          		; save all registers
    push    es								; save ES
    mov     bp, sp                 			; save current SP

    ;
    ; Convert EDX to segment:offset model and set ES:BX
    ;
    ; Some BIOSes do not like offset to be negative while reading
    ; from hard drives. This usually leads to "boot1: error" when trying
    ; to boot from hard drive, while booting normally from USB flash.
    ; The routines, responsible for this are apparently different.
    ; Thus we split linear address slightly differently for these
    ; capricious BIOSes to make sure offset is always positive.
    ;

	mov		bx, dx							; save offset to BX
	and		bh, 0x0f						; keep low 12 bits
	shr		edx, 4							; adjust linear address to segment base
	xor		dl, dl							; mask low 8 bits
	mov		es, dx							; save segment to ES

    ;
    ; Create the Disk Address Packet structure for the
    ; INT13/F42 (Extended Read Sectors) on the stack.
    ;

    ; push    DWORD 0              			; offset 12, upper 32-bit LBA
    push    ds                      		; For sake of saving memory,
    push    ds                      		; push DS register, which is 0.

    add     ecx, [gPartLBA]         		; offset 8, lower 32-bit LBA
    push    ecx

    push    es                      		; offset 6, memory segment

    push    bx                      		; offset 4, memory offset

    xor     ah, ah             				; offset 3, must be 0
    push    ax                      		; offset 2, number of sectors

    push    WORD 16                 		; offset 0-1, packet size

    ;
    ; INT13 Func 42 - Extended Read Sectors
    ;
    ; Arguments:
    ;   AH    = 0x42
    ;   [bios_drive_number] = drive number (0x80 + unit number)
    ;   DS:SI = pointer to Disk Address Packet
    ;
    ; Returns:
    ;   AH    = return status (sucess is 0)
    ;   carry = 0 success
    ;           1 error
    ;
    ; Packet offset 2 indicates the number of sectors read
    ; successfully.
    ;
	mov     dl, [gBIOSDriveNumber]			; load BIOS drive number
	mov     si, sp
	mov     ah, 0x42
	int     0x13
	jnc		.exit

    ;
    ; Issue a disk reset on error.
    ; Should this be changed to Func 0xD to skip the diskette controller
    ; reset?
    ;
	xor     ax, ax                  		; Func 0
	int     0x13                    		; INT 13
	stc                             		; set carry to indicate error

.exit:
    mov     sp, bp                  		; restore SP
    pop     es								; restore ES
    popad
    ret

%if VERBOSE

;--------------------------------------------------------------------------
; Write a string with 'boot1: ' prefix to the console.
;
; Arguments:
;   ES:DI   pointer to a NULL terminated string.
;
; Clobber list:
;   DI
;
log_string:
    pushad

    push	di
    mov		si, log_title_str
    call	print_string

    pop		si
    call	print_string

    popad
    
    ret

;-------------------------------------------------------------------------
; Write a string to the console.
;
; Arguments:
;   DS:SI   pointer to a NULL terminated string.
;
; Clobber list:
;   AX, BX, SI
;
print_string:
    mov     bx, 1                   		; BH=0, BL=1 (blue)

.loop:
    lodsb                           		; load a byte from DS:SI into AL
    cmp     al, 0               			; Is it a NULL?
    je      .exit                   		; yes, all done
    mov     ah, 0xE                 		; INT10 Func 0xE
    int     0x10                    		; display byte in tty mode
    jmp     .loop

.exit:
    ret

%endif ; VERBOSE

%if DEBUG

;--------------------------------------------------------------------------
; Write the 4-byte value to the console in hex.
;
; Arguments:
;   EAX = Value to be displayed in hex.
;
print_hex:
    pushad
    mov     cx, WORD 4
    bswap   eax
.loop:
    push    ax
    ror     al, 4
    call    print_nibble            		; display upper nibble
    pop     ax
    call    print_nibble            		; display lower nibble
    ror     eax, 8
    loop    .loop

%if UNUSED
	mov     al, 10							; carriage return
	call    print_char
	mov     al, 13
	call    print_char
%endif ; UNUSED

    popad
    ret
	
print_nibble:
    and     al, 0x0f
    add     al, '0'
    cmp     al, '9'
    jna     .print_ascii
    add     al, 'A' - '9' - 1
.print_ascii:
    call    print_char
    ret

;--------------------------------------------------------------------------
; Write a ASCII character to the console.
;
; Arguments:
;   AL = ASCII character.
;
print_char:
    pushad
    mov     bx, 1                   		; BH=0, BL=1 (blue)
    mov     ah, 0x0e                		; bios INT 10, Function 0xE
    int     0x10                    		; display byte in tty mode
    popad
    ret

%endif ; DEBUG

;--------------------------------------------------------------------------
; getc - wait for a key press
;
getc:
    pushad
    mov     ah, 0
    int		0x16
    popad
    ret

;--------------------------------------------------------------------------
; Convert big-endian HFSPlus allocation block to sector unit
;
; Arguments:
;   EDX = allocation block
;
; Returns:
;   ECX = allocation block converted to sector unit
;
; Clobber list:
;   EDX
;
blockToSector:
	push	eax
	mov		eax, [gBlockSize]
	bswap	edx								; convert allocation block to little-endian
	mul		edx				 				; multiply with block number
	mov		ecx, eax						; result in EAX
	pop		eax
	ret

;--------------------------------------------------------------------------
; Static data.
;

%if VERBOSE
log_title_str		db		CR, LF, 'boot1: ', NULL
boot1he_error_str	db		'extended block signature not found', NULL
startupfile_err_str	db		'startupfile not found', NULL
hfsp_error_str		db		'HFS+ signature error', NULL
bios_error_str		db		'BIOS int 13h error', NULL
startupfile_str		db		'loading startupfile', NULL
boot1he_start_str	db		'starting extended block', NULL
%endif ; VERBOSE

;--------------------------------------------------------------------------
; Pad the rest of the 512 byte sized sector with zeroes. The last
; two bytes is the mandatory boot sector signature.
;
; If the booter code becomes too large, then nasm will complain
; that the 'times' argument is negative.

pad_table_and_sig:
	times			510-($-$$) db 0
	dw				kBootSignature

;
; Global variables
;

	ABSOLUTE		kHFSPlusBuffer + HFSPlusVolumeHeader_size

gPartLBA			resd	1
gBIOSDriveNumber	resw	1
gBlockSize			resd	1

; END
