/*
 * Command for controlling Ethernet interface.
 *
 * Copyright 2015 Chromium OS Authors
 */

#include "common.h"

#include "drivers/bus/usb/usb.h"
#include "drivers/net/net.h"
#include "netboot/dhcp.h"
#include "net/uiplib.h"
#include "netboot/netboot.h"
#include "netboot/tftp.h"

/* Wait for the device to show up for no longer than 5 seconds. */
#define DEV_TIMEOUT_US 5000000

static NetDevice *get_net_device(void)
{
	NetDevice *ndev = net_get_device();
	int ready = 0;

	if (ndev)
		ndev->ready(ndev, &ready);

	if (!ready) {
		net_wait_for_link();
		ndev = net_get_device();
	}

	return ndev;
}

static const uint32_t MaxPayloadSize = CONFIG_KERNEL_SIZE;

static int do_enet(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	NetDevice *ndev;
	int ready;

	if (argc < 2)
		return CMD_RET_USAGE;

	ndev = get_net_device();
	if (!ndev)
		return CMD_RET_FAILURE;

	if (!strcmp(argv[1], "send")) {
		unsigned long addr;
		int length;

		if (argc != 4)
			return CMD_RET_USAGE;

		if (!ready)
			return CMD_RET_FAILURE;

		/* We might want to chek these for sanity. */
		addr = strtoul(argv[2], 0, 16);
		length = strtoul(argv[1], 0, 0);

		ndev->send(ndev, (void *) addr, length);
		return CMD_RET_SUCCESS;
	}

	if (!strcmp(argv[1], "dhcp")) {
		uip_ipaddr_t my_ip, next_ip, server_ip;
		const char *dhcp_bootfile;

		if (!try_dhcp(&my_ip, &next_ip, &server_ip, &dhcp_bootfile))
			return CMD_RET_SUCCESS;

	}

	if (!strcmp(argv[1], "tftp")) {
		uip_ipaddr_t tftp_ip; 
		char file[256];
		unsigned long addr;
		uint32_t size;		
		
		// Find out who we are.
        	uip_ipaddr_t my_ip, next_ip, server_ip;
        	const char *dhcp_bootfile;

		addr = strtoul(argv[2], 0, 16);
		uiplib_ipaddrconv(argv[3], &tftp_ip);
		memset(file, 0, 256);
		strcpy(file, argv[4]);
		
		uip_init();

		printf("addr is:%lu\n", addr);
		printf("tftp server ip is %d.%d.%d.%d\n", uip_ipaddr1(&tftp_ip), uip_ipaddr2(&tftp_ip),
                			uip_ipaddr3(&tftp_ip), uip_ipaddr4(&tftp_ip));
		printf("file is :%s\n", file);
        	if (try_dhcp(&my_ip, &next_ip, &server_ip, &dhcp_bootfile))
                	printf("Dhcp failed, retrying.\n");
		if (tftp_read((void *)addr, &tftp_ip, file, &size, MaxPayloadSize)) {
                	printf("Tftp failed.\n");
                	if (dhcp_release(server_ip))
                        	printf("Dhcp release failed.\n");
			return CMD_RET_FAILURE;
        	}
        	printf("The bootfile was %d bytes long.\n", size);
		if (dhcp_release(server_ip)) {
                	printf("Dhcp release failed.\n");
        	}
		return CMD_RET_SUCCESS;
	}
	return CMD_RET_FAILURE;
}

U_BOOT_CMD(
	enet,	5,	1,
	"ethernet interface utilities",
	"dhcp          - try obtainitg IP address over DHCP\n"
	"enet tftp addr serverip file  - read the file data to address 'addr'"
	"enet send addr len - send a frame from memory,\n"
	"                     'len' bytes starting at address 'addr'\n"
);
