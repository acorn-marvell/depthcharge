#include "armada38x_spi.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libpayload.h>


/******************************************************************************
base type define
*******************************************************************************/
/* The following is a list of Marvell status    */
#define MV_ERROR		    (-1)
#define MV_OK			    (0)	/* Operation succeeded                   */
#define MV_FAIL			    (1)	/* Operation failed                      */
#define MV_BAD_VALUE        (2)	/* Illegal value (general)               */
#define MV_OUT_OF_RANGE     (3)	/* The value is out of range             */
#define MV_BAD_PARAM        (4)	/* Illegal parameter in function called  */
#define MV_BAD_PTR          (5)	/* Illegal pointer value                 */
#define MV_BAD_SIZE         (6)	/* Illegal size                          */
#define MV_BAD_STATE        (7)	/* Illegal state of state machine        */
#define MV_SET_ERROR        (8)	/* Set operation failed                  */
#define MV_GET_ERROR        (9)	/* Get operation failed                  */
#define MV_CREATE_ERROR     (10)	/* Fail while creating an item           */
#define MV_NOT_FOUND        (11)	/* Item not found                        */
#define MV_NO_MORE          (12)	/* No more items found                   */
#define MV_NO_SUCH          (13)	/* No such item                          */
#define MV_TIMEOUT          (14)	/* Time Out                              */
#define MV_NO_CHANGE        (15)	/* Parameter(s) is already in this value */
#define MV_NOT_SUPPORTED    (16)	/* This request is not support           */
#define MV_NOT_IMPLEMENTED  (17)	/* Request supported but not implemented */
#define MV_NOT_INITIALIZED  (18)	/* The item is not initialized           */
#define MV_NO_RESOURCE      (19)	/* Resource not available (memory ...)   */
#define MV_FULL             (20)	/* Item is full (Queue or table etc...)  */
#define MV_EMPTY            (21)	/* Item is empty (Queue or table etc...) */
#define MV_INIT_ERROR       (22)	/* Error occured while INIT process      */
#define MV_HW_ERROR         (23)	/* Hardware error                        */
#define MV_TX_ERROR         (24)	/* Transmit operation not succeeded      */
#define MV_RX_ERROR         (25)	/* Recieve operation not succeeded       */
#define MV_NOT_READY	    (26)	/* The other side is not ready yet       */
#define MV_ALREADY_EXIST    (27)	/* Tried to create existing item         */
#define MV_OUT_OF_CPU_MEM   (28)	/* Cpu memory allocation failed.         */
#define MV_NOT_STARTED      (29)	/* Not started yet                       */
#define MV_BUSY             (30)	/* Item is busy.                         */
#define MV_TERMINATE        (31)	/* Item terminates it's work.            */
#define MV_NOT_ALIGNED      (32)	/* Wrong alignment                       */
#define MV_NOT_ALLOWED      (33)	/* Operation NOT allowed                 */
#define MV_WRITE_PROTECT    (34)	/* Write protected                       */
#define MV_DROPPED          (35)	/* Packet dropped                        */
#define MV_STOLEN           (36)	/* Packet stolen */
#define MV_CONTINUE         (37)        /* Continue */
#define MV_RETRY		    (38)	/* Operation failed need retry           */

#define MV_INVALID  (int)(-1)

#define MV_FALSE	0
#define MV_TRUE     (!(MV_FALSE))



#define MV_6820_DEV_ID		0x6820
#define MV_6810_DEV_ID		0x6810
#define MV_BOARD_TCLK_250MHZ    250000000

#define MV_MEMIO32_READ(addr)        ((*((volatile unsigned int*)(addr))))
#define MV_32BIT_LE(X)  (X)
#define MV_32BIT_LE_FAST(val)            MV_32BIT_LE(val)
/* 32bit read in little endian mode */
static unsigned int MV_MEMIO_LE32_READ(unsigned int addr)
{
	unsigned int data;

	data= (unsigned int)MV_MEMIO32_READ(addr);

	return (unsigned int)MV_32BIT_LE_FAST(data);
}
#define INTER_REGS_BASE			0xF1000000
#define MV_REG_READ(offset)	(MV_MEMIO_LE32_READ(INTER_REGS_BASE | (offset)))
#define MV_SPI_REG_READ		MV_REG_READ

