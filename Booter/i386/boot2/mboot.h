/* File added by David F. Elliott <dfe@cox.net> on 2007/06/27 */
#include "multiboot.h"

/*
The following DWORD tells the loader what features we require of it.
bit  0 set: Align modules on 4KB. We have no modules, we may not need this.
bit  1 set: Provide info about memory. We probably don't need this either
bit  2    : We might want this.  If so we need to tell the loader to stick
              us in text mode.  We currently assume that the loader will put
              us in text mode if we lack this because that is what GRUB does.
bit 16 set: This is not ELF, use the multiboot_header fields.
            We definitely need this flag.
*/
/* #define MULTIBOOT_HEADER_FLAGS 0x00010003 */

#define MULTIBOOT_HEADER_FLAGS \
    (MULTIBOOT_HEADER_HAS_ADDR|MULTIBOOT_HEADER_WANT_MEMORY|MULTIBOOT_HEADER_MODS_ALIGNED)

#ifndef __ASSEMBLER__
/* Put any desired prototypes or other C stuff here. */
#endif
