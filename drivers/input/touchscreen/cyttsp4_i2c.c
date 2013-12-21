/*
 * Source for:
 * Cypress TrueTouch(TM) Standard Product (TTSP) I2C touchscreen driver.
 * For use with Cypress Gen4 and Solo parts.
 * Supported parts include:
 * CY8CTMA398
 * CY8CTMA884
 *
 * Copyright (C) 2009-2011 Cypress Semiconductor, Inc.
 * Copyright (C) 2011 Motorola Mobility, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contact Cypress Semiconductor at www.cypress.com <kev@cypress.com>
 *
 */

#include "cyttsp4_core.h"

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/byteorder/generic.h>
#include <linux/bitops.h>
#include <linux/pm_runtime.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif /* CONFIG_HAS_EARLYSUSPEND */
#include <linux/input/touch_platform.h>
#include <linux/of_gpio.h>
#include <linux/input/cyttsp4_params.h>

#define CY_I2C_DATA_SIZE  (3 * 256)


#include <linux/i2c.h>
#include <linux/slab.h>

#ifdef KERNEL_ABOVE_2_6_38
#include <linux/input/mt.h>
#endif

#define DRIVER_NAME "cyttsp4-i2c"
#define INPUT_PHYS_NAME "cyttsp4-i2c/input0"


struct cyttsp4_i2c {
	struct cyttsp4_bus_ops ops;
	struct i2c_client *client;
	void *ttsp_client;
	u8 wr_buf[CY_I2C_DATA_SIZE];
};

/************************************************************************************/
/**                           Platform Data                                        **/ 

static int cyttsp4_reset_gpio = 0;
static int cyttsp4_irq_gpio = 0;

static int cyttsp4_hw_reset(void)
{
	int ret = 0;

	gpio_request(cyttsp4_reset_gpio, "touch_reset");
	gpio_direction_output(cyttsp4_reset_gpio, 1);

	gpio_request(cyttsp4_irq_gpio, "touch_interrupt");
	gpio_direction_input(cyttsp4_irq_gpio);
	gpio_set_value(cyttsp4_reset_gpio, 1);
	pr_info("%s: gpio_set_value(step%d)=%d\n", __func__, 1, 1);
	msleep(20);
//	msleep(100);
	gpio_set_value(cyttsp4_reset_gpio, 0);
	pr_info("%s: gpio_set_value(step%d)=%d\n", __func__, 2, 0);
	msleep(40);
//	msleep(200);
	gpio_direction_input(cyttsp4_irq_gpio);
	gpio_set_value(cyttsp4_reset_gpio, 1);
	msleep(20);
	pr_info("%s: gpio_set_value(step%d)=%d\n", __func__, 3, 1);

	return ret;
}

static int cyttsp4_hw_recov(int on)
{
	int retval = 0;

	pr_info("%s: on=%d\n", __func__, on);
	if (on == 0) {
		cyttsp4_hw_reset();
		retval = 0;
	} else
		retval = -ENOSYS;

	return retval;
}


static int cyttsp4_irq_stat(void)
{
	int retval = 0;

	gpio_request(cyttsp4_irq_gpio, "touch_interrupt");
	gpio_direction_input(cyttsp4_irq_gpio);
	retval = gpio_get_value(cyttsp4_irq_gpio);
	//printk("CYPRESS gpio irq value %d\n", retval);

	return retval;
}

#if 0

#define CY_ABS_MIN_X 0
#define CY_ABS_MIN_Y 0
#define CY_ABS_MIN_P 0
#define CY_ABS_MIN_W 0
#define CY_ABS_MIN_T 0 /* //1 */
#define CY_ABS_MAX_X CY_MAXX
#define CY_ABS_MAX_Y CY_MAXY
#define CY_ABS_MAX_P 255
#define CY_ABS_MAX_W 255
#define CY_ABS_MAX_T 9 /* //10 */
#define CY_IGNORE_VALUE 0xFFFF

#else

#define CY_ABS_MIN_X 0
#define CY_ABS_MIN_Y 0
#define CY_ABS_MIN_P 0
#define CY_ABS_MIN_W 0
#define CY_ABS_MIN_T 1
#define CY_ABS_MAX_X CY_MAXX
#define CY_ABS_MAX_Y CY_MAXY
#define CY_ABS_MAX_P 255
#define CY_ABS_MAX_W 255
#define CY_ABS_MAX_T 10
#define CY_IGNORE_VALUE 0xFFFF

