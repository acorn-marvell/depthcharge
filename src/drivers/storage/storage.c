/*
 * Copyright (c) 2014, Linux Foundation. All rights reserved
 * Copyright 2014 Chromium OS Authors
 */
#include <libpayload.h>

#include "storage.h"
#include "base/list.h"
#include "drivers/storage/blockdev.h"

typedef struct {

	/* max 10 storage devices is enough */
	BlockDev *known_devices[10];
	int curr_device;
	int total;

} storage_devices;

static storage_devices current_devices;

int storage_show(void)
{
	int i;
	BlockDev **bd;

	for (i = 0, bd = current_devices.known_devices;
	     i < current_devices.total;
	     i++, bd++)
		printf("%c %2d: %s\n",
		       current_devices.curr_device == i ? '*' : ' ',
		       i, (*bd)->name ? (*bd)->name : "UNNAMED");

	printf("%d devices total\n", i);
	return 0;
}

int storage_read(int base_block, int num_blocks, int* dest_addr)
{
	BlockDev *bd;
	int i;

	if ((current_devices.curr_device < 0) ||
	    (current_devices.curr_device >= current_devices.total)) {
		printf("Is storage subsystem initialized?");
		return -1;
	}

	bd = current_devices.known_devices[current_devices.curr_device];
	i = bd->ops.read(&bd->ops, base_block, num_blocks, dest_addr);
	return i != num_blocks;
}

int storage_write(int base_block, int num_blocks, int* src_addr)
{
	BlockDev *bd;
	int i;

	if ((current_devices.curr_device < 0) ||
	    (current_devices.curr_device >= current_devices.total)) {
		printf("Is storage subsystem initialized?");
		return -1;
	}

	bd = current_devices.known_devices[current_devices.curr_device];
	i = bd->ops.write(&bd->ops, base_block, num_blocks, src_addr);
	return i != num_blocks;
}

int storage_dev(int device, char *const *device_name)
{
	int rv = 0;

	if (!current_devices.total) {
		printf("No initialized devices present\n");
	} else {
		unsigned long cur_device;

		cur_device = device ?
			strtoul(device_name[0], NULL, 0) :
			current_devices.curr_device;
		if (cur_device >= current_devices.total) {
			printf("%d: bad device index. Current devices:",
			       (int)cur_device);
			storage_show();
			rv = -1;
		} else {
			current_devices.curr_device = cur_device;
			printf("%s\n",
			       current_devices.known_devices[cur_device]->name);
		}
	}
	return rv;
}

int storage_init(void)
{
	int i, count;
	const ListNode *controllers[] = {
		&fixed_block_dev_controllers,
		&removable_block_dev_controllers
	};

	const ListNode *devices[] = {
		&fixed_block_devices,
		&removable_block_devices
	};

	for (i = 0; i < ARRAY_SIZE(controllers); i++)
		for (const ListNode *node = controllers[i]->next;
		     node;
		     node = node->next) {
			BlockDevCtrlr *bdc;

			bdc = container_of(node, BlockDevCtrlr, list_node);
			if (bdc->ops.update && bdc->need_update)
				bdc->ops.update(&bdc->ops);
			count++;
		}

	for (count = i = 0; i < ARRAY_SIZE(devices); i++)
		for (const ListNode *node = devices[i]->next;
		     node;
		     node = node->next) {
			BlockDev *bd;

			bd = container_of(node, BlockDev, list_node);
			current_devices.known_devices[count++] = bd;
		}

	current_devices.total = count;
	current_devices.curr_device = 0;

	return storage_show();
}
