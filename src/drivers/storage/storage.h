/*
 * Copyright 2012 Google Inc.
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
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 */
#ifndef __DRIVERS_STORAGE_H__
#define __DRIVERS_STORAGE_H__

int storage_show(void);
int storage_read(int base_block, int num_blocks, int *dest_addr);
int storage_write(int base_block, int num_blocks, int *src_addr);
int storage_dev(int device, char *const *device_name);
int storage_init(void);

#endif /* __DRIVERS_STORAGE_USB_H__ */
