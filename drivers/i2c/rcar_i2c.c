/*
 * drivers/i2c/rcar_i2c.c
 *     This file is driver of Renesas R-Car I2C.
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <common.h>
#include <i2c.h>
#include <asm/io.h>

struct rcar_i2c {
	u32 icscr;
	u32 icmcr;
	u32 icssr;
	u32 icmsr;
	u32 icsier;
	u32 icmier;
	u32 icccr;
	u32 icsar;
	u32 icmar;
	u32 icrxdtxd;
};

#define MCR_MDBS	0x80		/* non-fifo mode switch	*/
#define MCR_FSCL	0x40		/* override SCL pin	*/
#define MCR_FSDA	0x20		/* override SDA pin	*/
#define MCR_OBPC	0x10		/* override pins	*/
#define MCR_MIE		0x08		/* master if enable	*/
#define MCR_TSBE	0x04
#define MCR_FSB		0x02		/* force stop bit	*/
#define MCR_ESG		0x01		/* en startbit gen.	*/

#define MSR_MASK	0x7f
#define MSR_MNR		0x40		/* nack received	*/
#define MSR_MAL		0x20		/* arbitration lost	*/
#define MSR_MST		0x10		/* sent a stop		*/
#define MSR_MDE		0x08
#define MSR_MDT		0x04
#define MSR_MDR		0x02
#define MSR_MAT		0x01		/* slave addr xfer done	*/

static struct rcar_i2c *base;

#define IRQ_WAIT 1000

static int
i2c_raw_write(struct rcar_i2c *base, u8 id, u8 reg, u8 *val, int size)
{
	/* set slave address */
	writel((id << 1) | 0, &base->icmar);
	/* set register address */
	writel(reg, &base->icrxdtxd);
	/* clear status */
	writel(0, &base->icmsr);
	/* start master send */
	writel(MCR_MDBS | MCR_MIE | MCR_ESG, &base->icmcr);

	while ((readl(&base->icmsr) & (MSR_MAT | MSR_MDE))
		!= (MSR_MAT | MSR_MDE))
		udelay(10);

	/* clear ESG */
	writel(MCR_MDBS | MCR_MIE, &base->icmcr);
	/* start SCLclk */
	writel(~(MSR_MAT | MSR_MDE), &base->icmsr);

	while (!(readl(&base->icmsr) & MSR_MDE))
		udelay(10);

	/* set send date */
	writel(*val, &base->icrxdtxd);
	/* start SCLclk */
	writel(~MSR_MDE, &base->icmsr);

	while (!(readl(&base->icmsr) & MSR_MDE))
		udelay(10);

	/* set stop condition */
	writel(MCR_MDBS | MCR_MIE | MCR_FSB, &base->icmcr);
	/* start SCLclk */
	writel(~MSR_MDE, &base->icmsr);

	while (!(readl(&base->icmsr) & MSR_MST))
		udelay(10);

	writel(0, &base->icmcr);

	return 0;
}

static u8 i2c_raw_read(struct rcar_i2c *base, u8 id, u8 reg)
{
	u8 ret;

	/* set slave address, send */
	writel((id << 1) | 0, &base->icmar);
	/* set register address */
	writel(reg, &base->icrxdtxd);
	/* clear status */
	writel(0, &base->icmsr);
	/* start master send */
	writel(MCR_MDBS | MCR_MIE | MCR_ESG, &base->icmcr);

	while ((readl(&base->icmsr) & (MSR_MAT | MSR_MDE))
		!= (MSR_MAT | MSR_MDE))
		udelay(10);

	/* clear ESG */
	writel(MCR_MDBS | MCR_MIE, &base->icmcr);
	/* start SCLclk */
	writel(~(MSR_MAT | MSR_MDE), &base->icmsr);

	while (!(readl(&base->icmsr) & MSR_MDE))
		udelay(10);

	/* set slave address, receive */
	writel((id << 1) | 1, &base->icmar);
	/* start master receive */
	writel(MCR_MDBS | MCR_MIE | MCR_ESG, &base->icmcr);
	/* clear status */
	writel(0, &base->icmsr);

	while ((readl(&base->icmsr) & (MSR_MAT | MSR_MDR))
		!= (MSR_MAT | MSR_MDR))
		udelay(10);

	/* clear ESG */
	writel(MCR_MDBS | MCR_MIE, &base->icmcr);
	/* prepare stop condition */
	writel(MCR_MDBS | MCR_MIE | MCR_FSB, &base->icmcr);
	/* start SCLclk */
	writel(~(MSR_MAT | MSR_MDR), &base->icmsr);

	while (!(readl(&base->icmsr) & MSR_MDR))
		udelay(10);

	/* get receive data */
	ret = (u8)readl(&base->icrxdtxd);
	/* start SCLclk */
	writel(~MSR_MDR, &base->icmsr);

	while (!(readl(&base->icmsr) & MSR_MST))
		udelay(10);

	writel(0, &base->icmcr);

	return ret;
}

void i2c_init(int speed, int slaveaddr)
{
	base = (struct rcar_i2c *)CONFIG_RCAR_I2C_BASE;

	/*
	 * reset slave mode.
	 * slave mode is not used on this driver
	 */
	writel(0, &base->icsier);
	writel(0, &base->icsar);
	writel(0, &base->icscr);
	writel(0, &base->icssr);

	/* reset master mode */
	writel(0, &base->icmier);
	writel(0, &base->icmcr);
	writel(0, &base->icmsr);
	writel(0, &base->icmar);

	/* Recommended values of bus speed 400kHz by GEN2 H/W spec. */
	writel(3 << 3 | 6, &base->icccr);
}

/*
 * i2c_read: - Read multiple bytes from an i2c device
 *
 * The higher level routines take into account that this function is only
 * called with len < page length of the device (see configuration file)
 *
 * @chip:   address of the chip which is to be read
 * @addr:   i2c data address within the chip
 * @alen:   length of the i2c data address (1..2 bytes)
 * @buffer: where to write the data
 * @len:    how much byte do we want to read
 * @return: 0 in case of success
 */
int i2c_read(u8 chip, u32 addr, int alen, u8 *buffer, int len)
{
	int i = 0;
	for (i = 0; i < len; i++)
		buffer[i] = i2c_raw_read(base, chip, addr + i);

	return 0;
}

/*
 * i2c_write: -  Write multiple bytes to an i2c device
 *
 * The higher level routines take into account that this function is only
 * called with len < page length of the device (see configuration file)
 *
 * @chip:   address of the chip which is to be written
 * @addr:   i2c data address within the chip
 * @alen:   length of the i2c data address (1..2 bytes)
 * @buffer: where to find the data to be written
 * @len:    how much byte do we want to read
 * @return: 0 in case of success
 */
int i2c_write(u8 chip, u32 addr, int alen, u8 *buffer, int len)
{
	return i2c_raw_write(base, chip, addr, buffer, len);
}

/*
 * i2c_probe: - Test if a chip answers for a given i2c address
 *
 * @chip:   address of the chip which is searched for
 * @return: 0 if a chip was found, -1 otherwhise
 */
int i2c_probe(u8 chip)
{
	u8 byte;
	return i2c_read(chip, 0, 0, &byte, 1);
}
