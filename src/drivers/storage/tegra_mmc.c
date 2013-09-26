/*
 * (C) Copyright 2009 SAMSUNG Electronics
 * Minkyu Kang <mk7.kang@samsung.com>
 * Jaehoon Chung <jh80.chung@samsung.com>
 * Portions Copyright 2011-2013 NVIDIA Corporation
 *
 * Copyright 2013 Google Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <assert.h>
#include <libpayload.h>
#include <stddef.h>
#include <stdint.h>

#include "base/time.h"
#include "drivers/storage/tegra_mmc.h"

enum {
	// For card identification, and also the highest low-speed SDOI card
	// frequency (actually 400Khz).
	TegraMmcMinFreq = 375000,

	// Highest HS eMMC clock as per the SD/MMC spec (actually 52MHz).
	TegraMmcMaxFreq = 48000000,

	TegraMmcVoltages = (MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195),

	// MSB of the mmc.voltages. Current value is made by MMC_VDD_33_34.
	TegraMmcVoltagesMsb = 22,
};


static void tegra_pad_init_mmc(TegraMmcHost *host)
{
// TODO(hungte) Implement or move this to Coreboot.
}

static void tegra_mmc_set_power(TegraMmcHost *host, uint16_t power)
{
	uint8_t pwr = 0;
	mmc_debug("%s: power = %x\n", __func__, power);

	if (power != (uint16_t)-1) {
		switch (1 << power) {
		case MMC_VDD_165_195:
			pwr = TEGRA_MMC_PWRCTL_SD_BUS_VOLTAGE_V1_8;
			break;
		case MMC_VDD_29_30:
		case MMC_VDD_30_31:
			pwr = TEGRA_MMC_PWRCTL_SD_BUS_VOLTAGE_V3_0;
			break;
		case MMC_VDD_32_33:
		case MMC_VDD_33_34:
			pwr = TEGRA_MMC_PWRCTL_SD_BUS_VOLTAGE_V3_3;
			break;
		}
	}
	mmc_debug("%s: pwr = %X\n", __func__, pwr);

	// Set the bus voltage first (if any)
	writeb(pwr, &host->reg->pwrcon);
	if (pwr == 0)
		return;

	// Now enable bus power
	pwr |= TEGRA_MMC_PWRCTL_SD_BUS_POWER;
	writeb(pwr, &host->reg->pwrcon);
}

static void tegra_mmc_prepare_data(TegraMmcHost *host, MmcData *data,
				   struct bounce_buffer *bbstate)
{
	uint8_t ctrl;

	mmc_debug("buf: %p (%p), data->blocks: %u, data->blocksize: %u\n",
		bbstate->bounce_buffer, bbstate->user_buffer, data->blocks,
		data->blocksize);

	writel((uint32_t)bbstate->bounce_buffer, &host->reg->sysad);
	/*
	 * DMASEL[4:3]
	 * 00 = Selects SDMA
	 * 01 = Reserved
	 * 10 = Selects 32-bit Address ADMA2
	 * 11 = Selects 64-bit Address ADMA2
	 */
	ctrl = readb(&host->reg->hostctl);
	ctrl &= ~TEGRA_MMC_HOSTCTL_DMASEL_MASK;
	ctrl |= TEGRA_MMC_HOSTCTL_DMASEL_SDMA;
	writeb(ctrl, &host->reg->hostctl);

	// We do not handle DMA boundaries, so set it to max (512 KiB)
	writew((7 << 12) | (data->blocksize & 0xFFF), &host->reg->blksize);
	writew(data->blocks, &host->reg->blkcnt);
}

static void tegra_mmc_set_transfer_mode(TegraMmcHost *host, MmcData *data)
{
	uint16_t mode;
	mmc_debug(" mmc_set_transfer_mode called\n");
	/*
	 * TRNMOD
	 * MUL1SIN0[5]	: Multi/Single Block Select
	 * RD1WT0[4]	: Data Transfer Direction Select
	 *	1 = read
	 *	0 = write
	 * ENACMD12[2]	: Auto CMD12 Enable
	 * ENBLKCNT[1]	: Block Count Enable
	 * ENDMA[0]	: DMA Enable
	 */
	mode = (TEGRA_MMC_TRNMOD_DMA_ENABLE |
		TEGRA_MMC_TRNMOD_BLOCK_COUNT_ENABLE);

	if (data->blocks > 1)
		mode |= TEGRA_MMC_TRNMOD_MULTI_BLOCK_SELECT;

	if (data->flags & MMC_DATA_READ)
		mode |= TEGRA_MMC_TRNMOD_DATA_XFER_DIR_SEL_READ;

	writew(mode, &host->reg->trnmod);
}

