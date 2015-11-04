/*
 * Copyright 2015 Google Inc.
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

#include <libpayload.h>
#include <stdio.h>
#include "config.h"
#include "base/container_of.h"
#include "board/cyclone/common.h"
#include "armada38x_i2c.h"


#undef MV_DEBUG
//#define MV_DEBUG
#ifdef MV_DEBUG
#define DB(x) x
#else
#define DB(x)
#endif

#define MAX_I2C_NUM                             2
#define TWSI_SPEED                              100000

#define MAX_RETRY_CNT 1000
#define TWSI_TIMEOUT_VALUE 		0x500

#define MV_TWSI_SLAVE_REGS_OFFSET(chanNum)      (0x11000 + (chanNum * 0x100))
#define MV_TWSI_SLAVE_REGS_BASE(unit)   (MV_TWSI_SLAVE_REGS_OFFSET(unit))
#define TWSI_SLAVE_ADDR_REG(chanNum)	(MV_TWSI_SLAVE_REGS_BASE(chanNum) + 0x00)

#define MV_CPUIF_REGS_OFFSET(cpu)               (0x21800 + (cpu) * 0x100)
#define MV_CPUIF_REGS_BASE(cpu)                 (MV_CPUIF_REGS_OFFSET(cpu))
#define CPU_MAIN_INT_CAUSE_REG(vec, cpu)        (MV_CPUIF_REGS_BASE(cpu) + 0x80 + (vec * 0x4))
#define CPU_MAIN_INT_TWSI_OFFS(i)               (2 + i)
#define CPU_MAIN_INT_CAUSE_TWSI(i)              (31 + i)

#define TWSI_CPU_MAIN_INT_CAUSE_REG(cpu)        CPU_MAIN_INT_CAUSE_REG(1, (cpu))
#define MV_TWSI_CPU_MAIN_INT_CAUSE(chNum, cpu)  TWSI_CPU_MAIN_INT_CAUSE_REG(cpu)

#define MV_MBUS_REGS_OFFSET                     (0x20000)
#define MV_CPUIF_SHARED_REGS_BASE               (MV_MBUS_REGS_OFFSET)
#define CPU_INT_SOURCE_CONTROL_REG(i)           (MV_CPUIF_SHARED_REGS_BASE + 0xB00 + (i * 0x4))

#define CPU_INT_SOURCE_CONTROL_IRQ_OFFS         28
#define CPU_INT_SOURCE_CONTROL_IRQ_MASK         (1 << CPU_INT_SOURCE_CONTROL_IRQ_OFFS )

#define TWSI_SLAVE_ADDR_GCE_ENA         BIT0
#define TWSI_SLAVE_ADDR_7BIT_OFFS       0x1
#define TWSI_SLAVE_ADDR_7BIT_MASK       (0xFF << TWSI_SLAVE_ADDR_7BIT_OFFS)
#define TWSI_SLAVE_ADDR_10BIT_OFFS      0x7
#define TWSI_SLAVE_ADDR_10BIT_MASK      0x300
#define TWSI_SLAVE_ADDR_10BIT_CONST     0xF0

#define TWSI_DATA_REG(chanNum)          (MV_TWSI_SLAVE_REGS_BASE(chanNum) + 0x04)
#define TWSI_DATA_COMMAND_OFFS          0x0
#define TWSI_DATA_COMMAND_MASK          (0x1 << TWSI_DATA_COMMAND_OFFS)
#define TWSI_DATA_COMMAND_WR            (0x1 << TWSI_DATA_COMMAND_OFFS)
#define TWSI_DATA_COMMAND_RD            (0x0 << TWSI_DATA_COMMAND_OFFS)
#define TWSI_DATA_ADDR_7BIT_OFFS        0x1
#define TWSI_DATA_ADDR_7BIT_MASK        (0xFF << TWSI_DATA_ADDR_7BIT_OFFS)
#define TWSI_DATA_ADDR_10BIT_OFFS       0x7
#define TWSI_DATA_ADDR_10BIT_MASK       0x300
#define TWSI_DATA_ADDR_10BIT_CONST      0xF0

#define TWSI_CONTROL_REG(chanNum)       (MV_TWSI_SLAVE_REGS_BASE(chanNum) + 0x08)
#define TWSI_CONTROL_ACK                BIT2
#define TWSI_CONTROL_INT_FLAG_SET       BIT3
#define TWSI_CONTROL_STOP_BIT           BIT4
#define TWSI_CONTROL_START_BIT          BIT5
#define TWSI_CONTROL_ENA                BIT6
#define TWSI_CONTROL_INT_ENA            BIT7

#define TWSI_STATUS_BAUDE_RATE_REG(chanNum)     (MV_TWSI_SLAVE_REGS_BASE(chanNum) + 0x0c)
#define TWSI_BAUD_RATE_N_OFFS           0
#define TWSI_BAUD_RATE_N_MASK           (0x7 << TWSI_BAUD_RATE_N_OFFS)
#define TWSI_BAUD_RATE_M_OFFS           3
#define TWSI_BAUD_RATE_M_MASK           (0xF << TWSI_BAUD_RATE_M_OFFS)

#define TWSI_EXTENDED_SLAVE_ADDR_REG(chanNum)   (MV_TWSI_SLAVE_REGS_BASE(chanNum) + 0x10)
#define TWSI_EXTENDED_SLAVE_OFFS        0
#define TWSI_EXTENDED_SLAVE_MASK        (0xFF << TWSI_EXTENDED_SLAVE_OFFS)

#define TWSI_SOFT_RESET_REG(chanNum)    (MV_TWSI_SLAVE_REGS_BASE(chanNum) + 0x1c)

#define TWSI_BUS_ERROR                                            0x00
#define TWSI_START_CON_TRA                                        0x08
#define TWSI_REPEATED_START_CON_TRA                               0x10
#define TWSI_AD_PLS_WR_BIT_TRA_ACK_REC                            0x18
#define TWSI_AD_PLS_WR_BIT_TRA_ACK_NOT_REC                        0x20
#define TWSI_M_TRAN_DATA_BYTE_ACK_REC                             0x28
#define TWSI_M_TRAN_DATA_BYTE_ACK_NOT_REC                         0x30
#define TWSI_M_LOST_ARB_DUR_AD_OR_DATA_TRA                        0x38
#define TWSI_AD_PLS_RD_BIT_TRA_ACK_REC                            0x40
#define TWSI_AD_PLS_RD_BIT_TRA_ACK_NOT_REC                        0x48
#define TWSI_M_REC_RD_DATA_ACK_TRA                                0x50
#define TWSI_M_REC_RD_DATA_ACK_NOT_TRA                            0x58
#define TWSI_SLA_REC_AD_PLS_WR_BIT_ACK_TRA                        0x60
#define TWSI_M_LOST_ARB_DUR_AD_TRA_AD_IS_TRGT_TO_SLA_ACK_TRA_W    0x68
#define TWSI_GNL_CALL_REC_ACK_TRA                                 0x70
#define TWSI_M_LOST_ARB_DUR_AD_TRA_GNL_CALL_AD_REC_ACK_TRA        0x78
#define TWSI_SLA_REC_WR_DATA_AF_REC_SLA_AD_ACK_TRAN               0x80
#define TWSI_SLA_REC_WR_DATA_AF_REC_SLA_AD_ACK_NOT_TRAN           0x88
#define TWSI_SLA_REC_WR_DATA_AF_REC_GNL_CALL_ACK_TRAN             0x90
#define TWSI_SLA_REC_WR_DATA_AF_REC_GNL_CALL_ACK_NOT_TRAN         0x98
#define TWSI_SLA_REC_STOP_OR_REPEATED_STRT_CON                    0xA0
#define TWSI_SLA_REC_AD_PLS_RD_BIT_ACK_TRA                        0xA8
#define TWSI_M_LOST_ARB_DUR_AD_TRA_AD_IS_TRGT_TO_SLA_ACK_TRA_R    0xB0
#define TWSI_SLA_TRA_RD_DATA_ACK_REC                              0xB8
#define TWSI_SLA_TRA_RD_DATA_ACK_NOT_REC                          0xC0
#define TWSI_SLA_TRA_LAST_RD_DATA_ACK_REC                         0xC8
#define TWSI_SEC_AD_PLS_WR_BIT_TRA_ACK_REC                        0xD0
#define TWSI_SEC_AD_PLS_WR_BIT_TRA_ACK_NOT_REC                    0xD8
#define TWSI_SEC_AD_PLS_RD_BIT_TRA_ACK_REC                        0xE0
#define TWSI_SEC_AD_PLS_RD_BIT_TRA_ACK_NOT_REC                    0xE8
#define TWSI_NO_REL_STS_INT_FLAG_IS_KEPT_0                        0xF8

/* The TWSI interface supports both 7-bit and 10-bit addressing.            */
/* This enumerator describes addressing type.                               */
typedef enum _mvTwsiAddrType {
    ADDR7_BIT,                      /* 7 bit address    */
    ADDR10_BIT                      /* 10 bit address   */
} MV_TWSI_ADDR_TYPE;

