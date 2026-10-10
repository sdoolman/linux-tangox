/*
 * Copyright (C) 2015 Mans Rullgard <mans@mansr.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <asm/byteorder.h>
#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/time.h>
#include <asm/traps.h>
#include <asm/cpu-info.h>
#include <asm/prom.h>
#include <linux/etherdevice.h>

#include "setup.h"

void __init arch_init_irq(void)
{
	irqchip_init();
}

static void tangox_machine_halt(void)
{
	while (1)
		cpu_relax();
}

static void tangox_machine_power_off(void)
{
	while (1)
		cpu_relax();
}

#include <linux/delay.h>
#include "memmap.h"

static void tangox_machine_restart(char *command)
{
	void __iomem *host_base = (void __iomem *)(0xA0000000UL | HOST_BASE);
	void __iomem *remap_base = (void __iomem *)(0xA0000000UL | REMAP_CTL_BASE);
	void __iomem *sys_base = (void __iomem *)(0xA0000000UL | SYS_BASE);
	u32 tmp;
	int i;

	local_irq_disable();

	/* 1. Reset TangoX EHCI */
	tmp = readl(host_base + 0x1410);
	writel(tmp & ~1, host_base + 0x1410);
	mdelay(5);

	/* 2. Reset TangoX OHCI */
	writel(1U << 31, host_base + 0x1514);
	writel(0, host_base + 0x1504);
	mdelay(5);

	/* 3. Reset internal USB PHY */
	tmp = readl(host_base + 0x1700);
	writel(tmp | 1, host_base + 0x1700);
	udelay(30);
	writel(tmp, host_base + 0x1700);
	mdelay(5);

	/* 4. Restore CPU remap registers to Yamon boot state */
	for (i = 0; i < 8; i++) {
		if (tangox_boot_remap[i])
			writel(tangox_boot_remap[i], remap_base + i * 4);
		else
			writel(i == 0 ? REMAP0_BASE : (i == 1 ? REMAP1_BASE : (0x04000000UL * (i - 1))), remap_base + i * 4);
	}
	mb();

	/* 5. Trigger clean hardware watchdog reset (~100 us delay) */
	writeb(0x80, sys_base + 0xFD07);
	writeb(0x01, sys_base + 0xFD04);
	writel(2700, sys_base + 0xFD00);
	writeb(0x00, sys_base + 0xFD07);

	while (1)
		cpu_relax();
}

void __init plat_mem_setup(void)
{
	_machine_restart = tangox_machine_restart;
	_machine_halt = tangox_machine_halt;
	pm_power_off = tangox_machine_power_off;

	ioport_resource.start = 0;
	ioport_resource.end = 0x7fffffff;

	iomem_resource.start = 0;
	iomem_resource.end = 0x7fffffff;

	__dt_setup_arch(__dtb_start);
}

void __init plat_time_init(void)
{
	struct device_node *cpu;
	struct clk *clk;
	unsigned rate;
	int ccres;

	of_clk_init(NULL);
	clocksource_probe();

	cpu = of_find_node_by_path("cpu0");
	if (!cpu)
		return;

	clk = of_clk_get(cpu, 0);
	if (IS_ERR(clk))
		return;

	rate = clk_get_rate(clk);

	__asm__ ("rdhwr %0, $3" : "=r" (ccres));
	mips_hpt_frequency = rate / ccres;

	pr_info("CPU clock %d Hz\n", rate);
}

void __init device_tree_init(void)
{
	unflatten_and_copy_device_tree();
}

static int __init tangox_of_set_prop(struct device_node *node, char *name,
				     void *val, int len)
{
	struct property *prop;

	prop = kzalloc(sizeof(*prop) + len, GFP_KERNEL);
	if (!prop)
		return -ENOMEM;

	prop->name = kstrdup(name, GFP_KERNEL);
	if (!prop->name) {
		kfree(prop);
		return -ENOMEM;
	}

	prop->length = len;

	if (len) {
		prop->value = prop + 1;
		memcpy(prop->value, val, len);
	}

	of_update_property(node, prop);

	return 0;
}

static int __init tangox_of_eth_setup(const char *name, int num)
{
	struct device_node *node;
	unsigned char *mac;

	node = of_find_node_by_path(name);
	if (!node && num == 0)
		node = of_find_node_by_path("/soc/ethernet@26000");
	if (!node && num == 1)
		node = of_find_node_by_path("/soc/ethernet@26800");
	if (!node)
		return -ENODEV;

	mac = tangox_ethernet_getmac(num);
	if (!mac || !is_valid_ether_addr(mac))
		return 0;

	pr_info("tangox: assigned MAC %pM to %s\n", mac, node->full_name);
	tangox_of_set_prop(node, "local-mac-address", mac, 6);

	return 0;
}

static int __init tangox_of_sata_phy_setup(void)
{
	static const int clk_tab[16] __initconst = {
		120, 100, 60, 50, 30, 25,
	};
	struct device_node *node;
	unsigned int cfg = tangox_sata_cfg();
	int clk_sel;
	__be32 clk_ref;
	__be32 rx_ssc[2];
	__be32 tx_ssc;
	__be32 tx_erc;

	node = of_find_node_by_path("sata_phy");
	if (!node)
		return -ENODEV;

	rx_ssc[0] = cpu_to_be32(cfg & 1);
	rx_ssc[1] = cpu_to_be32(cfg >> 1 & 1);
	tx_ssc = cpu_to_be32(cfg >> 2 & 1);
	clk_ref = cpu_to_be32(clk_tab[cfg >> 4 & 15]);
	tx_erc = cpu_to_be32(cfg >> 8 & 15);
	clk_sel = cfg >> 15 & 1;

	tangox_of_set_prop(node, "sigma,rx-ssc", rx_ssc, 8);
	tangox_of_set_prop(node, "sigma,tx-ssc", &tx_ssc, 4);
	tangox_of_set_prop(node, "clock-frequency", &clk_ref, 4);
	tangox_of_set_prop(node, "sigma,tx-erc", &tx_erc, 4);
	if (clk_sel)
		tangox_of_set_prop(node, "sigma,internal-clock", NULL, 0);

	return 0;
}

static int __init plat_of_setup(void)
{
	if (!of_have_populated_dt())
		panic("device tree not present");

	xenv_config();

	tangox_of_eth_setup("eth0", 0);
	tangox_of_eth_setup("eth1", 1);

	tangox_of_sata_phy_setup();

	return of_platform_populate(NULL, of_default_bus_match_table,
				    NULL, NULL);
}
arch_initcall(plat_of_setup);
