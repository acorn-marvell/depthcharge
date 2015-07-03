#include <arch/io.h>
#include <stdio.h>
#include "eth.h"

void mvEthSwitchRegRead(MV_U32 ethPortNum, MV_U32 switchPort,
                             MV_U32 switchReg, MV_U16 *data){
        MV_U32                  smiReg;
        volatile MV_U32 timeout;

        /* check parameters */
        if ((switchPort << ETH_PHY_SMI_DEV_ADDR_OFFS) & ~ETH_PHY_SMI_DEV_ADDR_MASK) {
                //mvOsPrintf("mvEthPhyRegRead: Err. Illegal PHY device address\n");
                return;
        }
        if ((switchReg <<  ETH_PHY_SMI_REG_ADDR_OFFS) & ~ETH_PHY_SMI_REG_ADDR_MASK) {
                mvOsPrintf("mvEthPhyRegRead: Err. Illegal PHY register offset %u\n",
                                switchReg);
                return;
        }

        timeout = ETH_PHY_TIMEOUT;
        /* wait till the SMI is not busy*/
        do {
                /* read smi register */
                smiReg = MV_REG_READ(ETH_SMI_REG(MV_ETH_SMI_PORT));

                if (timeout-- == 0) {
                        mvOsPrintf("mvEthPhyRegRead: SMI busy timeout\n");
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
                        mvOsPrintf("mvEthPhyRegRead: SMI read-valid timeout\n");
                        return;
                }
        } while (!(smiReg & ETH_PHY_SMI_READ_VALID_MASK));

        /* Wait for the data to update in the SMI register */
        //for (timeout = 0; timeout < ETH_PHY_TIMEOUT; timeout++)
        //      ;

        *data = smiReg & ETH_PHY_SMI_DATA_MASK;

        return;
}
void mvEthSwitchRegWrite(MV_U32 ethPortNum, MV_U32 switchPort,
                                 MV_U32 switchReg, MV_U16 data)
{
        MV_U32                  smiReg;
        volatile MV_U32 timeout;

        /* check parameters */
        if ((switchPort <<  ETH_PHY_SMI_DEV_ADDR_OFFS) & ~ETH_PHY_SMI_DEV_ADDR_MASK) {
                mvOsPrintf("mvEthPhyRegWrite: Err. Illegal phy address 0x%x\n", switchPort);
                return;
        }
        if ((switchReg <<  ETH_PHY_SMI_REG_ADDR_OFFS) & ~ETH_PHY_SMI_REG_ADDR_MASK) {
                mvOsPrintf("mvEthPhyRegWrite: Err. Illegal register offset 0x%x\n", switchReg);
                return;
        }

        timeout = ETH_PHY_TIMEOUT;

        /* wait till the SMI is not busy*/
        do {
                /* read smi register */
                smiReg = MV_REG_READ(ETH_SMI_REG(MV_ETH_SMI_PORT));
                if (timeout-- == 0) {
                        mvOsPrintf("mvEthPhyRegWrite: SMI busy timeout\n");
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

MV_U32 mvBoardSwitchCpuPortGet(MV_U32 switchIdx)
{
    return 5;
}


void mvEnableSwitchDelay(MV_U32 ethPortNum)
{
	MV_U16 reg;
	MV_U32 cpuPort =  mvBoardSwitchCpuPortGet(0);
	/* Enable RGMII delay on Tx and Rx for port 5 switch 1 */
        mvEthSwitchRegRead(ethPortNum, MV_E6171_PORTS_OFFSET + cpuPort, MV_E6171_SWITCH_PHIYSICAL_CTRL_REG, &reg);
        mvEthSwitchRegWrite(ethPortNum, MV_E6171_PORTS_OFFSET + cpuPort, MV_E6171_SWITCH_PHIYSICAL_CTRL_REG,
                                                 (reg|0xC000));
}
