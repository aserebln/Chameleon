/*
 * Copyright (c) 1999 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Portions Copyright (c) 1999 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 1.1 (the "License").  You may not use this file
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
 * boot2() -- second stage boot.
 *
 * This function must be located at 0:BOOTER_ADDR and will be called
 * by boot1 or by NBP.
 */

#include <architecture/i386/asm_help.h>
#include "memory.h"
#include "mboot.h"

#define data32  .byte 0x66
#define retf    .byte 0xcb

    .file "boot2.s"
    .section __INIT,__text	// turbo - This initialization code must reside within the first segment

    //.data
    .section __INIT,__data	// turbo - Data that must be in the first segment

EXPORT(_chainbootdev)  .byte 0x80
EXPORT(_chainbootflag) .byte 0x00

    //.text
    .section __INIT,__text	// turbo

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Booter entry point. Called by boot1 or by the NBP.
# This routine must be the first in the TEXT segment.
#
# Arguments:
#   DX    = Boot device
#
# Returns:
#
LABEL(boot2)
    pushl   %ecx                # Save general purpose registers
    pushl   %ebx
    pushl   %ebp
    pushl   %esi
    pushl   %edi
    push    %ds                 # Save DS, ES
    push    %es

    mov     %cs, %ax            # Update segment registers.
    mov     %ax, %ds            # Set DS and ES to match CS
    mov     %ax, %es

    data32
    call    __switch_stack      # Switch to new stack

    data32
    call    __real_to_prot      # Enter protected mode.

    fninit                      # FPU init

    # We are now in 32-bit protected mode.
    # Transfer execution to C by calling boot().

    pushl   %edx                # bootdev
    call    _boot

    testb   $0xff, _chainbootflag
    jnz     start_chain_boot    # Jump to a foreign booter

    call    __prot_to_real      # Back to real mode.

    data32
    call    __switch_stack      # Restore original stack
    
    pop     %es                 # Restore original ES and DS
    pop     %ds
    popl    %edi                # Restore all general purpose registers
    popl    %esi                # except EAX.
    popl    %ebp
    popl    %ebx
    popl    %ecx

    retf                        # Hardcode a far return

start_chain_boot:
    xorl    %edx, %edx
    movb    _chainbootdev, %dl  # Setup DL with the BIOS device number

    call    __prot_to_real      # Back to real mode.

    data32
    call    __switch_stack      # Restore original stack
    
    pop     %es                 # Restore original ES and DS
    pop     %ds
    popl    %edi                # Restore all general purpose registers
    popl    %esi                # except EAX.
    popl    %ebp
    popl    %ebx
    popl    %ecx

    data32
    ljmp    $0, $0x7c00         # Jump to boot code already in memory


# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Multiboot support added by David F. Elliott <dfe@cox.net> on 2007/06/26
# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# Multiboot section
# The above is used when booting with the normal booter (boot1h/boot1u).
# The following is used when booting with a multiboot capable loader such as grub
# Unlike the normal booter which starts in real mode, we start in protected mode

# Unforuntately, GRUB refuses to load a multiboot "kernel" below 1MB.
# This is basically due to the fact that GRUB likes to live below 1MB because
# it starts up in real mode just like we would normally be starting up if
# we weren't being Multiboot loaded by GRUB.
# Therefore, we must tell our loader to load us above 1MB.  To make it easy,
# we simply specify exactly 1 MB higher than we want.
# This means of course that when we enter we are not where the assembler
# and linker expect us to be.  Please remember this when modifying code.
#define OFFSET_1MEG 0x100000

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# NOTE: While we would ideally place the header in say __TEXT,__const we
# cannot do that and have GRUB find it because GRUB only searches the first
# 8k of the binary.  Since __TEXT,__const follows __TEXT,__text (the code
# section) and since the code is well over 8k long, it doesn't work.
.align 2, 0x90 # Make sure we're on a 4-byte boundary.  Required by Multiboot.
_multiboot_header:
    # magic (NOTE: this shows up as 02b0 ad1b in a hex dump)
    .long   MULTIBOOT_HEADER_MAGIC
    # flags
    .long   MULTIBOOT_HEADER_FLAGS
    # checksum
    .long   -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)
    # header_addr
    .long   (_multiboot_header + OFFSET_1MEG)
    # load_addr
    .long   (boot2 + OFFSET_1MEG)
    # load_end_addr # Tell multiboot to load the whole file
    .long   0 
    # bss_end_addr # boot() will zero its own bss
    .long   0
    # entry_addr
    .long   (_multiboot_entry + OFFSET_1MEG)