/* This structure describes TWSI address.                                   */
typedef struct _mvTwsiAddr {
    uint32_t            address;    /* address          */
    MV_TWSI_ADDR_TYPE   type;       /* Address type     */
} MV_TWSI_ADDR;

/* This structure describes a TWSI slave.                                   */
typedef struct _mvTwsiSlave {
    MV_TWSI_ADDR        slaveAddr;
    int                 validOffset;            /* whether the slave has offset (i.e. Eeprom  etc.)     */
    uint32_t            offset;         /* offset in the slave.                                 */
    int                 moreThan256;    /* whether the ofset is bigger then 256                 */
} MV_TWSI_SLAVE;

/* This enumerator describes TWSI protocol commands.                        */
typedef enum _mvTwsiCmd {
    MV_TWSI_WRITE,   /* TWSI write command - 0 according to spec   */
    MV_TWSI_READ   /* TWSI read command  - 1 according to spec */
} MV_TWSI_CMD;


static void twsiIntFlgClr(uint8_t chanNum);
static uint8_t twsiMainIntGet(uint8_t chanNum);
static void twsiAckBitSet(uint8_t chanNum);
static uint32_t twsiStsGet(uint8_t chanNum);
static void twsiReset(uint8_t chanNum);
static int twsiAddr7BitSet(uint8_t chanNum, uint32_t deviceAddress, MV_TWSI_CMD command);
static int twsiAddr10BitSet(uint8_t chanNum, uint32_t deviceAddress, MV_TWSI_CMD command);
static int twsiDataTransmit(uint8_t chanNum, uint8_t *pBlock, uint32_t blockSize);
static int twsiDataReceive(uint8_t chanNum, uint8_t *pBlock, uint32_t blockSize);
static int twsiTargetOffsSet(uint8_t chanNum, uint32_t offset, uint8_t moreThan256);
static int mvTwsiStartBitSet(uint8_t chanNum);
static int mvTwsiStopBitSet(uint8_t chanNum);
static int mvTwsiAddrSet(uint8_t chanNum, MV_TWSI_ADDR *twsiAddr, MV_TWSI_CMD command);
static uint32_t mvTwsiInit(uint8_t chanNum, uint32_t frequency, uint32_t Tclk, MV_TWSI_ADDR *twsiAddr, uint8_t generalCallEnable);
static int mvTwsiRead(uint8_t chanNum, MV_TWSI_SLAVE *twsiSlave, uint8_t *pBlock, uint32_t blockSize);
static int mvTwsiWrite(uint8_t chanNum, MV_TWSI_SLAVE *twsiSlave, uint8_t *pBlock, uint32_t blockSize);
static uint32_t mvBoardTclkGet(void);
static uint32_t whoAmI(void);

static uint32_t mvBoardTclkGet(void)
{         
        uint32_t tclk;
        tclk = (MV_REG_READ(MPP_SAMPLE_AT_RESET));
        tclk = ((tclk & (1 << 15)) >> 15);
        switch (tclk) {
        case 0:
                return MV_BOARD_TCLK_250MHZ;
        case 1:
                return MV_BOARD_TCLK_200MHZ;
        default:
                return MV_BOARD_TCLK_250MHZ;
        }
} 

static uint8_t twsiTimeoutChk(uint32_t timeout, const char *pString)
{
	if (timeout >= TWSI_TIMEOUT_VALUE) {
		DB(printf("%s", pString));
		return MV_TRUE;
	}
	return MV_FALSE;

}

