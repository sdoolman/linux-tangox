/*
 * MIPS Decompressor with LRAM Milestone Instrumentation (0xA0061938) and WDT Rearm
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/libfdt.h>
#include <asm/addrspace.h>

#define PCH_LRAM_STAGE   (*(volatile u32 *)0xA0061938UL)
#define PCH_WDT_CFG      (*(volatile u32 *)0xA001FD04UL)
#define PCH_WDT_CNT      (*(volatile u32 *)0xA001FD00UL)

static inline void pch_set_stage(u32 stage)
{
    PCH_LRAM_STAGE = stage;
}

static inline void pch_pet_watchdog(void)
{
    PCH_WDT_CFG = 0x00000001UL;
    PCH_WDT_CNT = 0x91557000UL; /* ~45s at 54MHz */
}

unsigned long free_mem_ptr;
unsigned long free_mem_end_ptr;
extern unsigned char __image_begin, __image_end;

#ifdef CONFIG_DEBUG_ZBOOT
extern void puts(const char *s);
extern void puthex(unsigned long long val);
#else
#define puts(s) do {} while (0)
#define puthex(val) do {} while (0)
#endif

extern char __appended_dtb[];

void error(char *x)
{
    volatile u32 *tele = (volatile u32 *)0xA0061938UL;
    if (tele[0] != 0x706d6f43UL) {
        u32 err_code = 0xEEEEEEEEUL;
        if (x) {
            err_code = ((u32)(u8)x[0]) |
                       (((u32)(u8)x[1]) << 8) |
                       (((u32)(u8)x[2]) << 16) |
                       (((u32)(u8)x[3]) << 24);
        }
        pch_set_stage(err_code);
    }
    puts("\n\n");
    puts(x);
    puts("\n\n -- System halted");
    while (1) ;
}

#define STATIC static
#define POSTINC 1
#ifdef CONFIG_KERNEL_GZIP
#include "../../../../lib/decompress_inflate.c"
#endif

#ifdef CONFIG_KERNEL_LZO
#include "../../../../lib/decompress_unlzo.c"
#endif

#ifdef CONFIG_KERNEL_XZ
#include "../../../../lib/decompress_unxz.c"
#endif

unsigned long __stack_chk_guard;
void __stack_chk_guard_setup(void) { __stack_chk_guard = 0x000a0dff; }
void __stack_chk_fail(void) { error("stack corrupted\n"); }

unsigned long decompress_kernel(unsigned long boot_heap_start)
{
    unsigned long zimage_start, zimage_size;
    unsigned long dtb_dest = 0;

    pch_set_stage(0x44444444UL); /* 1. decompress_kernel entered */
    pch_pet_watchdog();

    __stack_chk_guard_setup();

    zimage_start = (unsigned long)(&__image_begin);
    zimage_size = (unsigned long)(&__image_end) - (unsigned long)(&__image_begin);

    free_mem_ptr = boot_heap_start;
    free_mem_end_ptr = boot_heap_start + BOOT_HEAP_SIZE;

    pch_set_stage(0x55555555UL); /* 2. calling __decompress */
    pch_pet_watchdog();

    __decompress((char *)zimage_start, zimage_size, 0, 0,
                 (void *)VMLINUX_LOAD_ADDRESS_ULL, 0, 0, error);

    pch_pet_watchdog();

    /* Telemetry: verify entry point opcode directly */
    {
        u32 entry_val = *(volatile u32 *)KERNEL_ENTRY;
        if (entry_val != 0x3C08A006UL) {
            /* Entry mismatch! Record the opcode found at KERNEL_ENTRY and halt */
            pch_set_stage(entry_val);
            pch_pet_watchdog();
            while (1) ;
        }
    }

    pch_set_stage(0x66666666UL); /* 3. __decompress complete & entry opcode verified! */

    if (IS_ENABLED(CONFIG_MIPS_RAW_APPENDED_DTB) &&
        fdt_magic((void *)&__appended_dtb) == FDT_MAGIC) {
        unsigned int image_size, dtb_size;
        dtb_size = fdt_totalsize((void *)&__appended_dtb);
        image_size = le32_to_cpup((void *)&__image_end - 4);
        dtb_dest = (unsigned long)VMLINUX_LOAD_ADDRESS_ULL + image_size;
        memcpy((void *)dtb_dest, __appended_dtb, dtb_size);
    }

    pch_pet_watchdog();
    return dtb_dest;
}
