/*
 * Copyright 2008, Freescale Semiconductor, Inc
 * Andy Fleming
 *
 * Copyright 2013 Google Inc.  All rights reserved.
 *
 * Based vaguely on the Linux code
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
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <assert.h>
#include <endian.h>
#include <libpayload.h>
#include <stdint.h>

#include "config.h"
#include "drivers/storage/mmc.h"
typedef unsigned int uint;

/* Set block count limit because of 16 bit register limit on some hardware*/
#ifndef CONFIG_SYS_MMC_MAX_BLK_COUNT
#define CONFIG_SYS_MMC_MAX_BLK_COUNT 65535
#endif

/* Set to 1 to turn on debug messages. */
int __mmc_debug = 0;
int __mmc_trace = 0;

int mmc_busy_wait_io(volatile uint32_t *address, uint32_t *output,
		     uint32_t io_mask, uint32_t timeout_ms)
{
	uint32_t value = (uint32_t)-1;
	uint64_t start = timer_us(0);

	if (!output)
		output = &value;
	for (; *output & io_mask; *output = readl(address)) {
		if (timer_us(start) > timeout_ms * 1000)
			return -1;
	}
	return 0;
}

int mmc_busy_wait_io_until(volatile uint32_t *address, uint32_t *output,
			   uint32_t io_mask, uint32_t timeout_ms)
{
	uint32_t value = 0;
	uint64_t start = timer_us(0);

	if (!output)
		output = &value;
	for (; !(*output & io_mask); *output = readl(address)) {
		if (timer_us(start) > timeout_ms * 1000)
			return -1;
	}
	return 0;
}

static int mmc_send_cmd(MmcCtrlr *ctrlr, MmcCommand *cmd, MmcData *data)
{
	int ret = -1, retries = 2;

	mmc_trace("CMD_SEND:%d %p\n", cmd->cmdidx, ctrlr);
	mmc_trace("\tARG\t\t\t %#8.8x\n", cmd->cmdarg);
	mmc_trace("\tFLAG\t\t\t %d\n", cmd->flags);
	if (data) {
		mmc_trace("\t%s %d block(s) of %d bytes (%p)\n",
			  data->flags == MMC_DATA_READ ? "READ" : "WRITE",
			  data->blocks,
			  data->blocksize,
			  data->dest);
	}

	while (retries--) {
		ret = ctrlr->send_cmd(ctrlr, cmd, data);

		switch (cmd->resp_type) {
		case MMC_RSP_NONE:
			mmc_trace("\tMMC_RSP_NONE\n");
			break;

		case MMC_RSP_R1:
			mmc_trace("\tMMC_RSP_R1,5,6,7 \t %#8.8x\n",
				  cmd->response[0]);
			break;

		case MMC_RSP_R1b:
			mmc_trace("\tMMC_RSP_R1b\t\t %#8.8x\n",
				  cmd->response[0]);
			break;

		case MMC_RSP_R2:
			mmc_trace("\tMMC_RSP_R2\t\t %#8.8x\n",
				  cmd->response[0]);
			mmc_trace("\t          \t\t %#8.8x\n",
				  cmd->response[1]);
			mmc_trace("\t          \t\t %#8.8x\n",
				  cmd->response[2]);
			mmc_trace("\t          \t\t %#8.8x\n",
				  cmd->response[3]);
			break;

		case MMC_RSP_R3:
			mmc_trace("\tMMC_RSP_R3,4\t\t %#8.8x\n",
				  cmd->response[0]);
			break;

		default:
			mmc_trace("\tERROR MMC rsp not supported\n");
			break;
		}
		mmc_trace("\trv:\t\t\t %d\n", ret);

		/* Retry failed data commands, bail out otherwise.  */
		if (!data || !ret)
			break;
	}
	return ret;
}

