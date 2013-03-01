/*
 * Copyright 2013 Google Inc.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <libpayload.h>
#include <stdint.h>

#include "drivers/gpio/gpio.h"
#include "drivers/gpio/s5p.h"

typedef struct __attribute__((packed)) S5pGpioRegs
{
	uint32_t con;
	uint32_t dat;
	uint32_t pud;
	uint32_t drv;
	uint32_t conpdn;
	uint32_t pudpdn;
} S5pGpioRegs;

typedef struct S5pGpioBank
{
	int num_bits;
	S5pGpioRegs *regs;
} S5pGpioBank;

typedef struct S5pGpioGroup
{
	int num_banks;
	S5pGpioBank *banks[8];
} S5pGpioGroup;

static S5pGpioBank bank_a0 = { 8, (void *)(uintptr_t)0x11400000 };
static S5pGpioBank bank_a1 = { 6, (void *)(uintptr_t)0x11400020 };
static S5pGpioBank bank_a2 = { 8, (void *)(uintptr_t)0x11400040 };

static S5pGpioBank bank_b0 = { 5, (void *)(uintptr_t)0x11400060 };
static S5pGpioBank bank_b1 = { 5, (void *)(uintptr_t)0x11400080 };
static S5pGpioBank bank_b2 = { 4, (void *)(uintptr_t)0x114000a0 };
static S5pGpioBank bank_b3 = { 4, (void *)(uintptr_t)0x114000c0 };

static S5pGpioBank bank_c0 = { 7, (void *)(uintptr_t)0x114000e0 };
static S5pGpioBank bank_c1 = { 4, (void *)(uintptr_t)0x11400100 };
static S5pGpioBank bank_c2 = { 7, (void *)(uintptr_t)0x11400120 };
static S5pGpioBank bank_c3 = { 7, (void *)(uintptr_t)0x11400140 };
static S5pGpioBank bank_c4 = { 7, (void *)(uintptr_t)0x114002e0 };

static S5pGpioBank bank_d0 = { 4, (void *)(uintptr_t)0x11400160 };
static S5pGpioBank bank_d1 = { 8, (void *)(uintptr_t)0x11400180 };

static S5pGpioBank bank_e0 = { 8, (void *)(uintptr_t)0x13400000 };
static S5pGpioBank bank_e1 = { 2, (void *)(uintptr_t)0x13400020 };

static S5pGpioBank bank_f0 = { 4, (void *)(uintptr_t)0x13400040 };
static S5pGpioBank bank_f1 = { 4, (void *)(uintptr_t)0x13400060 };

static S5pGpioBank bank_g0 = { 8, (void *)(uintptr_t)0x13400080 };
static S5pGpioBank bank_g1 = { 8, (void *)(uintptr_t)0x134000a0 };
static S5pGpioBank bank_g2 = { 2, (void *)(uintptr_t)0x134000c0 };

static S5pGpioBank bank_h0 = { 4, (void *)(uintptr_t)0x134000e0 };
static S5pGpioBank bank_h1 = { 8, (void *)(uintptr_t)0x13400100 };

static S5pGpioBank bank_v0 = { 8, (void *)(uintptr_t)0x10d10000 };
static S5pGpioBank bank_v1 = { 8, (void *)(uintptr_t)0x10d10020 };
static S5pGpioBank bank_v2 = { 8, (void *)(uintptr_t)0x10d10060 };
static S5pGpioBank bank_v3 = { 8, (void *)(uintptr_t)0x10d10080 };
static S5pGpioBank bank_v4 = { 2, (void *)(uintptr_t)0x10d100c0 };

static S5pGpioBank bank_x0 = { 8, (void *)(uintptr_t)0x11400c00 };
static S5pGpioBank bank_x1 = { 8, (void *)(uintptr_t)0x11400c20 };
static S5pGpioBank bank_x2 = { 8, (void *)(uintptr_t)0x11400c40 };
static S5pGpioBank bank_x3 = { 8, (void *)(uintptr_t)0x11400c60 };

static S5pGpioBank bank_y0 = { 6, (void *)(uintptr_t)0x114001a0 };
static S5pGpioBank bank_y1 = { 4, (void *)(uintptr_t)0x114001c0 };
static S5pGpioBank bank_y2 = { 6, (void *)(uintptr_t)0x114001e0 };
static S5pGpioBank bank_y3 = { 8, (void *)(uintptr_t)0x11400200 };
static S5pGpioBank bank_y4 = { 8, (void *)(uintptr_t)0x11400220 };
static S5pGpioBank bank_y5 = { 8, (void *)(uintptr_t)0x11400240 };
static S5pGpioBank bank_y6 = { 8, (void *)(uintptr_t)0x11400260 };

static S5pGpioBank bank_z =  { 7, (void *)(uintptr_t)0x03860000 };

S5pGpioGroup groups[] = {
	[GPIO_A] = { 3, { &bank_a0, &bank_a1, &bank_a2 }},
	[GPIO_B] = { 4, { &bank_b0, &bank_b1, &bank_b2, &bank_b3 }},
	[GPIO_C] = { 5, { &bank_c0, &bank_c1, &bank_c2, &bank_c3, &bank_c4 }},
	[GPIO_D] = { 2, { &bank_d0, &bank_d1 }},
	[GPIO_E] = { 2, { &bank_e0, &bank_e1 }},
	[GPIO_F] = { 2, { &bank_f0, &bank_f1 }},
	[GPIO_G] = { 3, { &bank_g0, &bank_g1, &bank_g2 }},
	[GPIO_H] = { 2, { &bank_h0, &bank_h1, }},
	[GPIO_V] = { 5, { &bank_v0, &bank_v1, &bank_v2, &bank_v3, &bank_v4 }},
	[GPIO_X] = { 4, { &bank_x0, &bank_x1, &bank_x2, &bank_x3 }},
	[GPIO_Y] = { 7, { &bank_y0, &bank_y1, &bank_y2, &bank_y3, &bank_y4,
			  &bank_y5, &bank_y6 }},
	[GPIO_Z] = { 1, { &bank_z }}
};

static S5pGpioRegs *s5p_get_regs(unsigned num)
{
	unsigned group_num = s5p_gpio_group(num);
	if (group_num >= ARRAY_SIZE(groups))
		return NULL;
	S5pGpioGroup *group = &groups[group_num];

	unsigned bank_num = s5p_gpio_bank(num);
	if (bank_num >= group->num_banks)
		return NULL;
	S5pGpioBank *bank = group->banks[bank_num];

	unsigned bit_num = s5p_gpio_bit(num);
	if (bit_num >= bank->num_bits)
		return NULL;

	return bank->regs;
}

int gpio_use(unsigned num, unsigned use)
{
	S5pGpioRegs *regs = s5p_get_regs(num);
	if (!regs)
		return -1;

	unsigned bit_num = s5p_gpio_bit(num);

	uint32_t con = readl(&regs->con);
	con &= ~(0xf << (bit_num * 4));
	con |= ((use & 0xf) << (bit_num * 4));
	writel(con, &regs->con);

	return 0;
}

int gpio_direction(unsigned num, unsigned input)
{
	if (input)
		return gpio_use(num, 0);
	else
		return gpio_use(num, 1);
}

int gpio_get_value(unsigned num)
{
	S5pGpioRegs *regs = s5p_get_regs(num);
	if (!regs)
		return -1;

	unsigned bit_num = s5p_gpio_bit(num);
	uint32_t dat = readl(&regs->dat);
	return (dat >> bit_num) & 0x1;
}

int gpio_set_value(unsigned num, unsigned value)
{
	S5pGpioRegs *regs = s5p_get_regs(num);
	if (!regs)
		return -1;

	unsigned bit_num = s5p_gpio_bit(num);
	uint32_t dat = readl(&regs->dat);
	dat &= ~(0x1 << bit_num);
	dat |= ((value & 0x1) << bit_num);
	writel(dat, &regs->dat);
	return 0;
}

int s5p_gpio_set_pud(unsigned num, unsigned value)
{
	S5pGpioRegs *regs = s5p_get_regs(num);
	if (!regs)
		return -1;

	unsigned bit_num = s5p_gpio_bit(num);

	uint32_t pud = readl(&regs->pud);
	pud &= ~(0x3 << (bit_num * 2));
	pud |= ((value & 0x3) << (bit_num * 2));
	writel(pud, &regs->pud);

	return 0;
}