/*
 * Copyright 2013 Google Inc.
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

#ifndef __BASE_DEVICE_TREE_H__
#define __BASE_DEVICE_TREE_H__

#include <stdint.h>

#include "base/list.h"

/*
 * Flattened device tree structures/constants.
 */

typedef struct FdtHeader {
	uint32_t magic;
	uint32_t totalsize;
	uint32_t structure_offset;
	uint32_t strings_offset;
	uint32_t reserve_map_offset;

	uint32_t version;
	uint32_t last_compatible_version;

	uint32_t boot_cpuid_phys;

	uint32_t strings_size;
	uint32_t structure_size;
} FdtHeader;

static const uint32_t FdtMagic = 0xd00dfeed;

static const uint32_t TokenBeginNode = 1;
static const uint32_t TokenEndNode = 2;
static const uint32_t TokenProperty = 3;
static const uint32_t TokenEnd = 9;

typedef struct FdtProperty
{
	const char *name;
	void *data;
	uint32_t size;
} FdtProperty;



/*
 * Unflattened device tree structures.
 */

typedef struct DeviceTreeProperty
{
	FdtProperty prop;

	ListNode list_node;
} DeviceTreeProperty;

typedef struct DeviceTreeNode
{
	const char *name;
	// List of DeviceTreeProperty-s.
	ListNode properties;
	// List of DeviceTreeNodes.
	ListNode children;

	ListNode list_node;
} DeviceTreeNode;

typedef struct DeviceTreeReserveMapEntry
{
	uint64_t start;
	uint64_t size;

	ListNode list_node;
} DeviceTreeReserveMapEntry;

typedef struct DeviceTree
{
	void *header;
	uint32_t header_size;

	ListNode reserve_map;

	DeviceTreeNode *root;
} DeviceTree;



/*
 * Flattened device tree functions. These generally return the number of bytes
 * which were consumed reading the requested value.
 */

// Read the property, if any, at offset offset.
int fdt_next_property(void *blob, uint32_t offset, FdtProperty *prop);
// Read the name of the node, if any, at offset offset.
int fdt_node_name(void *blob, uint32_t offset, const char **name);

void fdt_print_node(void *blob, uint32_t offset);
int fdt_skip_node(void *blob, uint32_t offset);

// Read a flattened device tree into a heirarchical structure which refers to
// the contents of the flattened tree in place. Modifying the flat tree
// invalidates the unflattened one.
DeviceTree *fdt_unflatten(void *blob);



/*
 * Unflattened device tree functions.
 */

// Figure out how big a device tree would be if it were flattened.
uint32_t dt_flat_size(DeviceTree *tree);
// Flatten a device tree into the buffer pointed to by dest.
void dt_flatten(DeviceTree *tree, void *dest);
void dt_print_node(DeviceTreeNode *node);
// Read #address-cells and #size-cells properties from a node.
void dt_read_cell_props(DeviceTreeNode *node, u32 *addrcp, u32 *sizecp);
// Look up or create a node relative to a parent node, through its path
// represented as an array of strings.
DeviceTreeNode *dt_find_node(DeviceTreeNode *parent, const char **path,
			     u32 *addrcp, u32 *sizecp, int create);
// Look up or create a node relative to a parent node, through its path
// represented as a string of '/' separated node names.
DeviceTreeNode *dt_find_node_by_path(DeviceTreeNode *parent, const char *path,
				     u32 *addrcp, u32 *sizecp, int create);
// Look up a node relative to a parent node, through its compatible string.
DeviceTreeNode *dt_find_compat(DeviceTreeNode *parent, const char *compatible);
// Write src into *dest as a 'length'-byte big-endian integer.
void dt_write_int(u8 *dest, u64 src, size_t length);
// Add different kinds of properties to a node, or update existing ones.
void dt_add_bin_prop(DeviceTreeNode *node, char *name, void *data, size_t size);
void dt_add_string_prop(DeviceTreeNode *node, char *name, char *str);
void dt_add_u32_prop(DeviceTreeNode *node, char *name, u32 val);
void dt_add_reg_prop(DeviceTreeNode *node, u64 *addrs, u64 *sizes,
		     int count, u32 addr_cells, u32 size_cells);

void dt_find_bin_prop(DeviceTreeNode *node, const char *name, void **data,
		      size_t *size);

/*
 * Fixups to apply to a kernel's device tree before booting it.
 */

typedef struct DeviceTreeFixup
{
	// The function which does the fixing.
	int (*fixup)(struct DeviceTreeFixup *fixup, DeviceTree *tree);

	ListNode list_node;
} DeviceTreeFixup;

extern ListNode device_tree_fixups;

int dt_apply_fixups(DeviceTree *tree);

#endif /* __BASE_DEVICE_TREE_H__ */