static int mmc_send_status(MmcMedia *media, int tries)
{
	MmcCommand cmd;
	cmd.cmdidx = MMC_CMD_SEND_STATUS;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = media->rca << 16;
	cmd.flags = 0;

	while (tries--) {
		int err = mmc_send_cmd(media->ctrlr, &cmd, NULL);
		if (err)
			return err;
		else if (cmd.response[0] & MMC_STATUS_RDY_FOR_DATA)
			break;
		else if (cmd.response[0] & MMC_STATUS_MASK) {
			mmc_error("Status Error: %#8.8x\n", cmd.response[0]);
			return MMC_COMM_ERR;
		}

		udelay(100);
	}

	mmc_trace("CURR STATE:%d\n",
		  (cmd.response[0] & MMC_STATUS_CURR_STATE) >> 9);

	if (tries < 0) {
		mmc_error("Timeout waiting card ready\n");
		return MMC_TIMEOUT;
	}
	return 0;
}
static int mmc_set_capacity(struct MmcMedia *mmc, int part_num)
{
	switch (part_num) {
	case 0:
		mmc->capacity = mmc->capacity_user;
		break;
	case 1:
	case 2:
		mmc->capacity = mmc->capacity_boot;
		break;
	case 3:
		mmc->capacity = mmc->capacity_rpmb;
		break;
	case 4:
	case 5:
	case 6:
	case 7:
		mmc->capacity = mmc->capacity_gp[part_num - 4];
		break;
	default:
		return -1;
	}

	mmc->dev.block_count = mmc->capacity / mmc->read_bl_len;
	return 0;
}
static int mmc_set_blocklen(MmcCtrlr *ctrlr, int len)
{
	MmcCommand cmd;
	cmd.cmdidx = MMC_CMD_SET_BLOCKLEN;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = len;
	cmd.flags = 0;

	return mmc_send_cmd(ctrlr, &cmd, NULL);
}

static uint32_t mmc_write(MmcMedia *media, uint32_t start, lba_t block_count,
			  const void *src)
{
	MmcCommand cmd;
	cmd.resp_type = MMC_RSP_R1;
	cmd.flags = 0;

	if (block_count > 1)
		cmd.cmdidx = MMC_CMD_WRITE_MULTIPLE_BLOCK;
	else
		cmd.cmdidx = MMC_CMD_WRITE_SINGLE_BLOCK;

	if (media->high_capacity)
		cmd.cmdarg = start;
	else
		cmd.cmdarg = start * media->write_bl_len;

	MmcData data;
	data.src = src;
	data.blocks = block_count;
	data.blocksize = media->write_bl_len;
	data.flags = MMC_DATA_WRITE;

	if (mmc_send_cmd(media->ctrlr, &cmd, &data)) {
		mmc_error("mmc write failed\n");
		return 0;
	}

	/* SPI multiblock writes terminate using a special
	 * token, not a STOP_TRANSMISSION request.
	 */
	if ((block_count > 1) /*&& !(media->ctrlr->caps & MMC_AUTO_CMD12)*/) {
		udelay(100);
		cmd.cmdidx = MMC_CMD_STOP_TRANSMISSION;
		cmd.cmdarg = 0;
		cmd.resp_type = MMC_RSP_R1b;
		cmd.flags = 0;
		if (mmc_send_cmd(media->ctrlr, &cmd, NULL)) {
			mmc_error("mmc fail to send stop cmd\n");
			return 0;
		}

		/* Waiting for the ready status */
		mmc_send_status(media, MMC_IO_RETRIES);
	}

	return block_count;
}

static int mmc_read(MmcMedia *media, void *dest, uint32_t start,
		    lba_t block_count)
{

	MmcCommand cmd;
	cmd.resp_type = MMC_RSP_R1;
	cmd.flags = 0;

	if (block_count > 1)
		cmd.cmdidx = MMC_CMD_READ_MULTIPLE_BLOCK;
	else
		cmd.cmdidx = MMC_CMD_READ_SINGLE_BLOCK;

	if (media->high_capacity)
		cmd.cmdarg = start;
	else
		cmd.cmdarg = start * media->read_bl_len;

	MmcData data;
	data.dest = dest;
	data.blocks = block_count;
	data.blocksize = media->read_bl_len;
	data.flags = MMC_DATA_READ;

	if (mmc_send_cmd(media->ctrlr, &cmd, &data))
		return 0;

	if ((block_count > 1) /*&& !(media->ctrlr->caps & MMC_AUTO_CMD12)*/) {
		udelay(100);
		cmd.cmdidx = MMC_CMD_STOP_TRANSMISSION;
		cmd.cmdarg = 0;
		cmd.resp_type = MMC_RSP_R1b;
		cmd.flags = 0;
		if (mmc_send_cmd(media->ctrlr, &cmd, NULL)) {
			mmc_error("mmc fail to send stop cmd\n");
			return 0;
		}

		/* Waiting for the ready status */
		mmc_send_status(media, MMC_IO_RETRIES);
	}

	return block_count;
}