# Stick a couple of nop here so that we hopefully make disassemblers realize we have instructions again
    nop
    nop
    nop
.align 3, 0x90 # Align to 8 byte boundary which should be enough nops

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
_multiboot_entry:
    # First thing's first, let's get ourselves a usable GDT.
    # Multiboot specifically says that GDT is not to be considered valid
    # because for all we know the loader could have erased the memory where
    # the GDT was located.  Even if it doesn't, it's very likely that we'll
    # clobber it when we move ourselves into low memory.

    # We have a GDT at the _Gdt symbol location which is defined in
    # i386/libsaio/table.c.  Like all GDTs, entry 0 is unusable.  The first
    # entry (0x08) is the one normally used by the booter.  However, it is
    # specifically defined as byte-granularity to ensure the booter does not
    # execute any code above 1 MB.  As mentioned above, we are above 1 MB so
    # we can't use the 0x08 selector.  Fortunately, the booter specifies the
    # 0x28 selector for the kernel init code using the typical 4K granularity
    # and making all 32-bits of address space available.

    # To load a GDT with the lgdt instruction we need to have the linear
    # address of a GDTR structure.  The 16-bit boot code uses the _Gdtr
    # variable which we will use later.  However, we are still in extended
    # memory so even if we added 1 MB to _Gdtr the contents of it would
    # still be pointing in low memory.  Therefore we have a _Gdtr_high
    # which points to _Gdt + OFFSET_1MEG (see bottom of file)
    # Notice that _Gdtr_high itself is located 1 MB above where the
    # assembler/linker thinks it is.
    lgdt    _Gdtr_high + OFFSET_1MEG

    # Now that we have a GDT we want to first reload the CS register. We do
    # that using a far jump to the desired selector with the desired offset.
    # The desired offset in this case is exactly 1 MB higher than the
    # assembler/linker thinks it is.  As mentioned above, we use the kernel
    # init code selector instead of the boot code selector.
    jmp     $0x28,$(Lpost_gdt_switch+OFFSET_1MEG)
Lpost_gdt_switch:

    # Now that we have the right code selector we also want the rest of the
    # selectors to be correct.  We use the same selector (0x10) as the
    # __real_to_prot function in libsaio/asm.s.  This is important as it
    # means we won't need to do this again after the next GDT switch.

    # We have to clobber one register because segment registers can only be
    # loaded from GP registers.  Fortunately, only eax and ebx are provided
    # by multiboot so we can clobber anything else.  We choose ecx.

    movl    $0x10, %ecx
    movl    %ecx,%ss
    movl    %ecx,%ds
    movl    %ecx,%es
    movl    %ecx,%fs
    movl    %ecx,%gs

    # Initialize our stack pointer to the one normally used by the booter
    # NOTE: This is somewhat dangerous considering it seems to be a de-facto
    # Multiboot standard that the area below 1 MB is for the loader (e.g. GRUB)
    # We may consider later putting the stack at + 1MB just like the code
    # but we'd have to eventually get it below 1 MB because until we do we can't
    # run any real-mode code (e.g. BIOS functions).
    # Doing it this early we potentially run the risk that our multiboot_info
    # pointer in ebx is already stuck somewhere in our stack segment
    # Of course, the best method would be to have a couple of choices for
    # stack space and put it wherever the multiboot_info is not.
    movl    $ADDR32(STACK_SEG,STACK_OFS), %esp

    # Some final notes about machine state:
    
    # We have no IDT and thus we leave interrupts disabled.  This is the same
    # state that __real_to_prot leaves the machine in so it's not a problem.

    # The A20 gate is enabled (it better be, we're above 1 MB)
    # It is enabled as the first thing in boot() but it won't hurt for it
    # to be enabled when it already is.

    # Unlike when booting from real mode, when booting from Multiboot we have
    # no stack to begin with.  This means that __switch_stack must never be
    # called because it is preloaded with STACK_SEG,STACK_OFS which is where
    # we already are.  Were it to be called it would effectively reset to the
    # top of the stack which would not be good.
    # We might think about adding a couple of instructions here to change
    # its default values to something that could be used if necessary.

    # At this point we're good enough to run C code.  I am no assembler guru
    # so we won't be returning from it.
    pushl   %ebx
    pushl   %eax
    call    _multiboot_to_boot # NOTE: near relative call, so we stay in high mem
    # NORETURN

    # NOTE: Once we get in to C, we should never return
    # We let the C code take care of doing a chain boot which right now
    # means to print an error message and restart the booter