#endif

static struct touch_settings cyttsp4_sett_param_regs = {
	.data = (uint8_t *)&cyttsp4_param_regs[0],
	.size = sizeof(cyttsp4_param_regs),
	.tag = 0,
};

static struct touch_settings cyttsp4_sett_param_size = {
	.data = (uint8_t *)&cyttsp4_param_size[0],
	.size = sizeof(cyttsp4_param_size),
	.tag = 0,
};

/* Design Data Table */
static u8 cyttsp4_ddata[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23, 24 /* test padding, 25, 26, 27, 28, 29, 30, 31 */
};

static struct touch_settings cyttsp4_sett_ddata = {
	.data = (uint8_t *)&cyttsp4_ddata[0],
	.size = sizeof(cyttsp4_ddata),
	.tag = 0,
};

/* Manufacturing Data Table */
static u8 cyttsp4_mdata[] = {
	65, 64, /* test truncation */63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48,
	47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32,
	31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16,
	15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
};

static struct touch_settings cyttsp4_sett_mdata = {
	.data = (uint8_t *)&cyttsp4_mdata[0],
	.size = sizeof(cyttsp4_mdata),
	.tag = 0,
};



#define CY_USE_INCLUDE_FBL
#ifdef CY_USE_INCLUDE_FBL
#include "linux/input/cyttsp4_img.h"
static struct touch_firmware cyttsp4_firmware = {
	.img = cyttsp4_img,
	.size = sizeof(cyttsp4_img),
	.ver = cyttsp4_ver,
	.vsize = sizeof(cyttsp4_ver),
};
#else
static u8 test_img[] = {0, 1, 2, 4, 8, 16, 32, 64, 128};
static u8 test_ver[] = {0, 0, 0, 0, 0x10, 0x20, 0, 0, 0};
static struct touch_firmware cyttsp4_firmware = {
	.img = test_img,
	.size = sizeof(test_img),
	.ver = test_ver,
	.vsize = sizeof(test_ver),
};
#endif

static const uint16_t cyttsp4_abs[] = {
	ABS_MT_POSITION_X, CY_ABS_MIN_X, CY_ABS_MAX_X, 0, 0,
	ABS_MT_POSITION_Y, CY_ABS_MIN_Y, CY_ABS_MAX_Y, 0, 0,
	ABS_MT_PRESSURE, CY_ABS_MIN_P, CY_ABS_MAX_P, 0, 0,
	ABS_MT_TOUCH_MAJOR, CY_ABS_MIN_W, CY_ABS_MAX_W, 0, 0,
	ABS_MT_TRACKING_ID, CY_ABS_MIN_T, CY_ABS_MAX_T, 0, 0,
};

struct touch_framework cyttsp4_framework = {
	.abs = (uint16_t *)&cyttsp4_abs[0],
	.size = sizeof(cyttsp4_abs)/sizeof(uint16_t),
	.enable_vkeys = 1,
};

struct touch_platform_data cyttsp4_i2c_touch_platform_data = {
	.sett = {
		NULL,	/* Reserved */
		NULL,	/* Command Registers */
		NULL,	/* Touch Report */
		NULL,	/* Cypress Data Record */
		NULL,	/* Test Record */
		NULL,	/* Panel Configuration Record */
		&cyttsp4_sett_param_regs,
		&cyttsp4_sett_param_size,
		NULL,	/* Reserved */
		NULL,	/* Reserved */
		NULL,	/* Operational Configuration Record */
		&cyttsp4_sett_ddata,	/* Design Data Record */
		&cyttsp4_sett_mdata,	/* Manufacturing Data Record */
	},
	.fw = &cyttsp4_firmware,
	.frmwrk = &cyttsp4_framework,
	.addr = {CY_I2C_TCH_ADR, CY_I2C_LDR_ADR},
	.flags = /*0x01 | 0x02 | */0x20 | 0x40,
	.hw_reset = cyttsp4_hw_reset,
	.hw_recov = cyttsp4_hw_recov,
	.irq_stat = cyttsp4_irq_stat,
};

/************************************************************************************/