/*******************************************************************************
* mvTwsiStartBitSet - Set start bit on the bus
*
* DESCRIPTION:
*       This routine sets the start bit on the TWSI bus.
*       The routine first checks for interrupt flag condition, then it sets
*       the start bit  in the TWSI Control register.
*       If the interrupt flag condition check previously was set, the function
*       will clear it.
*       The function then wait for the start bit to be cleared by the HW.
*       Then it waits for the interrupt flag to be set and eventually, the
*       TWSI status is checked to be 0x8 or 0x10(repeated start bit).
*
* INPUT:
*       chanNum - TWSI channel.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_OK is start bit was set successfuly on the bus.
*       MV_FAIL if interrupt flag was set before setting start bit.
*
*******************************************************************************/
static int mvTwsiStartBitSet(uint8_t chanNum)
{
	uint8_t isIntFlag = MV_FALSE;
	uint32_t timeout, temp;

	DB(printf("TWSI: mvTwsiStartBitSet \n"));
	/* check Int flag */
	if (twsiMainIntGet(chanNum))
		isIntFlag = MV_TRUE;
	/* set start Bit */
	temp = MV_REG_READ(TWSI_CONTROL_REG(chanNum));
	MV_REG_WRITE(TWSI_CONTROL_REG(chanNum), temp | TWSI_CONTROL_START_BIT);

	/* in case that the int flag was set before i.e. repeated start bit */
	if (isIntFlag) {
		DB(printf("TWSI: mvTwsiStartBitSet repeated start Bit\n"));
		twsiIntFlgClr(chanNum);
	}

	/* wait for interrupt */
	timeout = 0;
	while (!twsiMainIntGet(chanNum) && (timeout++ < TWSI_TIMEOUT_VALUE))
		;

	/* check for timeout */
	if (MV_TRUE == twsiTimeoutChk(timeout,
				      (const char *)"TWSI: mvTwsiStartBitSet ERROR - Start Clear bit TimeOut .\n"))
		return MV_TIMEOUT;

	/* check that start bit went down */
	if ((MV_REG_READ(TWSI_CONTROL_REG(chanNum)) & TWSI_CONTROL_START_BIT) != 0) {
		printf("TWSI: mvTwsiStartBitSet ERROR - start bit didn't went down\n");
		return MV_FAIL;
	}

	/* check the status */
	temp = twsiStsGet(chanNum);
	if ((TWSI_M_LOST_ARB_DUR_AD_OR_DATA_TRA == temp) || (TWSI_M_LOST_ARB_DUR_AD_TRA_GNL_CALL_AD_REC_ACK_TRA == temp)) {
		DB(printf("TWSI: Lost Arb, status %x \n", temp));
		return MV_RETRY;
	} else if ((temp != TWSI_START_CON_TRA) && (temp != TWSI_REPEATED_START_CON_TRA)) {
		printf("TWSI: mvTwsiStartBitSet ERROR - status %x after Set Start Bit. \n", temp);
		return MV_FAIL;
	}

	return MV_OK;

}

/*******************************************************************************
* mvTwsiStopBitSet - Set stop bit on the bus
*
* DESCRIPTION:
*       This routine set the stop bit on the TWSI bus.
*       The function then wait for the stop bit to be cleared by the HW.
*       Finally the function checks for status of 0xF8.
*
* INPUT:
*	chanNum - TWSI channel
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_TRUE is stop bit was set successfuly on the bus.
*
*******************************************************************************/
static int mvTwsiStopBitSet(uint8_t chanNum)
{
	uint32_t timeout, temp;

	/* Generate stop bit */
	temp = MV_REG_READ(TWSI_CONTROL_REG(chanNum));
	MV_REG_WRITE(TWSI_CONTROL_REG(chanNum), temp | TWSI_CONTROL_STOP_BIT);

	twsiIntFlgClr(chanNum);

	/* wait for stop bit to come down */
	timeout = 0;
	while (((MV_REG_READ(TWSI_CONTROL_REG(chanNum)) & TWSI_CONTROL_STOP_BIT) != 0)
	       && (timeout++ < TWSI_TIMEOUT_VALUE))
		;

	/* check for timeout */
	if (MV_TRUE == twsiTimeoutChk(timeout, (const char *)"TWSI: mvTwsiStopBitSet ERROR - Stop bit TimeOut .\n"))
		return MV_TIMEOUT;

	/* check that the stop bit went down */
	if ((MV_REG_READ(TWSI_CONTROL_REG(chanNum)) & TWSI_CONTROL_STOP_BIT) != 0) {
		printf("TWSI: mvTwsiStopBitSet ERROR - stop bit didn't went down. \n");
		return MV_FAIL;
	}

	/* check the status */
	temp = twsiStsGet(chanNum);
	if ((TWSI_M_LOST_ARB_DUR_AD_OR_DATA_TRA == temp) || (TWSI_M_LOST_ARB_DUR_AD_TRA_GNL_CALL_AD_REC_ACK_TRA == temp)) {
		DB(printf("TWSI: Lost Arb, status %x \n", temp));
		return MV_RETRY;
	} else if (temp != TWSI_NO_REL_STS_INT_FLAG_IS_KEPT_0) {
		printf("TWSI: mvTwsiStopBitSet ERROR - status %x after Stop Bit. \n", temp);
		return MV_FAIL;
	}

	return MV_OK;
}

/*******************************************************************************
* twsiMainIntGet - Get twsi bit from main Interrupt cause.
*
* DESCRIPTION:
*       This routine returns the twsi interrupt flag value.
*
* INPUT:
*       None.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_TRUE is interrupt flag is set, MV_FALSE otherwise.
*
*******************************************************************************/
static uint32_t whoAmI(void)
{
        uint32_t value;

        __asm__ __volatile__("mrc p15, 0, %0, c0, c0, 5   @ read CPUID reg\n" : "=r"(value) : : "memory");
        return value & 0x1;
}
static uint8_t twsiMainIntGet(uint8_t chanNum)
{
	uint32_t temp;

	/* get the int flag bit */

	temp = MV_REG_READ(MV_TWSI_CPU_MAIN_INT_CAUSE(chanNum, whoAmI()));
	if (temp & (1<<CPU_MAIN_INT_TWSI_OFFS(chanNum))) /*    (TWSI_CPU_MAIN_INT_BIT(chanNum))) */
		return MV_TRUE;

	return MV_FALSE;
}

/*******************************************************************************
* twsiIntFlgClr - Clear Interrupt flag.
*
* DESCRIPTION:
*       This routine clears the interrupt flag. It does NOT poll the interrupt
*       to make sure the clear. After clearing the interrupt, it waits for at
*       least 1 miliseconds.
*
* INPUT:
*	chanNum - TWSI channel
*
* OUTPUT:
*       None.
*
* RETURN:
*       None.
*
*******************************************************************************/
static void twsiIntFlgClr(uint8_t chanNum)
{
	uint32_t temp;

	/* wait for 1 mili to prevent TWSI register write after write problems */
	mdelay(1);
	/* clear the int flag bit */
	temp = MV_REG_READ(TWSI_CONTROL_REG(chanNum));
	MV_REG_WRITE(TWSI_CONTROL_REG(chanNum), temp & ~(TWSI_CONTROL_INT_FLAG_SET));

	/* wait for 1 mili sec for the clear to take effect */
	mdelay(1);

	return;
}

/*******************************************************************************
* twsiAckBitSet - Set acknowledge bit on the bus
*
* DESCRIPTION:
*       This routine set the acknowledge bit on the TWSI bus.
*
* INPUT:
*       None.
*
* OUTPUT:
*       None.
*
* RETURN:
*       None.
*
*******************************************************************************/
static void twsiAckBitSet(uint8_t chanNum)
{
	uint32_t temp;

	/*Set the Ack bit */
	temp = MV_REG_READ(TWSI_CONTROL_REG(chanNum));
	MV_REG_WRITE(TWSI_CONTROL_REG(chanNum), temp | TWSI_CONTROL_ACK);

	/* Add delay of 1ms */
	mdelay(1);
	return;
}

