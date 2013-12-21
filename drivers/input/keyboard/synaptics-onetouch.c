/* Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 * Copyright (c) 2013, Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pwm.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>

#define SO_ADDR_IF_CONFIG		0x0000
#define SO_ADDR_GENERAL_CONFIG		0x0001

#define SYNAPTICS_ONETOUCH_OF_COMPAT	"synaptics,so340010"
#define SYNAPTICS_ONETOUCH_DEV_NAME	"so340010"

static struct i2c_client *__client;

static int setreg(u16 address, u16 value)
{
	struct i2c_msg msg;
	u8 buf[4];

	if (!__client)
		return -EINVAL;

	/* 16 bit address + 16 bit value (big-endian) */
	buf[0] = (address >> 8) & 0xff;
	buf[1] = address & 0xff;
	buf[2] = (value >> 8) & 0xff;
	buf[3] = value & 0xff;

	msg.addr = __client->addr;
	msg.flags = 0;
	msg.len = 4;
	msg.buf = buf;
	if (i2c_transfer(__client->adapter, &msg, 1) != 1) {
		pr_err("%s - i2c_transfer failed\n", __func__);
	}

	return 0;
}

static int __devinit synaptics_onetouch_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	__client = client;

	setreg(SO_ADDR_IF_CONFIG, 0x0007);	// init controller

	return 0;
}

static int __devexit synaptics_onetouch_remove(struct i2c_client *client)
{
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id synaptics_onetouch_match_table[] = {
	{ .compatible = SYNAPTICS_ONETOUCH_OF_COMPAT, },
	{ },
};
#else
#define synaptics_one_touch_match_table NULL
#endif

static const struct i2c_device_id synaptics_onetouch_i2c_id[] = {
	{ SYNAPTICS_ONETOUCH_DEV_NAME, 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, synaptics_onetouch_i2c_id);

static struct i2c_driver synaptics_onetouch_driver = {
	.driver = {
		.name 		= SYNAPTICS_ONETOUCH_DEV_NAME,
		.owner 		= THIS_MODULE,
		.of_match_table = synaptics_onetouch_match_table,
	},
		.probe		= synaptics_onetouch_probe,
		.remove		= __devexit_p(synaptics_onetouch_remove),
		.id_table	= synaptics_onetouch_i2c_id,
};

module_i2c_driver(synaptics_onetouch_driver);

MODULE_DESCRIPTION("synaptics onetouch i2c driver");
MODULE_LICENSE("GPL v2");
