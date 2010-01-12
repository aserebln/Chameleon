/*
    File added by David F. Elliott <dfe@cox.net> on 2007/06/26
*/

#include "libsaio.h"
#include "boot.h"
#include "bootstruct.h"

#include "mboot.h"

int multiboot_timeout=0;
int multiboot_timeout_set=0;
int multiboot_partition=0;
int multiboot_partition_set=0;

// Global multiboot info, if using multiboot.
struct multiboot_info *gMI;

extern void continue_at_low_address(void);

// prototype hi_multiboot and keep its implementation below multiboot_to_boot
// to ensure that it doesn't get inlined by the compiler
// We don't want it inlined because we specifically want the stack frame
// pointer to be as high as possible and the hi_multiboot function
// copies multiboot_info onto its stack.
uint32_t hi_multiboot(int multiboot_magic, struct multiboot_info *mi_orig);
// prototype dochainload for the same reason.
void dochainload();

#define OFFSET_1MEG 0x100000
#define BAD_BOOT_DEVICE 0xffffffff

// This assumes that the address of the first argument to the function will
// be exactly 4 bytes above the address of the return address.
// It is intended to be used as an lvalue with a statement like this -= OFFSET_1MEG;
#define RETURN_ADDRESS_USING_FIRST_ARG(arg) \
    (*(uint32_t*)((char*)&(arg) - 4))

#define FIX_RETURN_ADDRESS_USING_FIRST_ARG(arg) \
    RETURN_ADDRESS_USING_FIRST_ARG(arg) -= OFFSET_1MEG

extern void jump_to_chainbooter();
extern unsigned char chainbootdev;
extern unsigned char chainbootflag;

void chainLoad();
void waitThenReload();

int multibootRamdiskReadBytes( int biosdev, unsigned int blkno,
                      unsigned int byteoff,
                      unsigned int byteCount, void * buffer );
int multiboot_get_ramdisk_info(int biosdev, struct driveInfo *dip);
static long multiboot_LoadExtraDrivers(FileLoadDrivers_t FileLoadDrivers_p);

// Starts off in the multiboot context 1 MB high but eventually gets into low memory
// and winds up with a bootdevice in eax which is all that boot() wants
// This lets the stack pointer remain very high.
// If we were to call boot directly from multiboot then the whole multiboot_info
// would be on the stack which would possibly be using way too much stack.
void multiboot_to_boot(int multiboot_magic, struct multiboot_info *mi_orig)
{
    uint32_t bootdevice = hi_multiboot(multiboot_magic, mi_orig);
    if(bootdevice != BAD_BOOT_DEVICE)
    {
        // boot only returns to do a chain load.
        for(;;)
        {   // NOTE: boot only uses the last byte (the drive number)
            common_boot(bootdevice);
            if(chainbootflag)
                chainLoad();
            else
                waitThenReload();
        }
    }
    // Avoid returning to high-memory address which isn't valid in the segment
    // we are now in.
    // Calling sleep() ensures the user ought to be able to use Ctrl+Alt+Del
    // because the BIOS will have interrupts on.
    for(;;)
        sleep(10);
    // NOTE: *IF* we needed to return we'd have to fix up our return address to
    // be in low memory using the same trick as below.
    // However, there doesn't seem to be any point in returning to assembly
    // particularly when the remaining code merely halts the processor.
}

void chainLoad()
{
    /*  TODO: We ought to load the appropriate partition table, for example
        the MBR if booting a primary partition or the particular extended
        partition table if booting a logical drive.  For example, the
        regular MS MBR booter will relocate itself (e.g. the MBR) from
        0:7C00 to 0:0600 and will use SI as the offset when reading
        the partition data from itself.  Thus when it jumps to the partition
        boot sector, SI will be 0x600 + 446 + i<<4 where i is the partition
        table index.
    
        On the other hand, our code for the non-Multiboot case doesn't do
        this either, although GRUB does.
     */

    const unsigned char *bootcode = (const unsigned char*)0x7c00;
    if(bootcode[0x1fe] == 0x55 && bootcode[0x1ff] == 0xaa)
    {
        printf("Calling chainbooter\n");
        jump_to_chainbooter();
        /* NORETURN */
    }
    else
    {
        printf("Bad chain boot sector magic: %02x%02x\n", bootcode[0x1fe], bootcode[0x1ff]);
    }
}