static int mmc_go_idle(MmcMedia *media)
{
 	mdelay(1);

	MmcCommand cmd;
	cmd.cmdidx = MMC_CMD_GO_IDLE_STATE;
	cmd.cmdarg = 0;
	cmd.resp_type = MMC_RSP_NONE;
	cmd.flags = 0;

	int err = mmc_send_cmd(media->ctrlr, &cmd, NULL);
	if (err)
		return err;

	// Some cards need more than half second to respond to next command (ex,
	// SEND_OP_COND).
	mdelay(2);

	return 0;
}

static int sd_send_op_cond(MmcMedia *media)
{
	int err;
	MmcCommand cmd;

	int tries = MMC_IO_RETRIES;
	while (tries--) {
		cmd.cmdidx = MMC_CMD_APP_CMD;
		cmd.resp_type = MMC_RSP_R1;
		cmd.cmdarg = 0;
		cmd.flags = 0;

		err = mmc_send_cmd(media->ctrlr, &cmd, NULL);
		if (err)
			return err;

		cmd.cmdidx = SD_CMD_APP_SEND_OP_COND;
		cmd.resp_type = MMC_RSP_R3;

		/*
		 * Most cards do not answer if some reserved bits
		 * in the ocr are set. However, Some controller
		 * can set bit 7 (reserved for low voltages), but
		 * how to manage low voltages SD card is not yet
		 * specified.
		 */
		cmd.cmdarg = (media->ctrlr->voltages & 0xff8000);

		if (media->version == SD_VERSION_2)
			cmd.cmdarg |= OCR_HCS;

		err = mmc_send_cmd(media->ctrlr, &cmd, NULL);
		if (err)
			return err;

		// OCR_BUSY means "initialization complete".
		if (cmd.response[0] & OCR_BUSY)
			break;

		udelay(100);
	}
	if (tries < 0)
		return MMC_UNUSABLE_ERR;

	if (media->version != SD_VERSION_2)
		media->version = SD_VERSION_1_0;

	media->ocr = cmd.response[0];
	media->high_capacity = ((media->ocr & OCR_HCS) == OCR_HCS);
	media->rca = 0;
	return 0;
}
/* We pass in the cmd since otherwise the init seems to fail */
static int mmc_send_op_cond_iter(MmcMedia *mmc, MmcCommand *cmd, int use_arg)
{
    	int err;

	cmd->cmdidx = MMC_CMD_SEND_OP_COND;
	cmd->resp_type = MMC_RSP_R3;
	cmd->cmdarg = 0;
	if (use_arg) {
		cmd->cmdarg =
			(mmc->ctrlr->voltages &
			(mmc->op_cond_response & OCR_VOLTAGE_MASK)) |
			(mmc->op_cond_response & OCR_ACCESS_MODE);
		if (mmc->ctrlr->caps & MMC_MODE_HC)
			cmd->cmdarg |= OCR_HCS;
	}
	
	err = mmc_send_cmd(mmc->ctrlr, cmd, NULL);
	
	if (err)
		return err;
	
	mmc->op_cond_response = cmd->response[0];
	
	return 0;
}
int mmc_send_op_cond(struct MmcMedia *mmc)
{
    	struct MmcCommand cmd; 
	int err, i;
	/* Some cards seem to need this */
	mmc_go_idle(mmc);
	/* Asking to the card its capabilities */
	mmc->op_cond_pending = 1; 
	for (i = 0; i < 2; i++) {
		err = mmc_send_op_cond_iter(mmc, &cmd, i != 0);
		if (err)
			return err; 

		/* exit if not busy (flag seems to be inverted) and it is not the first command*/
		if (i && mmc->op_cond_response & OCR_BUSY) {
			mmc->op_cond_pending = 0; /* op_cond compilted */
			return 0;
		}    
		udelay(1000);                                                                                                    
	}    
	return MMC_IN_PROGRESS;
}

static int mmc_complete_op_cond(MmcMedia *media)
{
	MmcCommand cmd;
	int timeout = MMC_INIT_TIMEOUT_US;
	uint64_t start;
	media->op_cond_pending = 0;
	start = timer_us(0);

	while (1) {
		// CMD1 queries whether initialization is done.
		int err = mmc_send_op_cond_iter(media, &cmd, 1);
		if (err)
			return err;
		// OCR_BUSY means "initialization complete".
		if (media->op_cond_response & OCR_BUSY)
			break;

		// Check if init timeout has expired.
		if (timer_us(start) > timeout)
			return MMC_UNUSABLE_ERR;

		udelay(100);
	}

	media->version = MMC_VERSION_UNKNOWN;
	media->ocr = cmd.response[0];

	media->high_capacity = ((media->ocr & OCR_HCS) == OCR_HCS);
	media->rca = 0;
	return 0;
}

