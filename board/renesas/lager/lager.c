/*
 * board/renesas/lager/lager.c
 *     This file is lager board support.
 *
 * Copyright (C) 2013 Renesas Electronics Corporation
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
#include <malloc.h>
#include <asm/processor.h>
#include <asm/mach-types.h>
#include <asm/io.h>
#include <asm/arch/sys_proto.h>
#include <asm/gpio.h>
#include <asm/arch/rmobile.h>

DECLARE_GLOBAL_DATA_PTR;

struct pin_db {
	u32	addr;	/* register address */
	u32	mask;	/* mask value */
	u32	val;	/* setting value */
};

#define	PMMR		0xE6060000
#define	GPSR1		0xE6060008
#define	GPSR2		0xE606000C
#define	GPSR4		0xE6060014
#define	IPSR6		0xE6060038
#define	IPSR14		0xE6060058
#define	PUPR4		0xE6060110
#define	PUPR5		0xE6060114
#define	PUPR6		0xE6060118

#define	SetREG(x) \
	writel((readl((x)->addr) & ~((x)->mask)) | ((x)->val), (x)->addr)

#define	SetGuardREG(x)				\
{ \
	u32	val; \
	val = (readl((x)->addr) & ~((x)->mask)) | ((x)->val); \
	writel(~val, PMMR); \
	writel(val, (x)->addr); \
}

struct pin_db	pin_guard[] = {
	/* SCIF0 */
#if defined(CONFIG_NORFLASH)
	{ GPSR4,  0x34000000, 0x00000000 },	/* TX0, RX0, SCIF_CLK */
	{ IPSR14, 0x00000FC7, 0x00000481 },
	{ GPSR4,  0x00000000, 0x34000000 },
 #endif
	{ GPSR2,  0x00000000, 0x3FFC0000 },	/* ETHER */
	{ GPSR1,  0x02000000, 0x00000000 },	/* IRQ0 */
	{ IPSR6,  0x00000007, 0x00000001 },
	{ GPSR1,  0x00000000, 0x02000000 },
};

struct pin_db	pin_tbl[] = {
	{ PUPR4,  0xC0000000, 0x00000000 },
	{ PUPR5,  0xE0000000, 0x00000000 },
	{ PUPR6,  0x0000007F, 0x00000000 },
};

void pin_init(void)
{
	struct pin_db	*db;

	for (db = pin_guard; db < &pin_guard[sizeof(pin_guard) /
			sizeof(struct pin_db)]; db++) {
		SetGuardREG(db);
	}
	for (db = pin_tbl; db < &pin_tbl[sizeof(pin_tbl) /
			sizeof(struct pin_db)]; db++) {
		SetREG(db);
	}
}

#define s_init_wait(cnt) \
		({	\
			volatile u32 i = 0x10000 * cnt;	\
			while (i > 0)	\
				i--;	\
		})