void waitThenReload()
{
    /* FIXME: Ctrl+Alt+Del does not work under Boot Camp */
    printf("Darwin booter exited for some reason.\n");
    printf("Please reboot (Ctrl+Alt+Del) your machine.\n");
    printf("Restarting Darwin booter in 5 seconds...");
    sleep(1);
    printf("4...");
    sleep(1);
    printf("3...");
    sleep(1);
    printf("2...");
    sleep(1);
    printf("1...");
    sleep(1);
    printf("0\n");
}

// Declare boot2_sym as an opaque struct so it can't be converted to a pointer
// i.e. ensure the idiot programmer (me) makes sure to use address-of
// Technically it's a function but it's real mode code and we sure don't
// want to call it under any circumstances.
extern struct {} boot2_sym asm("boot2");

// prototype multiboot and keep its implementation below hi_multiboot to
// ensure that it doesn't get inlined by the compiler
static inline uint32_t multiboot(int multiboot_magic, struct multiboot_info *mi);


/*!
    Returns a pointer to the first safe address we can use for stowing the multiboot info.
    This might actually be a bit pedantic because mboot.c32 and GRUB both stow the multiboot
    info in low memory meaning that the >= 128 MB location we choose is plenty high enough.
 */
void *determine_safe_hi_addr(int multiboot_magic, struct multiboot_info *mi_orig)
{
    // hi_addr must be at least up in 128MB+ space so it doesn't get clobbered
    void *hi_addr = (void*)PREBOOT_DATA;

    // Fail if the magic isn't correct.  We'll complain later.
    if(multiboot_magic != MULTIBOOT_INFO_MAGIC)
        return NULL;
    // Make sure the command-line isn't in high memory.
    if(mi_orig->mi_flags & MULTIBOOT_INFO_HAS_CMDLINE)
    {
        char *end = mi_orig->mi_cmdline;
        if(end != NULL)
        {
            for(; *end != '\0'; ++end)
                ;
            ++end;
            if( (void*)end > hi_addr)
                hi_addr = end;
        }
    }
    // Make sure the module information isn't in high memory
    if(mi_orig->mi_flags & MULTIBOOT_INFO_HAS_MODS)
    {
        struct multiboot_module *modules = (void*)mi_orig->mi_mods_addr;
        int i;
        for(i=0; i < mi_orig->mi_mods_count; ++i)
        {
            // make sure the multiboot_module struct itself won't get clobbered
            void *modinfo_end = modules+i+1;
            if(modinfo_end > hi_addr)
                hi_addr = modinfo_end;
            // make sure the module itself won't get clobbered
            modinfo_end = (void*)modules[i].mm_mod_end;
            if(modinfo_end > hi_addr)
                hi_addr = modinfo_end;
            // make sure the module string doesn't get clobbered
            char *end = modules[i].mm_string;
            for(; *end != '\0'; ++end)
                ;
            ++end;
            modinfo_end = end;
            if(modinfo_end > hi_addr)
                hi_addr = modinfo_end;
        }
    }
    // TODO: Copy syms (never needed), mmap, drives, config table, loader name, apm table, VBE info

    // Round up to page size
    hi_addr = (void*)(((uint32_t)hi_addr + 0xfff) & ~(uint32_t)0xfff);
    return hi_addr;
}

/*!
    Like malloc but with a preceding input/output parameter which points to the next available
    location for data.  The original value of *hi_addr is returned and *hi_addr is incremented
    by size bytes.
 */
void * _hi_malloc(void **hi_addr, size_t size)
{
    void *ret = *hi_addr;
    *hi_addr += size;
    return ret;
}

/*!
    Like strdup but with a preceding input/output parameter.  The original value of *hi_addr is
    returned and *hi_addr is incremented by the number of bytes necessary to complete the string
    copy including its NUL terminator.
 */
