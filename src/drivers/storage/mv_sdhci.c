#include <libpayload.h>
#include "drivers/storage/sdhci.h"

static char *MVSDH_NAME = "mv_sdh";
SdhciHost *new_mv_sdhci_host(unsigned int regbase, unsigned int clock_min, unsigned int clock_max, unsigned int quirks)
{
	SdhciHost *host = xzalloc(sizeof(SdhciHost));
	if(!host) {
		printf("Failed to call xzalloc for sdhci_host!\n");
		return NULL;				
	}
	memset(host, 0, sizeof(SdhciHost));
	host->name = MVSDH_NAME;
	host->ioaddr = (void *)regbase;
	host->quirks = quirks;
	host->host_caps |= MMC_MODE_HC;
	
	if (quirks & SDHCI_QUIRK_REG32_RW)
        	host->version = sdhci_readl(host, SDHCI_HOST_VERSION - 2) >> 16;
    	else
        	host->version = sdhci_readw(host, SDHCI_HOST_VERSION);
	
	add_sdhci(host);
    
	return host;
		
}