static int mmc_send_ext_csd(MmcCtrlr *ctrlr, unsigned char *ext_csd)
{
	int rv;
	/* Get the Card Status Register */
	MmcCommand cmd;
	cmd.cmdidx = MMC_CMD_SEND_EXT_CSD;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = 0;
	cmd.flags = 0;

	MmcData data;
	data.dest = (char *)ext_csd;
	data.blocks = 1;
	data.blocksize = 512;
	data.flags = MMC_DATA_READ;

	rv = mmc_send_cmd(ctrlr, &cmd, &data);

	if (!rv && __mmc_trace) {
		int i, size;

		size = data.blocks * data.blocksize;
		mmc_trace("\t%p ext_csd:", ctrlr);
		for (i = 0; i < size; i++) {
			if (!(i % 32))
			    printf("\n");
			printf(" %2.2x", ext_csd[i]);
		}
		printf("\n");
	}
	return rv;
}

static int mmc_switch(MmcMedia *media, uint8_t set, uint8_t index,
		      uint8_t value)
{
	MmcCommand cmd;
	cmd.cmdidx = MMC_CMD_SWITCH;
	cmd.resp_type = MMC_RSP_R1b;
	cmd.cmdarg = ((MMC_SWITCH_MODE_WRITE_BYTE << 24) |
			   (index << 16) | (value << 8));
	cmd.flags = 0;

	int ret = mmc_send_cmd(media->ctrlr, &cmd, NULL);

	/* Waiting for the ready status */
	mmc_send_status(media, MMC_IO_RETRIES);
	return ret;

}

static void mmc_set_bus_width(MmcCtrlr *ctrlr, uint32_t width)
{
	ctrlr->bus_width = width;
	ctrlr->set_ios(ctrlr);
}

static int mmc_change_freq(MmcMedia *media)
{
	char cardtype;
	int err;
	ALLOC_CACHE_ALIGN_BUFFER(unsigned char, ext_csd, 512);

	media->caps = 0;

	/* Only version 4 supports high-speed */
	if (media->version < MMC_VERSION_4)
		return 0;

	err = mmc_send_ext_csd(media->ctrlr, ext_csd);
	if (err)
		return err;

		cardtype = ext_csd[EXT_CSD_CARD_TYPE] & 0xf;

		err = mmc_switch(media, EXT_CSD_CMD_SET_NORMAL,
			 EXT_CSD_HS_TIMING, 1);

	if (err)
		return err;

	/* Now check to see that it worked */
	err = mmc_send_ext_csd(media->ctrlr, ext_csd);
	if (err)
		return err;

	/* No high-speed support */
	if (!ext_csd[EXT_CSD_HS_TIMING])
		return 0;

        /* High Speed is set, there are two types: 52MHz and 26MHz */
        if (cardtype & MMC_HS_52MHZ)
                media->caps |= MMC_MODE_HS_52MHz | MMC_MODE_HS;
        else
                media->caps |= MMC_MODE_HS;
	return 0;
}

static void mmc_set_clock(MmcCtrlr *ctrlr, uint32_t clock)
{
	clock = MIN(clock, ctrlr->f_max);
	clock = MAX(clock, ctrlr->f_min);

	ctrlr->bus_hz = clock;
	ctrlr->set_ios(ctrlr);
}

static const int fbase[] = {
	10000,
	100000,
	1000000,
	10000000,
};

/* Multiplier values for TRAN_SPEED. Multiplied by 10 to be nice
* to platforms without floating point. */
static const int multipliers[] = {
	0,  // reserved
	10,
	12,
	13,
	15,
	20,
	25,
	30,
	35,
	40,
	45,
	50,
	55,
	60,
	70,
	80,
};