static int tegra_mmc_wait_inhibit(TegraMmcHost *host,
				  MmcCommand *cmd,
				  MmcData *data,
				  unsigned int timeout)
{
	/*
	 * PRNSTS
	 * CMDINHDAT[1] : Command Inhibit (DAT)
	 * CMDINHCMD[0] : Command Inhibit (CMD)
	 */
	unsigned int mask = TEGRA_MMC_PRNSTS_CMD_INHIBIT_CMD;

	/*
	 * We shouldn't wait for data inhibit for stop commands, even
	 * though they might use busy signaling
	 */
	if ((data == NULL) && (cmd->resp_type & MMC_RSP_BUSY))
		mask |= TEGRA_MMC_PRNSTS_CMD_INHIBIT_DAT;

	while (readl(&host->reg->prnsts) & mask) {
		if (timeout == 0) {
			mmc_error("%s: timeout error\n", __func__);
			return -1;
		}
		timeout--;
		udelay(1000);
	}

	return 0;
}

static int tegra_mmc_send_cmd_bounced(MmcCtrlr *ctrlr, MmcCommand *cmd,
			MmcData *data, struct bounce_buffer *bbstate)
{
	TegraMmcHost *host = container_of(ctrlr, TegraMmcHost, mmc);
	int flags, i;
	int result;
	unsigned int mask = 0;
	unsigned int retry = 0x100000;
	mmc_debug(" mmc_send_cmd called\n");

	result = tegra_mmc_wait_inhibit(host, cmd, data, 10 /* ms */);

	if (result < 0)
		return result;

	if (data)
		tegra_mmc_prepare_data(host, data, bbstate);

	mmc_debug("cmd->arg: %08x\n", cmd->cmdarg);
	writel(cmd->cmdarg, &host->reg->argument);

	if (data)
		tegra_mmc_set_transfer_mode(host, data);

	if ((cmd->resp_type & MMC_RSP_136) && (cmd->resp_type & MMC_RSP_BUSY))
		return -1;

	/*
	 * CMDREG
	 * CMDIDX[13:8]	: Command index
	 * DATAPRNT[5]	: Data Present Select
	 * ENCMDIDX[4]	: Command Index Check Enable
	 * ENCMDCRC[3]	: Command CRC Check Enable
	 * RSPTYP[1:0]
	 *	00 = No Response
	 *	01 = Length 136
	 *	10 = Length 48
	 *	11 = Length 48 Check busy after response
	 */
	if (!(cmd->resp_type & MMC_RSP_PRESENT))
		flags = TEGRA_MMC_CMDREG_RESP_TYPE_SELECT_NO_RESPONSE;
	else if (cmd->resp_type & MMC_RSP_136)
		flags = TEGRA_MMC_CMDREG_RESP_TYPE_SELECT_LENGTH_136;
	else if (cmd->resp_type & MMC_RSP_BUSY)
		flags = TEGRA_MMC_CMDREG_RESP_TYPE_SELECT_LENGTH_48_BUSY;
	else
		flags = TEGRA_MMC_CMDREG_RESP_TYPE_SELECT_LENGTH_48;

	if (cmd->resp_type & MMC_RSP_CRC)
		flags |= TEGRA_MMC_TRNMOD_CMD_CRC_CHECK;
	if (cmd->resp_type & MMC_RSP_OPCODE)
		flags |= TEGRA_MMC_TRNMOD_CMD_INDEX_CHECK;
	if (data)
		flags |= TEGRA_MMC_TRNMOD_DATA_PRESENT_SELECT_DATA_TRANSFER;

	mmc_debug("cmd: %d\n", cmd->cmdidx);

	writew((cmd->cmdidx << 8) | flags, &host->reg->cmdreg);

	for (i = 0; i < retry; i++) {
		mask = readl(&host->reg->norintsts);
		// Command Complete
		if (mask & TEGRA_MMC_NORINTSTS_CMD_COMPLETE) {
			if (!data)
				writel(mask, &host->reg->norintsts);
			break;
		}
	}

	if (i == retry) {
		mmc_error("%s: waiting for status update\n", __func__);
		writel(mask, &host->reg->norintsts);
		return MMC_TIMEOUT;
	}

	if (mask & TEGRA_MMC_NORINTSTS_CMD_TIMEOUT) {
		// Timeout Error
		mmc_debug("timeout: %08x cmd %d\n", mask, cmd->cmdidx);
		writel(mask, &host->reg->norintsts);
		return MMC_TIMEOUT;
	} else if (mask & TEGRA_MMC_NORINTSTS_ERR_INTERRUPT) {
		// Error Interrupt
		mmc_error("%08x cmd %d\n", mask, cmd->cmdidx);
		writel(mask, &host->reg->norintsts);
		return -1;
	}

	if (cmd->resp_type & MMC_RSP_PRESENT) {
		if (cmd->resp_type & MMC_RSP_136) {
			// CRC is stripped so we need to do some shifting.
			for (i = 0; i < 4; i++) {
				uint32_t *offset = &host->reg->rspreg3 - i;
				cmd->response[i] = readl(offset) << 8;

				if (i != 3) {
					cmd->response[i] |=
						readb((uint8_t *)offset - 1);
				}
				mmc_debug("cmd->resp[%d]: %08x\n",
						i, cmd->response[i]);
			}
		} else if (cmd->resp_type & MMC_RSP_BUSY) {
			for (i = 0; i < retry; i++) {
				// PRNTDATA[23:20] : DAT[3:0] Line Signal
				if (readl(&host->reg->prnsts)
					& (1 << 20))	// DAT[0]
					break;
			}

			if (i == retry) {
				mmc_error("%s: card is still busy\n", __func__);
				writel(mask, &host->reg->norintsts);
				return MMC_TIMEOUT;
			}

			cmd->response[0] = readl(&host->reg->rspreg0);
			mmc_debug("cmd->resp[0]: %08x\n", cmd->response[0]);
		} else {
			cmd->response[0] = readl(&host->reg->rspreg0);
			mmc_debug("cmd->resp[0]: %08x\n", cmd->response[0]);
		}
	}

	if (data) {
		uint64_t start = timer_us(0);

		while (1) {
			mask = readl(&host->reg->norintsts);

			if (mask & TEGRA_MMC_NORINTSTS_ERR_INTERRUPT) {
				// Error Interrupt
				writel(mask, &host->reg->norintsts);
				mmc_error("%s: error during transfer: 0x%08x\n",
						__func__, mask);
				return -1;
			} else if (mask & TEGRA_MMC_NORINTSTS_DMA_INTERRUPT) {
				/*
				 * DMA Interrupt, restart the transfer where
				 * it was interrupted.
				 */
				unsigned int address = readl(&host->reg->sysad);

				mmc_debug("DMA end\n");
				writel(TEGRA_MMC_NORINTSTS_DMA_INTERRUPT,
				       &host->reg->norintsts);
				writel(address, &host->reg->sysad);
			} else if (mask & TEGRA_MMC_NORINTSTS_XFER_COMPLETE) {
				// Transfer Complete
				mmc_debug("r/w is done\n");
				break;
			} else if (timer_us(start) > 2000) {
				writel(mask, &host->reg->norintsts);
				mmc_error("%s: MMC Timeout\n"
				       "    Interrupt status        0x%08x\n"
				       "    Interrupt status enable 0x%08x\n"
				       "    Interrupt signal enable 0x%08x\n"
				       "    Present status          0x%08x\n",
				       __func__, mask,
				       readl(&host->reg->norintstsen),
				       readl(&host->reg->norintsigen),
				       readl(&host->reg->prnsts));
				return -1;
			}
		}
		writel(mask, &host->reg->norintsts);
	}

	udelay(1000);
	return 0;
}

