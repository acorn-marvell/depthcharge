/*
 * Copyright (C) 2015 Marvell Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "config.h"
#include "libpayload.h"
#include "nand.h"
#include "armada38x_nand.h"
#include "armada38x_nand_private.h"

/***********************************************************************
 * Definitions
 ***********************************************************************/

//#undef MV_DEBUG
//#define MV_DEBUG
#ifdef MV_DEBUG
#define DB(x) x
#else
#define DB(x)
#endif

#define NFC_DPRINT printf

#define NAND_SMALL_BADBLOCK_POS         5
#define NAND_LARGE_BADBLOCK_POS         0

#define HZ 1333
#define	CHIP_DELAY_TIMEOUT		(20 * HZ/10)
#define NFC_MAX_NUM_OF_DESCR    (33)
#define NFC_8BIT1K_ECC_SPARE    (32)

#define NFC_SR_MASK             (0xfff)
#define NFC_SR_BBD_MASK         (NFC_SR_CS0_BBD_MASK | NFC_SR_CS1_BBD_MASK)

enum nfc_page_size
{
        NFC_PAGE_512B = 0,
        NFC_PAGE_2KB,
        NFC_PAGE_4KB,
        NFC_PAGE_8KB,
        NFC_PAGE_16KB,
        NFC_PAGE_SIZE_MAX_CNT
};

/* error code and state */
enum {
        ERR_NONE        = 0,
        ERR_DMABUSERR   = -1,
        ERR_CMD_TO      = -2,
        ERR_DATA_TO     = -3,
        ERR_DBERR       = -4,
        ERR_BBD         = -5,
};
enum {
        STATE_READY     = 0,
        STATE_CMD_HANDLE,   
        STATE_DMA_READING,
        STATE_DMA_WRITING,
        STATE_DMA_DONE,
        STATE_PIO_READING,
        STATE_PIO_WRITING,
}; 

struct orion_nfc_info {
	unsigned int		mmio_phys_base;

	unsigned int 		buf_start;
	unsigned int		buf_count;

	unsigned char		*data_buff;

	/* saved column/page_addr during CMD_SEQIN */
	int			seqin_column;
	int			seqin_page_addr;

	/* relate to the command */
	unsigned int		state;

	/* flash information */
	unsigned int		tclk;		/* Clock supplied to NFC */
	unsigned int		nfc_width;	/* Width of NFC 16/8 bits */
	unsigned int		num_devs;	/* Number of NAND devices
						   (2 for ganged mode).   */
	unsigned int		num_cs;		/* Number of NAND devices
						   chip-selects.	  */
	MV_NFC_ECC_MODE		ecc_type;

	enum nfc_page_size	page_size;
	uint32_t 		page_per_block;	/* Pages per block (PG_PER_BLK) */
	uint32_t 		flash_width;	/* Width of Flash memory (DWIDTH_M) */
	size_t	 		read_id_bytes;
	
	int                     page_shift;
	int                     pagemask;

	size_t			data_size;	/* data size in FIFO */
	size_t			read_size;
	int 			retcode;
	uint32_t		dscr;		/* IRQ events - status */

	int			chained_cmd;
	uint32_t		column;
	uint32_t		page_addr;
	MV_NFC_CMD_TYPE		cmd;
	MV_NFC_CTRL		nfcCtrl;

	/* RW buffer chunks config */
	MV_U32			sgBuffAddr[MV_NFC_RW_MAX_BUFF_NUM];
	MV_U32			sgBuffSize[MV_NFC_RW_MAX_BUFF_NUM];
	MV_U32			sgNumBuffs;

	int                     initialized;
};

MV_U32 pg_sz[NFC_PAGE_SIZE_MAX_CNT] = {512, 2048, 4096, 8192, 16384};
static struct orion_nfc_info g_info;
static MtdDevCtrlr *mtd_ctrl = NULL;


struct nand_oobfree {
	uint32_t offset;
	uint32_t length;
};

#define MTD_MAX_OOBFREE_ENTRIES	8
/*
 * ECC layout control structure. Exported to userspace for
 * diagnosis and to allow creation of raw images
 */
struct nand_ecclayout {
	uint32_t eccbytes;
	uint32_t eccpos[128];
	uint32_t oobavail;
	struct nand_oobfree oobfree[MTD_MAX_OOBFREE_ENTRIES];
};

/*
 * LookuC Layout
 */

static struct nand_ecclayout ecc_latout_512B_hamming = {
	.eccbytes = 6,
	.eccpos = {8, 9, 10, 11, 12, 13 },
	.oobfree = { {2, 6} }
};

static struct nand_ecclayout ecc_layout_2KB_hamming = {
	.eccbytes = 24,
	.eccpos = {
		40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55,
		56, 57, 58, 59, 60, 61, 62, 63},
	.oobfree = { {2, 38} }
};

static struct nand_ecclayout ecc_layout_2KB_bch4bit = {
	.eccbytes = 32,
	.eccpos = {
		32, 33, 34, 35, 36, 37, 38, 39,
		40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55,
		56, 57, 58, 59, 60, 61, 62, 63},
	.oobfree = { {2, 30} }
};

static struct nand_ecclayout ecc_layout_4KB_bch4bit = {
	.eccbytes = 64,
	.eccpos = {
		32,  33,  34,  35,  36,  37,  38,  39,
		40,  41,  42,  43,  44,  45,  46,  47,
		48,  49,  50,  51,  52,  53,  54,  55,
		56,  57,  58,  59,  60,  61,  62,  63,
		96,  97,  98,  99,  100, 101, 102, 103,
		104, 105, 106, 107, 108, 109, 110, 111,
		112, 113, 114, 115, 116, 117, 118, 119,
		120, 121, 122, 123, 124, 125, 126, 127},
	/* Bootrom looks in bytes 0 & 5 for bad blocks */
	.oobfree = { {1, 4}, {6, 26}, { 64, 32} }
};

static struct nand_ecclayout ecc_layout_8KB_bch4bit = {
	.eccbytes = 128,
	.eccpos = {
		32,  33,  34,  35,  36,  37,  38,  39,
		40,  41,  42,  43,  44,  45,  46,  47,
		48,  49,  50,  51,  52,  53,  54,  55,
		56,  57,  58,  59,  60,  61,  62,  63,

		96,  97,  98,  99,  100, 101, 102, 103,
		104, 105, 106, 107, 108, 109, 110, 111,
		112, 113, 114, 115, 116, 117, 118, 119,
		120, 121, 122, 123, 124, 125, 126, 127,

		160, 161, 162, 163, 164, 165, 166, 167,
		168, 169, 170, 171, 172, 173, 174, 175,
		176, 177, 178, 179, 180, 181, 182, 183,
		184, 185, 186, 187, 188, 189, 190, 191,

		224, 225, 226, 227, 228, 229, 230, 231,
		232, 233, 234, 235, 236, 237, 238, 239,
		240, 241, 242, 243, 244, 245, 246, 247,
		248, 249, 250, 251, 252, 253, 254, 255},

	/* Bootrom looks in bytes 0 & 5 for bad blocks */
	.oobfree = { {1, 4}, {6, 26}, { 64, 32}, {128, 32}, {192, 32} }
};

