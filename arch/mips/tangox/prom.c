/*********************************************************************
 Sigma Designs SMP8643 prom.c with LRAM Milestone Instrumentation (0xA00619A0)
 *********************************************************************/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/bootinfo.h>
#include <asm/io.h>

#include "memmap.h"
#include "setup.h"

#define PCH_LRAM_STAGE   (*(volatile u32 *)0xA0061938UL)
#define PCH_LRAM_ARRAY   ((volatile u32 *)0xA0061900UL)
#define PCH_WDT_CFG      (*(volatile u32 *)0xA001FD04UL)
#define PCH_WDT_CNT      (*(volatile u32 *)0xA001FD00UL)

static inline void pch_stage(u32 s, int idx) {
    PCH_LRAM_STAGE = s;
    PCH_LRAM_ARRAY[idx] = s;
}
static inline void pch_wdt(void) {
    PCH_WDT_CFG = 0x00000001UL;
    PCH_WDT_CNT = 0x91557000UL;
}

static char tangox_system_type[32];

const char *get_system_type(void)
{
	return tangox_system_type;
}

static void __init tangox_systype_init(void)
{
	void __iomem *cid = (void __iomem *)(0xA0000000UL | (HOST_BASE + 0xfee8));
	u32 chip_type, chip_rev;

	chip_type = readl(cid);
	chip_rev = readl(cid + 4);

	snprintf(tangox_system_type, sizeof(tangox_system_type),
		 "Sigma Designs SMP%04x ES%d", chip_type, chip_rev);

	pr_info("%s\n", tangox_system_type);
}

#define REMAP_SIZE 0x04000000

u32 tangox_boot_remap[8];
EXPORT_SYMBOL(tangox_boot_remap);

u8 tangox_remap[64];
u8 tangox_remap_inv[64];
EXPORT_SYMBOL(tangox_remap);
EXPORT_SYMBOL(tangox_remap_inv);

static void __init tangox_remap_init(void)
{
	void __iomem *base = (void __iomem *)(0xA0000000UL | REMAP_CTL_BASE);
	int i;

	for (i = 0; i < 8; i++)
		tangox_boot_remap[i] = readl(base + i * 4);

	for (i = 0; i < 64; i++)
		tangox_remap[i] = tangox_remap_inv[i] = i;

	for (i = 2; i < 8; i++) {
		unsigned long addr = tangox_boot_remap[i];
		if (addr) {
		tangox_remap[i - 1] = addr >> 26;
		tangox_remap_inv[addr >> 26] = i - 1;
		}
	}

	pr_info("SMP8643 Remap CTL: [0]=0x%08x [1]=0x%08x [2]=0x%08x [3]=0x%08x [4]=0x%08x\n",
		tangox_boot_remap[0], tangox_boot_remap[1], tangox_boot_remap[2],
		tangox_boot_remap[3], tangox_boot_remap[4]);
	pr_info("SMP8643 DMA Remap: CPU bank 1 (0x04000000) -> GBUS 0x%08x\n",
		(u32)tangox_remap[1] << 26);
}

static void __init tangox_cmdline_setup(void)
{
	int argc = fw_arg0, i;
	char **argv = (char **)fw_arg1;

	if (argc > 1 && argv && (unsigned long)argv >= 0x80000000UL) {
		for (i = 1; i < argc; i++) {
			if (argv[i] && (unsigned long)argv[i] >= 0x80000000UL) {
				if (arcs_cmdline[0])
					strlcat(arcs_cmdline, " ", COMMAND_LINE_SIZE);
				strlcat(arcs_cmdline, argv[i], COMMAND_LINE_SIZE);
			}
		}
	}

	if (!arcs_cmdline[0])
		strlcpy(arcs_cmdline, "console=ttyS0,115200 lpj=1650688 earlyprintk init=/init", COMMAND_LINE_SIZE);
	else if (!strstr(arcs_cmdline, "lpj="))
		strlcat(arcs_cmdline, " lpj=1650688", COMMAND_LINE_SIZE);

	pr_info("Kernel cmdline: %s\n", arcs_cmdline);
}

void __init prom_init(void)
{
	pch_stage(0x88888888UL, 4); /* 1. prom_init entered */
	pch_wdt();

	tangox_systype_init();
	tangox_remap_init();
	pch_stage(0x99999999UL, 5); /* 2. remap check done */
	pch_wdt();

	tangox_cmdline_setup();
	prom_console_init();
	pch_stage(0xAAAAAAAAUL, 6); /* 3. console init done */
	pch_wdt();

	mips_machtype = MACH_TANGOX;
	pch_stage(0xBBBBBBBBUL, 7); /* 4. prom_init complete! */
	pch_wdt();
}

void __init prom_free_prom_memory(void)
{
}