static int tegra_mmc_send_cmd(MmcCtrlr *ctrlr, MmcCommand *cmd, MmcData *data)
{
	void *buf;
	unsigned int bbflags;
	size_t len;
	struct bounce_buffer bbstate;
	int ret;

	if (data) {
		if (data->flags & MMC_DATA_READ) {
			buf = data->dest;
			bbflags = GEN_BB_WRITE;
		} else {
			buf = (void *)data->src;
			bbflags = GEN_BB_READ;
		}
		len = data->blocks * data->blocksize;

		bounce_buffer_start(&bbstate, buf, len, bbflags);
	}

	ret = tegra_mmc_send_cmd_bounced(ctrlr, cmd, data, &bbstate);

	if (data)
		bounce_buffer_stop(&bbstate);

	return ret;
}

static void tegra_mmc_change_clock(TegraMmcHost *host, uint32_t clock)
{
	int div;
	uint16_t clk;
	unsigned long timeout;

	mmc_debug(" mmc_change_clock called\n");

	/*
	 * Change Tegra SDMMCx clock divisor here. Source is PLLP_OUT0
	 */
	if (clock == 0)
		goto out;
	clock_adjust_periph_pll_div(host->mmc_id, CLOCK_ID_PERIPH, clock,
				    &div);
	mmc_debug("div = %d\n", div);

	writew(0, &host->reg->clkcon);

	/*
	 * CLKCON
	 * SELFREQ[15:8]	: base clock divided by value
	 * ENSDCLK[2]		: SD Clock Enable
	 * STBLINTCLK[1]	: Internal Clock Stable
	 * ENINTCLK[0]		: Internal Clock Enable
	 */
	div >>= 1;
	clk = ((div << TEGRA_MMC_CLKCON_SDCLK_FREQ_SEL_SHIFT) |
	       TEGRA_MMC_CLKCON_INTERNAL_CLOCK_ENABLE);
	writew(clk, &host->reg->clkcon);

	// Wait max 10 ms
	timeout = 10;
	while (!(readw(&host->reg->clkcon) &
		 TEGRA_MMC_CLKCON_INTERNAL_CLOCK_STABLE)) {
		if (timeout == 0) {
			mmc_error("%s: timeout error\n", __func__);
			return;
		}
		timeout--;
		udelay(1000);
	}

	clk |= TEGRA_MMC_CLKCON_SD_CLOCK_ENABLE;
	writew(clk, &host->reg->clkcon);

	mmc_debug("mmc_change_clock: clkcon = %08X\n", clk);

out:
	host->clock = clock;
}