static struct nand_ecclayout ecc_layout_4KB_bch8bit = {
	.eccbytes = 64,
	.eccpos = {
		32,  33,  34,  35,  36,  37,  38,  39,
		40,  41,  42,  43,  44,  45,  46,  47,
		48,  49,  50,  51,  52,  53,  54,  55,
		56,  57,  58,  59,  60,  61,  62,  63},
	/* Bootrom looks in bytes 0 & 5 for bad blocks */
	.oobfree = { {1, 4}, {6, 26},  }
};

static struct nand_ecclayout ecc_layout_8KB_bch8bit = {
	.eccbytes = 0,
	.eccpos = {},
	/* HW ECC handles all ECC data and all spare area is free for OOB */
	.oobfree = {{0,160} }
};

static struct nand_ecclayout ecc_layout_8KB_bch12bit = {
	.eccbytes = 0,
	.eccpos = { },
	/* Bootrom looks in bytes 0 & 5 for bad blocks */
	.oobfree = { {1, 4}, {6, 58}, }
};

static struct nand_ecclayout ecc_layout_16KB_bch12bit = {
	.eccbytes = 0,
	.eccpos = { },
	/* Bootrom looks in bytes 0 & 5 for bad blocks */
	.oobfree = { {1, 4}, {6, 122},  }
};

struct orion_nfc_naked_info {
	struct nand_ecclayout* 	ecc_layout;
	uint32_t		bb_bytepos;
	uint32_t		chunk_size;
	uint32_t		chunk_spare;
	uint32_t		chunk_cnt;
	uint32_t		last_chunk_size;
	uint32_t		last_chunk_spare;
};

			                     /* PageSize*/          /* ECc Type */
static struct orion_nfc_naked_info orion_nfc_naked_info_lkup[NFC_PAGE_SIZE_MAX_CNT][MV_NFC_ECC_MAX_CNT] = {
	/* 512B Pages */
	{{    	/* Hamming */
		&ecc_latout_512B_hamming, 512, 512, 16, 1, 0, 0
	}, { 	/* BCH 4bit */
		NULL, 0, 0, 0, 0, 0, 0
	}, { 	/* BCH 8bit */
		NULL, 0, 0, 0, 0, 0, 0
	}, { 	/* BCH 12bit */
		NULL, 0, 0, 0, 0, 0, 0
	}, { 	/* BCH 16bit */
		NULL, 0, 0, 0, 0, 0, 0
	}, { 	/* No ECC */
		NULL, 0, 0, 0, 0, 0, 0
	}},
	/* 2KB Pages */
	{{	/* Hamming */
		&ecc_layout_2KB_hamming, 2048, 2048, 40, 1, 0, 0
	}, { 	/* BCH 4bit */
		&ecc_layout_2KB_bch4bit, 2048, 2048, 32, 1, 0, 0
	}, { 	/* BCH 8bit */
		NULL, 2018, 1024, 0, 1, 1024, 32
	}, { 	/* BCH 12bit */
		NULL, 1988, 704, 0, 2, 640, 0
	}, { 	/* BCH 16bit */
		NULL, 1958, 512, 0, 4, 0, 32
	}, { 	/* No ECC */
		NULL, 0, 0, 0, 0, 0, 0
	}},
	/* 4KB Pages */
	{{	/* Hamming */
		NULL, 0, 0, 0, 0, 0, 0
	}, { 	/* BCH 4bit */
		&ecc_layout_4KB_bch4bit, 4034, 2048, 32, 2, 0, 0
	}, { 	/* BCH 8bit */
		&ecc_layout_4KB_bch8bit, 4006, 1024, 0, 4, 0, 64
	}, { 	/* BCH 12bit */
		NULL, 3946, 704,  0, 5, 576, 32
	}, { 	/* BCH 16bit */
		NULL, 3886, 512, 0, 8, 0, 32
	}, { 	/* No ECC */
		NULL, 0, 0, 0, 0, 0, 0
	}},
	/* 8KB Pages */
	{{	/* Hamming */
		NULL, 0, 0, 0, 0, 0, 0
	}, { 	/* BCH 4bit */
		&ecc_layout_8KB_bch4bit, 8102, 2048, 32, 4, 0, 0
	}, { 	/* BCH 8bit */
		&ecc_layout_8KB_bch8bit, 7982, 1024, 0, 8, 0, 160
	}, { 	/* BCH 12bit */
		&ecc_layout_8KB_bch12bit, 7862, 704, 0, 11, 448, 64
	}, { 	/* BCH 16bit */
		NULL, 7742, 512, 0, 16, 0, 32
	}, { 	/* No ECC */
		NULL, 0, 0, 0, 0, 0, 0
	}},
	/* 16KB Pages */
	{{	/* Hamming */
		NULL, 0, 0, 0, 0, 0, 0
	}, { 	/* BCH 4bit */
		NULL, 15914, 2048, 32, 8, 0, 0
	}, { 	/* BCH 8bit */
		NULL, 15930, 1024, 0, 16, 0, 352
	}, { 	/* BCH 12bit */
		&ecc_layout_16KB_bch12bit, 15724, 704, 0, 23, 192, 128
	}, { 	/* BCH 16bit */
		NULL, 15484, 512, 0, 32, 0, 32
	}, { 	/* No ECC */
		NULL, 0, 0, 0, 0, 0, 0
	}}};

#define ECC_LAYOUT      (orion_nfc_naked_info_lkup[info->page_size][info->ecc_type].ecc_layout)
#define BB_BYTE_POS     (orion_nfc_naked_info_lkup[info->page_size][info->ecc_type].bb_bytepos)
#define CHUNK_CNT       (orion_nfc_naked_info_lkup[info->page_size][info->ecc_type].chunk_cnt)
#define CHUNK_SZ        (orion_nfc_naked_info_lkup[info->page_size][info->ecc_type].chunk_size)
#define CHUNK_SPR       (orion_nfc_naked_info_lkup[info->page_size][info->ecc_type].chunk_spare)
#define LST_CHUNK_SZ    (orion_nfc_naked_info_lkup[info->page_size][info->ecc_type].last_chunk_size)
#define LST_CHUNK_SPR   (orion_nfc_naked_info_lkup[info->page_size][info->ecc_type].last_chunk_spare)

struct orion_nfc_cmd_info {

        uint32_t                events_p1;      /* post command events */
        uint32_t                events_p2;      /* post data events */
        MV_NFC_PIO_RW_MODE      rw;
};