#define MV_SPI_REGS_OFFSET(unit)                (0x10600 + (unit * 0x80))
#define MV_SPI_REGS_BASE(unit)		(MV_SPI_REGS_OFFSET(unit))
#define		MV_SPI_IF_CONFIG_REG(spiId)			(MV_SPI_REGS_BASE(spiId) + 0x04)
#define		MV_SPI_SPR_OFFSET				0
#define		MV_SPI_SPR_MASK					(0xF << MV_SPI_SPR_OFFSET)
#define		MV_SPI_SPPR_0_OFFSET				4
#define		MV_SPI_SPPR_0_MASK				(0x1 << MV_SPI_SPPR_0_OFFSET)
#define		MV_SPI_SPPR_HI_OFFSET				6
#define		MV_SPI_SPPR_HI_MASK				(0x3 << MV_SPI_SPPR_HI_OFFSET)

#define MV_MEMIO32_WRITE(addr, data) ((*((volatile unsigned int*)(addr))) = ((unsigned int)(data)))
#define MV_MEMIO_LE32_WRITE(addr, data) MV_MEMIO32_WRITE(addr, MV_32BIT_LE_FAST(data))
#define MV_REG_WRITE(offset, val) MV_MEMIO_LE32_WRITE((INTER_REGS_BASE | (offset)), (val))
#define MV_SPI_REG_WRITE	MV_REG_WRITE

#define MV_REG_BIT_SET(offset, bitMask)                 \
        (MV_MEMIO32_WRITE((INTER_REGS_BASE | (offset)), \
         (MV_MEMIO32_READ(INTER_REGS_BASE | (offset)) | \
          MV_32BIT_LE_FAST(bitMask))))
#define MV_SPI_REG_BIT_SET	MV_REG_BIT_SET
#define		MV_SPI_BYTE_LENGTH_OFFSET			5	/* bit 5 */
#define		MV_SPI_BYTE_LENGTH_MASK				(0x1  << MV_SPI_BYTE_LENGTH_OFFSET)

#define MV_REG_BIT_RESET(offset,bitMask)                \
        (MV_MEMIO32_WRITE((INTER_REGS_BASE | (offset)), \
         (MV_MEMIO32_READ(INTER_REGS_BASE | (offset)) & \
          MV_32BIT_LE_FAST(~bitMask))))
#define MV_SPI_REG_BIT_RESET	MV_REG_BIT_RESET
#define		MV_SPI_IF_CTRL_REG(spiId)			(MV_SPI_REGS_BASE(spiId) + 0x00)
#define		MV_SPI_CS_ENABLE_OFFSET				0		/* bit 0 */
#define		MV_SPI_CS_ENABLE_MASK				(0x1  << MV_SPI_CS_ENABLE_OFFSET)

typedef enum {
	SPI_TYPE_FLASH = 0,
	SPI_TYPE_SLIC_ZARLINK_SILABS,
	SPI_TYPE_SLIC_LANTIQ,
	SPI_TYPE_SLIC_ZSI,
	SPI_TYPE_SLIC_ISI
} MV_SPI_TYPE;

#define		MV_SPI_CS_NUM_OFFSET				2
#define		MV_SPI_CS_NUM_MASK				(0x7 << MV_SPI_CS_NUM_OFFSET)
#define		MV_SPI_CPOL_OFFSET				11
#define		MV_SPI_CPOL_MASK				(0x1 << MV_SPI_CPOL_OFFSET)
#define		MV_SPI_CPHA_OFFSET				12
#define		MV_SPI_CPHA_MASK				(0x1 << MV_SPI_CPHA_OFFSET)
#define		MV_SPI_TXLSBF_OFFSET				13
#define		MV_SPI_TXLSBF_MASK				(0x1 << MV_SPI_TXLSBF_OFFSET)
#define		MV_SPI_RXLSBF_OFFSET				14
#define		MV_SPI_RXLSBF_MASK				(0x1 << MV_SPI_RXLSBF_OFFSET)

#define NAND_SPI_PAGE_SIZE       2048
#define NAND_SPI_OOB_SIZE        64