static s32 cyttsp4_i2c_read_block_data(void *handle, u16 subaddr,
	size_t length, void *values, int i2c_addr, bool use_subaddr)
{
	struct cyttsp4_i2c *ts = container_of(handle, struct cyttsp4_i2c, ops);
	int retval = 0;
	u8 sub_addr[2];
	int subaddr_len;

	if (use_subaddr) {
		subaddr_len = 1;
		sub_addr[0] = subaddr;
	}

	ts->client->addr = i2c_addr;
	if (!use_subaddr)
		goto read_packet;

	/* write subaddr */
	retval = i2c_master_send(ts->client, sub_addr, subaddr_len);

	//printk("cyttsp4_i2c_READ_block_data res %d\n", retval);

	if (retval < 0)
		return retval;
	else if (retval != subaddr_len)
		return -EIO;

read_packet:
	retval = i2c_master_recv(ts->client, values, length);

	return (retval < 0) ? retval : retval != length ? -EIO : 0;
}

static s32 cyttsp4_i2c_write_block_data(void *handle, u16 subaddr,
	size_t length, const void *values, int i2c_addr, bool use_subaddr)
{
	struct cyttsp4_i2c *ts = container_of(handle, struct cyttsp4_i2c, ops);
	int retval;

	if (use_subaddr) {
		ts->wr_buf[0] = subaddr;
		memcpy(&ts->wr_buf[1], values, length);
		length += 1;
	} else {
		memcpy(&ts->wr_buf[0], values, length);
	}
	ts->client->addr = i2c_addr;
	retval = i2c_master_send(ts->client, ts->wr_buf, length);

	//printk("cyttsp4_i2c_WRITE_block_data res %d\n", retval);

	return (retval < 0) ? retval : retval != length ? -EIO : 0;
}

#ifdef CONFIG_OF
static int cyttsp4_i2c_parse_dt(struct device *dev, struct cyttsp4_i2c_platform_data *pdata)
{
	//int retval; //TODO: error checking
	struct device_node *np = dev->of_node;

	/* reset, irq gpio info */
	pdata->irq_gpio = of_get_named_gpio(np, "cypress,irq-gpio", 0);
	pdata->reset_gpio = of_get_named_gpio(np, "cypress,reset-gpio", 0);

	printk("DIMACYPRESS  parse dt reset gpio %d irq gpio %d\n",pdata->reset_gpio,pdata->irq_gpio);

//	return -ENODEV;
	return 0;
}
#else
static int cyttsp4_i2c_parse_dt(struct device *dev, struct synaptics_dsx_platform_data *pdata)
{
	return -ENODEV;
}
#endif


static int __devinit cyttsp4_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int retval = 0;
	struct cyttsp4_i2c *ts;
	struct cyttsp4_i2c_platform_data *platform_data =
			client->dev.platform_data;

	printk(KERN_ERR "DIMACYPRESS cypress probe started\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	if (client->dev.of_node) {
		platform_data = devm_kzalloc(&client->dev,
			sizeof(struct cyttsp4_i2c_platform_data), GFP_KERNEL);
		if (!platform_data) {
			dev_err(&client->dev, "%s: Failed to allocate memory for pdata\n", __func__);
			return -ENOMEM;
		}

		retval = cyttsp4_i2c_parse_dt(&client->dev, platform_data);
		if (retval)
			return retval;
	}

	if (!platform_data) {
		dev_err(&client->dev,
				"%s: No platform data found\n",
				__func__);
		return -EINVAL;
	}


	if (gpio_is_valid(platform_data->irq_gpio)) {
		retval = gpio_request(platform_data->irq_gpio, "touch_interrupt");
		if (retval) {
			dev_err(&client->dev,
				"%s: Failed to request touch_interrupt GPIO, rc=%d\n",
				__func__, retval);
			goto err_request_gpio_irq;
		}

		retval = gpio_direction_input(platform_data->irq_gpio);
		if (retval) {
			dev_err(&client->dev,
				"%s: Failed to configure touch_interrupt GPIO, rc=%d\n",
				__func__, retval);
			goto err_configure_gpio_irq;
		}
	}

	if (gpio_is_valid(platform_data->reset_gpio)) {
		retval = gpio_request(platform_data->reset_gpio, "touch_reset");
		if (retval) {
			dev_err(&client->dev,
				"%s: Failed to request touch_reset GPIO, rc=%d\n",
				__func__, retval);
			goto err_request_reset_gpio;
		}
		retval = gpio_direction_output(platform_data->reset_gpio, 1);
		if (retval) {
			dev_err(&client->dev,
				"%s: Failed to set reset GPIO direction, rc=%d\n",
				__func__, retval);
			goto err_reset;
		}

		gpio_set_value(platform_data->reset_gpio, 0);
	}

	client->irq = gpio_to_irq(platform_data->irq_gpio);
	/* GPIOs */
	cyttsp4_reset_gpio = platform_data->reset_gpio;
	cyttsp4_irq_gpio = platform_data->irq_gpio;

	/* allocate and clear memory */
	ts = kzalloc(sizeof(struct cyttsp4_i2c), GFP_KERNEL);
	if (ts == NULL) {
		pr_err("%s: Error, kzalloc.\n", __func__);
		retval = -ENOMEM;
		goto cyttsp4_i2c_probe_exit;
	}


	/* register driver_data */
	ts->client = client;
	i2c_set_clientdata(client, ts);
	ts->ops.write = cyttsp4_i2c_write_block_data;
	ts->ops.read = cyttsp4_i2c_read_block_data;
	ts->ops.dev = &client->dev;
	ts->ops.dev->bus = &i2c_bus_type;

	ts->ttsp_client = cyttsp4_core_init(&ts->ops, &client->dev,
			client->irq, client->name, (void*) &cyttsp4_i2c_touch_platform_data);
	if (ts->ttsp_client == NULL) {
		kfree(ts);
		ts = NULL;
		retval = -ENODATA;
		pr_err("%s: Registration fail ret=%d\n", __func__, retval);
		goto cyttsp4_i2c_probe_exit;
	}

	dev_dbg(ts->ops.dev, "%s: Registration complete\n", __func__);