char * _hi_strdup(void **hi_addr, char *src)
{
    char *dstStart;
    char *dst = dstStart = *hi_addr;
    for(; *src != '\0'; ++src, ++dst, ++(*hi_addr))
        *dst = *src;
    *dst = '\0';
    ++(*hi_addr);
    return dstStart;
}

// Convenience macros
#define hi_malloc(size) _hi_malloc(&hi_addr, (size))
#define hi_strdup(src) _hi_strdup(&hi_addr, (src))

/*!
    Copies the Multiboot info and any associated data (e.g. various strings and any multiboot modules)
    up to very high RAM (above 128 MB) to ensure it doesn't get clobbered by the booter.
 */
struct multiboot_info * copyMultibootInfo(int multiboot_magic, struct multiboot_info *mi_orig)
{
    void *hi_addr = determine_safe_hi_addr(multiboot_magic, mi_orig);
    if(hi_addr == NULL)
        return NULL;

    struct multiboot_info *mi_copy = hi_malloc(sizeof(*mi_copy));
    memcpy(mi_copy, mi_orig, sizeof(*mi_copy));
    
    // Copy the command line
    if(mi_orig->mi_flags & MULTIBOOT_INFO_HAS_CMDLINE)
    {
        mi_copy->mi_cmdline = hi_strdup(mi_orig->mi_cmdline);
    }
    // Copy the loader name
    if(mi_orig->mi_flags & MULTIBOOT_INFO_HAS_LOADER_NAME)
    {
        mi_copy->mi_loader_name = hi_strdup(mi_orig->mi_loader_name);
    }
    // Copy the module info
    if(mi_orig->mi_flags & MULTIBOOT_INFO_HAS_MODS)
    {
        struct multiboot_module *dst_modules = hi_malloc(sizeof(*dst_modules)*mi_orig->mi_mods_count);
        struct multiboot_module *src_modules = (void*)mi_orig->mi_mods_addr;
        mi_copy->mi_mods_addr = (uint32_t)dst_modules;

        // Copy all of the module info plus the actual module into high memory
        int i;
        for(i=0; i < mi_orig->mi_mods_count; ++i)
        {
            // Assume mod_end is 1 past the actual end (i.e. it is start + size, not really end (i.e. start + size - 1))
            // This is what GRUB and mboot.c32 do although the spec is unclear on this.
            uint32_t mod_length = src_modules[i].mm_mod_end - src_modules[i].mm_mod_start;

            dst_modules[i].mm_mod_start = (uint32_t)hi_malloc(mod_length);
            dst_modules[i].mm_mod_end = (uint32_t)dst_modules[i].mm_mod_start + mod_length;
            memcpy((char*)dst_modules[i].mm_mod_start, (char*)src_modules[i].mm_mod_start, mod_length);
            
            dst_modules[i].mm_string = hi_strdup(src_modules[i].mm_string);
            dst_modules[i].mm_reserved = src_modules[i].mm_reserved;
        }
    }
    // Make sure that only stuff that didn't need to be copied or that we did deep copy is indicated in the copied struct.
    mi_copy->mi_flags &= MULTIBOOT_INFO_HAS_MEMORY | MULTIBOOT_INFO_HAS_BOOT_DEVICE | MULTIBOOT_INFO_HAS_CMDLINE | MULTIBOOT_INFO_HAS_LOADER_NAME | MULTIBOOT_INFO_HAS_MODS;

    return mi_copy;
}