static struct orion_nfc_cmd_info orion_nfc_cmd_info_lkup[MV_NFC_CMD_COUNT] = {
	/* Phase 1 interrupts */			/* Phase 2 interrupts */			/* Read/Write */  /* MV_NFC_CMD_xxxxxx */
	{(NFC_SR_RDDREQ_MASK), 				(0),						MV_NFC_PIO_READ}, /* READ_ID */
	{(NFC_SR_RDDREQ_MASK), 				(0),						MV_NFC_PIO_READ}, /* READ_STATUS */
	{(0), 						(MV_NFC_STATUS_RDY | MV_NFC_STATUS_BBD),	MV_NFC_PIO_NONE}, /* ERASE */
	{(0), 						(0), 						MV_NFC_PIO_NONE}, /* MULTIPLANE_ERASE */
	{(0), 						(MV_NFC_STATUS_RDY), 				MV_NFC_PIO_NONE}, /* RESET */
	{(0), 						(0), 						MV_NFC_PIO_READ}, /* CACHE_READ_SEQ */
	{(0), 						(0), 						MV_NFC_PIO_READ}, /* CACHE_READ_RAND */
	{(0), 						(0), 						MV_NFC_PIO_NONE}, /* EXIT_CACHE_READ */
	{(0), 						(0), 						MV_NFC_PIO_READ}, /* CACHE_READ_START */
	{(NFC_SR_RDDREQ_MASK | NFC_SR_UNCERR_MASK), 	(0), 						MV_NFC_PIO_READ}, /* READ_MONOLITHIC */
	{(0), 						(0),						MV_NFC_PIO_READ}, /* READ_MULTIPLE */
	{(NFC_SR_RDDREQ_MASK | NFC_SR_UNCERR_MASK), 	(0), 						MV_NFC_PIO_READ}, /* READ_NAKED */
	{(NFC_SR_RDDREQ_MASK | NFC_SR_UNCERR_MASK), 	(0), 						MV_NFC_PIO_READ}, /* READ_LAST_NAKED */
	{(0), 						(0), 						MV_NFC_PIO_NONE}, /* READ_DISPATCH */
	{(MV_NFC_STATUS_WRD_REQ), 			(MV_NFC_STATUS_RDY | MV_NFC_STATUS_BBD),	MV_NFC_PIO_WRITE},/* WRITE_MONOLITHIC */
	{(0), 						(0), 						MV_NFC_PIO_WRITE},/* WRITE_MULTIPLE */
	{(MV_NFC_STATUS_WRD_REQ),			(MV_NFC_STATUS_PAGED),				MV_NFC_PIO_WRITE},/* WRITE_NAKED */
	{(0), 						(0), 						MV_NFC_PIO_WRITE},/* WRITE_LAST_NAKED */
	{(0), 						(0), 						MV_NFC_PIO_NONE}, /* WRITE_DISPATCH */
	{(MV_NFC_STATUS_CMDD),				(0),						MV_NFC_PIO_NONE}, /* WRITE_DISPATCH_START */
	{(0),						(MV_NFC_STATUS_RDY | MV_NFC_STATUS_BBD), 	MV_NFC_PIO_NONE}, /* WRITE_DISPATCH_END */
};

/***********************************************************************
 * Private functions
 ***********************************************************************/
static int prepare_read_prog_cmd(struct orion_nfc_info *info,
                        int column, int page_addr)
{
        MV_U32 size;
        if (mvNfcFlashPageSizeGet(&info->nfcCtrl, &size, &info->data_size)
            != MV_OK)
                return -EINVAL;
        return 0;
}

static int orion_nfc_cmd_prepare(struct orion_nfc_info *info,
		MV_NFC_MULTI_CMD *descInfo, u32 *numCmds)
{
	MV_U32	i;
	MV_NFC_MULTI_CMD *currDesc;

	currDesc = descInfo;
	if (info->cmd == MV_NFC_CMD_READ_MONOLITHIC) {
		/* Main Chunks */
		for (i=0; i<CHUNK_CNT; i++)
		{
			if (i == 0)
				currDesc->cmd = MV_NFC_CMD_READ_MONOLITHIC;
			else if ((i == (CHUNK_CNT-1)) && (LST_CHUNK_SZ == 0) && (LST_CHUNK_SPR == 0))
				currDesc->cmd = MV_NFC_CMD_READ_LAST_NAKED;
			else
				currDesc->cmd = MV_NFC_CMD_READ_NAKED;

			currDesc->pageAddr = info->page_addr;
			currDesc->pageCount = 1;
			currDesc->virtAddr = (MV_U32 *)(info->data_buff + (i * CHUNK_SZ));
			//currDesc->physAddr = info->data_buff_phys + (i * CHUNK_SZ);
			currDesc->length = (CHUNK_SZ + CHUNK_SPR);

			if (CHUNK_SPR == 0)
				currDesc->numSgBuffs = 1;
			else
			{
				currDesc->numSgBuffs = 2;
				//currDesc->sgBuffAddr[0] = (info->data_buff_phys + (i * CHUNK_SZ));
				currDesc->sgBuffAddrVirt[0] = (MV_U32 *)(info->data_buff + (i * CHUNK_SZ));
				currDesc->sgBuffSize[0] = CHUNK_SZ;
				//currDesc->sgBuffAddr[1] = (info->data_buff_phys + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (i * CHUNK_SPR));
				currDesc->sgBuffAddrVirt[1] = (MV_U32 *)(info->data_buff + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (i * CHUNK_SPR));
				currDesc->sgBuffSize[1] = CHUNK_SPR;
			}

			currDesc++;
		}

		/* Last chunk if existing */
		if ((LST_CHUNK_SZ != 0) || (LST_CHUNK_SPR != 0))
		{
			currDesc->cmd = MV_NFC_CMD_READ_LAST_NAKED;
			currDesc->pageAddr = info->page_addr;
			currDesc->pageCount = 1;
			currDesc->length = (LST_CHUNK_SPR + LST_CHUNK_SZ);

			if ((LST_CHUNK_SZ == 0) && (LST_CHUNK_SPR != 0))	/* Spare only */
			{
				currDesc->virtAddr = (MV_U32 *)(info->data_buff + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (CHUNK_SPR * CHUNK_CNT));
				//currDesc->physAddr = info->data_buff_phys + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (CHUNK_SPR * CHUNK_CNT);
				currDesc->numSgBuffs = 1;
				currDesc->length = LST_CHUNK_SPR;
			}
			else if ((LST_CHUNK_SZ != 0) && (LST_CHUNK_SPR == 0))	/* Data only */
			{
				currDesc->virtAddr = (MV_U32 *)(info->data_buff + (CHUNK_SZ * CHUNK_CNT));
				//currDesc->physAddr = info->data_buff_phys + (CHUNK_SZ * CHUNK_CNT);
				currDesc->numSgBuffs = 1;
				currDesc->length = LST_CHUNK_SZ;
			}
			else /* Both spare and data */
			{
				currDesc->numSgBuffs = 2;
				//currDesc->sgBuffAddr[0] = (info->data_buff_phys + (CHUNK_SZ * CHUNK_CNT));
				currDesc->sgBuffAddrVirt[0] = (MV_U32 *)(info->data_buff + (CHUNK_SZ * CHUNK_CNT));
				currDesc->sgBuffSize[0] = LST_CHUNK_SZ;
				//currDesc->sgBuffAddr[1] = (info->data_buff_phys + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (CHUNK_SPR * CHUNK_CNT));
				currDesc->sgBuffAddrVirt[1] =  (MV_U32 *)(info->data_buff + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (CHUNK_SPR * CHUNK_CNT));
				currDesc->sgBuffSize[1] = LST_CHUNK_SPR;
			}
			currDesc++;
		}

		*numCmds = CHUNK_CNT + (((LST_CHUNK_SZ) || (LST_CHUNK_SPR)) ? 1 : 0);
	} else if (info->cmd == MV_NFC_CMD_WRITE_MONOLITHIC) {
		/* Write Dispatch */
		currDesc->cmd = MV_NFC_CMD_WRITE_DISPATCH_START;
		currDesc->pageAddr = info->page_addr;
		currDesc->pageCount = 1;
		currDesc->numSgBuffs = 1;
		currDesc->length = 0;
		currDesc++;

		/* Main Chunks */
		for (i=0; i<CHUNK_CNT; i++)
		{
			currDesc->cmd = MV_NFC_CMD_WRITE_NAKED;
			currDesc->pageAddr = info->page_addr;
			currDesc->pageCount = 1;
			currDesc->virtAddr = (MV_U32 *)(info->data_buff + (i * CHUNK_SZ));
			//currDesc->physAddr = info->data_buff_phys + (i * CHUNK_SZ);
			currDesc->length = (CHUNK_SZ + CHUNK_SPR);

			if (CHUNK_SPR == 0)
				currDesc->numSgBuffs = 1;
			else
			{
				currDesc->numSgBuffs = 2;
				//currDesc->sgBuffAddr[0] = (info->data_buff_phys + (i * CHUNK_SZ));
				currDesc->sgBuffAddrVirt[0] = (MV_U32 *)(info->data_buff + (i * CHUNK_SZ));
				currDesc->sgBuffSize[0] = CHUNK_SZ;
				//currDesc->sgBuffAddr[1] = (info->data_buff_phys + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (i * CHUNK_SPR));
				currDesc->sgBuffAddrVirt[1] = (MV_U32 *)(info->data_buff + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (i * CHUNK_SPR));
				currDesc->sgBuffSize[1] = CHUNK_SPR;
			}

			currDesc++;
		}

		/* Last chunk if existing */
		if ((LST_CHUNK_SZ != 0) || (LST_CHUNK_SPR != 0))
		{
			currDesc->cmd = MV_NFC_CMD_WRITE_NAKED;
			currDesc->pageAddr = info->page_addr;
			currDesc->pageCount = 1;
			currDesc->length = (LST_CHUNK_SZ + LST_CHUNK_SPR);

			if ((LST_CHUNK_SZ == 0) && (LST_CHUNK_SPR != 0))	/* Spare only */
			{
				currDesc->virtAddr = (MV_U32 *)(info->data_buff + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (CHUNK_SPR * CHUNK_CNT));
				//currDesc->physAddr = info->data_buff_phys + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (CHUNK_SPR * CHUNK_CNT);
				currDesc->numSgBuffs = 1;
			}
			else if ((LST_CHUNK_SZ != 0) && (LST_CHUNK_SPR == 0))	/* Data only */
			{
				currDesc->virtAddr = (MV_U32 *)(info->data_buff + (CHUNK_SZ * CHUNK_CNT));
				//currDesc->physAddr = info->data_buff_phys + (CHUNK_SZ * CHUNK_CNT);
				currDesc->numSgBuffs = 1;
			}
			else /* Both spare and data */
			{
				currDesc->numSgBuffs = 2;
				//currDesc->sgBuffAddr[0] = (info->data_buff_phys + (CHUNK_SZ * CHUNK_CNT));
				currDesc->sgBuffAddrVirt[0] = (MV_U32 *)(info->data_buff + (CHUNK_SZ * CHUNK_CNT));
				currDesc->sgBuffSize[0] = LST_CHUNK_SZ;
				//currDesc->sgBuffAddr[1] = (info->data_buff_phys + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (CHUNK_SPR * CHUNK_CNT));
				currDesc->sgBuffAddrVirt[1] = (MV_U32 *)(info->data_buff + (CHUNK_SZ * CHUNK_CNT) + LST_CHUNK_SZ + (CHUNK_SPR * CHUNK_CNT));
				currDesc->sgBuffSize[1] = LST_CHUNK_SPR;
			}
			currDesc++;
		}

		/* Write Dispatch END */
		currDesc->cmd = MV_NFC_CMD_WRITE_DISPATCH_END;
		currDesc->pageAddr = info->page_addr;
		currDesc->pageCount = 1;
		currDesc->numSgBuffs = 1;
		currDesc->length = 0;

		*numCmds = CHUNK_CNT + (((LST_CHUNK_SZ) || (LST_CHUNK_SPR)) ? 1 : 0) + 2;
	} else {
		descInfo[0].cmd = info->cmd;
		descInfo[0].pageAddr = info->page_addr;
		descInfo[0].pageCount = 1;
		descInfo[0].virtAddr = (MV_U32 *)info->data_buff;
		//descInfo[0].physAddr = info->data_buff_phys;
		descInfo[0].numSgBuffs = 1;
		descInfo[0].length = info->data_size;
		*numCmds = 1;
	}

	return 0;
}