/* SPI transfer flags */
#define SPI_XFER_BEGIN	0x01			/* Assert CS before transfer */
#define SPI_XFER_END	0x02			/* Deassert CS after transfer */

#define		MV_SPI_INT_CAUSE_REG(spiId)			(MV_SPI_REGS_BASE(spiId) + 0x10)
#define		MV_SPI_DATA_OUT_REG(spiId)			(MV_SPI_REGS_BASE(spiId) + 0x08)
#define		MV_SPI_WAIT_RDY_MAX_LOOP			100000
#define		MV_SPI_DATA_IN_REG(spiId)			(MV_SPI_REGS_BASE(spiId) + 0x0c)

#define CMD_READ_ID              0x9f        /*from spi_flash_internal.h        */
#define CMD_RDSR                 0x0f        /* Read Status Register            */
#define CMD_READ_TO_CACHE        0x13        /* Read Data Bytes to cache        */
#define CMD_PP_TO_CACHE          0x02        /* Program Load to cache           */
#define CMD_READ_FROM_CACHE      0x03        /* Read Data Bytes from cache      */
#define CMD_FAST_READ            0x0b        /* Read Data Bytes at Higher Speed */
#define CMD_WREN                 0x06        /* Write Enable                    */
#define CMD_PROGRAM_EXECUTE      0x10        /* Program execute                 */
#define CMD_WRSR                 0x1f        /* Write Status Register           */
#define CMD_SECTOR_ERASE         0xd8        /* Sector Erase                    */

#define SR_OIP_BIT              (1 << 0)     /* Write-in-Progress               */
#define ECC_EN_BIT              (1 << 4)     /* Ecc enabled                     */

#define SPI_FLASH_PROG_TIMEOUT		(2 * 1000)
#define	SPI_CPHA	0x01			/* clock phase */
#define	SPI_CPOL	0x02			/* clock polarity */
#define	SPI_MODE_3	(SPI_CPOL|SPI_CPHA)

#define CONFIG_ENV_SPI_MAX_HZ           50000000
#define CONFIG_ENV_SPI_CS               0
#define CONFIG_ENV_SPI_BUS              0
#define CONFIG_SF_DEFAULT_SPEED        CONFIG_ENV_SPI_MAX_HZ
#define CONFIG_SF_DEFAULT_MODE         SPI_MODE_3
#define CONFIG_SF_DEFAULT_CS		CONFIG_ENV_SPI_CS
#define CONFIG_SF_DEFAULT_BUS		CONFIG_ENV_SPI_BUS

#define CONFIG_SPI_FLASH_STMICRO
#define CONFIG_SPI_FLASH_MACRONIX
#define CONFIG_SPI_FLASH_WINBOND

#define CMD_WRITE_STATUS		0x01
#define CMD_PAGE_PROGRAM		0x02
#define CMD_WRITE_DISABLE		0x04
#define CMD_READ_STATUS			0x05
#define CMD_WRITE_ENABLE		0x06
#define CMD_ERASE_4K			0x20
#define CMD_ERASE_32K			0x52
#define CMD_ERASE_64K			0xd8
#define CMD_ERASE_CHIP			0xc7
#define min(x,y) ((x)<(y)?(x):(y))
#define STATUS_WIP			0x01
#define CMD_READ_ARRAY_FAST		0x0b

#define IDCODE_CONT_LEN 0
#define IDCODE_PART_LEN 5
#define IDCODE_LEN (IDCODE_CONT_LEN + IDCODE_PART_LEN)


/******************************************************************************
base type define end
*******************************************************************************/

/******************************************************************************
struct define
*******************************************************************************/
typedef struct {
	unsigned short		ctrlModel;
	unsigned int		tclk;
} MV_SPI_HAL_DATA;

typedef struct {
	int		clockPolLow;
	enum {
		SPI_CLK_HALF_CYC,
		SPI_CLK_BEGIN_CYC
	}		clockPhase;
	int		txMsbFirst;
	int		rxMsbFirst;
} MV_SPI_IF_PARAMS;

typedef struct {
	/* Does this device support 16 bits access */
	int	en16Bit;
	/* should we assert / disassert CS for each byte we read / write */
	int	byteCsAsrt;
	int clockPolLow;
	unsigned int	baudRate;
	unsigned int clkPhase;
} MV_SPI_TYPE_INFO;