static int mmc_startup(struct MmcMedia *mmc)
{
	int err, i;
	uint mult, freq;
	u64 cmult, csize, capacity;
	struct MmcCommand cmd;
	//ALLOC_CACHE_ALIGN_BUFFER(u8, ext_csd, MMC_MAX_BLOCK_LEN);
	//ALLOC_CACHE_ALIGN_BUFFER(u8, test_csd, MMC_MAX_BLOCK_LEN);
	ALLOC_CACHE_ALIGN_BUFFER(unsigned char, ext_csd, EXT_CSD_SIZE);
	ALLOC_CACHE_ALIGN_BUFFER(unsigned char, test_csd, EXT_CSD_SIZE);
	int timeout = 1000;

#ifdef CONFIG_MMC_SPI_CRC_ON
	if (mmc_host_is_spi(mmc)) { /* enable CRC check for spi */
		cmd.cmdidx = MMC_CMD_SPI_CRC_ON_OFF;
		cmd.resp_type = MMC_RSP_R1;
		cmd.cmdarg = 1;
		err = mmc_send_cmd(mmc, &cmd, NULL);

		if (err)
			return err;
	}
#endif

	cmd.cmdidx = MMC_CMD_ALL_SEND_CID;
	cmd.resp_type = MMC_RSP_R2;
	cmd.cmdarg = 0;
	
	err = mmc_send_cmd(mmc->ctrlr, &cmd, NULL);

	if (err)
		return err;

	memcpy(mmc->cid, cmd.response, 16);

	/*
	 * For MMC cards, set the Relative Address.
	 * For SD cards, get the Relatvie Address.
	 * This also puts the cards into Standby State
	 */
	cmd.cmdidx = SD_CMD_SEND_RELATIVE_ADDR;
	cmd.cmdarg = mmc->rca << 16;
	cmd.resp_type = MMC_RSP_R6;

	err = mmc_send_cmd(mmc->ctrlr, &cmd, NULL);

	if (err)
		return err;

	if (IS_SD(mmc))
		mmc->rca = (cmd.response[0] >> 16) & 0xffff;

	/* Get the Card-Specific Data */
	cmd.cmdidx = MMC_CMD_SEND_CSD;
	cmd.resp_type = MMC_RSP_R2;
	cmd.cmdarg = mmc->rca << 16;

	err = mmc_send_cmd(mmc->ctrlr, &cmd, NULL);

	/* Waiting for the ready status */
	mmc_send_status(mmc, timeout);

	if (err)
		return err;

	mmc->csd[0] = cmd.response[0];
	mmc->csd[1] = cmd.response[1];
	mmc->csd[2] = cmd.response[2];
	mmc->csd[3] = cmd.response[3];

	if (mmc->version == MMC_VERSION_UNKNOWN) {
		int version = (cmd.response[0] >> 26) & 0xf;

		switch (version) {
			case 0:
				mmc->version = MMC_VERSION_1_2;
				break;
			case 1:
				mmc->version = MMC_VERSION_1_4;
				break;
			case 2:
				mmc->version = MMC_VERSION_2_2;
				break;
			case 3:
				mmc->version = MMC_VERSION_3;
				break;
			case 4:
				mmc->version = MMC_VERSION_4;
				break;
			default:
				mmc->version = MMC_VERSION_1_2;
				break;
		}
	}

	/* divide frequency by 10, since the mults are 10x bigger */
	freq = fbase[(cmd.response[0] & 0x7)];
	mult = multipliers[((cmd.response[0] >> 3) & 0xf)];

	mmc->tran_speed = freq * mult;

	mmc->dsr_imp = ((cmd.response[1] >> 12) & 0x1);
	mmc->read_bl_len = 1 << ((cmd.response[1] >> 16) & 0xf);

	if (IS_SD(mmc))
		mmc->write_bl_len = mmc->read_bl_len;
	else
		mmc->write_bl_len = 1 << ((cmd.response[3] >> 22) & 0xf);

	if (mmc->high_capacity) {
		csize = (mmc->csd[1] & 0x3f) << 16
			| (mmc->csd[2] & 0xffff0000) >> 16;
		cmult = 8;
	} else {
		csize = (mmc->csd[1] & 0x3ff) << 2
			| (mmc->csd[2] & 0xc0000000) >> 30;
		cmult = (mmc->csd[2] & 0x00038000) >> 15;
	}

	mmc->capacity_user = (csize + 1) << (cmult + 2);
	mmc->capacity_user *= mmc->read_bl_len;
	mmc->capacity_boot = 0;
	mmc->capacity_rpmb = 0;
	for (i = 0; i < 4; i++)
		mmc->capacity_gp[i] = 0;

	if (mmc->read_bl_len > MMC_MAX_BLOCK_LEN)
		mmc->read_bl_len = MMC_MAX_BLOCK_LEN;

	if (mmc->write_bl_len > MMC_MAX_BLOCK_LEN)
		mmc->write_bl_len = MMC_MAX_BLOCK_LEN;

	if ((mmc->dsr_imp) && (0xffffffff != mmc->dsr)) {
		cmd.cmdidx = MMC_CMD_SET_DSR;
		cmd.cmdarg = (mmc->dsr & 0xffff) << 16;
		cmd.resp_type = MMC_RSP_NONE;
		if (mmc_send_cmd(mmc->ctrlr, &cmd, NULL))
			printf("MMC: SET_DSR failed\n");
	}
	/* Select the card, and put it into Transfer Mode */
	cmd.cmdidx = MMC_CMD_SELECT_CARD;
	cmd.resp_type = MMC_RSP_R1;
	cmd.cmdarg = mmc->rca << 16;
	err = mmc_send_cmd(mmc->ctrlr, &cmd, NULL);

	if (err)
		return err;

	/*
	 * For SD, its erase group is always one sector
	 */
	mmc->erase_grp_size = 1;
	mmc->part_config = MMCPART_NOAVAILABLE;
	if ((mmc->version >= MMC_VERSION_4)) {
		/* check  ext_csd version and capacity */
		err = mmc_send_ext_csd(mmc->ctrlr, ext_csd);
		if (!err && (ext_csd[EXT_CSD_REV] >= 2)) {
			/*
			 * According to the JEDEC Standard, the value of
			 * ext_csd's capacity is valid if the value is more
			 * than 2GB
			 */
			capacity = ext_csd[EXT_CSD_SEC_CNT] << 0
					| ext_csd[EXT_CSD_SEC_CNT + 1] << 8
					| ext_csd[EXT_CSD_SEC_CNT + 2] << 16
					| ext_csd[EXT_CSD_SEC_CNT + 3] << 24;
			capacity *= MMC_MAX_BLOCK_LEN;
			if ((capacity >> 20) > 2 * 1024)
				mmc->capacity_user = capacity;
		}

		switch (ext_csd[EXT_CSD_REV]) {
		case 1:
			mmc->version = MMC_VERSION_4_1;
			break;
		case 2:
			mmc->version = MMC_VERSION_4_2;
			break;
		case 3:
			mmc->version = MMC_VERSION_4_3;
			break;
		case 5:
			mmc->version = MMC_VERSION_4_41;
			break;
		case 6:
			mmc->version = MMC_VERSION_4_5;
			break;
		}

		/*
		 * Host needs to enable ERASE_GRP_DEF bit if device is
		 * partitioned. This bit will be lost every time after a reset
		 * or power off. This will affect erase size.
		 */
		 #define PART_SUPPORT		(0x1)
		 #define PART_ENH_ATTRIB		(0x1f)
		if ((ext_csd[EXT_CSD_PARTITIONING_SUPPORT] & PART_SUPPORT) &&
		    (ext_csd[EXT_CSD_PARTITIONS_ATTRIBUTE] & PART_ENH_ATTRIB)) {
			err = mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL,
				EXT_CSD_ERASE_GROUP_DEF, 1);

			if (err)
				return err;

			/* Read out group size from ext_csd */
			mmc->erase_grp_size =
				ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE] *
					MMC_MAX_BLOCK_LEN * 1024;
		} else {
			/* Calculate the group size from the csd value. */
			int erase_gsz, erase_gmul;
			erase_gsz = (mmc->csd[2] & 0x00007c00) >> 10;
			erase_gmul = (mmc->csd[2] & 0x000003e0) >> 5;
			mmc->erase_grp_size = (erase_gsz + 1)
				* (erase_gmul + 1);
		}

		/* store the partition info of emmc */
		if ((ext_csd[EXT_CSD_PARTITIONING_SUPPORT] & PART_SUPPORT) ||
		    ext_csd[EXT_CSD_BOOT_MULT])
			mmc->part_config = ext_csd[EXT_CSD_PART_CONF];
		
		mmc->capacity_boot = ext_csd[EXT_CSD_BOOT_MULT] << 17;

		mmc->capacity_rpmb = ext_csd[EXT_CSD_RPMB_MULT] << 17;
		for (i = 0; i < 4; i++) {
			int idx = EXT_CSD_GP_SIZE_MULT + i * 3;
			mmc->capacity_gp[i] = (ext_csd[idx + 2] << 16) +
				(ext_csd[idx + 1] << 8) + ext_csd[idx];
			mmc->capacity_gp[i] *=
				ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE];
			mmc->capacity_gp[i] *= ext_csd[EXT_CSD_HC_WP_GRP_SIZE];
		}
	}

	err = mmc_set_capacity(mmc, mmc->part_num);
	if (err)
		return err;

		err = mmc_change_freq(mmc);

	if (err)
		return err;

	/* Restrict card's capabilities by what the host can do */
	mmc->ctrlr->card_caps &= mmc->ctrlr->host_caps;

	{
		int idx;

		/* An array of possible bus widths in order of preference */
		static unsigned ext_csd_bits[] = {
			//EXT_CSD_BUS_WIDTH_8,
			EXT_CSD_BUS_WIDTH_4,
			EXT_CSD_BUS_WIDTH_1,
		};

		/* An array to map CSD bus widths to host cap bits */
		static unsigned ext_to_hostcaps[] = {
			[EXT_CSD_BUS_WIDTH_4] = MMC_MODE_4BIT,
			[EXT_CSD_BUS_WIDTH_8] = MMC_MODE_8BIT,
		};

		/* An array to map chosen bus width to an integer */
		static unsigned widths[] = {
			/*8,*/ 4, 1,
		};
		
		for (idx=0; idx < ARRAY_SIZE(ext_csd_bits); idx++) {
			unsigned int extw = ext_csd_bits[idx];

			/*
			 * Check to make sure the controller supports
			 * this bus width, if it's more than 1
			 */
			if (extw != EXT_CSD_BUS_WIDTH_1 &&
					!(mmc->ctrlr->host_caps & ext_to_hostcaps[extw]))
				continue;
			
			err = mmc_switch(mmc, EXT_CSD_CMD_SET_NORMAL,
					EXT_CSD_BUS_WIDTH, extw);

			if (err)
				continue;
			
			mmc_set_bus_width(mmc->ctrlr, widths[idx]);

			err = mmc_send_ext_csd(mmc->ctrlr, test_csd);
			if (!err && ext_csd[EXT_CSD_PARTITIONING_SUPPORT] \
				    == test_csd[EXT_CSD_PARTITIONING_SUPPORT]
				 && ext_csd[EXT_CSD_ERASE_GROUP_DEF] \
				    == test_csd[EXT_CSD_ERASE_GROUP_DEF] \
				 && ext_csd[EXT_CSD_REV] \
				    == test_csd[EXT_CSD_REV]
				 && ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE] \
				    == test_csd[EXT_CSD_HC_ERASE_GRP_SIZE]
				 && memcmp(&ext_csd[EXT_CSD_SEC_CNT], \
					&test_csd[EXT_CSD_SEC_CNT], 4) == 0) {

				mmc->ctrlr->card_caps |= ext_to_hostcaps[extw];
				break;
			}
		}

		if (mmc->ctrlr->card_caps & MMC_MODE_HS) {
			if (mmc->ctrlr->card_caps & MMC_MODE_HS_52MHz)
				mmc->tran_speed = 52000000;
			else
				mmc->tran_speed = 26000000;
		}
	}
	mmc_set_clock(mmc->ctrlr, mmc->tran_speed);

	mmc->dev.block_count = mmc->capacity / mmc->read_bl_len;
	mmc->dev.block_size = mmc->read_bl_len;
	
	return 0;
}

