#ifndef _MV_SPI_H_
#define _MV_SPI_H_

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































#endif /* _MV_SPI_H_ */