// When we enter, we're actually 1 MB high.
// Fortunately, memcpy is position independent, and it's all we need
uint32_t hi_multiboot(int multiboot_magic, struct multiboot_info *mi_orig)
{
    // Copy the multiboot info out of the way.
    // We can't bitch about the magic yet because printf won't work
    // because it contains an absolute location of putchar which
    // contains absolute locations to other things which eventually
    // makes a BIOS call from real mode which of course won't work
    // because we're stuck in extended memory at this point.
    struct multiboot_info *mi_p = copyMultibootInfo(multiboot_magic, mi_orig);

    // Get us in to low memory so we can run everything

    // We cannot possibly be more than 383.5k and copying extra won't really hurt anything
    // We use the address of the assembly entrypoint to get our starting location.
    memcpy(&boot2_sym, (char*)&boot2_sym + OFFSET_1MEG, 0x5fe00 /* 383.5k */);

    // This is a little assembler routine that returns to us in the correct selector
    // instead of the kernel selector we're running in now and at the correct
    // instruction pointer ( current minus 1 MB ).  It does not fix our return
    // address nor does it fix the return address of our caller.
    continue_at_low_address();

    // Now fix our return address.
    FIX_RETURN_ADDRESS_USING_FIRST_ARG(multiboot_magic);

    // We can now do just about anything, including return to our caller correctly.
    // However, our caller must fix his return address if he wishes to return to
    // his caller and so on and so forth.

    /*  Zero the BSS and initialize malloc */
    initialize_runtime();

    gMI = mi_p;

    /*  Set up a temporary bootArgs so we can call console output routines
        like printf that check the v_display.  Note that we purposefully
        do not initialize anything else at this early stage.

        We are reasonably sure we're already in text mode if GRUB booted us.
        This is the same assumption that initKernBootStruct makes.
        We could check the multiboot info I guess, but why bother?
     */
    boot_args temporaryBootArgsData;
    bzero(&temporaryBootArgsData, sizeof(boot_args));
    bootArgs = &temporaryBootArgsData;
    bootArgs->Video.v_display = VGA_TEXT_MODE;

    // Install ramdisk and extra driver hooks
    p_get_ramdisk_info = &multiboot_get_ramdisk_info;
    p_ramdiskReadBytes = &multibootRamdiskReadBytes;
    LoadExtraDrivers_p = &multiboot_LoadExtraDrivers;

    // Since we call multiboot ourselves, its return address will be correct.
    // That is unless it's inlined in which case it does not matter.
    uint32_t bootdevice = multiboot(multiboot_magic, mi_p);
    // We're about to exit and temporaryBootArgs will no longer be valid
    bootArgs = NULL;
    return bootdevice;
}

enum {
    kReturnKey     = 0x0d,
    kEscapeKey     = 0x1b,
    kBackspaceKey  = 0x08,
    kASCIIKeyMask  = 0x7f
};

// This is the meat of our implementation.  It grabs the boot device from
// the multiboot_info and returns it as is.  If it fails it returns
// BAD_BOOT_DEVICE.  We can call an awful lot of libsa and libsaio but
// we need to take care not to call anything that requires malloc because
// it won't be initialized until boot() does it.
static inline uint32_t multiboot(int multiboot_magic, struct multiboot_info *mi)
{
    if(multiboot_magic != MULTIBOOT_INFO_MAGIC)
    {
        printf("Wrong Multiboot magic\n");
        sleep(2);
        return BAD_BOOT_DEVICE;
    }
    printf("Multiboot info @0x%x\n", (uint32_t)mi);
    if(mi->mi_flags & MULTIBOOT_INFO_HAS_LOADER_NAME)
        printf("Loaded by %s\n", mi->mi_loader_name);

    // Multiboot puts boot device in high byte
    // Normal booter wants it in low byte
    int bootdevice = mi->mi_boot_device_drive;

    bool doSelectDevice = false;
    if(mi->mi_flags & MULTIBOOT_INFO_HAS_BOOT_DEVICE)
    {
        printf("Boot device 0x%x\n", bootdevice);
    }
    else
    {
        printf("Multiboot info does not include chosen boot device\n");
        doSelectDevice = true;
        bootdevice = BAD_BOOT_DEVICE;
    }
    if(mi->mi_flags & MULTIBOOT_INFO_HAS_CMDLINE)
    {
        const char *val;
        int size;
        
        if(getValueForBootKey(mi->mi_cmdline, "biosdev", &val, &size))
        {
            char *endptr;
            int intVal = strtol(val, &endptr, 16 /* always hex */);
            if(*val != '\0' && (*endptr == '\0' || *endptr == ' ' || *endptr == '\t'))
            {
                printf("Boot device overridden to %02x with biosdev=%s\n", intVal, val);
                bootdevice = intVal;
                doSelectDevice = false;
            }
            else
                doSelectDevice = true;
        }
		
        if(getValueForBootKey(mi->mi_cmdline, "timeout", &val, &size))
        {
            char *endptr;
            int intVal = strtol(val, &endptr, 0);
            if(*val != '\0' && (*endptr == '\0' || *endptr == ' ' || *endptr == '\t'))
            {
                printf("Timeout overridden to %d with timeout=%s\n", intVal, val);
                multiboot_timeout = intVal;
                multiboot_timeout_set = 1;
            }
        }		
		
        if(getValueForBootKey(mi->mi_cmdline, "partno", &val, &size))
        {
            char *endptr;
            int intVal = strtol(val, &endptr, 0);
            if(*val != '\0' && (*endptr == '\0' || *endptr == ' ' || *endptr == '\t'))
            {
                printf("Default partition overridden to %d with timeout=%s\n", intVal, val);
                multiboot_partition = intVal;
                multiboot_partition_set = 1;
            }
        }				
    }
    if(doSelectDevice)
    {
        bootdevice = selectAlternateBootDevice(bootdevice);
    }
    if(bootdevice == BAD_BOOT_DEVICE)
        sleep(2); // pause for a second before halting
    return bootdevice;
}