static int mmc_send_if_cond(MmcMedia *media)
{
	MmcCommand cmd;
	cmd.cmdidx = SD_CMD_SEND_IF_COND;
	// Set if host supports voltages between 2.7 and 3.6 V.
	cmd.cmdarg = ((media->ctrlr->voltages & 0xff8000) != 0) << 8 | 0xaa;
	cmd.resp_type = MMC_RSP_R7;
	cmd.flags = 0;
	int err = mmc_send_cmd(media->ctrlr, &cmd, NULL);
	if (err)
		return err;

	if ((cmd.response[0] & 0xff) != 0xaa)
		return MMC_UNUSABLE_ERR;
	else
		media->version = SD_VERSION_2;
	return 0;
}

int mmc_setup_media(MmcCtrlr *ctrlr)
{
	int err;
	MmcMedia *media = xzalloc(sizeof(*media));
	media->ctrlr = ctrlr;

	mmc_set_bus_width(ctrlr, 1);
	mmc_set_clock(ctrlr, 1);

	/* Reset the Card */
	err = mmc_go_idle(media);
	if (err) {
		free(media);
		return err;
	}

	/* Test for SD version 2 */
	err = mmc_send_if_cond(media);

	/* Get SD card operating condition */
	err = sd_send_op_cond(media);

	/* If the command timed out, we check for an MMC card */
	if (err == MMC_TIMEOUT) {
		
		err = mmc_send_op_cond(media);
		
		if (err && err != MMC_IN_PROGRESS) {
			mmc_error("Card did not respond to voltage select!\n");
			free(media);
			return MMC_UNUSABLE_ERR;
		}
	}

	if (err && err != MMC_IN_PROGRESS) {
		free(media);
		return err;
	}
	if (err == MMC_IN_PROGRESS){
		udelay(30000);
		err = mmc_complete_op_cond(media);
	}
	if (!err) {
		err = mmc_startup(media);
		if (!err) {
			ctrlr->media = media;
			return 0;
		}
	}

	free(media);
	return err;
}