struct spi_slave {
	unsigned int	bus;
	unsigned int	cs;
};

struct nand_spi {
	struct spi_slave *spi;

	char *data_buf;
	int   data_pos;

	unsigned int page_size;
	unsigned int sector_size;
	unsigned int page_per_sector;

	int column;
	int page_addr;
	int ecc_status;
};

struct winbond_spi_flash_params {
	unsigned short	id;
	unsigned short	nr_blocks;
	const char	*name;
};

/******************************************************************************
struct define end 
*******************************************************************************/

/******************************************************************************
param define
*******************************************************************************/
static MV_SPI_HAL_DATA	spiHalData;
static MV_SPI_TYPE_INFO *currSpiInfo = NULL;
static MV_SPI_TYPE_INFO spiTypes[] = {
	{
		.en16Bit = MV_TRUE,
		.clockPolLow = MV_TRUE,
		.byteCsAsrt = MV_FALSE,
		.baudRate = (20 << 20), /*  20M */
		.clkPhase = SPI_CLK_BEGIN_CYC
	},
	{
		.en16Bit = MV_FALSE,
		.clockPolLow = MV_TRUE,
		.byteCsAsrt = MV_TRUE,
		.baudRate = 0x00800000,
		.clkPhase = SPI_CLK_BEGIN_CYC
	},
	{
		.en16Bit = MV_FALSE,
		.clockPolLow = MV_TRUE,
		.byteCsAsrt = MV_FALSE,
		.baudRate = 0x00800000,
		.clkPhase = SPI_CLK_BEGIN_CYC
	},
	{
		.en16Bit = MV_FALSE,
		.clockPolLow = MV_TRUE,
		.byteCsAsrt = MV_TRUE,
		.baudRate = 0x00800000,
		.clkPhase = SPI_CLK_HALF_CYC
	},
	{
		.en16Bit = MV_FALSE,
		.clockPolLow = MV_FALSE,
		.byteCsAsrt = MV_TRUE,
		.baudRate = 0x00200000,
		.clkPhase = SPI_CLK_HALF_CYC
	}
};

static const struct winbond_spi_flash_params winbond_spi_flash_table[] = {
	{
		.id			= 0x3013,
		.nr_blocks		= 8,
		.name			= "W25X40",
	},
	{
		.id			= 0x3015,
		.nr_blocks		= 32,
		.name			= "W25X16",
	},
	{
		.id			= 0x3016,
		.nr_blocks		= 64,
		.name			= "W25X32",
	},
	{
		.id			= 0x3017,
		.nr_blocks		= 128,
		.name			= "W25X64",
	},
	{
		.id			= 0x4014,
		.nr_blocks		= 16,
		.name			= "W25Q80BL",
	},
	{
		.id			= 0x4015,
		.nr_blocks		= 32,
		.name			= "W25Q16",
	},
	{
		.id			= 0x4016,
		.nr_blocks		= 64,
		.name			= "W25Q32",
	},
	{
		.id			= 0x4017,
		.nr_blocks		= 128,
		.name			= "W25Q64",
	},
	{
		.id			= 0x4018,
		.nr_blocks		= 256,
		.name			= "W25Q128",
	},
	{
		.id			= 0x5014,
		.nr_blocks		= 128,
		.name			= "W25Q80",
	},
        {
                .id         = 0x6017,
                .nr_blocks      = 128,
                .name           = "W25Q64",
        },
};

struct mrvl_spi_flash *spi_flash_probe_winbond(struct spi_slave *spi, unsigned char *idcode);

static const struct {
	const unsigned char shift;
	const unsigned char idcode;
	struct mrvl_spi_flash *(*probe) (struct spi_slave *spi, unsigned char *idcode);
} flashes[] = {
#ifdef CONFIG_SPI_FLASH_WINBOND
	{ 0, 0xef, spi_flash_probe_winbond, },
#endif
};

/******************************************************************************
param define end
*******************************************************************************/