void s_init(void)
{
	struct r8a7790_rwdt *rwdt = (struct r8a7790_rwdt *)RWDT_BASE;
	struct r8a7790_swdt *swdt = (struct r8a7790_swdt *)SWDT_BASE;
	struct r8a7790_lbsc *lbsc = (struct r8a7790_lbsc *)LBSC_BASE;
	struct r8a7790_dbsc3 *dbsc3_0 = (struct r8a7790_dbsc3 *)DBSC3_0_BASE;

	/* Watchdog init */
	writel(0xA5A5A500, &rwdt->rwtcsra);
	writel(0xA5A5A500, &swdt->swtcsra);

	/* PFC */
	pin_init();

#if defined(CONFIG_NORFLASH)
	/* LBSC */
	writel(0x00000020, &lbsc->cs0ctrl);
	writel(0x00000020, &lbsc->cs1ctrl);
	writel(0x00002020, &lbsc->ecs0ctrl);
	writel(0x00002020, &lbsc->ecs1ctrl);

	writel(0x077F077F, &lbsc->cswcr0);
	writel(0x077F077F, &lbsc->cswcr1);
	writel(0x077F077F, &lbsc->ecswcr0);
	writel(0x077F077F, &lbsc->ecswcr1);

	/* DBSC3 */
	s_init_wait(10);

	writel(0x0000A55A, &dbsc3_0->dbpdlck);
	writel(0x00000001, &dbsc3_0->dbpdrga);
	writel(0x80000000, &dbsc3_0->dbpdrgd);
	writel(0x00000004, &dbsc3_0->dbpdrga);
	while ((readl(&dbsc3_0->dbpdrgd) & 0x00000001) != 0x00000001)
		;

	writel(0x00000006, &dbsc3_0->dbpdrga);
	writel(0x0001C000, &dbsc3_0->dbpdrgd);

	writel(0x00000023, &dbsc3_0->dbpdrga);
	writel(0x00FD2480, &dbsc3_0->dbpdrgd);

	writel(0x00000010, &dbsc3_0->dbpdrga);
	writel(0xF004649B, &dbsc3_0->dbpdrgd);

	writel(0x0000000F, &dbsc3_0->dbpdrga);
	writel(0x00181EE4, &dbsc3_0->dbpdrgd);

	writel(0x0000000E, &dbsc3_0->dbpdrga);
	writel(0x33C03812, &dbsc3_0->dbpdrgd);

	writel(0x00000003, &dbsc3_0->dbpdrga);
	writel(0x0300C481, &dbsc3_0->dbpdrgd);

	writel(0x00000007, &dbsc3_0->dbkind);
	writel(0x10030A02, &dbsc3_0->dbconf0);
	writel(0x00000001, &dbsc3_0->dbphytype);
	writel(0x00000000, &dbsc3_0->dbbl);
	writel(0x0000000B, &dbsc3_0->dbtr0);
	writel(0x00000008, &dbsc3_0->dbtr1);
	writel(0x00000000, &dbsc3_0->dbtr2);
	writel(0x0000000B, &dbsc3_0->dbtr3);
	writel(0x000C000B, &dbsc3_0->dbtr4);
	writel(0x00000027, &dbsc3_0->dbtr5);
	writel(0x0000001C, &dbsc3_0->dbtr6);
	writel(0x00000005, &dbsc3_0->dbtr7);
	writel(0x00000018, &dbsc3_0->dbtr8);
	writel(0x00000008, &dbsc3_0->dbtr9);
	writel(0x0000000C, &dbsc3_0->dbtr10);
	writel(0x00000009, &dbsc3_0->dbtr11);
	writel(0x00000012, &dbsc3_0->dbtr12);
	writel(0x000000D0, &dbsc3_0->dbtr13);
	writel(0x00140005, &dbsc3_0->dbtr14);
	writel(0x00050004, &dbsc3_0->dbtr15);
	writel(0x70233005, &dbsc3_0->dbtr16);
	writel(0x000C0000, &dbsc3_0->dbtr17);
	writel(0x00000300, &dbsc3_0->dbtr18);
	writel(0x00000040, &dbsc3_0->dbtr19);
	writel(0x00000001, &dbsc3_0->dbrnk0);
	writel(0x00020001, &dbsc3_0->dbadj0);
	writel(0x20082008, &dbsc3_0->dbadj2);
	writel(0x00020002, &dbsc3_0->dbwt0cnf0);
	writel(0x0000000F, &dbsc3_0->dbwt0cnf4);

	writel(0x00000015, &dbsc3_0->dbpdrga);
	writel(0x00000D70, &dbsc3_0->dbpdrgd);

	writel(0x00000016, &dbsc3_0->dbpdrga);
	writel(0x00000006, &dbsc3_0->dbpdrgd);

	writel(0x00000017, &dbsc3_0->dbpdrga);
	writel(0x00000018, &dbsc3_0->dbpdrgd);

	writel(0x00000012, &dbsc3_0->dbpdrga);
	writel(0x9D5CBB66, &dbsc3_0->dbpdrgd);

	writel(0x00000013, &dbsc3_0->dbpdrga);
	writel(0x1A868300, &dbsc3_0->dbpdrgd);

	writel(0x00000023, &dbsc3_0->dbpdrga);
	writel(0x00FDB6C0, &dbsc3_0->dbpdrgd);

	writel(0x00000014, &dbsc3_0->dbpdrga);
	writel(0x300214D8, &dbsc3_0->dbpdrgd);

	writel(0x0000001A, &dbsc3_0->dbpdrga);
	writel(0x930035C7, &dbsc3_0->dbpdrgd);

	writel(0x00000060, &dbsc3_0->dbpdrga);
	writel(0x330657B2, &dbsc3_0->dbpdrgd);

	writel(0x00000011, &dbsc3_0->dbpdrga);
	writel(0x1000040B, &dbsc3_0->dbpdrgd);

	writel(0x0000FA00, &dbsc3_0->dbcmd);
	writel(0x00000001, &dbsc3_0->dbpdrga);
	writel(0x00000071, &dbsc3_0->dbpdrgd);

	writel(0x00000004, &dbsc3_0->dbpdrga);
	while ((readl(&dbsc3_0->dbpdrgd) & 0x00000001) != 0x00000001)
		;

	writel(0x0000FA00, &dbsc3_0->dbcmd);
	writel(0x2100FA00, &dbsc3_0->dbcmd);
	writel(0x0000FA00, &dbsc3_0->dbcmd);
	writel(0x0000FA00, &dbsc3_0->dbcmd);
	writel(0x0000FA00, &dbsc3_0->dbcmd);
	writel(0x0000FA00, &dbsc3_0->dbcmd);
	writel(0x0000FA00, &dbsc3_0->dbcmd);
	writel(0x0000FA00, &dbsc3_0->dbcmd);
	writel(0x0000FA00, &dbsc3_0->dbcmd);

	writel(0x110000DB, &dbsc3_0->dbcmd);

	writel(0x00000001, &dbsc3_0->dbpdrga);
	writel(0x00000181, &dbsc3_0->dbpdrgd);

	writel(0x00000004, &dbsc3_0->dbpdrga);
	while ((readl(&dbsc3_0->dbpdrgd) & 0x00000001) != 0x00000001)
		;

	writel(0x00000001, &dbsc3_0->dbpdrga);
	writel(0x0000FE01, &dbsc3_0->dbpdrgd);

	writel(0x00000004, &dbsc3_0->dbpdrga);
	while ((readl(&dbsc3_0->dbpdrgd) & 0x00000001) != 0x00000001)
		;

	writel(0x00000000, &dbsc3_0->dbbs0cnt1);
	writel(0x01004C20, &dbsc3_0->dbcalcnf);
	writel(0x014000AA, &dbsc3_0->dbcaltr);
	writel(0x00000140, &dbsc3_0->dbrfcnf0);
	writel(0x00081860, &dbsc3_0->dbrfcnf1);
	writel(0x00010000, &dbsc3_0->dbrfcnf2);
	writel(0x00000001, &dbsc3_0->dbrfen);
	writel(0x00000001, &dbsc3_0->dbacen);
#endif /* CONFIG_NORFLASH */
}