/*******************************************************************************
* twsiInit - Initialize TWSI interface
*
* DESCRIPTION:
*       This routine:
*	-Reset the TWSI.
*	-Initialize the TWSI clock baud rate according to given frequancy
*	 parameter based on Tclk frequancy and enables TWSI slave.
*       -Set the ack bit.
*	-Assign the TWSI slave address according to the TWSI address Type.
*
* INPUT:
*	chanNum - TWSI channel
*       frequancy - TWSI frequancy in KHz. (up to 100KHZ)
*
* OUTPUT:
*       None.
*
* RETURN:
*       Actual frequancy.
*
*******************************************************************************/
static uint32_t mvTwsiInit(uint8_t chanNum, uint32_t frequancy, uint32_t Tclk, MV_TWSI_ADDR *pTwsiAddr, uint8_t generalCallEnable)
{
	uint32_t n, m, freq, margin, minMargin = 0xffffffff;
	uint32_t power;
	uint32_t actualFreq = 0, actualN = 0, actualM = 0, val;

	if (frequancy > 100000)
		printf("Warning TWSI frequancy is too high, please use up to 100Khz.\n");

	DB(printf("TWSI: mvTwsiInit - Tclk = %d freq = %d\n", Tclk, frequancy));
	/* Calucalte N and M for the TWSI clock baud rate */
	for (n = 0; n < 8; n++) {
		for (m = 0; m < 16; m++) {
			power = 2 << n;	/* power = 2^(n+1) */
			freq = Tclk / (10 * (m + 1) * power);
			margin = abs(frequancy - freq);

			if ((freq <= frequancy) && (margin < minMargin)) {
				minMargin = margin;
				actualFreq = freq;
				actualN = n;
				actualM = m;
			}
		}
	}
	DB(printf("TWSI: mvTwsiInit - actN %u actM %u actFreq %u\n", actualN, actualM, actualFreq));
	/* Reset the TWSI logic */
	twsiReset(chanNum);

	/* Set the baud rate */
	val = ((actualM << TWSI_BAUD_RATE_M_OFFS) | actualN << TWSI_BAUD_RATE_N_OFFS);
	MV_REG_WRITE(TWSI_STATUS_BAUDE_RATE_REG(chanNum), val);

	/* Enable the TWSI and slave */
	MV_REG_WRITE(TWSI_CONTROL_REG(chanNum), TWSI_CONTROL_ENA | TWSI_CONTROL_ACK);

	/* set the TWSI slave address */
	if (pTwsiAddr->type == ADDR10_BIT) {	/* 10 Bit deviceAddress */
		/* writing the 2 most significant bits of the 10 bit address */
		val = ((pTwsiAddr->address & TWSI_SLAVE_ADDR_10BIT_MASK) >> TWSI_SLAVE_ADDR_10BIT_OFFS);
		/* bits 7:3 must be 0x11110 */
		val |= TWSI_SLAVE_ADDR_10BIT_CONST;
		/* set GCE bit */
		if (generalCallEnable)
			val |= TWSI_SLAVE_ADDR_GCE_ENA;
		/* write slave address */
		MV_REG_WRITE(TWSI_SLAVE_ADDR_REG(chanNum), val);

		/* writing the 8 least significant bits of the 10 bit address */
		val = (pTwsiAddr->address << TWSI_EXTENDED_SLAVE_OFFS) & TWSI_EXTENDED_SLAVE_MASK;
		MV_REG_WRITE(TWSI_EXTENDED_SLAVE_ADDR_REG(chanNum), val);
	} else {		/*7 bit address */

		/* set the 7 Bits address */
		MV_REG_WRITE(TWSI_EXTENDED_SLAVE_ADDR_REG(chanNum), 0x0);
		val = (pTwsiAddr->address << TWSI_SLAVE_ADDR_7BIT_OFFS) & TWSI_SLAVE_ADDR_7BIT_MASK;
		MV_REG_WRITE(TWSI_SLAVE_ADDR_REG(chanNum), val);
	}

	/* unmask twsi int */
	val = MV_REG_READ(TWSI_CONTROL_REG(chanNum));
	MV_REG_WRITE(TWSI_CONTROL_REG(chanNum), val | TWSI_CONTROL_INT_ENA);

	/* unmask twsi int in Interrupt source control register */
	val = (MV_REG_READ(CPU_INT_SOURCE_CONTROL_REG(CPU_MAIN_INT_CAUSE_TWSI(chanNum))) |
							(1<<CPU_INT_SOURCE_CONTROL_IRQ_OFFS));
	MV_REG_WRITE(CPU_INT_SOURCE_CONTROL_REG(CPU_MAIN_INT_CAUSE_TWSI(chanNum)), val);

	/* Add delay of 1ms */
	mdelay(1);

	return actualFreq;
}

/*******************************************************************************
* twsiStsGet - Get the TWSI status value.
*
* DESCRIPTION:
*       This routine returns the TWSI status value.
*
* INPUT:
*	chanNum - TWSI channel
*
* OUTPUT:
*       None.
*
* RETURN:
*       uint32_t - the TWSI status.
*
*******************************************************************************/
static uint32_t twsiStsGet(uint8_t chanNum)
{
	return MV_REG_READ(TWSI_STATUS_BAUDE_RATE_REG(chanNum));

}

/*******************************************************************************
* twsiReset - Reset the TWSI.
*
* DESCRIPTION:
*       Resets the TWSI logic and sets all TWSI registers to their reset values.
*
* INPUT:
*      chanNum - TWSI channel
*
* OUTPUT:
*       None.
*
* RETURN:
*       None
*
*******************************************************************************/
static void twsiReset(uint8_t chanNum)
{
	/* Reset the TWSI logic */
	MV_REG_WRITE(TWSI_SOFT_RESET_REG(chanNum), 0);

	/* wait for 2 mili sec */
	mdelay(2);

	return;
}

/******************************* POLICY ****************************************/

/*******************************************************************************
* mvTwsiAddrSet - Set address on TWSI bus.
*
* DESCRIPTION:
*       This function Set address (7 or 10 Bit address) on the Twsi Bus.
*
* INPUT:
*	chanNum - TWSI channel
*       pTwsiAddr - twsi address.
*	command	 - read / write .
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_OK - if setting the address completed succesfully.
*	MV_FAIL otherwmise.
*
*******************************************************************************/
static int mvTwsiAddrSet(uint8_t chanNum, MV_TWSI_ADDR *pTwsiAddr, MV_TWSI_CMD command)
{
	DB(printf("TWSI: mvTwsiAddr7BitSet addr %x , type %d, cmd is %s\n", pTwsiAddr->address,
		      pTwsiAddr->type, ((command == MV_TWSI_WRITE) ? "Write" : "Read")));
	/* 10 Bit address */
	if (pTwsiAddr->type == ADDR10_BIT)
		return twsiAddr10BitSet(chanNum, pTwsiAddr->address, command);

	/* 7 Bit address */
	else
		return twsiAddr7BitSet(chanNum, pTwsiAddr->address, command);

}