int orion_nfc_wait_for_completion_timeout(struct orion_nfc_info *info, int timeout)
{
	MV_U32 mask;
	MV_ULONG time;

	/* Clear the interrupt and pass the status UP */
	mask = ~MV_REG_READ(NFC_CONTROL_REG) & 0xFFF;
	if (mask & 0x800)
		mask |= 0x1000;
	DB(NFC_DPRINT(">>> wait_for_completion_timeout timeout mask is:[0x%x]\n", mask));
	info->dscr = MV_REG_READ(NFC_STATUS_REG);
	time = timer_us(0);
	while ((info->dscr & mask) == 0) {
		if (timer_us(time) > timeout) {
			DB(printf(">>> orion_nfc_wait_for_completion_timeout command timed out!, status  (0x%x)\n", info->dscr));
			return 0;
		}
		udelay(10);
		info->dscr = MV_REG_READ(NFC_STATUS_REG);
	}

	/* Disable all interrupts */
	mvNfcIntrSet(&info->nfcCtrl, 0xFFF, MV_FALSE);

	DB(NFC_DPRINT(">>> orion_nfc_wait_for_completion_timeout(0x%x)\n", info->dscr));
	MV_REG_WRITE(NFC_STATUS_REG, info->dscr);

	return 1;
}


static int orion_nfc_error_check(struct orion_nfc_info *info)
{
	switch (info->cmd) {
		case MV_NFC_CMD_ERASE:
		case MV_NFC_CMD_MULTIPLANE_ERASE:
		case MV_NFC_CMD_WRITE_MONOLITHIC:
		case MV_NFC_CMD_WRITE_MULTIPLE:
		case MV_NFC_CMD_WRITE_NAKED:
		case MV_NFC_CMD_WRITE_LAST_NAKED:
		case MV_NFC_CMD_WRITE_DISPATCH:
		case MV_NFC_CMD_WRITE_DISPATCH_START:
		case MV_NFC_CMD_WRITE_DISPATCH_END:
			if (info->dscr & (MV_NFC_CS0_BAD_BLK_DETECT_INT | MV_NFC_CS1_BAD_BLK_DETECT_INT)) {
				info->retcode = ERR_BBD;
				return 1;
			}
			break;

		case MV_NFC_CMD_CACHE_READ_SEQ:
		case MV_NFC_CMD_CACHE_READ_RAND:
		case MV_NFC_CMD_EXIT_CACHE_READ:
		case MV_NFC_CMD_CACHE_READ_START:
		case MV_NFC_CMD_READ_MONOLITHIC:
		case MV_NFC_CMD_READ_MULTIPLE:
		case MV_NFC_CMD_READ_NAKED:
		case MV_NFC_CMD_READ_LAST_NAKED:
		case MV_NFC_CMD_READ_DISPATCH:
			if (info->dscr & MV_NFC_UNCORR_ERR_INT) {
				info->dscr = ERR_DBERR;
				return 1;
			}
			break;

		default:
			break;
	}

	info->retcode = ERR_NONE;
	return 0;
}