Lhltloop:
    hlt     # This causes Parallels to power off although a normal PC will just hang
    jmp Lhltloop

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# _continue_at_low_address does some trickery to get the caller running from the low address with the right selector
    .globl _continue_at_low_address
    .align 2, 0x90
_continue_at_low_address:
    # Our stack frame has been set up with the return address on top
    # First, fix that to be 1 MB lower
    subl    $OFFSET_1MEG, (%esp)
    # Now load the proper low-memory GDTR from the low-memory location which
    # will cause the low-memory GDT to be used.
    lgdt    _Gdtr
    # Now jump to GDT selector 8 using the low address of this function
    # This finally puts us in low memory in the right selector (0x08)
    jmpl $0x08,$L_continue_at_low_address_next
L_continue_at_low_address_next:
    # We don't need to set ss,ds,es,fs, or gs because they are already 0x10
    # and the old GDT had the same information for selector 0x10 as the new
    # one does.
    # Since we've already fixed our return pointer, simply return
    # Note that our caller must fix his return pointer as well and that his
    # caller must fix his return pointer and so on and so forth.
    ret

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# _jump_to_chainbooter sets up dl, switches from protected to real mode, and
# jumps to 0:7C00.  Unlike the usual code, the stack is not switched since
# there is no stack to switch to.
    .globl _jump_to_chainbooter
_jump_to_chainbooter:
    # TODO: Take segment/offset arguments and put them in ES:SI?

    xorl    %edx, %edx
    movb    _chainbootdev, %dl  # Setup DL with the BIOS device number

    call    __prot_to_real      # Back to real mode.

    # TODO: Set SS:SP to something reasonable?  For instance, Microsoft MBR
    # code starts out by setting up the stack at 0:7c00 for itself and leaves
    # that intact.  Thus the stack by default will grow down from the code
    # entrypoint.  On the other hand, our own boot0 sets up the stack at
    # 0:fff0 and it seems that most boot code doesn't care and simply sets
    # SS:SP itself as one of the first things it does.

    data32
    ljmp    $0, $0x7c00         # Jump to boot code already in memory

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# _cause_crash jumps to offset 0 of a selector we know is not in our GDT
# This causes Parallels to output all sorts of nice debugging information
# We aren't using it right now so it's in an if 0 block.
#if 0
    .globl _cause_crash
    .align 2, 0x90
_cause_crash:
    # Cause a crash, there is no GDT selector f0
    jmp     $0xf0,$0
    hlt
    jmp _cause_crash
#endif

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# _Gdtr_high is a pointer to the extended-memory loaded _Gdt
# See the comments above as to why we have OFFSET_1MEG.
    //.data
    .section __INIT,__data	// turbo
    .align 2, 0x90
_Gdtr_high:
    .word GDTLIMIT
    .long vtop(_Gdt + OFFSET_1MEG)