int mvSpiBaudRateSet(unsigned char spiId, unsigned int serialBaudRate)
{
	unsigned int spr, sppr;
	unsigned int divider;
	unsigned int bestSpr = 0, bestSppr = 0;
	unsigned char exactMatch = 0;
	unsigned int minBaudOffset = 0xFFFFFFFF;
	unsigned int cpuClk = spiHalData.tclk; /*mvCpuPclkGet();*/
	unsigned int tempReg;

	/* Find the best prescale configuration - less or equal */
	for (spr = 1; spr <= 15; spr++) {
		for (sppr = 0; sppr <= 7; sppr++) {
			divider = spr * (1 << sppr);
			/* check for higher - irrelevent */
			if ((cpuClk / divider) > serialBaudRate)
				continue;

			/* check for exact fit */
			if ((cpuClk / divider) == serialBaudRate) {
				bestSpr = spr;
				bestSppr = sppr;
				exactMatch = 1;
				break;
			}

			/* check if this is better than the previous one */
			if ((serialBaudRate - (cpuClk / divider)) < minBaudOffset) {
				minBaudOffset = (serialBaudRate - (cpuClk / divider));
				bestSpr = spr;
				bestSppr = sppr;
			}
		}

		if (exactMatch == 1)
			break;
	}

	if (bestSpr == 0) {
		printf("%s ERROR: SPI baud rate prescale error!\n", __func__);
		return MV_OUT_OF_RANGE;
	}

	/* configure the Prescale */
	tempReg = MV_SPI_REG_READ(MV_SPI_IF_CONFIG_REG(spiId)) & ~(MV_SPI_SPR_MASK | MV_SPI_SPPR_0_MASK |
			MV_SPI_SPPR_HI_MASK);
	tempReg |= ((bestSpr << MV_SPI_SPR_OFFSET) |
			((bestSppr & 0x1) << MV_SPI_SPPR_0_OFFSET) |
			((bestSppr >> 1) << MV_SPI_SPPR_HI_OFFSET));
	MV_SPI_REG_WRITE(MV_SPI_IF_CONFIG_REG(spiId), tempReg);

	return MV_OK;
}

void mvSpiCsDeassert(unsigned char spiId)
{
	MV_SPI_REG_BIT_RESET(MV_SPI_IF_CTRL_REG(spiId), MV_SPI_CS_ENABLE_MASK);
}

int mvSpiCsSet(unsigned char spiId, unsigned char csId)
{
	unsigned int	ctrlReg;
	static unsigned char lastCsId = 0xFF;

	if (csId > 7)
		return MV_BAD_PARAM;

	if (lastCsId == csId)
		return MV_OK;

	ctrlReg = MV_SPI_REG_READ(MV_SPI_IF_CTRL_REG(spiId));
	ctrlReg &= ~MV_SPI_CS_NUM_MASK;
	ctrlReg |= (csId << MV_SPI_CS_NUM_OFFSET);
	MV_SPI_REG_WRITE(MV_SPI_IF_CTRL_REG(spiId), ctrlReg);

	lastCsId = csId;

	return MV_OK;
}

int mvSpiIfConfigSet(unsigned char spiId, MV_SPI_IF_PARAMS *ifParams)
{
	unsigned int	ctrlReg;

	ctrlReg = MV_SPI_REG_READ(MV_SPI_IF_CONFIG_REG(spiId));

	/* Set Clock Polarity */
	ctrlReg &= ~(MV_SPI_CPOL_MASK | MV_SPI_CPHA_MASK |
			MV_SPI_TXLSBF_MASK | MV_SPI_RXLSBF_MASK);
	if (ifParams->clockPolLow)
		ctrlReg |= MV_SPI_CPOL_MASK;

	if (ifParams->clockPhase == SPI_CLK_BEGIN_CYC)
		ctrlReg |= MV_SPI_CPHA_MASK;

	if (ifParams->txMsbFirst)
		ctrlReg |= MV_SPI_TXLSBF_MASK;

	if (ifParams->rxMsbFirst)
		ctrlReg |= MV_SPI_RXLSBF_MASK;

	MV_SPI_REG_WRITE(MV_SPI_IF_CONFIG_REG(spiId), ctrlReg);

	return MV_OK;
}