static void tegra_mmc_set_ios(MmcCtrlr *ctrlr)
{
	TegraMmcHost *host = container_of(ctrlr, TegraMmcHost, mmc);
	uint8_t ctrl;
	mmc_debug(" mmc_set_ios called\n");

	mmc_debug("bus_width: %x, clock: %d\n", host->mmc.bus_width,
		  host->clock);

	// Change clock first
	tegra_mmc_change_clock(host, host->clock);

	ctrl = readb(&host->reg->hostctl);

	/*
	 * WIDE8[5]
	 * 0 = Depend on WIDE4
	 * 1 = 8-bit mode
	 * WIDE4[1]
	 * 1 = 4-bit mode
	 * 0 = 1-bit mode
	 */
	if (host->mmc.bus_width == 8)
		ctrl |= (1 << 5);
	else if (host->mmc.bus_width == 4)
		ctrl |= (1 << 1);
	else
		ctrl &= ~(1 << 1);

	writeb(ctrl, &host->reg->hostctl);
	mmc_debug("mmc_set_ios: hostctl = %08X\n", ctrl);
}

static void tegra_mmc_reset(TegraMmcHost *host)
{
	unsigned int timeout;
	mmc_debug(" mmc_reset called\n");

	/*
	 * RSTALL[0] : Software reset for all
	 * 1 = reset
	 * 0 = work
	 */
	writeb(TEGRA_MMC_SWRST_SW_RESET_FOR_ALL, &host->reg->swrst);

	host->clock = 0;

	// Wait max 100 ms
	timeout = 100;

	// hw clears the bit when it's done
	while (readb(&host->reg->swrst) & TEGRA_MMC_SWRST_SW_RESET_FOR_ALL) {
		if (timeout == 0) {
			mmc_error("%s: timeout error\n", __func__);
			return;
		}
		timeout--;
		udelay(1000);
	}

	// Set SD bus voltage & enable bus power
	tegra_mmc_set_power(host, TegraMmcVoltagesMsb - 1);
	mmc_debug("%s: power control = %02X, host control = %02X\n", __func__,
		readb(&host->reg->pwrcon), readb(&host->reg->hostctl));

	// Make sure SDIO pads are set up
	tegra_pad_init_mmc(host);
}