static int orion_nfc_do_cmd_pio(struct orion_nfc_info *info)
{
	int timeout = CHIP_DELAY_TIMEOUT;
	MV_STATUS status;
	MV_U32	i, j, numCmds;
	MV_U32 ndcr;

	/* static allocation to avoid stack overflow */
	static MV_NFC_MULTI_CMD descInfo[NFC_MAX_NUM_OF_DESCR];

	/* Clear all status bits */
	MV_REG_WRITE(NFC_STATUS_REG, NFC_SR_MASK);

	DB(NFC_DPRINT("\nStarting PIO command %d (cs %d) - NDCR=0x%08x\n",
				info->cmd, info->nfcCtrl.currCs, MV_REG_READ(NFC_CONTROL_REG)));

	/* Build the chain of commands */
	orion_nfc_cmd_prepare(info, descInfo, &numCmds);
	DB(NFC_DPRINT("Prepared %d commands in sequence\n", numCmds));

	/* Execute the commands */
	for (i=0; i < numCmds; i++) {
		/* Verify that command is supported in PIO mode */
		if ((orion_nfc_cmd_info_lkup[descInfo[i].cmd].events_p1 == 0) &&
		    (orion_nfc_cmd_info_lkup[descInfo[i].cmd].events_p2 == 0)) {
			goto fail_stop;
		}

		/* clear the return code */
		info->dscr = 0;

		/* STEP1: Initiate the command */
		DB(NFC_DPRINT("About to issue Descriptor #%d (command %d, pageaddr 0x%x, length %d).\n",
			    i, descInfo[i].cmd, descInfo[i].pageAddr, descInfo[i].length));
		if ((status = mvNfcCommandPio(&info->nfcCtrl, &descInfo[i], MV_FALSE)) != MV_OK) {
			DB(printf("mvNfcCommandPio() failed for command %d (%d).\n", descInfo[i].cmd, status));
			goto fail_stop;
		}
		DB(NFC_DPRINT("After issue command %d (NDSR=0x%x)\n", descInfo[i].cmd, MV_REG_READ(NFC_STATUS_REG)));

		/* Check if command phase interrupts events are needed */
		if (orion_nfc_cmd_info_lkup[descInfo[i].cmd].events_p1) {
			/* Enable necessary interrupts for command phase */
			DB(NFC_DPRINT("Enabling part1 interrupts (IRQs 0x%x)\n", orion_nfc_cmd_info_lkup[descInfo[i].cmd].events_p1));
			mvNfcIntrSet(&info->nfcCtrl, orion_nfc_cmd_info_lkup[descInfo[i].cmd].events_p1, MV_TRUE);

			/* STEP2: wait for interrupt */
			if (!orion_nfc_wait_for_completion_timeout(info, timeout)) {
				DB(printf("command %d execution timed out (CS %d, NDCR=0x%x, NDSR=0x%x).\n",
				       descInfo[i].cmd, info->nfcCtrl.currCs, MV_REG_READ(NFC_CONTROL_REG), MV_REG_READ(NFC_STATUS_REG)));
				info->retcode = ERR_CMD_TO;
				goto fail_stop;
			}

			/* STEP3: Check for errors */
			if (orion_nfc_error_check(info)) {
				DB(NFC_DPRINT("Command level errors (DSCR=%08x, retcode=%d)\n", info->dscr, info->retcode));
				goto fail_stop;
			}
		}

		/* STEP4: PIO Read/Write data if needed */
		if (descInfo[i].numSgBuffs > 1)
		{
			for (j=0; j< descInfo[i].numSgBuffs; j++) {
				DB(NFC_DPRINT("Starting SG#%d PIO Read/Write (%d bytes, R/W mode %d)\n", j,
					    descInfo[i].sgBuffSize[j], orion_nfc_cmd_info_lkup[descInfo[i].cmd].rw));
				mvNfcReadWritePio(&info->nfcCtrl, descInfo[i].sgBuffAddrVirt[j],
						  descInfo[i].sgBuffSize[j], orion_nfc_cmd_info_lkup[descInfo[i].cmd].rw);
			}
		}
		else {
			DB(NFC_DPRINT("Starting nonSG PIO Read/Write (%d bytes, R/W mode %d)\n",
				    descInfo[i].length, orion_nfc_cmd_info_lkup[descInfo[i].cmd].rw));
			mvNfcReadWritePio(&info->nfcCtrl, descInfo[i].virtAddr,
					  descInfo[i].length, orion_nfc_cmd_info_lkup[descInfo[i].cmd].rw);
		}

		/* check if data phase events are needed */
		if (orion_nfc_cmd_info_lkup[descInfo[i].cmd].events_p2) {
			/* Enable the RDY interrupt to close the transaction */
			DB(NFC_DPRINT("Enabling part2 interrupts (IRQs 0x%x)\n", orion_nfc_cmd_info_lkup[descInfo[i].cmd].events_p2));
			mvNfcIntrSet(&info->nfcCtrl, orion_nfc_cmd_info_lkup[descInfo[i].cmd].events_p2, MV_TRUE);

			/* STEP5: Wait for transaction to finish */
			if (!orion_nfc_wait_for_completion_timeout(info, timeout)) {
				DB(printf("command %d execution timed out (NDCR=0x%08x, NDSR=0x%08x, NDECCCTRL=0x%08x)\n", descInfo[i].cmd,
						MV_REG_READ(NFC_CONTROL_REG), MV_REG_READ(NFC_STATUS_REG), MV_REG_READ(NFC_ECC_CONTROL_REG)));
				info->retcode = ERR_DATA_TO;
				goto fail_stop;
			}

			/* STEP6: Check for errors BB errors (in erase) */
			if (orion_nfc_error_check(info)) {
				DB(NFC_DPRINT("Data level errors (DSCR=0x%08x, retcode=%d)\n", info->dscr, info->retcode));
				goto fail_stop;
			}
		}
		/* Bug fix SYSTEMSW-295, poll  NFC_CTRL_ND_RUN_MASK for 10ms */
		for (j = 0; j < 100; j++) {
			udelay(100);
			ndcr = MV_REG_READ(NFC_CONTROL_REG);
			if ((ndcr & NFC_CTRL_ND_RUN_MASK) == 0)
				break;
		}
		/* Fallback - in case the NFC did not reach the idle state */
		if (ndcr & NFC_CTRL_ND_RUN_MASK) {
			//printk(KERN_DEBUG "WRONG NFC STAUS: command %d, NDCR=0x%08x, NDSR=0x%08x, NDECCCTRL=0x%08x)\n",
		    //   	info->cmd, MV_REG_READ(NFC_CONTROL_REG), MV_REG_READ(NFC_STATUS_REG), MV_REG_READ(NFC_ECC_CONTROL_REG));
			MV_REG_WRITE(NFC_CONTROL_REG, (ndcr & ~NFC_CTRL_ND_RUN_MASK));
		}
	}

	DB(NFC_DPRINT("Command done (NDCR=0x%08x, NDSR=0x%08x)\n", MV_REG_READ(NFC_CONTROL_REG), MV_REG_READ(NFC_STATUS_REG)));
	info->retcode = ERR_NONE;

	return 0;

fail_stop:
	ndcr = MV_REG_READ(NFC_CONTROL_REG);
	if (ndcr & NFC_CTRL_ND_RUN_MASK) {
		DB(printf("WRONG NFC STAUS: command %d, NDCR=0x%08x, NDSR=0x%08x, NDECCCTRL=0x%08x)\n",
		       info->cmd, MV_REG_READ(NFC_CONTROL_REG), MV_REG_READ(NFC_STATUS_REG), MV_REG_READ(NFC_ECC_CONTROL_REG)));
		MV_REG_WRITE(NFC_CONTROL_REG, (ndcr & ~NFC_CTRL_ND_RUN_MASK));
	}
	mvNfcIntrSet(&info->nfcCtrl, 0xFFF, MV_FALSE);
	udelay(10);
	return -ETIMEDOUT;
}

