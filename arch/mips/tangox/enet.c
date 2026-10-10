#include <linux/init.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <asm/io.h>

#include "memmap.h"
#include "setup.h"

static int tangox_phy_init(struct phy_device *phydev)
{
	int val;

	pr_info("tangox: applying Popcorn Hour VSC8601 RGMII skew tuning...\n");

	/* Standard Page 0, Reg 23 (MII_RESV1): Enable RGMII Skew (bit 8 = 0x0100, 2.0ns delay) */
	val = phy_read(phydev, 23);
	if (val >= 0)
		phy_write(phydev, 23, val | 0x0100);

	pr_info("tangox: VSC8601 RGMII skew applied successfully!\n");
	return 0;
}

static void __init tangox_enet_set_bw(void __iomem *reg)
{
	u32 val = readl(reg);
	val = (val & ~0xff) | 0x18;
	writel(val, reg);
}

static int __init tangox_enet_setup(void)
{
	void __iomem *ctl = ioremap(SYS_BASE + 0x130, 8);
	int i;

	for (i = 0; i < 2; i++)
		tangox_enet_set_bw(ctl + i * 4);

	iounmap(ctl);

	if (of_machine_is_compatible("syabas,pch-c200") ||
	    of_machine_is_compatible("syabas,pch-a210")) {
		pr_info("PCH Ethernet PHY fixup enabled\n");
		phy_register_fixup_for_uid(0x00070420, 0xfffffff0,
					   tangox_phy_init);
	}

	return 0;
}
arch_initcall(tangox_enet_setup);