/*******************************************************************************
* twsiAddr10BitSet - Set 10 Bit address on TWSI bus.
*
* DESCRIPTION:
*       There are two address phases:
*       1) Write '11110' to data register bits [7:3] and 10-bit address MSB
*          (bits [9:8]) to data register bits [2:1] plus a write(0) or read(1) bit
*          to the Data register. Then it clears interrupt flag which drive
*          the address on the TWSI bus. The function then waits for interrupt
*          flag to be active and status 0x18 (write) or 0x40 (read) to be set.
*       2) write the rest of 10-bit address to data register and clears
*          interrupt flag which drive the address on the TWSI bus. The
*          function then waits for interrupt flag to be active and status
*          0xD0 (write) or 0xE0 (read) to be set.
*
* INPUT:
*	chanNum - TWSI channel
*       deviceAddress - twsi address.
*	command	 - read / write .
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_OK - if setting the address completed succesfully.
*	MV_FAIL otherwmise.
*
*******************************************************************************/
static int twsiAddr10BitSet(uint8_t chanNum, uint32_t deviceAddress, MV_TWSI_CMD command)
{
	uint32_t val, timeout;

	/* writing the 2 most significant bits of the 10 bit address */
	val = ((deviceAddress & TWSI_DATA_ADDR_10BIT_MASK) >> TWSI_DATA_ADDR_10BIT_OFFS);
	/* bits 7:3 must be 0x11110 */
	val |= TWSI_DATA_ADDR_10BIT_CONST;
	/* set command */
	val |= command;
	MV_REG_WRITE(TWSI_DATA_REG(chanNum), val);
	/* WA add a delay */
	mdelay(1);

	/* clear Int flag */
	twsiIntFlgClr(chanNum);

	/* wait for Int to be Set */
	timeout = 0;
	while (!twsiMainIntGet(chanNum) && (timeout++ < TWSI_TIMEOUT_VALUE))
		;

	/* check for timeout */
	if (MV_TRUE ==
	    twsiTimeoutChk(timeout, (const char *)"TWSI: twsiAddr10BitSet ERROR - 1st addr (10Bit) Int TimeOut.\n"))
		return MV_TIMEOUT;

	/* check the status */
	val = twsiStsGet(chanNum);
	if ((TWSI_M_LOST_ARB_DUR_AD_OR_DATA_TRA == val) || (TWSI_M_LOST_ARB_DUR_AD_TRA_GNL_CALL_AD_REC_ACK_TRA == val)) {
		DB(printf("TWSI: Lost Arb, status %x \n", val));
		return MV_RETRY;
	} else if (((val != TWSI_AD_PLS_RD_BIT_TRA_ACK_REC) && (command == MV_TWSI_READ)) ||
	    ((val != TWSI_AD_PLS_WR_BIT_TRA_ACK_REC) && (command == MV_TWSI_WRITE))) {
		printf("TWSI: twsiAddr10BitSet ERROR - status %x 1st addr (10 Bit) in %s mode.\n", val,
			   ((command == MV_TWSI_WRITE) ? "Write" : "Read"));
		return MV_FAIL;
	}

	/* set  8 LSB of the address */
	val = (deviceAddress << TWSI_DATA_ADDR_7BIT_OFFS) & TWSI_DATA_ADDR_7BIT_MASK;
	MV_REG_WRITE(TWSI_DATA_REG(chanNum), val);

	/* clear Int flag */
	twsiIntFlgClr(chanNum);

	/* wait for Int to be Set */
	timeout = 0;
	while (!twsiMainIntGet(chanNum) && (timeout++ < TWSI_TIMEOUT_VALUE))
		;

	/* check for timeout */
	if (MV_TRUE ==
	    twsiTimeoutChk(timeout, (const char *)"TWSI: twsiAddr10BitSet ERROR - 2nd (10 Bit) Int TimOut.\n"))
		return MV_TIMEOUT;

	/* check the status */
	val = twsiStsGet(chanNum);
	if ((TWSI_M_LOST_ARB_DUR_AD_OR_DATA_TRA == val) || (TWSI_M_LOST_ARB_DUR_AD_TRA_GNL_CALL_AD_REC_ACK_TRA == val)) {
		DB(printf("TWSI: Lost Arb, status %x \n", val));
		return MV_RETRY;
	} else if (((val != TWSI_SEC_AD_PLS_RD_BIT_TRA_ACK_REC) && (command == MV_TWSI_READ)) ||
	    ((val != TWSI_SEC_AD_PLS_WR_BIT_TRA_ACK_REC) && (command == MV_TWSI_WRITE))) {
		printf("TWSI: twsiAddr10BitSet ERROR - status %x 2nd addr(10 Bit) in %s mode.\n", val,
			   ((command == MV_TWSI_WRITE) ? "Write" : "Read"));
		return MV_FAIL;
	}

	return MV_OK;
}

/*******************************************************************************
* twsiAddr7BitSet - Set 7 Bit address on TWSI bus.
*
* DESCRIPTION:
*       This function writes 7 bit address plus a write or read bit to the
*       Data register. Then it clears interrupt flag which drive the address on
*       the TWSI bus. The function then waits for interrupt flag to be active
*       and status 0x18 (write) or 0x40 (read) to be set.
*
* INPUT:
*	chanNum - TWSI channel
*       deviceAddress - twsi address.
*	command	 - read / write .
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_OK - if setting the address completed succesfully.
*	MV_FAIL otherwmise.
*
*******************************************************************************/
static int twsiAddr7BitSet(uint8_t chanNum, uint32_t deviceAddress, MV_TWSI_CMD command)
{
	uint32_t val, timeout;

	/* set the address */
	val = (deviceAddress << TWSI_DATA_ADDR_7BIT_OFFS) & TWSI_DATA_ADDR_7BIT_MASK;
	/* set command */
	val |= command;
	MV_REG_WRITE(TWSI_DATA_REG(chanNum), val);
	/* WA add a delay */
	mdelay(1);

	/* clear Int flag */
	twsiIntFlgClr(chanNum);

	/* wait for Int to be Set */
	timeout = 0;
	while (!twsiMainIntGet(chanNum) && (timeout++ < TWSI_TIMEOUT_VALUE))
		;

	/* check for timeout */
	if (MV_TRUE ==
	    twsiTimeoutChk(timeout, (const char *)"TWSI: twsiAddr7BitSet ERROR - Addr (7 Bit) int TimeOut.\n"))
		return MV_TIMEOUT;

	/* check the status */
	val = twsiStsGet(chanNum);
	if ((TWSI_M_LOST_ARB_DUR_AD_OR_DATA_TRA == val) || (TWSI_M_LOST_ARB_DUR_AD_TRA_GNL_CALL_AD_REC_ACK_TRA == val)) {
		DB(printf("TWSI: Lost Arb, status %x \n", val));
		return MV_RETRY;
	} else if (((val != TWSI_AD_PLS_RD_BIT_TRA_ACK_REC) && (command == MV_TWSI_READ)) ||
	    ((val != TWSI_AD_PLS_WR_BIT_TRA_ACK_REC) && (command == MV_TWSI_WRITE))) {
		/* only in debug, since in boot we try to read the SPD of both DRAM, and we don't
		   want error messeges in case DIMM doesn't exist. */
		DB(printf
		   ("TWSI: twsiAddr7BitSet ERROR - status %x addr (7 Bit) in %s mode.\n", val,
		    ((command == MV_TWSI_WRITE) ? "Write" : "Read")));
		return MV_FAIL;
	}

	return MV_OK;
}

