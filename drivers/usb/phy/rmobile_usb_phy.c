/*
 * Renesas R-Car USB PHY support
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 * Copyright (C) 2015 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <common.h>
#include <malloc.h>
#include <asm/processor.h>
#include <asm/mach-types.h>
#include <asm/io.h>
#include <asm/errno.h>
#include <asm/arch/sys_proto.h>
#include <asm/gpio.h>
#include <asm/arch/rmobile.h>

#define USBHS_LPSTS			0xe6590102
#define USBHS_UGCTRL			0xe6590180
#define USBHS_UGCTRL2			0xe6590184
#define USBHS_UGSTS			0xe6590188

/* Low Power Status register (LPSTS) */
#define USBHS_LPSTS_SUSPM		(1 << 14)

/* USB General control register (UGCTRL) */
#define USBHS_UGCTRL_CONNECT		(1 << 2)
#define USBHS_UGCTRL_PLLRESET		(1 << 0)

/* USB General control register 2 (UGCTRL2) */
#define USBHS_UGCTRL2_USB2SEL		(0x1 << 31)
#define USBHS_UGCTRL2_USB2SEL_PCI	0
#define USBHS_UGCTRL2_USB2SEL_USB30	(1 << 31)
#define USBHS_UGCTRL2_USB0SEL		(0x3 << 4)
#define USBHS_UGCTRL2_USB0SEL_PCI	(1 << 4)
#define USBHS_UGCTRL2_USB0SEL_HS_USB	(3 << 4)

/* USB General status register (UGSTS) */
#define USBHS_UGSTS_LOCK		(0x3 << 8)

#define NUM_USB_CHANNELS		2

#define USBHS_MSTP704			(1 << 4)

void rmobile_usb_phy_init(int mask, int value)
{
	u32 val;

	/* enable ubshs clk */
	val = readl(MSTPSR7);
	val &= ~USBHS_MSTP704;
	writel(val, SMSTPCR7);

	val = readl(USBHS_UGCTRL2);
	val &= ~mask;
	val |= value;
	writel(val, USBHS_UGCTRL2);
}

void rmobile_usb_phy_exit(void)
{
	u32 val;

	/* disable ubshs clk */
	val = readl(MSTPSR7);
	val |= USBHS_MSTP704;
	writel(val, SMSTPCR7);
}

void rmobile_usb_phy_power_on(int value)
{
	u32 val;
	int i;

	/* Skip if it's not USBHS */
	if (value != USBHS_UGCTRL2_USB0SEL_HS_USB)
		return 0;

	/* Power on USBHS PHY */
	val = readl(USBHS_UGCTRL);
	val &= ~USBHS_UGCTRL_PLLRESET;
	writel(val, USBHS_UGCTRL);

	val = readw(USBHS_LPSTS);
	val |= USBHS_LPSTS_SUSPM;
	writew(val, USBHS_LPSTS);

	for (i = 0; i < 20; i++) {
		val = readl(USBHS_UGSTS);
		if ((val & USBHS_UGSTS_LOCK) == USBHS_UGSTS_LOCK) {
			val = readl(USBHS_UGCTRL);
			val |= USBHS_UGCTRL_CONNECT;
			writel(val, USBHS_UGCTRL);
			return;
		}
		udelay(1);
	}
}

void rmobile_usb_phy_power_off(int value)
{
	u32 val;

	/* Skip if it's not USBHS */
	if (value != USBHS_UGCTRL2_USB0SEL_HS_USB)
		return 0;

	/* Power off USBHS PHY */
	val = readl(USBHS_UGCTRL);
	val &= ~USBHS_UGCTRL_CONNECT;
	writel(val, USBHS_UGCTRL);

	val = readw(USBHS_LPSTS);
	val &= ~USBHS_LPSTS_SUSPM;
	writew(val, USBHS_LPSTS);

	val = readl(USBHS_UGCTRL);
	val |= USBHS_UGCTRL_PLLRESET;
	writel(val, USBHS_UGCTRL);
}