int mvSpiParamsSet(unsigned char spiId, unsigned char csId, MV_SPI_TYPE type)
{
	MV_SPI_IF_PARAMS ifParams;

	if (MV_OK != mvSpiCsSet(spiId, csId)) {
		printf("Error, setting SPI CS failed\n");
		return MV_ERROR;
	}

	if (currSpiInfo != (&(spiTypes[type]))) {
		currSpiInfo = &(spiTypes[type]);
		mvSpiBaudRateSet(spiId, currSpiInfo->baudRate);

		ifParams.clockPolLow = currSpiInfo->clockPolLow;
		ifParams.clockPhase = currSpiInfo->clkPhase;
		ifParams.txMsbFirst = MV_FALSE;
		ifParams.rxMsbFirst = MV_FALSE;
		mvSpiIfConfigSet(spiId, &ifParams);
	}

	return MV_OK;
}

#define		MV_SPI_TMNG_PARAMS_REG(spiId)			(MV_SPI_REGS_BASE(spiId) + 0x18)
#define		MV_SPI_TMISO_SAMPLE_OFFSET			6
#define		MV_SPI_TMISO_SAMPLE_MASK			(0x3 << MV_SPI_TMISO_SAMPLE_OFFSET)

int mvSpiInit(unsigned char spiId, unsigned int serialBaudRate, MV_SPI_HAL_DATA *halData)
{
	int ret;
	unsigned int timingReg;

	spiHalData.ctrlModel = halData->ctrlModel;
	spiHalData.tclk = halData->tclk;

	/* Set the serial clock */
	ret = mvSpiBaudRateSet(spiId, serialBaudRate);
	if (ret != MV_OK)
		return ret;

	/* Configure the default SPI mode to be 16bit */
	MV_SPI_REG_BIT_SET(MV_SPI_IF_CONFIG_REG(spiId), MV_SPI_BYTE_LENGTH_MASK);

	timingReg = MV_REG_READ(MV_SPI_TMNG_PARAMS_REG(spiId));
        timingReg &= ~MV_SPI_TMISO_SAMPLE_MASK;
        timingReg |= (0x2) << MV_SPI_TMISO_SAMPLE_OFFSET;
        MV_REG_WRITE(MV_SPI_TMNG_PARAMS_REG(spiId), timingReg);

	/* Verify that the CS is deasserted */
	mvSpiCsDeassert(spiId);

	mvSpiParamsSet(spiId, 0, SPI_TYPE_FLASH);

	return MV_OK;
}

int mvSysSpiInit(unsigned char spiId, unsigned int serialBaudRate)
{
	MV_SPI_HAL_DATA halData;

	halData.ctrlModel = MV_6810_DEV_ID;
	halData.tclk = MV_BOARD_TCLK_250MHZ;

	return mvSpiInit(spiId, serialBaudRate, &halData);
}

void mvSpiCsAssert(unsigned char spiId)
{
	MV_SPI_REG_BIT_SET(MV_SPI_IF_CTRL_REG(spiId), MV_SPI_CS_ENABLE_MASK);
}

int mvSpi8bitDataTxRx(unsigned char spiId, unsigned char txData, unsigned char *pRxData)
{
	unsigned int i;
	int ready = MV_FALSE;

	if (currSpiInfo->byteCsAsrt)
		mvSpiCsAssert(spiId);

	/* First clear the bit in the interrupt cause register */
	MV_SPI_REG_WRITE(MV_SPI_INT_CAUSE_REG(spiId), 0x0);

	/* Transmit data */
	//printf("txData = %d ...\n",txData);
	MV_SPI_REG_WRITE(MV_SPI_DATA_OUT_REG(spiId), txData);
	//unsigned char test = MV_SPI_REG_READ(MV_SPI_DATA_OUT_REG(spiId));
	//printf("test = %d ...\n",test);

	/* wait with timeout for memory ready */
	for (i = 0; i < MV_SPI_WAIT_RDY_MAX_LOOP; i++) {
		if (MV_SPI_REG_READ(MV_SPI_INT_CAUSE_REG(spiId))) {
			ready = MV_TRUE;
			break;
		}
	}

	if (!ready) {
		if (currSpiInfo->byteCsAsrt) {
			mvSpiCsDeassert(spiId);
			/* WA to compansate Zarlink SLIC CS off time */
			//mvOsUDelay(4);
			udelay(4);
		}
		return MV_TIMEOUT;
	}

	/* check that the RX data is needed */
	if (pRxData)
		*pRxData = MV_SPI_REG_READ(MV_SPI_DATA_IN_REG(spiId));

	if (currSpiInfo->byteCsAsrt) {
		mvSpiCsDeassert(spiId);
		/* WA to compansate Zarlink SLIC CS off time */
		//mvOsUDelay(4);
		udelay(4);
	}

	return MV_OK;
}