static int tegra_mmc_init(BlockDevCtrlrOps *me)
{
	TegraMmcHost *host = container_of(me, TegraMmcHost, mmc.ctrlr.ops);
	unsigned int mask;
	mmc_debug(" mmc_core_init called\n");

	tegra_mmc_reset(host);

	mmc_debug("host version = %x\n", readw(&host->reg->hcver));

	// mask all
	writel(0xffffffff, &host->reg->norintstsen);
	writel(0xffffffff, &host->reg->norintsigen);

	writeb(0xe, &host->reg->timeoutcon);	// TMCLK * 2^27
	/*
	 * NORMAL Interrupt Status Enable Register init
	 * [5] ENSTABUFRDRDY : Buffer Read Ready Status Enable
	 * [4] ENSTABUFWTRDY : Buffer write Ready Status Enable
	 * [3] ENSTADMAINT   : DMA boundary interrupt
	 * [1] ENSTASTANSCMPLT : Transfre Complete Status Enable
	 * [0] ENSTACMDCMPLT : Command Complete Status Enable
	*/
	mask = readl(&host->reg->norintstsen);
	mask &= ~(0xffff);
	mask |= (TEGRA_MMC_NORINTSTSEN_CMD_COMPLETE |
		 TEGRA_MMC_NORINTSTSEN_XFER_COMPLETE |
		 TEGRA_MMC_NORINTSTSEN_DMA_INTERRUPT |
		 TEGRA_MMC_NORINTSTSEN_BUFFER_WRITE_READY |
		 TEGRA_MMC_NORINTSTSEN_BUFFER_READ_READY);
	writel(mask, &host->reg->norintstsen);

	/*
	 * NORMAL Interrupt Signal Enable Register init
	 * [1] ENSTACMDCMPLT : Transfer Complete Signal Enable
	 */
	mask = readl(&host->reg->norintsigen);
	mask &= ~(0xffff);
	mask |= TEGRA_MMC_NORINTSIGEN_XFER_COMPLETE;
	writel(mask, &host->reg->norintsigen);

	return 0;
}

int tegra_mmc_getcd(struct mmc *mmc)
{
	TegraMmcHost *host = (TegraMmcHost *)mmc->priv;

	mmc_debug("tegra_mmc_getcd called\n");

	if (fdt_gpio_isvalid(&host->cd_gpio))
		return fdtdec_get_gpio(&host->cd_gpio);

	return 1;
}

static int do_mmc_init(int dev_index)
{
	TegraMmcHost *host;
	char gpusage[12]; // "SD/MMCn PWR" or "SD/MMCn CD"
	struct mmc *mmc;

	// DT should have been read & host config filled in
	host = &mmc_host[dev_index];
	if (!host->enabled)
		return -1;

	mmc_debug(" do_mmc_init: index %d, bus width %d "
		"pwr_gpio %d cd_gpio %d\n",
		dev_index, host->width,
		host->pwr_gpio.gpio, host->cd_gpio.gpio);

	host->clock = 0;
	clock_start_periph_pll(host->mmc_id, CLOCK_ID_PERIPH, 20000000);

	if (fdt_gpio_isvalid(&host->pwr_gpio)) {
		sprintf(gpusage, "SD/MMC%d PWR", dev_index);
		gpio_request(host->pwr_gpio.gpio, gpusage);
		gpio_direction_output(host->pwr_gpio.gpio, 1);
		mmc_debug(" Power GPIO name = %s\n", host->pwr_gpio.name);
	}

	if (fdt_gpio_isvalid(&host->cd_gpio)) {
		sprintf(gpusage, "SD/MMC%d CD", dev_index);
		gpio_request(host->cd_gpio.gpio, gpusage);
		gpio_direction_input(host->cd_gpio.gpio);
		mmc_debug(" CD GPIO name = %s\n", host->cd_gpio.name);
	}

	mmc = &mmc_dev[dev_index];

	sprintf(mmc->name, "Tegra SD/MMC");
	mmc->priv = host;
	mmc->send_cmd = mmc_send_cmd;
	mmc->set_ios = mmc_set_ios;
	mmc->init = mmc_core_init;
	mmc->getcd = tegra_mmc_getcd;
	mmc->getwp = NULL;

	mmc->voltages = TegraMmcVoltages;
	mmc->host_caps = 0;
	if (host->width == 8)
		mmc->host_caps |= MMC_MODE_8BIT;
	if (host->width >= 4)
		mmc->host_caps |= MMC_MODE_4BIT;
	mmc->host_caps |= MMC_MODE_HS_52MHz | MMC_MODE_HS | MMC_MODE_HC;

	/*
	 * min freq is for card identification, and is the highest
	 *  low-speed SDIO card frequency (actually 400KHz)
	 * max freq is highest HS eMMC clock as per the SD/MMC spec
	 *  (actually 52MHz)
	 */
	mmc->f_min = 375000;
	mmc->f_max = 48000000;

	mmc_register(mmc);

	return 0;
}

