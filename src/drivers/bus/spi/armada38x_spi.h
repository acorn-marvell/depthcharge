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

#ifndef __ARMADA38X_SPI_H__
#define __ARMADA38X_SPI_H__

#include "drivers/flash/spi.h"
#include "drivers/bus/spi/spi.h"

typedef struct mrvl_spi_flash {
	struct spi_slave *spi;
	const char	*name;
	/* Total flash size */
	unsigned int		size;
	/* Write (page) size */
	unsigned int		page_size;
	/* Erase (sector) size */
	unsigned int		sector_size;
	unsigned char 		addr_cycles;
}mrvl_spi_flash;

typedef struct SpiController{
	SpiOps ops;
	mrvl_spi_flash* spi_flash;
}SpiController;

SpiController *new_spi(unsigned bus_num, unsigned cs);

#endif /* __ARMADA38X_SPI_H__ */