static int mrvl_spi_xfer(struct spi_slave *slave, unsigned int bitlen, const void *dout,
	     void *din, unsigned long flags)
{
	int ret;
	unsigned char* pdout = (unsigned char*)dout;
	unsigned char* pdin = (unsigned char*)din;
	int tmp_bitlen = bitlen;
	unsigned char tmp_dout = 0;

	/* Verify that the SPI mode is in 8bit mode */
	MV_REG_BIT_RESET(MV_SPI_IF_CONFIG_REG(slave->bus), MV_SPI_BYTE_LENGTH_MASK);

	/* TX/RX in 8bit chanks */

	//int i = 0;
	while (tmp_bitlen > 0)
	{
		if(pdout)
			tmp_dout = (*pdout) & 0xff;

		/* Transmitted and wait for the transfer to be completed */
		if ((ret = mvSpi8bitDataTxRx(slave->bus,tmp_dout, pdin)) != MV_OK)
			return ret;

		/* increment the pointers */
		
		if (pdin)
		{
			//printf("in[%d]=[0x%x]    ",i,*pdin);
			pdin++;
			//i++;
		}
		
		if (pdout)
		{
			//printf("out[%d]=[0x%x]    ",i,*pdout);
			pdout++;
			//i++;
		}

		tmp_bitlen-=8;
	}

	return 0;
}

static int spi_flash_read_write(struct spi_slave *spi,
				const unsigned char *cmd, unsigned int cmd_len,
				const unsigned char *data_out, unsigned char *data_in,
				unsigned int data_len)
{
	unsigned long flags = SPI_XFER_BEGIN;
	int ret;

	//printf("data_len = %d ...\n ",data_len);

	if (data_len == 0)
		flags |= SPI_XFER_END;

	ret = mrvl_spi_xfer(spi, cmd_len * 8, cmd, NULL, flags);
	if (ret) {
		printf("SF: Failed to send command (%zu bytes): %d\n",
				cmd_len, ret);
	} else if (data_len != 0) {
		ret = mrvl_spi_xfer(spi, data_len * 8, data_out, data_in, SPI_XFER_END);
		if (ret)
			printf("SF: Failed to transfer %zu bytes of data: %d\n",
					data_len, ret);
	}

	return ret;
}


int spi_flash_cmd_read(struct spi_slave *spi, const unsigned char *cmd,
		unsigned int cmd_len, void *data, unsigned int data_len)
{
	return spi_flash_read_write(spi, cmd, cmd_len, NULL, data, data_len);
}

int spi_flash_cmd(struct spi_slave *spi, unsigned char cmd, void *response, unsigned int len)
{
	return spi_flash_cmd_read(spi, &cmd, 1, response, len);
}

static struct spi_slave *spi_setup_slave(unsigned int bus, unsigned int cs,
				unsigned int max_hz, unsigned int mode)
{
	struct spi_slave *slave;

	slave = malloc(sizeof(struct spi_slave));
	if (!slave)
		return NULL;

	slave->bus = bus;
	slave->cs = cs;

	mvSysSpiInit(bus,max_hz);
	return slave;
}

struct mrvl_spi_flash *spi_flash_probe_winbond(struct spi_slave *spi, unsigned char *idcode)
{
	const struct winbond_spi_flash_params *params;
	struct mrvl_spi_flash *flash;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(winbond_spi_flash_table); i++) {
		params = &winbond_spi_flash_table[i];
		if (params->id == ((idcode[1] << 8) | idcode[2]))
			break;
	}

	if (i == ARRAY_SIZE(winbond_spi_flash_table)) {
		printf("SF: Unsupported Winbond ID %02x%02x\n",
				idcode[1], idcode[2]);
		return NULL;
	}

	flash = malloc(sizeof(*flash));
	if (!flash) {
		printf("SF: Failed to allocate memory\n");
		return NULL;
	}

	flash->spi = spi;
	flash->name = params->name;

	flash->page_size = 256;
	flash->sector_size = 4096;
	flash->size = 4096 * 16 * params->nr_blocks;
	flash->addr_cycles = 3;

	return flash;
}