/*******************************************************************************
* twsiDataWrite - Trnasmit a data block over TWSI bus.
*
* DESCRIPTION:
*       This function writes a given data block to TWSI bus in 8 bit granularity.
*	first The function waits for interrupt flag to be active then
*       For each 8-bit data:
*        The function writes data to data register. It then clears
*        interrupt flag which drives the data on the TWSI bus.
*        The function then waits for interrupt flag to be active and status
*        0x28 to be set.
*
*
* INPUT:
*	chanNum - TWSI channel
*       pBlock - Data block.
*	blockSize - number of chars in pBlock.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_OK - if transmiting the block completed succesfully,
*	MV_BAD_PARAM - if pBlock is NULL,
*	MV_FAIL otherwmise.
*
*******************************************************************************/
static int twsiDataTransmit(uint8_t chanNum, uint8_t *pBlock, uint32_t blockSize)
{
	uint32_t timeout, temp, blockSizeWr = blockSize;

	if (NULL == pBlock)
		return MV_BAD_PARAM;

	/* wait for Int to be Set */
	timeout = 0;
	while (!twsiMainIntGet(chanNum) && (timeout++ < TWSI_TIMEOUT_VALUE))
		;

	/* check for timeout */
	if (MV_TRUE ==
	    twsiTimeoutChk(timeout, (const char *)"TWSI: twsiDataTransmit ERROR - Read Data Int TimeOut.\n"))
		return MV_TIMEOUT;

	while (blockSizeWr) {
		/* write the data */
		MV_REG_WRITE(TWSI_DATA_REG(chanNum), (uint32_t) *pBlock);
		DB(printf("TWSI: twsiDataTransmit place = %d write %x \n", blockSize - blockSizeWr, *pBlock));
		pBlock++;
		blockSizeWr--;

		twsiIntFlgClr(chanNum);

		/* wait for Int to be Set */
		timeout = 0;
		while (!twsiMainIntGet(chanNum) && (timeout++ < TWSI_TIMEOUT_VALUE))
			;

		/* check for timeout */
		if (MV_TRUE ==
		    twsiTimeoutChk(timeout, (const char *)"TWSI: twsiDataTransmit ERROR - Read Data Int TimeOut.\n"))
			return MV_TIMEOUT;

		/* check the status */
		temp = twsiStsGet(chanNum);
		if ((TWSI_M_LOST_ARB_DUR_AD_OR_DATA_TRA == temp) || \
			(TWSI_M_LOST_ARB_DUR_AD_TRA_GNL_CALL_AD_REC_ACK_TRA == temp)) {
			DB(printf("TWSI: Lost Arb, status %x \n", temp));
			return MV_RETRY;
		} else if (temp != TWSI_M_TRAN_DATA_BYTE_ACK_REC) {
			printf("TWSI: twsiDataTransmit ERROR - status %x in write trans\n", temp);
			return MV_FAIL;
		}

	}

	return MV_OK;
}

/*******************************************************************************
* twsiDataReceive - Receive data block from TWSI bus.
*
* DESCRIPTION:
*       This function receive data block from TWSI bus in 8bit granularity
*       into pBlock buffer.
*	first The function waits for interrupt flag to be active then
*       For each 8-bit data:
*        It clears the interrupt flag which allows the next data to be
*        received from TWSI bus.
*	 The function waits for interrupt flag to be active,
*	 and status reg is 0x50.
*	 Then the function reads data from data register, and copies it to
*	 the given buffer.
*
* INPUT:
*	chanNum - TWSI channel
*       blockSize - number of bytes to read.
*
* OUTPUT:
*       pBlock - Data block.
*
* RETURN:
*       MV_OK - if receive transaction completed succesfully,
*	MV_BAD_PARAM - if pBlock is NULL,
*	MV_FAIL otherwmise.
*
*******************************************************************************/
static int twsiDataReceive(uint8_t chanNum, uint8_t *pBlock, uint32_t blockSize)
{
	uint32_t timeout, temp, blockSizeRd = blockSize;

	if (NULL == pBlock)
		return MV_BAD_PARAM;

	/* wait for Int to be Set */
	timeout = 0;
	while (!twsiMainIntGet(chanNum) && (timeout++ < TWSI_TIMEOUT_VALUE))
		;

	/* check for timeout */
	if (MV_TRUE ==
	    twsiTimeoutChk(timeout, (const char *)"TWSI: twsiDataReceive ERROR - Read Data int Time out .\n"))
		return MV_TIMEOUT;

	while (blockSizeRd) {
		if (blockSizeRd == 1) {
			/* clear ack and Int flag */
			temp = MV_REG_READ(TWSI_CONTROL_REG(chanNum));
			temp &= ~(TWSI_CONTROL_ACK);
			MV_REG_WRITE(TWSI_CONTROL_REG(chanNum), temp);
		}
		twsiIntFlgClr(chanNum);
		/* wait for Int to be Set */
		timeout = 0;
		while ((!twsiMainIntGet(chanNum)) && (timeout++ < TWSI_TIMEOUT_VALUE))
			;

		/* check for timeout */
		if (MV_TRUE ==
		    twsiTimeoutChk(timeout, (const char *)"TWSI: twsiDataReceive ERROR - Read Data Int Time out .\n"))
			return MV_TIMEOUT;

		/* check the status */
		temp = twsiStsGet(chanNum);
		if ((TWSI_M_LOST_ARB_DUR_AD_OR_DATA_TRA == temp) || \
			(TWSI_M_LOST_ARB_DUR_AD_TRA_GNL_CALL_AD_REC_ACK_TRA == temp)) {
			DB(printf("TWSI: Lost Arb, status %x \n", temp));
			return MV_RETRY;
		} else if ((temp != TWSI_M_REC_RD_DATA_ACK_TRA) && (blockSizeRd != 1)) {
			printf("TWSI: twsiDataReceive ERROR - status %x in read trans \n", temp);
			return MV_FAIL;
		} else if ((temp != TWSI_M_REC_RD_DATA_ACK_NOT_TRA) && (blockSizeRd == 1)) {
			printf("TWSI: twsiDataReceive ERROR - status %x in Rd Terminate\n", temp);
			return MV_FAIL;
		}

		/* read the data */
		*pBlock = (uint8_t) MV_REG_READ(TWSI_DATA_REG(chanNum));
		DB(printf("TWSI: twsiDataReceive  place %d read %x \n", blockSize - blockSizeRd, *pBlock));
		pBlock++;
		blockSizeRd--;
	}

	return MV_OK;
}