static inline int is_buf_blank(uint8_t *buf, size_t len)
{
        for (; len > 0; len--)
                if (*buf++ != 0xff)
                        return 0;
        return 1;
}

static void orion_nfc_cmdfunc(MtdDev *mtd, unsigned command,
				int column, int page_addr)
{
	struct orion_nfc_info *info = (struct orion_nfc_info *)mtd->priv;

	info->data_size = 0;
	info->state = STATE_READY;
	info->chained_cmd = 0;
	info->retcode = ERR_NONE;

	switch (command) {
	case NAND_CMD_READOOB:
		info->buf_count = mtd->writesize + mtd->oobsize;
		info->buf_start = mtd->writesize + column;
		info->cmd = MV_NFC_CMD_READ_MONOLITHIC;
		info->column = column;
		info->page_addr = page_addr;
		if (prepare_read_prog_cmd(info, column, page_addr))
			break;

		orion_nfc_do_cmd_pio(info);

		/* We only are OOB, so if the data has error, does not matter */
		if (info->retcode == ERR_DBERR)
			info->retcode = ERR_NONE;
		break;

	case NAND_CMD_READ0:
		info->buf_start = column;
		info->buf_count = mtd->writesize + mtd->oobsize;
		memset(info->data_buff, 0xff, info->buf_count);
		info->cmd = MV_NFC_CMD_READ_MONOLITHIC;
		info->column = column;
		info->page_addr = page_addr;

		if (prepare_read_prog_cmd(info, column, page_addr))
			break;

		orion_nfc_do_cmd_pio(info);

		if (info->retcode == ERR_DBERR) {
			/* for blank page (all 0xff), HW will calculate its ECC as
			 * 0, which is different from the ECC information within
			 * OOB, ignore such double bit errors
			 */
			if (is_buf_blank(info->data_buff, mtd->writesize))
				info->retcode = ERR_NONE;
			else
				DB(printf("%s: retCode == ERR_DBERR\n", __FUNCTION__));
		}
		break;
	case NAND_CMD_SEQIN:
		info->buf_start = column;
		info->buf_count = mtd->writesize + mtd->oobsize;
		memset(info->data_buff + mtd->writesize, 0xff, mtd->oobsize);

		/* save column/page_addr for next CMD_PAGEPROG */
		info->seqin_column = column;
		info->seqin_page_addr = page_addr;
		break;
	case NAND_CMD_PAGEPROG:
		info->column = info->seqin_column;
		info->page_addr = info->seqin_page_addr;
		info->cmd = MV_NFC_CMD_WRITE_MONOLITHIC;
		if (prepare_read_prog_cmd(info,
				info->seqin_column, info->seqin_page_addr)) {
			DB(printf("prepare_read_prog_cmd() failed.\n"));
			break;
		}

		orion_nfc_do_cmd_pio(info);

		break;
	case NAND_CMD_ERASE1:
		info->column = 0;
		info->page_addr = page_addr;
		info->cmd = MV_NFC_CMD_ERASE;

		orion_nfc_do_cmd_pio(info);

		break;
	case NAND_CMD_ERASE2:
		break;
	case NAND_CMD_READID:
	case NAND_CMD_STATUS:
		info->buf_start = 0;
		info->buf_count = (command == NAND_CMD_READID) ?
				info->read_id_bytes : 1;
		info->data_size = 8;
		info->column = 0;
		info->page_addr = 0;
		info->cmd = (command == NAND_CMD_READID) ?
			MV_NFC_CMD_READ_ID : MV_NFC_CMD_READ_STATUS;

		orion_nfc_do_cmd_pio(info);

		break;
	case NAND_CMD_RESET:
		if (mvNfcReset() != MV_OK)
			DB(printf("device reset failed\n"));
		break;
	default:
		DB(printf("non-supported command.\n"));
		break;
	}

	if (info->retcode == ERR_DBERR) {
		DB(printf("double bit error @ page %08x (%d)\n",
				page_addr, info->cmd));
		info->retcode = ERR_NONE;
	}
}

static int orion_nfc_waitfunc(MtdDev *mtd)
{
	struct orion_nfc_info *info = (struct orion_nfc_info *)mtd->priv;

	/* orion_nfc_send_command has waited for command complete */
	if (info->retcode == ERR_NONE)
		return 0;
	else {
			/*
			 * any error make it return 0x01 which will tell
			 * the caller the erase and write fail
			 */
		return 0x01;
	}

	return 0;
}

static void orion_nfc_write_buf(MtdDev *mtd,
		const uint8_t *buf, int len)
{
	struct orion_nfc_info *info = (struct orion_nfc_info *)mtd->priv;
	int real_len = MIN(len, info->buf_count - info->buf_start);

	memcpy(info->data_buff + info->buf_start, buf, real_len);
	info->buf_start += real_len;
}

static uint8_t orion_nfc_read_byte(MtdDev *mtd)
{
	struct orion_nfc_info *info = (struct orion_nfc_info *)mtd->priv;
	char retval = 0xFF;

	if (info->buf_start < info->buf_count)
		/* Has just send a new command? */
		retval = info->data_buff[info->buf_start++];
	return retval;
}

static void orion_nfc_read_buf(MtdDev *mtd, uint8_t *buf, int len)
{
	struct orion_nfc_info *info = (struct orion_nfc_info *)mtd->priv;
	int real_len = MIN(len, info->buf_count - info->buf_start);

	memcpy(buf, info->data_buff + info->buf_start, real_len);
	info->buf_start += real_len;
}

static int nand_read_page(MtdDev *mtd, uint64_t addr, uint8_t *buf,
			  uint32_t *stats_corrected,
			  uint32_t *stats_failed)
{
	int page;
	struct orion_nfc_info *info = (struct orion_nfc_info *)mtd->priv;

	page = (int)(addr >> info->page_shift) & info->pagemask;
        orion_nfc_cmdfunc(mtd, NAND_CMD_READ0, 0x00, page);

	orion_nfc_read_buf(mtd, buf, mtd->writesize);

	if (info->retcode != ERR_NONE){
		*stats_failed +=1;
	}

	return 0;
}

static int nand_write_page(MtdDev *mtd, uint64_t addr, uint8_t *buf)
{
	int status;
	int ret = 0;
	int page;
	struct orion_nfc_info *info = (struct orion_nfc_info *)mtd->priv;

	page = (int)(addr >> info->page_shift) & info->pagemask;
	orion_nfc_cmdfunc(mtd, NAND_CMD_SEQIN, 0x00, page);
	
	orion_nfc_write_buf(mtd, buf, mtd->writesize);

	orion_nfc_cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
	status = orion_nfc_waitfunc(mtd);
	if (status & NAND_STATUS_FAIL) {
        	DB(printf("nand_write_page failed at 0x%llx\n", addr));
                ret = -EIO;
        }
	return ret;
}

