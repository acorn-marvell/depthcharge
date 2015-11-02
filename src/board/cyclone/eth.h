

#define MV_U32 u32
#define MV_U16 u16
#define MV_U8  u8

#define mvOsPrintf printf


#define INTER_REGS_BASE         0xF1000000
#define MV_REG_READ(offset)             \
        (readl((void *)(INTER_REGS_BASE | (offset))))

#define MV_REG_WRITE(offset, val)    \
        writel((val), (void *)(INTER_REGS_BASE | (offset)))

#define ETH_PHY_TIMEOUT             10000
#define MV_ETH_SMI_PORT   0
#define MV_ETH_BASE_ADDR_PORT1_2                (0x00030000)    /* Port1 = 0x30000   port1 = 0x34000 */
#define MV_ETH_BASE_ADDR_PORT0                  (0x00070000)

/* SMI register fields (ETH_PHY_SMI_REG) */

#define ETH_PHY_SMI_DATA_OFFS           0 /* Data */
#define ETH_PHY_SMI_DATA_MASK           (0xffff << ETH_PHY_SMI_DATA_OFFS)

#define ETH_PHY_SMI_DEV_ADDR_OFFS           16 /* PHY device address */
#define ETH_PHY_SMI_DEV_ADDR_MASK       (0x1f << ETH_PHY_SMI_DEV_ADDR_OFFS)

#define ETH_PHY_SMI_REG_ADDR_OFFS           21 /* PHY device register address */
#define ETH_PHY_SMI_REG_ADDR_MASK           (0x1f << ETH_PHY_SMI_REG_ADDR_OFFS)

#define ETH_PHY_SMI_OPCODE_OFFS         26      /* Write/Read opcode */
#define ETH_PHY_SMI_OPCODE_MASK         (3 << ETH_PHY_SMI_OPCODE_OFFS)
#define ETH_PHY_SMI_OPCODE_WRITE        (0 << ETH_PHY_SMI_OPCODE_OFFS)
#define ETH_PHY_SMI_OPCODE_READ         (1 << ETH_PHY_SMI_OPCODE_OFFS)

#define ETH_PHY_SMI_READ_VALID_BIT          27  /* Read Valid  */
#define ETH_PHY_SMI_READ_VALID_MASK         (1 << ETH_PHY_SMI_READ_VALID_BIT)

#define ETH_PHY_SMI_BUSY_BIT                28  /* Busy */
#define ETH_PHY_SMI_BUSY_MASK               (1 << ETH_PHY_SMI_BUSY_BIT)

#define MV_ETH_REGS_OFFSET(p)                   (((p) == 0) ? MV_ETH_BASE_ADDR_PORT0 : \
                                                (MV_ETH_BASE_ADDR_PORT1_2 + (((p) - 1) * 0x4000)))
#define MV_ETH_REGS_BASE(p)     MV_ETH_REGS_OFFSET(p)
#define ETH_REG_BASE(port)                  MV_ETH_REGS_BASE(port)
#define ETH_SMI_REG(port)                   (ETH_REG_BASE(port) + 0x2004)

#define MV_E6171_PORTS_OFFSET                                   0x10
#define MV_E6171_SWITCH_PHIYSICAL_CTRL_REG              0x1


void mvEnableSwitchDelay(MV_U32 ethPortNum);