mrvl_spi_flash *spi_flash_probe(unsigned int bus, unsigned int cs,
		unsigned int max_hz, unsigned int spi_mode)
{
	struct spi_slave *spi;
	struct mrvl_spi_flash *flash = NULL;
	int ret, i, shift;
	unsigned char idcode[IDCODE_LEN], *idp;

	spi = spi_setup_slave(bus, cs, max_hz, spi_mode);
	if (!spi) {
		printf("SF: Failed to set up slave\n");
		return NULL;
	}

	/* Read the ID codes */
	mvSpiCsSet(bus,cs);
	mvSpiCsAssert(bus);
	ret = spi_flash_cmd(spi, CMD_READ_ID, idcode, sizeof(idcode));
	mvSpiCsDeassert(bus);
	if (ret)
		goto err_read_id;

	/* count the number of continuation bytes */
	for (shift = 0, idp = idcode;
	     shift < IDCODE_CONT_LEN && *idp == 0x7f;
	     ++shift, ++idp)
		continue;

	/* search the table for matches in shift and id */
	for (i = 0; i < ARRAY_SIZE(flashes); ++i)
		if (flashes[i].shift == shift && flashes[i].idcode == *idp) {
			/* we have a match, call probe */
			flash = flashes[i].probe(spi, idp);
			if (flash)
				break;
		}

	if (!flash) {
		printf("SF: Unsupported manufacturer %02x\n", *idp);
		goto err_manufacturer_probe;
	}

	printf("SF: Detected %s with page size ", flash->name);
	printf("flash->sector_size = %d ...\n",flash->sector_size);
	printf("flash->size = %d ...\n",flash->size);

	return flash;

err_manufacturer_probe:
err_read_id:
	printf("error \n");

	return NULL;
}

static struct SpiController* g_spicontroller = NULL;

static int spi_claim_bus(SpiOps *spi_ops)
{
	printf("spi_claim_bus \n");
	mvSpiCsSet(g_spicontroller->spi_flash->spi->bus, g_spicontroller->spi_flash->spi->cs);
	mvSpiCsAssert(g_spicontroller->spi_flash->spi->bus);
	return 0;
}

static int spi_release_bus(SpiOps *ops)
{
	printf("spi_release_bus \n");
	mvSpiCsDeassert(g_spicontroller->spi_flash->spi->bus);
	return 0;
}

static int spi_xfer(SpiOps *ops, void *din, const void *dout, unsigned int bytes)
{
	int ret = -1;
	//printf("bytes = %d...\n",bytes);
	if(bytes)
	{
		ret = mrvl_spi_xfer(g_spicontroller->spi_flash->spi, bytes*8, dout, din, SPI_XFER_BEGIN | SPI_XFER_END);
	}
	else
	{
		ret = mrvl_spi_xfer(g_spicontroller->spi_flash->spi, bytes*8, dout, din,SPI_XFER_BEGIN | SPI_XFER_END);
	}
	return ret;
}

SpiController *new_spi(unsigned bus_num, unsigned cs)
{
	if(g_spicontroller)
		return g_spicontroller;
	else
	{
		g_spicontroller = (SpiController*)malloc(sizeof(SpiController));
		if(!g_spicontroller){
			printf("no mem to malloc spi controller\n");
			return NULL;
		}
		g_spicontroller->ops.start= spi_claim_bus;
		g_spicontroller->ops.transfer = spi_xfer;
		g_spicontroller->ops.stop = spi_release_bus;

		unsigned int config_speed = CONFIG_SF_DEFAULT_SPEED;
		unsigned int config_mode = CONFIG_SF_DEFAULT_MODE;
		g_spicontroller->spi_flash =  spi_flash_probe(bus_num, cs, config_speed, config_mode);

		return g_spicontroller;
	}
}




























