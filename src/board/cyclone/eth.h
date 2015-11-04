/*
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

#ifndef __BOARD_CYCLONE_ETH_H__
#define __BOARD_CYCLONE_ETH_H__

#define ETH_PHY_TIMEOUT                     10000
#define MV_ETH_SMI_PORT                     0
#define MV_ETH_BASE_ADDR_PORT1_2            (0x00030000)    /* Port1 = 0x30000   port1 = 0x34000 */
#define MV_ETH_BASE_ADDR_PORT0              (0x00070000)

/* SMI register fields (ETH_PHY_SMI_REG) */

#define ETH_PHY_SMI_DATA_OFFS               0 /* Data */
#define ETH_PHY_SMI_DATA_MASK               (0xffff << ETH_PHY_SMI_DATA_OFFS)

#define ETH_PHY_SMI_DEV_ADDR_OFFS           16 /* PHY device address */
#define ETH_PHY_SMI_DEV_ADDR_MASK           (0x1f << ETH_PHY_SMI_DEV_ADDR_OFFS)

#define ETH_PHY_SMI_REG_ADDR_OFFS           21 /* PHY device register address */
#define ETH_PHY_SMI_REG_ADDR_MASK           (0x1f << ETH_PHY_SMI_REG_ADDR_OFFS)

#define ETH_PHY_SMI_OPCODE_OFFS             26      /* Write/Read opcode */
#define ETH_PHY_SMI_OPCODE_MASK             (3 << ETH_PHY_SMI_OPCODE_OFFS)
#define ETH_PHY_SMI_OPCODE_WRITE            (0 << ETH_PHY_SMI_OPCODE_OFFS)
#define ETH_PHY_SMI_OPCODE_READ             (1 << ETH_PHY_SMI_OPCODE_OFFS)

#define ETH_PHY_SMI_READ_VALID_BIT          27  /* Read Valid  */
#define ETH_PHY_SMI_READ_VALID_MASK         (1 << ETH_PHY_SMI_READ_VALID_BIT)

#define ETH_PHY_SMI_BUSY_BIT                28  /* Busy */
#define ETH_PHY_SMI_BUSY_MASK               (1 << ETH_PHY_SMI_BUSY_BIT)

#define MV_ETH_REGS_OFFSET(p)               (((p) == 0) ? MV_ETH_BASE_ADDR_PORT0 : \
                                                (MV_ETH_BASE_ADDR_PORT1_2 + (((p) - 1) * 0x4000)))
#define MV_ETH_REGS_BASE(p)                 MV_ETH_REGS_OFFSET(p)
#define ETH_REG_BASE(port)                  MV_ETH_REGS_BASE(port)
#define ETH_SMI_REG(port)                   (ETH_REG_BASE(port) + 0x2004)

#define MV_E6171_PORTS_OFFSET               0x10
#define MV_E6171_SWITCH_PHIYSICAL_CTRL_REG  0x1

void mvEnableSwitchDelay(u32 ethPortNum);

#endif
