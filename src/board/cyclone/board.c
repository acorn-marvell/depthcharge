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

#include <arch/io.h>

#include "base/init_funcs.h"
#include "drivers/bus/usb/usb.h"
#include "drivers/storage/blockdev.h"
#include "drivers/bus/i2c/armada38x_i2c.h"
#include "drivers/tpm/tpm.h"
#include "vboot/util/flag.h"
#include "eth.h"
#include "drivers/bus/spi/armada38x_spi.h"
#include "drivers/storage/mtd/mtd.h"
#include "drivers/storage/mtd/nand/armada38x_nand.h"
#include "drivers/storage/mtd/stream.h"


#define  DDR_BASE_CS_LOW_MASK   0xffff0000
#define  DDR_SIZE_MASK          0xffff0000

#define  USB_WIN_ENABLE_BIT     0
#define  USB_WIN_ENABLE_MASK    (1 << USB_WIN_ENABLE_BIT)
#define  USB_WIN_TARGET_OFFSET  4
#define  USB_WIN_TARGET_MASK    (1 << USB_WIN_TARGET_OFFSET)
#define  USB_WIN_ATTR_OFFSET    8
#define  USB_WIN_ATTR_MASK      (1 << USB_WIN_ATTR_OFFSET)
#define  USB_WIN_SIZE_OFFSET    16
#define  USB_WIN_SIZE_MASK      (1 << USB_WIN_SIZE_OFFSET)

#define  USB_REG_BASE                   (void *)0xF1058000
#define  USB_CORE_MODE_REG              (USB_REG_BASE + 0x1A8)
#define  USB_CORE_MODE_OFFSET           0
#define  USB_CORE_MODE_MASK             (3 << USB_CORE_MODE_OFFSET)
#define  USB_CORE_MODE_HOST             (3 << USB_CORE_MODE_OFFSET)
#define  USB_CORE_MODE_DEVICE           (2 << USB_CORE_MODE_OFFSET)
#define  USB_CORE_CMD_REG               (USB_REG_BASE + 0x140)
#define  USB_CORE_CMD_RUN_BIT           0
#define  USB_CORE_CMD_RUN_MASK          (1 << USB_CORE_CMD_RUN_BIT)
#define  USB_CORE_CMD_RESET_BIT         1
#define  USB_CORE_CMD_RESET_MASK        (1 << USB_CORE_CMD_RESET_BIT)
#define  USB_BRIDGE_INTR_CAUSE_REG      (USB_REG_BASE + 0x310)
#define  USB_BRIDGE_INTR_MASK_REG       (USB_REG_BASE + 0x314)
#define  USB_BRIDGE_IPG_REG             (USB_REG_BASE + 0x360)

void *memcpy(void *dst, const void *src, size_t n)
{
	char *dp = dst;
	const char *sp = src;
	while (n--)
		*dp++ = *sp++;
	return dst;
}

static void enable_usb(int target)
{
	u32 baseReg, sizeReg, regVal;
	u32 base, size;
	u8 attr;

	/* setup memory windows */
	sizeReg = readl((void *)(0xF1020184 + target * 0x8));
	baseReg = readl((void *)(0xF1020180 + target * 0x8));
	base = baseReg & DDR_BASE_CS_LOW_MASK;
	size = (sizeReg & DDR_SIZE_MASK) >> 16;
	attr = 0xf & ~(1 << target);

	printf("%s: baseReg 0x%x, sizeReg 0x%x\n", __func__, baseReg, sizeReg);
	printf("%s: base 0x%x, size 0x%x, attr 0x%x\n", __func__, base, size, attr);

	sizeReg = ((target << USB_WIN_TARGET_OFFSET) |
		   (attr << USB_WIN_ATTR_OFFSET) |
		   (size << USB_WIN_SIZE_OFFSET) |
		   USB_WIN_ENABLE_MASK);

	baseReg = base;

	//enable xhci
	writel(sizeReg, (void *)(0xF10FC000 + target * 0x8));
        writel(baseReg, (void *)(0xF10FC004 + target * 0x8));
	//enable ehci
        writel(sizeReg, (void *)(0xF1058320 + target * 0x8));
        writel(baseReg, (void *)(0xF1058324 + target * 0x8));

	/* Wait 100 usec */
	udelay(100);

	/* Clear Interrupt Cause and Mask registers */
	writel(0, USB_BRIDGE_INTR_CAUSE_REG);
	writel(0, USB_BRIDGE_INTR_MASK_REG);

	/* Reset controller */
	regVal = readl(USB_CORE_CMD_REG);
	writel(regVal | USB_CORE_CMD_RESET_MASK, USB_CORE_CMD_REG);

	while (readl(USB_CORE_CMD_REG) & USB_CORE_CMD_RESET_MASK)
		;

	/* Change value of new register 0x360 */
	regVal = readl(USB_BRIDGE_IPG_REG);

	/*  Change bits[14:8] - IPG for non Start of Frame Packets
	 *  from 0x9(default) to 0xD
	 */
	regVal &= ~(0x7F << 8);
	regVal |= (0xD << 8);

	writel(regVal, USB_BRIDGE_IPG_REG);

	/* Set Mode register (Stop and Reset USB Core before) */
	/* Stop the controller */
	regVal = readl(USB_CORE_CMD_REG);
	regVal &= ~USB_CORE_CMD_RUN_MASK;
	writel(regVal, USB_CORE_CMD_REG);

	/* Reset the controller to get default values */
	regVal = readl(USB_CORE_CMD_REG);
	regVal |= USB_CORE_CMD_RESET_MASK;
	writel(regVal, USB_CORE_CMD_REG);

	/* Wait for the controller reset to complete */
	do {
		regVal = readl(USB_CORE_CMD_REG);
	} while (regVal & USB_CORE_CMD_RESET_MASK);

	/* Set USB_MODE register */
	regVal = USB_CORE_MODE_HOST;

	writel(regVal, USB_CORE_MODE_REG);
}

static int board_setup(void)
{
	enable_usb(0);
	mvEnableSwitchDelay(0);

        UsbHostController *usb_host20 = new_usb_hc(EHCI, 0xF1058100);
        list_insert_after(&usb_host20->list_node, &usb_host_controllers);
        UsbHostController *usb_host30 = new_usb_hc(XHCI, 0xF10F8000);
        list_insert_after(&usb_host30->list_node, &usb_host_controllers);

	new_armada38x_i2c(0, 0x4E);

        new_armada38x_nand();

	SpiController *spi = new_spi(1,0);
        flash_set_ops(&new_spi_flash(&spi->ops)->ops);
	
	return 0;
}

int get_mach_id(void)
{
	return 0x6800;
}

INIT_FUNC(board_setup);