#define	MSTPSR1		0xE6150038
#define	SMSTPCR1	0xE6150134
#define TMU0_MSTP125	(1 << 25)

#define	MSTPSR7		0xE61501C4
#define	SMSTPCR7	0xE615014C
#define SCIF0_MSTP721	(1 << 21)

#define	MSTPSR8		0xE61509A0
#define	SMSTPCR8	0xE6150990
#define ETHER_MSTP813	(1 << 13)

int board_early_init_f(void)
{
	u32 val;

	/* TMU0 */
	val = readl(MSTPSR1);
	val &= ~TMU0_MSTP125;
	writel(val, SMSTPCR1);

	/* SCIF0 */
	val = readl(MSTPSR7);
	val &= ~SCIF0_MSTP721;
	writel(val, SMSTPCR7);

	/* ETHER */
	val = readl(MSTPSR8);
	val &= ~ETHER_MSTP813;
	writel(val, SMSTPCR8);

	return 0;
}

static void ether_init(void)
{
	struct r8a7790_gpio *gpio5 = (struct r8a7790_gpio *)GPIO5_BASE;

	writel((readl(&gpio5->posneg) & ~0x80000000), &gpio5->posneg);
	writel((readl(&gpio5->inoutsel) | 0x80000000), &gpio5->inoutsel);

	writel((readl(&gpio5->outdt) & ~0x80000000), &gpio5->outdt);
	mdelay(20);
	writel((readl(&gpio5->outdt) | 0x80000000), &gpio5->outdt);
	udelay(1);
}


DECLARE_GLOBAL_DATA_PTR;
int board_init(void)
{
	/* board id for linux */
	gd->bd->bi_arch_number = MACH_TYPE_LAGER;
	/* adress of boot parameters */
	gd->bd->bi_boot_params = LAGER_SDRAM_BASE + 0x100;

	timer_init();

	ether_init();

	return 0;
}

int dram_init(void)
{
	gd->bd->bi_dram[0].start = CONFIG_SYS_SDRAM_BASE;
	gd->ram_size = CONFIG_SYS_SDRAM_SIZE;

	return 0;
}

const struct rmobile_sysinfo sysinfo = {
	CONFIG_RMOBILE_BOARD_STRING
};

int board_late_init(void)
{
	return 0;
}

void reset_cpu(ulong addr)
{
}