static int nand_block_isbad(MtdDev *mtd, uint64_t addr, int *bad)
{
	int page;
	uint32_t badblockpos;
	uint16_t b;
	struct orion_nfc_info *info = (struct orion_nfc_info *)mtd->priv;

	if (mtd->writesize > 512)
                badblockpos = NAND_LARGE_BADBLOCK_POS;
        else
                badblockpos = NAND_SMALL_BADBLOCK_POS;

	/* Get address of the next block */
	addr += mtd->erasesize;
	addr &= ~(mtd->erasesize - 1);

	/* Get start of oob in last page */
	addr -= mtd->oobsize;
	page = (int)(addr >> info->page_shift) & info->pagemask;

	orion_nfc_cmdfunc(mtd, NAND_CMD_READOOB, badblockpos, page);
	b = orion_nfc_read_byte(mtd);
	if (b != 0xFF) {
                *bad = 1;
                return 0;
        }
        *bad = 0;
        return 0;
}

static int nand_block_markbad(MtdDev *mtd, uint64_t addr)
{
	int page;
	uint8_t buf[6] = {0, 0, 0, 0, 0, 0};
        struct orion_nfc_info *info = (struct orion_nfc_info *)mtd->priv;

	/* Get address of the next block */
	addr += mtd->erasesize;
	addr &= ~(mtd->erasesize - 1);

	/* Get start of oob in last page */
	addr -= mtd->oobsize;

	page = (int)(addr >> info->page_shift) & info->pagemask;

	orion_nfc_cmdfunc(mtd, NAND_CMD_SEQIN, mtd->writesize, page);
	orion_nfc_write_buf(mtd, buf, 6);
	orion_nfc_cmdfunc(mtd, NAND_CMD_PAGEPROG, 0, page);

        return 0;
}


static int nand_erase_block(MtdDev *mtd, uint64_t addr)
{
	int status;
	int page;
        int ret = 0;
        struct orion_nfc_info *info = (struct orion_nfc_info *)mtd->priv;

	page = (int)(addr >> info->page_shift) & info->pagemask;
        orion_nfc_cmdfunc(mtd, NAND_CMD_ERASE1, -1, page);

        status = orion_nfc_waitfunc(mtd);
        if (status & NAND_STATUS_FAIL) {
                DB(printf("nand_erase_block failed at 0x%llx\n", addr));
                ret = -EIO;
        }
        return ret;
}


#define PAGE_SIZE 4096
#define MAX_BUFF_SIZE   (PAGE_SIZE * 5)
static int orion_nfc_init_buff(struct orion_nfc_info *info)
{         
        info->data_buff = malloc(MAX_BUFF_SIZE);
        if (info->data_buff == NULL)
                return -ENOMEM;
        return 0;
}

static int orion_nfc_detect_flash(struct orion_nfc_info *info)
{         
        MV_U32 my_page_size;
        mvNfcFlashPageSizeGet(&info->nfcCtrl, &my_page_size, NULL);
        /* Translate page size to enum */
        switch (my_page_size)
        {
                case 512:
                        info->page_size = NFC_PAGE_512B;
                        break; 
                case 2048:
                        info->page_size = NFC_PAGE_2KB;
                        break;
                case 4096:
                        info->page_size = NFC_PAGE_4KB;
                        break;
                case 8192:
                        info->page_size = NFC_PAGE_8KB;
                        break;
                case 16384:
                        info->page_size = NFC_PAGE_16KB;
                        break;
                default:
                        return -EINVAL;
        }
        info->flash_width = info->nfc_width;
	if (info->flash_width != 16 && info->flash_width != 8)
                return -EINVAL;

        /* calculate flash information */
        info->read_id_bytes = (pg_sz[info->page_size] >= 2048) ? 4 : 2;
        return 0;
}


int mvCtrlNandClkSet(int nfc_clk_freq)
{
	int divider;

	/* Set the division ratio of ECC Clock 0x00018748[13:8] (by default it's double of core clock) */
	MV_U32 nVal = MV_REG_READ(CORE_DIV_CLK_CTRL(1));

	/*
	 * Calculate nand divider for requested nfc_clk_freq. If integer divider
	 * cannot be achieved, it will be rounded-up, which will result in
	 * setting the closest lower frequency.
	 * ECC engine clock = (PLL frequency / divider)
	 * NFC clock = ECC clock / 2
	 */
	divider = DIV_ROUND_UP(MV_PLL_IN_CLK, (2 * nfc_clk_freq));
	if (divider == 5)     /* Temporary WA for A38x: the divider by 5 is not stable */
		divider = 4;   /* Temorary divider by 4  is used */
	DB(printf("%s: divider %d\n", __func__, divider));

	nVal &= ~(NAND_ECC_DIVCKL_RATIO_MASK);
	nVal |= (divider << NAND_ECC_DIVCKL_RATIO_OFFS);
	MV_REG_WRITE(CORE_DIV_CLK_CTRL(1), nVal);

	/* Set reload force of ECC clock 0x00018740[7:0] to 0x2 (meaning you will force only the ECC clock) */
	nVal = MV_REG_READ(CORE_DIV_CLK_CTRL(0));
	nVal &= ~(CORE_DIVCLK_RELOAD_FORCE_MASK);
	nVal |= CORE_DIVCLK_RELOAD_FORCE_VAL;
	MV_REG_WRITE(CORE_DIV_CLK_CTRL(0), nVal);

	/* Set reload ratio bit 0x00018740[8] to 1'b1 */
	MV_REG_BIT_SET(CORE_DIV_CLK_CTRL(0), CORE_DIVCLK_RELOAD_RATIO_MASK);
	mvOsDelay(1); /*  msec */
	/* Set reload ratio bit 0x00018740[8] to 0'b1 */
	MV_REG_BIT_RESET(CORE_DIV_CLK_CTRL(0), CORE_DIVCLK_RELOAD_RATIO_MASK);

	/* Return calculated nand clock frequency */
	return (MV_PLL_IN_CLK)/(2 * divider);
}

#define CONFIG_SYS_TCLK 250000000
static int nand_init(void)
{
	int ret = 0;
	MV_NFC_INFO nfcInfo;

	g_info.tclk = CONFIG_SYS_TCLK;
	g_info.num_devs       = 1;
	g_info.nfc_width      = 8;
	g_info.num_cs         = 1;
	g_info.ecc_type = MV_NFC_ECC_BCH_2K;//mvBoardNandECCModeGet();
	g_info.mmio_phys_base = MV_NFC_REGS_BASE;

	/* Initialize NFC HAL */
	nfcInfo.ioMode = MV_NFC_PIO_ACCESS;
	nfcInfo.eccMode = g_info.ecc_type;

	nfcInfo.ifMode = MV_NFC_IF_1X8;
	nfcInfo.autoStatusRead = MV_FALSE;
	nfcInfo.tclk = g_info.tclk;
	nfcInfo.readyBypass = MV_FALSE;
	nfcInfo.osHandle = NULL;
	nfcInfo.regsPhysAddr = INTER_REGS_BASE;
	{
		struct MV_NFC_HAL_DATA halData;
		memset(&halData, 0, sizeof(halData));
		halData.mvCtrlNandClkSetFunction = mvCtrlNandClkSet;
		mvNfcInit(&nfcInfo, &g_info.nfcCtrl, &halData);
	}
	mvNfcSelectChip(&g_info.nfcCtrl, MV_NFC_CS_0);
	mvNfcIntrSet(&g_info.nfcCtrl,  0xFFF, MV_FALSE);

	ret = orion_nfc_init_buff(&g_info);
	if (ret){
		DB(printf("nand_init: orion_nfc_init_buff failed\n"));
		goto fail_free_buf;
	}

	/* Clear all old events on the status register */
	MV_REG_WRITE(NFC_STATUS_REG, MV_REG_READ(NFC_STATUS_REG));

	ret = orion_nfc_detect_flash(&g_info);
	if (ret) {
		DB(printf("nand_init: failed to detect flash\n"));
		ret = -ENODEV;
		goto fail_free_buf;
	}

	return 0;

fail_free_buf:
	free(g_info.data_buff);
	return ret;
}

