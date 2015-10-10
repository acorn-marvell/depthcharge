#ifndef __DRIVER_STORAGE_MV_SDHCI_H__
#define __DRIVER_STORAGE_MV_SDHCI_H__
SdhciHost *new_mv_sdhci_host(unsigned int regbase, unsigned int clock_min, unsigned int clock_max, unsigned int quirks);
#define INTER_REGS_BASE         0xF1000000
#define MV_SDMMC_REGS_OFFSET            (0xD8000)
#define CONFIG_SYS_MMC_BASE                    (INTER_REGS_BASE + MV_SDMMC_REGS_OFFSET)
#endif