///////////////////////////////////////////////////////////////////////////
// Ramdisk and extra drivers code

int multibootRamdiskReadBytes( int biosdev, unsigned int blkno,
                      unsigned int byteoff,
                      unsigned int byteCount, void * buffer )
{
    int module_count = gMI->mi_mods_count;
    struct multiboot_module *modules = (void*)gMI->mi_mods_addr;
    if(biosdev < 0x100)
        return -1;
    if(biosdev >= (0x100 + module_count))
        return -1;
    struct multiboot_module *module = modules + (biosdev - 0x100);

    void *p_initrd = (void*)module->mm_mod_start;
    bcopy(p_initrd + blkno*512 + byteoff, buffer, byteCount);
    return 0;
}

int multiboot_get_ramdisk_info(int biosdev, struct driveInfo *dip)
{
    int module_count = gMI->mi_mods_count;
    struct multiboot_module *modules = (void*)gMI->mi_mods_addr;
    if(biosdev < 0x100)
        return -1;
    if(biosdev >= (0x100 + module_count))
        return -1;
    struct multiboot_module *module = modules + (biosdev - 0x100);
    dip->biosdev = biosdev;
    dip->uses_ebios = true;	// XXX aserebln uses_ebios isn't a boolean at all
    dip->di.params.phys_sectors = (module->mm_mod_end - module->mm_mod_start + 511) / 512;
    dip->valid = true;
    return 0;
}

static long multiboot_LoadExtraDrivers(FileLoadDrivers_t FileLoadDrivers_p)
{
    char extensionsSpec[1024];
    int ramdiskUnit;
    for(ramdiskUnit = 0; ramdiskUnit < gMI->mi_mods_count; ++ramdiskUnit)
    {
        int partCount; // unused
        BVRef ramdiskChain = diskScanBootVolumes(0x100 + ramdiskUnit, &partCount);
        if(ramdiskChain == NULL)
        {
            verbose("Ramdisk contains no partitions\n");
            continue;
        }
        for(; ramdiskChain != NULL; ramdiskChain = ramdiskChain->next)
        {
            sprintf(extensionsSpec, "rd(%d,%d)/Extra/", ramdiskUnit, ramdiskChain->part_no);
            struct dirstuff *extradir = opendir(extensionsSpec);
            closedir(extradir);
            if(extradir != NULL)
            {
                int ret = FileLoadDrivers_p(extensionsSpec, 0 /* this is a kext root dir, not a kext with plugins */);
                if(ret != 0)
                {
                    verbose("FileLoadDrivers failed on a ramdisk\n");
                    return ret;
                }
            }
        }
    }
    return 0;
}