/////////////////////////////////////////////////////////////////////////////
// BlockDevice utilities and callbacks

static inline MmcMedia *mmc_media(BlockDevOps *me)
{
	return container_of(me, MmcMedia, dev.ops);
}

static inline MmcCtrlr *mmc_ctrlr(MmcMedia *media)
{
	return media->ctrlr;
}

static int block_mmc_setup(BlockDevOps *me, lba_t start, lba_t count,
			   int is_read)
{
	MmcMedia *media = mmc_media(me);
	MmcCtrlr *ctrlr = mmc_ctrlr(media);

	if (count == 0)
		return 0;

	if (start > media->dev.block_count ||
	    start + count > media->dev.block_count)
		return 0;

	uint32_t bl_len = is_read ? media->read_bl_len :
		media->write_bl_len;

	if (mmc_set_blocklen(ctrlr, bl_len))
		return 0;

	return 1;
}

lba_t block_mmc_read(BlockDevOps *me, lba_t start, lba_t count, void *buffer)
{
	uint8_t *dest = (uint8_t *)buffer;
	if (block_mmc_setup(me, start, count, 1) == 0)
		return 0;

	lba_t todo = count;
	MmcMedia *media = mmc_media(me);
	MmcCtrlr *ctrlr = mmc_ctrlr(media);
	do {
		lba_t cur = MIN(todo, ctrlr->b_max);
		if (mmc_read(media, dest, start, cur) != cur)
			return 0;
		todo -= cur;
		mmc_debug("%s: Got %d blocks, more %d (total %d) to go.\n",
			  __func__, (int)cur, (int)todo, (int)count);
		start += cur;
		dest += cur * media->read_bl_len;
	} while (todo > 0);
	return count;
}