cyttsp4_i2c_probe_exit:
err_reset:
	if (gpio_is_valid(platform_data->reset_gpio)) {
		gpio_free(platform_data->reset_gpio);
	}

err_request_reset_gpio:
err_configure_gpio_irq:
	if (gpio_is_valid(platform_data->irq_gpio)) {
		gpio_free(platform_data->irq_gpio);
	}

err_request_gpio_irq :

	return retval;
}


/* registered in driver struct */
static int __devexit cyttsp4_i2c_remove(struct i2c_client *client)
{
	struct cyttsp4_i2c *ts;

	ts = i2c_get_clientdata(client);
	cyttsp4_core_release(ts->ttsp_client);
	kfree(ts);
	return 0;
}

#if 0
static int cyttsp4_i2c_shutdown(struct i2c_client *client)
{
	return 0;
}
#endif

#if !defined(CONFIG_HAS_EARLYSUSPEND) && !defined(CONFIG_PM_SLEEP)
#if defined(CONFIG_PM)
static int cyttsp4_i2c_suspend(struct i2c_client *client, pm_message_t message)
{
	struct cyttsp4_i2c *ts = i2c_get_clientdata(client);

	return cyttsp4_suspend(ts);
}

static int cyttsp4_i2c_resume(struct i2c_client *client)
{
	struct cyttsp4_i2c *ts = i2c_get_clientdata(client);

	return cyttsp4_resume(ts);
}
#endif
#endif

static const struct i2c_device_id cyttsp4_i2c_id[] = {
	{CY_I2C_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, cyttsp4_i2c_id);


#ifdef CONFIG_OF
static struct of_device_id cyttsp4_match_table[] = {
	{ .compatible = "cypress,ttsp4",},
	{ },
};
#else
#define cyttsp4_match_table NULL
#endif

static struct i2c_driver cyttsp4_i2c_driver = {
	.driver = {
		.name = CY_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = cyttsp4_match_table,
#if !defined(CONFIG_HAS_EARLYSUSPEND)
#if defined(CONFIG_PM_SLEEP)
		.pm = &cyttsp4_pm_ops,
#endif
#endif

	},
	.probe = cyttsp4_i2c_probe,
	.remove = __devexit_p(cyttsp4_i2c_remove),
	.id_table = cyttsp4_i2c_id,
#if !defined(CONFIG_HAS_EARLYSUSPEND) && !defined(CONFIG_PM_SLEEP)
#if defined(CONFIG_PM)
	.suspend = cyttsp4_i2c_suspend,
	.resume = cyttsp4_i2c_resume,
#endif
#endif
};

static int __init cyttsp4_i2c_init(void)
{
	return i2c_add_driver(&cyttsp4_i2c_driver);
}

static void __exit cyttsp4_i2c_exit(void)
{
	return i2c_del_driver(&cyttsp4_i2c_driver);
}

module_init(cyttsp4_i2c_init);
module_exit(cyttsp4_i2c_exit);

MODULE_ALIAS(CY_I2C_NAME);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch(R) Standard Product (TTSP) I2C driver");
MODULE_AUTHOR("Cypress");