/*******************************************************************************
* twsiTargetOffsSet - Set TWST target offset on TWSI bus.
*
* DESCRIPTION:
*       The function support TWSI targets that have inside address space (for
*       example EEPROMs). The function:
*       1) Convert the given offset into pBlock and size.
*		in case the offset should be set to a TWSI slave which support
*		more then 256 bytes offset, the offset setting will be done
*		in 2 transactions.
*       2) Use twsiDataTransmit to place those on the bus.
*
* INPUT:
*	chanNum - TWSI channel
*       offset - offset to be set on the EEPROM device.
*	moreThan256 - whether the EEPROM device support more than 256 byte offset.
*
* OUTPUT:
*       None.
*
* RETURN:
*       MV_OK - if setting the offset completed succesfully.
*	MV_FAIL otherwmise.
*
*******************************************************************************/
static int twsiTargetOffsSet(uint8_t chanNum, uint32_t offset, uint8_t moreThan256)
{
	uint8_t offBlock[2];
	uint32_t offSize;

	if (moreThan256 == MV_TRUE) {
		offBlock[0] = (offset >> 8) & 0xff;
		offBlock[1] = offset & 0xff;
		offSize = 2;
	} else {
		offBlock[0] = offset & 0xff;
		offSize = 1;
	}
	DB(printf("TWSI: twsiTargetOffsSet offSize = %x addr1 = %x addr2 = %x\n",
		      offSize, offBlock[0], offBlock[1]));
	return twsiDataTransmit(chanNum, offBlock, offSize);

}

/*******************************************************************************
* mvTwsiRead - Read data block from a TWSI Slave.
*
* DESCRIPTION:
*       The function calls the following functions:
*       -) mvTwsiStartBitSet();
*	if (EEPROM device)
*       	-) mvTwsiAddrSet(w);
*       	-) twsiTargetOffsSet();
*       	-) mvTwsiStartBitSet();
*       -) mvTwsiAddrSet(r);
*       -) twsiDataReceive();
*       -) mvTwsiStopBitSet();
*
* INPUT:
*	chanNum - TWSI channel
*      	pTwsiSlave - Twsi Slave structure.
*       blockSize - number of bytes to read.
*
* OUTPUT:
*      	pBlock - Data block.
*
* RETURN:
*       MV_OK - if EEPROM read transaction completed succesfully,
* 	MV_BAD_PARAM - if pBlock is NULL,
*	MV_FAIL otherwmise.
*
*******************************************************************************/
static int mvTwsiRead(uint8_t chanNum, MV_TWSI_SLAVE *pTwsiSlave, uint8_t *pBlock, uint32_t blockSize)
{
	int rc;
	int ret = MV_FAIL;
	uint32_t counter = 0;

	if ((NULL == pBlock) || (NULL == pTwsiSlave))
		return MV_BAD_PARAM;

	do	{
		if (counter > 0) /* wait for 1 mili sec for the clear to take effect */
			mdelay(1);
		ret = mvTwsiStartBitSet(chanNum);

		if (MV_RETRY == ret)
			continue;
		else if (MV_OK != ret) {
		mvTwsiStopBitSet(chanNum);
		DB(printf("mvTwsiRead: mvTwsiStartBitSet Faild\n"));
		return MV_FAIL;
	}

	DB(printf("TWSI: mvTwsiEepromRead after mvTwsiStartBitSet\n"));

	/* in case offset exsist (i.e. eeprom ) */
	if (MV_TRUE == pTwsiSlave->validOffset) {
		rc = mvTwsiAddrSet(chanNum, &(pTwsiSlave->slaveAddr), MV_TWSI_WRITE);
			if (MV_RETRY == rc)
				continue;
			else if (MV_OK != rc) {
			mvTwsiStopBitSet(chanNum);
			DB(printf("mvTwsiRead: mvTwsiAddrSet(%d,0x%x,%d) return rc=%d\n", chanNum,
							(uint32_t)&(pTwsiSlave->slaveAddr), MV_TWSI_WRITE, rc));
			return MV_FAIL;
		}
		DB(printf("TWSI: mvTwsiEepromRead after mvTwsiAddrSet\n"));

			ret = twsiTargetOffsSet(chanNum, pTwsiSlave->offset, pTwsiSlave->moreThan256);
			if (MV_RETRY == ret)
				continue;
			else if (MV_OK != ret) {
			mvTwsiStopBitSet(chanNum);
			DB(printf("mvTwsiRead: twsiTargetOffsSet Faild\n"));
			return MV_FAIL;
		}
		DB(printf("TWSI: mvTwsiEepromRead after twsiTargetOffsSet\n"));
			ret = mvTwsiStartBitSet(chanNum);
			if (MV_RETRY == ret)
				continue;
			else if (MV_OK != ret) {
			mvTwsiStopBitSet(chanNum);
			DB(printf("mvTwsiRead: mvTwsiStartBitSet 2 Faild\n"));
			return MV_FAIL;
		}
		DB(printf("TWSI: mvTwsiEepromRead after mvTwsiStartBitSet\n"));
	}
		ret =  mvTwsiAddrSet(chanNum, &(pTwsiSlave->slaveAddr), MV_TWSI_READ);
		if (MV_RETRY == ret)
			continue;
		else if (MV_OK != ret) {
		mvTwsiStopBitSet(chanNum);
		DB(printf("mvTwsiRead: mvTwsiAddrSet 2 Faild\n"));
		return MV_FAIL;
	}
	DB(printf("TWSI: mvTwsiEepromRead after mvTwsiAddrSet\n"));

		ret = twsiDataReceive(chanNum, pBlock, blockSize);
		if (MV_RETRY == ret)
			continue;
		else if (MV_OK != ret) {
		mvTwsiStopBitSet(chanNum);
		DB(printf("mvTwsiRead: twsiDataReceive Faild\n"));
		return MV_FAIL;
	}
	DB(printf("TWSI: mvTwsiEepromRead after twsiDataReceive\n"));

		ret =  mvTwsiStopBitSet(chanNum);
		if (MV_RETRY == ret)
			continue;
		else if (MV_OK != ret) {
		DB(printf("mvTwsiRead: mvTwsiStopBitSet 3 Faild\n"));
		return MV_FAIL;
	}
		counter++;
	} while ((MV_RETRY == ret) && (counter < MAX_RETRY_CNT));

	if (counter == MAX_RETRY_CNT)
		DB(printf("mvTwsiWrite: Retry Expire\n"));

	twsiAckBitSet(chanNum);

	DB(printf("TWSI: mvTwsiEepromRead after mvTwsiStopBitSet\n"));

	return MV_OK;
}

