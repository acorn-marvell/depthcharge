/*
 * Copyright (c) 2014, Linux Foundation. All rights reserved
 * Copyright 2014 Chromium OS Authors
 */

#include "debug/cli/common.h"
#include "drivers/storage/mtd/mtd.h"

static MtdDevCtrlr * nand_ctrl=NULL;

void set_nand_controller(MtdDevCtrlr *ctrl)
{
	nand_ctrl = ctrl;
}

static int nand_erase(int argc, char *const argv[])
{
	if(!nand_ctrl){
		printf("no nand device\n");
		return -1;
	}

	if(argc>1)
        {
                struct erase_info e_info;
                int offset, len, i;
                int *args[] = {&offset, &len};
                for (i = 0; i < ARRAY_SIZE(args); i++)
                        *args[i] = strtoul(argv[i], NULL, 0);

                e_info.addr = offset;
                e_info.len = len;
                nand_ctrl->dev->erase(nand_ctrl->dev, &e_info);
        }

	return 0;
}

static int nand_read(int argc, char *const argv[])
{
	int offset, len, *dest_addr, i;
        int *args[] = {&offset, &len, (int*) &dest_addr};
	size_t retlen;

	if(!nand_ctrl){
                printf("no nand device\n");
                return -1;
        }

        for (i = 0; i < ARRAY_SIZE(args); i++)
                *args[i] = strtoul(argv[i], NULL, 0);
        nand_ctrl->dev->read(nand_ctrl->dev, offset, len, &retlen, (unsigned char *)dest_addr);	

	return 0;
}

static int nand_write(int argc, char *const argv[])
{
	int offset, len, *src_addr, i;
        int *args[] = {&offset, &len, (int*) &src_addr};
        size_t retlen;

        if(!nand_ctrl){
                printf("no nand device\n");
                return -1;
        }

        for (i = 0; i < ARRAY_SIZE(args); i++)
                *args[i] = strtoul(argv[i], NULL, 0);
	nand_ctrl->dev->write(nand_ctrl->dev, offset, len, &retlen, (unsigned char *)src_addr);

	return 0;
}

static int nand_init(int argc, char *const argv[])
{
        if(nand_ctrl)
		nand_ctrl->update(nand_ctrl);
	return 0;
}

typedef struct {
	const char *subcommand_name;
	int (*subcmd)(int argc, char *const argv[]);
	int min_arg;
	int max_arg;
} cmd_map;

static const cmd_map cmdmap[] = {
	{ "init", nand_init, 0, 0 },
	{ "erase", nand_erase, 2, 2 },
	{ "read", nand_read, 3, 3 },
	{ "write", nand_write, 3, 3 },
};

static int do_nand(cmd_tbl_t *cmdtp, int flag,
		      int argc, char * const argv[])
{
	if (argc >= 2) {
		int i;

		for (i = 0; i < ARRAY_SIZE(cmdmap); i++)
			if (!strcmp(argv[1], cmdmap[i].subcommand_name)) {
				int nargs = argc - 2;

				if ((cmdmap[i].min_arg <= nargs) &&
				    (cmdmap[i].max_arg >= nargs))
					return cmdmap[i].subcmd(nargs, argv + 2);
			}
	}
	return CMD_RET_USAGE;
}

U_BOOT_CMD(
	nand, CONFIG_SYS_MAXARGS,	1,
	"command for nand devices",
	"\n"
	" init - initialize storage devices\n"
	" erase <offset> <len>\n"
	" read <offset> <len> <dest addr> - read from default device\n"
	" write <offset> <len> <src addr> - write to default device\n"
);

