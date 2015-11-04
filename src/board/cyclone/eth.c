/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Copyright 2015 Google Inc.
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

#include <stdio.h>
#include "common.h"
#include "eth.h"

void mvEthSwitchRegRead(u32 ethPortNum, u32 switchPort,
                             u32 switchReg, u16 *data)
{
        u32 smiReg;
        volatile u32 timeout;

        /* check parameters */
        if ((switchPort << ETH_PHY_SMI_DEV_ADDR_OFFS) & ~ETH_PHY_SMI_DEV_ADDR_MASK) {
                return;
        }
        if ((switchReg <<  ETH_PHY_SMI_REG_ADDR_OFFS) & ~ETH_PHY_SMI_REG_ADDR_MASK) {
                printf("mvEthPhyRegRead: Err. Illegal PHY register offset %u\n",
                                switchReg);
                return;
        }

        timeout = ETH_PHY_TIMEOUT;
        /* wait till the SMI is not busy*/
        do {
                /* read smi register */
                smiReg = MV_REG_READ(ETH_SMI_REG(MV_ETH_SMI_PORT));

                if (timeout-- == 0) {
                        printf("mvEthPhyRegRead: SMI busy timeout\n");
                        return;
                }
        } while (smiReg & ETH_PHY_SMI_BUSY_MASK);

        /* fill the phy address and regiser offset and read opcode */
        smiReg = (switchPort <<  ETH_PHY_SMI_DEV_ADDR_OFFS) | (switchReg << ETH_PHY_SMI_REG_ADDR_OFFS)|
                           ETH_PHY_SMI_OPCODE_READ;

        /* write the smi register */
        MV_REG_WRITE(ETH_SMI_REG(MV_ETH_SMI_PORT), smiReg);

        timeout = ETH_PHY_TIMEOUT;

        /*wait till readed value is ready */
        do {
                /* read smi register */
                smiReg = MV_REG_READ(ETH_SMI_REG(MV_ETH_SMI_PORT));

                if (timeout-- == 0) {
                        printf("mvEthPhyRegRead: SMI read-valid timeout\n");
                        return;
                }
        } while (!(smiReg & ETH_PHY_SMI_READ_VALID_MASK));

        /* Wait for the data to update in the SMI register */
        //for (timeout = 0; timeout < ETH_PHY_TIMEOUT; timeout++)
        //      ;

        *data = smiReg & ETH_PHY_SMI_DATA_MASK;

        return;
}
void mvEthSwitchRegWrite(u32 ethPortNum, u32 switchPort,
                                 u32 switchReg, u16 data)
{
        u32 smiReg;
        volatile u32 timeout;

        /* check parameters */
        if ((switchPort <<  ETH_PHY_SMI_DEV_ADDR_OFFS) & ~ETH_PHY_SMI_DEV_ADDR_MASK) {
                printf("mvEthPhyRegWrite: Err. Illegal phy address 0x%x\n", switchPort);
                return;
        }
        if ((switchReg <<  ETH_PHY_SMI_REG_ADDR_OFFS) & ~ETH_PHY_SMI_REG_ADDR_MASK) {
                printf("mvEthPhyRegWrite: Err. Illegal register offset 0x%x\n", switchReg);
                return;
        }

        timeout = ETH_PHY_TIMEOUT;

        /* wait till the SMI is not busy*/
        do {
                /* read smi register */
                smiReg = MV_REG_READ(ETH_SMI_REG(MV_ETH_SMI_PORT));
                if (timeout-- == 0) {
                        printf("mvEthPhyRegWrite: SMI busy timeout\n");
                return;
                }
        } while (smiReg & ETH_PHY_SMI_BUSY_MASK);

        /* fill the phy address and regiser offset and write opcode and data*/
        smiReg = (data << ETH_PHY_SMI_DATA_OFFS);
        smiReg |= (switchPort <<  ETH_PHY_SMI_DEV_ADDR_OFFS) | (switchReg << ETH_PHY_SMI_REG_ADDR_OFFS);
        smiReg &= ~ETH_PHY_SMI_OPCODE_READ;

        /* write the smi register */
        //DB(printf("%s: phyAddr=0x%x offset = 0x%x data=0x%x\n", __func__, phyAddr, regOffs, data));
        //DB(printf("%s: ethphyHalData.ethPhySmiReg = 0x%x smiReg=0x%x\n", __func__, ethphyHalData.ethPhySmiReg, smiReg));
        MV_REG_WRITE(ETH_SMI_REG(MV_ETH_SMI_PORT), smiReg);

        return;
}

u32 mvBoardSwitchCpuPortGet(u32 switchIdx)
{
    return 5;
}

void mvEnableSwitchDelay(u32 ethPortNum)
{
	u16 reg;
	u32 cpuPort =  mvBoardSwitchCpuPortGet(0);
	/* Enable RGMII delay on Tx and Rx for port 5 switch 1 */
        mvEthSwitchRegRead(ethPortNum, MV_E6171_PORTS_OFFSET + cpuPort, MV_E6171_SWITCH_PHIYSICAL_CTRL_REG, &reg);
        mvEthSwitchRegWrite(ethPortNum, MV_E6171_PORTS_OFFSET + cpuPort, MV_E6171_SWITCH_PHIYSICAL_CTRL_REG,
                                                 (reg|0xC000));
}