/*******************************************************************************
* mvTwsiWrite - Write data block to a TWSI Slave.
*
* DESCRIPTION:
*       The function calls the following functions:
*       -) mvTwsiStartBitSet();
*       -) mvTwsiAddrSet();
*	-)if (EEPROM device)
*       	-) twsiTargetOffsSet();
*       -) twsiDataTransmit();
*       -) mvTwsiStopBitSet();
*
* INPUT:
*	chanNum - TWSI channel
*      	eepromAddress - eeprom address.
*       blockSize - number of bytes to write.
*      	pBlock - Data block.
*
* OUTPUT:
*	None
*
* RETURN:
*       MV_OK - if EEPROM read transaction completed succesfully.
*	MV_BAD_PARAM - if pBlock is NULL,
*	MV_FAIL otherwmise.
*
* NOTE: Part of the EEPROM, required that the offset will be aligned to the
*	max write burst supported.
*******************************************************************************/
static int mvTwsiWrite(uint8_t chanNum, MV_TWSI_SLAVE *pTwsiSlave, uint8_t *pBlock, uint32_t blockSize)
{
	int ret = MV_FAIL;
	uint32_t counter = 0;
	if ((NULL == pBlock) || (NULL == pTwsiSlave))
		return MV_BAD_PARAM;

	do	{
		if (counter > 0) /* wait for 1 mili sec for the clear to take effect */
			mdelay(1);
		 ret = mvTwsiStartBitSet(chanNum);

		if (MV_RETRY == ret)
			continue;

		else if (MV_OK != ret) {
		mvTwsiStopBitSet(chanNum);
		DB(printf("mvTwsiWrite: mvTwsiStartBitSet faild\n"));
		return MV_FAIL;
	}

	DB(printf("TWSI: mvTwsiEepromWrite after mvTwsiStartBitSet\n"));
		ret = mvTwsiAddrSet(chanNum, &(pTwsiSlave->slaveAddr), MV_TWSI_WRITE);
		if (MV_RETRY == ret)
			continue;
		else if (MV_OK != ret) {
		mvTwsiStopBitSet(chanNum);
		DB(printf("mvTwsiWrite: mvTwsiAddrSet faild\n"));
		return MV_FAIL;
	}
	DB(printf("mvTwsiWrite :mvTwsiEepromWrite after mvTwsiAddrSet\n"));

	/* in case offset exsist (i.e. eeprom ) */
	if (MV_TRUE == pTwsiSlave->validOffset) {
			ret = twsiTargetOffsSet(chanNum, pTwsiSlave->offset, pTwsiSlave->moreThan256);
			if (MV_RETRY == ret)
				continue;
			else if (MV_OK != ret) {
			mvTwsiStopBitSet(chanNum);
			DB(printf("mvTwsiWrite: twsiTargetOffsSet faild\n"));
			return MV_FAIL;
		}
		DB(printf("mvTwsiWrite: mvTwsiEepromWrite after twsiTargetOffsSet\n"));
	}

		ret = twsiDataTransmit(chanNum, pBlock, blockSize);
		if (MV_RETRY == ret)
			continue;
		else if (MV_OK != ret) {
		mvTwsiStopBitSet(chanNum);
		DB(printf("mvTwsiWrite: twsiDataTransmit faild\n"));
		return MV_FAIL;
	}
	DB(printf("mvTwsiWrite: mvTwsiEepromWrite after twsiDataTransmit\n"));
		ret = mvTwsiStopBitSet(chanNum);
		if (MV_RETRY == ret)
			continue;
		else if (MV_OK != ret) {
		DB(printf("mvTwsiWrite: mvTwsiStopBitSet faild in last mvTwsiWrite\n"));
		return MV_FAIL;
	}
	DB(printf("mvTwsiWrite: mvTwsiEepromWrite after mvTwsiStopBitSet\n"));
		counter++;
	} while ((MV_RETRY == ret) && (counter < MAX_RETRY_CNT));

	if (counter == MAX_RETRY_CNT)
		DB(printf("mvTwsiWrite: Retry Expire\n"));

	return MV_OK;
}

static int i2c_init(unsigned bus)
{
	MV_TWSI_ADDR slave;

        if(bus >= MAX_I2C_NUM){
                return 1;
        }

        /* TWSI init */
        slave.type = ADDR7_BIT;
        slave.address = 0;
        mvTwsiInit(bus, TWSI_SPEED, mvBoardTclkGet(), &slave, 0);

        return 0;
}

static void i2c_reset(struct I2cOps *me)
{
	Armada38xI2c *bus = container_of(me, Armada38xI2c, ops);
	bus->initialized = 0;
}

static int i2c_transfer(struct I2cOps *me, I2cSeg *segments, int seg_count)
{
	MV_TWSI_SLAVE twsiSlave;
        Armada38xI2c *bus = container_of(me, Armada38xI2c, ops);
        I2cSeg *seg = segments;
        int ret = 0;

	if (!bus->initialized) {
                if (0 != i2c_init(bus->bus_num))
                        return 1;
                else
                        bus->initialized = 1;
        }

        while (!ret && seg_count--) {
		twsiSlave.slaveAddr.address = seg->chip;
		twsiSlave.slaveAddr.type =  ADDR7_BIT;
		twsiSlave.moreThan256 = MV_FALSE;
		twsiSlave.validOffset = MV_FALSE;
                if (seg->read)
                        ret = mvTwsiRead(bus->bus_num, &twsiSlave, seg->buf, seg->len);
                else
                        ret  = mvTwsiWrite(bus->bus_num, &twsiSlave, seg->buf, seg->len);
                seg++;
        }

	if(ret){
                i2c_reset(me);
                return 1;
        }

        return 0;
}

Armada38xI2c *new_armada38x_i2c(u8 bus_num)
{
        Armada38xI2c *bus = 0;

        if (!i2c_init(bus_num)) {
                bus = xzalloc(sizeof(*bus));
                bus->initialized = 1;
		bus->bus_num = bus_num;
                bus->ops.transfer = &i2c_transfer;
                if (CONFIG_CLI)
                        add_i2c_controller_to_list(&bus->ops,
                                                   "busnum%d", bus_num);
        }
        return bus;

}