static int armada38x_nand_block_isbad(MtdDev *mtd, uint64_t offs)
{
	int ret;
	int bad_block;

	DB(printf("%s 0x%llx\n", __func__, offs));

	if (!g_info.initialized)
		return -EPERM;
	if (offs >= mtd->size)
		return -EINVAL;
	if (offs & (mtd->erasesize - 1))
		return -EINVAL;

	ret = nand_block_isbad(mtd, offs, &bad_block);
	if (ret < 0)
		return ret;

	return bad_block;
}

static int armada38x_nand_block_markbad(MtdDev *mtd, uint64_t offs)
{
        int ret;

        DB(printf("%s 0x%llx\n", __func__, offs));

        if (!g_info.initialized)
                return -EPERM;
        if (offs >= mtd->size)
                return -EINVAL;
        if (offs & (mtd->erasesize - 1))
                return -EINVAL;

        ret = nand_block_markbad(mtd, offs);
	return ret;
}


/*
 * Notes:
 * - "from" is expected to be a NAND page address
 * - "len" does not have to be multiple of NAND page size
 */
static int armada38x_nand_read(MtdDev *mtd, uint64_t from, size_t len,
			 size_t *retlen, unsigned char *buf)
{
	int ret;
	size_t bytes;

	DB(printf("%s 0x%x <= 0x%llx [0x%x]\n", __func__,
	      (uint32_t)buf, from, len));

	*retlen = 0;

	if ((from + len) > mtd->size)
		return -EINVAL;
	if (from & (mtd->writesize - 1))
		return -EINVAL;
	if (buf == NULL)
		return -EINVAL;

	while (len > 0) {

		bytes = mtd->writesize;
		if (len < mtd->writesize) {
			/* if the remaining length is not a complete page,
			 * we need to read the page on an aligned page buffer
			 * and copy the required data length to buf
			 */
			bytes = len;
		}
		ret = nand_read_page(mtd, from,
				     buf,
				     &mtd->ecc_stats.corrected,
				     &mtd->ecc_stats.failed);
		if (ret < 0)
			return ret;
		from += bytes;
		buf += bytes;
		len -= bytes;
		*retlen += bytes;
	}
	return 0;
}

/*
 * Notes:
 * - "to" is expected to be a NAND page address
 * - "len" is expected to be multiple of NAND page size
 */
static int armada38x_nand_write(MtdDev *mtd, uint64_t to, size_t len,
			  size_t *retlen, const unsigned char *buf)
{
	int ret;

	DB(printf("%s 0x%x => 0x%llx [0x%x]\n", __func__,
	      (uint32_t)buf, to, len));

	*retlen = 0;

	if ((to + len) > mtd->size)
		return -EINVAL;
	if (to & (mtd->writesize - 1)){
		DB(printf("offset not aligned\n"));
		return -EINVAL;
	}
	if (len & (mtd->writesize - 1)){
		DB(printf("len not aligned\n"));
		return -EINVAL;
	}
	if (buf == NULL)
		return -EINVAL;

	while (len > 0) {

		ret = nand_write_page(mtd, to, (unsigned char *)buf);
		if (ret < 0)
			return ret;
		to += mtd->writesize;
		buf += mtd->writesize;
		len -= mtd->writesize;
		*retlen += mtd->writesize;
	}

	return 0;
}

static int armada38x_nand_erase(MtdDev *mtd, struct erase_info *instr)
{
	uint64_t offs = instr->addr;
	uint64_t size = instr->len;
	int ret;
	int bad_block;

	DB(printf("%s 0x%llx [0x%llx]\n", __func__, offs, size));

	if (!g_info.initialized){
		return -EPERM;
	}
	if ((offs + size) > mtd->size){
		return -EINVAL;
	}

	while (size > 0) {
		ret = nand_block_isbad(mtd, offs, &bad_block);
		if (ret < 0) {
			instr->fail_addr = offs;
			return ret;
		}
		if (!instr->scrub && bad_block) {
			DB(printf(": cannot erase bad block 0x%llx\n", offs));
			return -EIO;
		}
		ret = nand_erase_block(mtd, offs);
		if (ret < 0) {
			instr->fail_addr = offs;
			return ret;
		}
		offs += mtd->erasesize;
		if(size > mtd->erasesize)
			size -= mtd->erasesize;
		else 
			break;
	}
	return 0;
}

static int armada38x_nand_update(MtdDevCtrlr *mtd)
{
	unsigned int page_size = 0;
        unsigned int total_page_size = 0;
        unsigned int block_size = 0;
        unsigned int block_num = 0;
        unsigned int oobsize = 0;
	int ret;
	if (g_info.initialized)
		return 0;

	ret = nand_init();
	if (ret)
		return ret;

	mvNfcFlashPageSizeGet(&g_info.nfcCtrl, &page_size, &total_page_size);
	mvNfcFlashBlockSizeGet(&g_info.nfcCtrl, &block_size);
	mvNfcFlashBlockNumGet(&g_info.nfcCtrl, &block_num);
	mvNfcFlashOobSizeGet(&g_info.nfcCtrl, &oobsize);

	mtd->dev->size = block_num*block_size;
	mtd->dev->erasesize = block_size;
	mtd->dev->writesize = page_size;
	mtd->dev->oobsize = oobsize;

	mtd->dev->erase = armada38x_nand_erase;
	mtd->dev->read = armada38x_nand_read;
	mtd->dev->write = armada38x_nand_write;
	mtd->dev->block_isbad = armada38x_nand_block_isbad;
	mtd->dev->block_markbad = armada38x_nand_block_markbad;

	g_info.page_shift = __ffs(mtd->dev->writesize) - 1;
	g_info.pagemask = (mtd->dev->size >> g_info.page_shift) - 1;
	g_info.initialized = 1;
	return 0;
}

extern void set_nand_controller(MtdDevCtrlr *ctrl);
MtdDevCtrlr *new_armada38x_nand()
{
	if(mtd_ctrl)
		return mtd_ctrl;
	
	mtd_ctrl = xzalloc(sizeof(*mtd_ctrl));
	MtdDev *dev = xzalloc(sizeof(*dev));

	mtd_ctrl->update = armada38x_nand_update;
	mtd_ctrl->dev = dev;
	mtd_ctrl->dev->priv = &g_info;

	g_info.initialized = 0;
	if (CONFIG_CLI)
               set_nand_controller(mtd_ctrl);

	return mtd_ctrl;
}