/**
 * Get the host address and peripheral ID for a node.
 *
 * @param blob		fdt blob
 * @param node		Device index (0-3)
 * @param host		Structure to fill in (reg, width, mmc_id)
 */
static int mmc_get_config(const void *blob, int node, TegraMmcHost *host)
{
	mmc_debug("%s: node = %d\n", __func__, node);

	host->enabled = fdtdec_get_is_enabled(blob, node);

	host->reg = (struct tegra_mmc *)fdtdec_get_addr(blob, node, "reg");
	if ((fdt_addr_t)host->reg == FDT_ADDR_T_NONE) {
		mmc_debug("%s: no sdmmc base reg info found\n", __func__);
		return -FDT_ERR_NOTFOUND;
	}

	host->mmc_id = clock_decode_periph_id(blob, node);
	if (host->mmc_id == PERIPH_ID_NONE) {
		mmc_debug("%s: could not decode periph id\n", __func__);
		return -FDT_ERR_NOTFOUND;
	}

	/*
	 * NOTE: mmc->bus_width is determined by mmc.c dynamically.
	 * TBD: Override it with this value?
	 */
	host->width = fdtdec_get_int(blob, node, "bus-width", 0);
	if (!host->width)
		mmc_debug("%s: no sdmmc width found\n", __func__);

	// These GPIOs are optional
	fdtdec_decode_gpio(blob, node, "cd-gpios", &host->cd_gpio);
	fdtdec_decode_gpio(blob, node, "wp-gpios", &host->wp_gpio);
	fdtdec_decode_gpio(blob, node, "power-gpios", &host->pwr_gpio);

	mmc_debug("%s: found controller at %p, width = %d, periph_id = %d\n",
		__func__, host->reg, host->width, host->mmc_id);
	return 0;
}

/*
 * Process a list of nodes, adding them to our list of SDMMC ports.
 *
 * @param blob          fdt blob
 * @param node_list     list of nodes to process (any <=0 are ignored)
 * @param count         number of nodes to process
 * @return 0 if ok, -1 on error
 */
static int process_nodes(const void *blob, int node_list[], int count)
{
	TegraMmcHost *host;
	int i, node;
	struct mmc *mmc;

	mmc_debug("%s: count = %d\n", __func__, count);

	// build mmc_host[] for each controller
	for (i = 0; i < count; i++) {
		node = node_list[i];
		if (node <= 0)
			continue;

		host = &mmc_host[i];
		host->id = i;

		if (mmc_get_config(blob, node, host)) {
			mmc_error("%s: failed to decode dev %d\n",	__func__, i);
			return -1;
		}
		do_mmc_init(i);

		/*
		 * The removable flag is always set to 1 by function
		 * mmc_register(). Here we reset this flag based on values
		 * defined in dt.
		 *
		 *  eMMC: 0, SD: 1.
		 */
		mmc = &mmc_dev[i];
		mmc->block_dev.removable =
			fdtdec_get_int(blob, node, "nvidia,removable", 1);
	}
	return 0;
}

void tegra_mmc_init(void)
{
	int node_list[MAX_HOSTS], count;
	const void *blob = gd->fdt_blob;
	mmc_debug("%s entry\n", __func__);

	// See if any Tegra30 MMC controllers are present
	count = fdtdec_find_aliases_for_id(blob, "sdhci",
		COMPAT_NVIDIA_TEGRA30_SDMMC, node_list, MAX_HOSTS);
	mmc_debug("%s: count of T30 sdhci nodes is %d\n", __func__, count);
	if (process_nodes(blob, node_list, count)) {
		mmc_error("%s: Error processing T30 mmc node(s)!\n", __func__);
		return;
	}

	// Now look for any Tegra20 MMC controllers
	count = fdtdec_find_aliases_for_id(blob, "sdhci",
		COMPAT_NVIDIA_TEGRA20_SDMMC, node_list, MAX_HOSTS);
	mmc_debug("%s: count of T20 sdhci nodes is %d\n", __func__, count);
	if (process_nodes(blob, node_list, count)) {
		mmc_error("%s: Error processing T20 mmc node(s)!\n", __func__);
		return;
	}
}