lba_t block_mmc_write(BlockDevOps *me, lba_t start, lba_t count,
		      const void *buffer)
{
	const uint8_t *src = (const uint8_t *)buffer;

	if (block_mmc_setup(me, start, count, 0) == 0)
		return 0;

	lba_t todo = count;
	MmcMedia *media = mmc_media(me);
	MmcCtrlr *ctrlr = mmc_ctrlr(media);
	do {
		lba_t cur = MIN(todo, ctrlr->b_max);
		if (mmc_write(media, start, cur, src) != cur)
			return 0;
		todo -= cur;
		start += cur;
		src += cur * media->write_bl_len;
	} while (todo > 0);
	return count;
}

lba_t block_mmc_fill_write(BlockDevOps *me, lba_t start, lba_t count,
			   uint8_t fill_byte)
{
	if (block_mmc_setup(me, start, count, 0) == 0)
		return 0;

	MmcMedia *media = mmc_media(me);
	MmcCtrlr *ctrlr = mmc_ctrlr(media);
	size_t block_size = media->dev.block_size;
	/*
	 * We allocate max 4 MiB buffer on heap and set it to fill_byte and
	 * perform mmc_write operation using this 4MiB buffer until requested
	 * size on disk is written by the fill byte.
	 *
	 * 4MiB was chosen after repeating several experiments with the max
	 * buffer size to be used. Using 1 lba i.e. block_size buffer results in
	 * very large fill_write time. On the other hand, choosing 4MiB, 8MiB or
	 * even 128 Mib resulted in similar write times. With 2MiB, the
	 * fill_write time increased by several seconds. So, 4MiB was chosen as
	 * the default max buffer size.
	 */
	lba_t heap_lba = (4 * MiB) / block_size;
	/*
	 * Actual allocated buffer size is minimum of three entities:
	 * 1) 4MiB equivalent in lba
	 * 2) count: Number of lbas to erase
	 * 3) ctrlr->b_max: Max lbas that the block device allows write
	 * operation on at a time.
	 */
	lba_t buffer_lba = MIN(MIN(heap_lba, count), ctrlr->b_max);

	size_t buffer_bytes = buffer_lba * block_size;
	uint8_t *buffer = xmalloc(buffer_bytes);
	memset(buffer, fill_byte, buffer_bytes);

	lba_t todo = count;
	int ret = 0;

	do {
		lba_t curr_lba = MIN(buffer_lba, todo);

		if (mmc_write(media, start, curr_lba, buffer) != curr_lba)
			goto cleanup;
		todo -= curr_lba;
		start += curr_lba;
	} while (todo > 0);

	ret = count;

cleanup:
	free(buffer);
	return ret;
}
