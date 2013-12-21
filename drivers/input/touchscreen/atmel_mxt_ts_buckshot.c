/*
 * Atmel maXTouch Touchscreen driver
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Copyright (C) 2011 Atmel Corporation
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/i2c/atmel_mxt_ts_buckshot.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
//#include <../arch/arm/mach-msm/board-buckshot.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

// // TODO
// #define MXT_ACTIVE_LOAD_UA	15000
// #define MXT_LPM_LOAD_UA		10
// #define MXT_ACTIVE_LOAD_DIG_UA	10000
// #define MXT_LPM_LOAD_DIG_UA	10

/* Family ID */
#define MXT224_ID		0x80
#define MXT768E_ID		0xA1
#define MXT1386_ID		0xA0

/* Version */
#define MXT_VER_20		20
#define MXT_VER_21		21
#define MXT_VER_22		22

/* I2C slave address pairs */
struct mxt_address_pair {
	int bootloader;
	int application;
};

static const struct mxt_address_pair mxt_slave_addresses[] = {
	{ 0x24, 0x4a },
	{ 0x25, 0x4b },
	{ 0x25, 0x4b },
	{ 0x26, 0x4c },
	{ 0x27, 0x4d },
	{ 0x34, 0x5a },
	{ 0x35, 0x5b },
	{ 0 },
};

enum mxt_device_state { INIT, APPMODE, BOOTLOADER };

/* Firmware files */
#define MXT_FW_NAME		"maxtouch.fw"
#define MXT_CFG_NAME		"maxtouch.cfg"
#define MXT_CFG_MAGIC		"OBP_RAW V1"

/* Registers */
#define MXT_FAMILY_ID		0x00
#define MXT_VARIANT_ID		0x01
#define MXT_VERSION		0x02
#define MXT_BUILD		0x03
#define MXT_MATRIX_X_SIZE	0x04
#define MXT_MATRIX_Y_SIZE	0x05
#define MXT_OBJECT_NUM		0x06
#define MXT_OBJECT_START	0x07

#define MXT_OBJECT_SIZE		6

/* Object types */
#define MXT_DEBUG_DIAGNOSTIC_T37	37
#define MXT_GEN_MESSAGE_T5		5
#define MXT_GEN_COMMAND_T6		6
#define MXT_GEN_POWER_T7		7
#define MXT_GEN_ACQUIRE_T8		8
#define MXT_GEN_DATASOURCE_T53		53
#define MXT_TOUCH_MULTI_T9		9
#define MXT_TOUCH_KEYARRAY_T15		15
#define MXT_TOUCH_PROXIMITY_T23		23
#define MXT_TOUCH_PROXKEY_T52		52
#define MXT_PROCI_GRIPFACE_T20		20
#define MXT_PROCG_NOISE_T22		22
#define MXT_PROCI_ONETOUCH_T24		24
#define MXT_PROCI_TWOTOUCH_T27		27
#define MXT_PROCI_GRIP_T40		40
#define MXT_PROCI_PALM_T41		41
#define MXT_PROCI_TOUCHSUPPRESSION_T42	42
#define MXT_PROCI_STYLUS_T47		47
#define MXT_PROCG_NOISESUPPRESSION_T48	48
#define MXT_SPT_COMMSCONFIG_T18		18
#define MXT_SPT_GPIOPWM_T19		19
#define MXT_SPT_SELFTEST_T25		25
#define MXT_SPT_CTECONFIG_T28		28
#define MXT_SPT_USERDATA_T38		38
#define MXT_SPT_DIGITIZER_T43		43
#define MXT_SPT_MESSAGECOUNT_T44	44
#define MXT_SPT_CTECONFIG_T46		46

/* MXT_GEN_MESSAGE_T5 object */
#define MXT_RPTID_NOMSG		0xff
#define MXT_MSG_MAX_SIZE	9

/* MXT_GEN_COMMAND_T6 field */
#define MXT_COMMAND_RESET	0
#define MXT_COMMAND_BACKUPNV	1
#define MXT_COMMAND_CALIBRATE	2
#define MXT_COMMAND_REPORTALL	3
#define MXT_COMMAND_DIAGNOSTIC	5

/* MXT_GEN_POWER_T7 field */
#define MXT_POWER_IDLEACQINT	0
#define MXT_POWER_ACTVACQINT	1
#define MXT_POWER_ACTV2IDLETO	2

#define MXT_POWER_CFG_RUN	0
#define MXT_POWER_CFG_DEEPSLEEP	1

/* MXT_GEN_ACQUIRE_T8 field */
#define MXT_ACQUIRE_CHRGTIME	0
#define MXT_ACQUIRE_TCHDRIFT	2
#define MXT_ACQUIRE_DRIFTST	3
#define MXT_ACQUIRE_TCHAUTOCAL	4
#define MXT_ACQUIRE_SYNC	5
#define MXT_ACQUIRE_ATCHCALST	6
#define MXT_ACQUIRE_ATCHCALSTHR	7

/* MXT_TOUCH_MULTI_T9 field */
#define MXT_TOUCH_CTRL		0
#define MXT_TOUCH_XORIGIN	1
#define MXT_TOUCH_YORIGIN	2
#define MXT_TOUCH_XSIZE		3
#define MXT_TOUCH_YSIZE		4
#define MXT_TOUCH_BLEN		6
#define MXT_TOUCH_TCHTHR	7
#define MXT_TOUCH_TCHDI		8
#define MXT_TOUCH_ORIENT	9
#define MXT_TOUCH_MOVHYSTI	11
#define MXT_TOUCH_MOVHYSTN	12
#define MXT_TOUCH_NUMTOUCH	14
#define MXT_TOUCH_MRGHYST	15
#define MXT_TOUCH_MRGTHR	16
#define MXT_TOUCH_AMPHYST	17
#define MXT_TOUCH_XRANGE_LSB	18
#define MXT_TOUCH_XRANGE_MSB	19
#define MXT_TOUCH_YRANGE_LSB	20
#define MXT_TOUCH_YRANGE_MSB	21
#define MXT_TOUCH_XLOCLIP	22
#define MXT_TOUCH_XHICLIP	23
#define MXT_TOUCH_YLOCLIP	24
#define MXT_TOUCH_YHICLIP	25
#define MXT_TOUCH_XEDGECTRL	26
#define MXT_TOUCH_XEDGEDIST	27
#define MXT_TOUCH_YEDGECTRL	28
#define MXT_TOUCH_YEDGEDIST	29
#define MXT_TOUCH_JUMPLIMIT	30

/* MXT_TOUCH_KEYARRAY_T15 */

#define T15_NUM_KEYS		1
#define T15_RELEASE		1
#define T15_PRESS		0

int t15_keycodes[T15_NUM_KEYS] = {
	KEY_HOME,
};

static bool t15_keystatus[T15_NUM_KEYS];


/* MXT_PROCI_GRIPFACE_T20 field */
#define MXT_GRIPFACE_CTRL	0
#define MXT_GRIPFACE_XLOGRIP	1
#define MXT_GRIPFACE_XHIGRIP	2
#define MXT_GRIPFACE_YLOGRIP	3
#define MXT_GRIPFACE_YHIGRIP	4
#define MXT_GRIPFACE_MAXTCHS	5
#define MXT_GRIPFACE_SZTHR1	7
#define MXT_GRIPFACE_SZTHR2	8
#define MXT_GRIPFACE_SHPTHR1	9
#define MXT_GRIPFACE_SHPTHR2	10
#define MXT_GRIPFACE_SUPEXTTO	11

/* MXT_PROCI_NOISE field */
#define MXT_NOISE_CTRL		0
#define MXT_NOISE_OUTFLEN	1
#define MXT_NOISE_GCAFUL_LSB	3
#define MXT_NOISE_GCAFUL_MSB	4
#define MXT_NOISE_GCAFLL_LSB	5
#define MXT_NOISE_GCAFLL_MSB	6
#define MXT_NOISE_ACTVGCAFVALID	7
#define MXT_NOISE_NOISETHR	8
#define MXT_NOISE_FREQHOPSCALE	10
#define MXT_NOISE_FREQ0		11
#define MXT_NOISE_FREQ1		12
#define MXT_NOISE_FREQ2		13
#define MXT_NOISE_FREQ3		14
#define MXT_NOISE_FREQ4		15
#define MXT_NOISE_IDLEGCAFVALID	16

/* MXT_SPT_COMMSCONFIG_T18 */
#define MXT_COMMS_CTRL		0
#define MXT_COMMS_CMD		1

/* MXT_SPT_CTECONFIG_T28 field */
#define MXT_CTE_CTRL		0
#define MXT_CTE_CMD		1
#define MXT_CTE_MODE		2
#define MXT_CTE_IDLEGCAFDEPTH	3
#define MXT_CTE_ACTVGCAFDEPTH	4
#define MXT_CTE_VOLTAGE		5

#define MXT_VOLTAGE_DEFAULT	2700000
#define MXT_VOLTAGE_STEP	10000

#define MXT_VTG_MIN_UV		3000000
#define MXT_VTG_MAX_UV		3000000
#define MXT_ACTIVE_LOAD_UA	15000
#define MXT_LPM_LOAD_UA		10

/* Digital voltage @1.8 V */
#define MXT_VTG_DIG_MIN_UV	1800000
#define MXT_VTG_DIG_MAX_UV	1800000
#define MXT_ACTIVE_LOAD_DIG_UA	10000
#define MXT_LPM_LOAD_DIG_UA	10

#define MXT_I2C_VTG_MIN_UV	1800000
#define MXT_I2C_VTG_MAX_UV	1800000
#define MXT_I2C_LOAD_UA		10000
#define MXT_I2C_LPM_LOAD_UA	10

/* Define for MXT_GEN_COMMAND_T6 */
#define MXT_BOOT_VALUE		0xa5
#define MXT_RESET_VALUE		0x01
#define MXT_BACKUP_VALUE	0x55
#define MXT_BACKUP_TIME		25	/* msec */
#define MXT224_RESET_TIME	65	/* msec */
#define MXT336_HW_RESET_TIME	26	/* msec */
#define MXT768E_RESET_TIME	250	/* msec */
#define MXT1386_RESET_TIME	200	/* msec */
#define MXT_RESET_TIME		200	/* msec */
#define MXT_RESET_NOCHGREAD	400	/* msec */

#define MXT_FWRESET_TIME	1000	/* msec */

/* Command to unlock bootloader */
#define MXT_UNLOCK_CMD_MSB	0xaa
#define MXT_UNLOCK_CMD_LSB	0xdc

/* Bootloader mode status */
#define MXT_WAITING_BOOTLOAD_CMD	0xc0	/* valid 7 6 bit only */
#define MXT_WAITING_FRAME_DATA	0x80	/* valid 7 6 bit only */
#define MXT_FRAME_CRC_CHECK	0x02
#define MXT_FRAME_CRC_FAIL	0x03
#define MXT_FRAME_CRC_PASS	0x04
#define MXT_APP_CRC_FAIL	0x40	/* valid 7 8 bit only */
#define MXT_BOOT_STATUS_MASK	0x3f
#define MXT_BOOT_EXTENDED_ID	(1 << 5)
#define MXT_BOOT_ID_MASK	0x1f

/* Command process status */
#define MXT_STATUS_CFGERROR	(1 << 3)

/* Touch status */
#define MXT_SUPPRESS		(1 << 1)
#define MXT_AMP			(1 << 2)
#define MXT_VECTOR		(1 << 3)
#define MXT_MOVE		(1 << 4)
#define MXT_RELEASE		(1 << 5)
#define MXT_PRESS		(1 << 6)
#define MXT_DETECT		(1 << 7)

/* Touch orient bits */
#define MXT_XY_SWITCH		(1 << 0)
#define MXT_X_INVERT		(1 << 1)
#define MXT_Y_INVERT		(1 << 2)

/* Touchscreen absolute values */
#define MXT_MAX_AREA		0xff

#define MXT_MAX_FINGER		10

#define MXT_MEMACCESS_SIZE 32768
#define MXT_I2C_MAX_REQ_SIZE 256

struct mxt_info {
	u8 family_id;
	u8 variant_id;
	u8 version;
	u8 build;
	u8 matrix_xsize;
	u8 matrix_ysize;
	u8 object_num;
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mxt_early_suspend(struct early_suspend *es);
static void mxt_late_resume(struct early_suspend *es);
#endif

struct mxt_object {
	u8 type;
	u16 start_address;
	u16 size;
	u16 instances;
	u8 num_report_ids;

	/* to map object and message */
	u8 min_reportid;
	u8 max_reportid;
};

struct mxt_message {
	u8 reportid;
	u8 message[MXT_MSG_MAX_SIZE - 2];
};

struct mxt_finger {
	int status;
	int x;
	int y;
	int area;
	int pressure;
};

/* Each client has this additional data */
struct mxt_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct mxt_platform_data_buckshot *pdata;
	enum mxt_device_state state;
	struct mxt_object *object_table;
	struct mxt_info info;
	struct mxt_finger finger[MXT_MAX_FINGER];
	unsigned int irq;
	struct regulator *vcc_ana;
	struct regulator *vcc_i2c;
	struct regulator *vcc_dig;
	bool power_enabled;
	unsigned int max_x;
	unsigned int max_y;
	unsigned int driver_paused;
	struct bin_attribute mem_access_attr;
	int debug_enabled;
	u8 actv_cycle_time;
	u8 idle_cycle_time;
	u8 is_stopped;
	u8 max_reportid;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
	int suspend_state;
#endif

	/* Cached parameters from object table */
	u8 T9_reportid_min;
	u8 T9_reportid_max;
	u8 T6_reportid;
	u16 T5_address;
	u16 T5_msg_size;
        u16 T15_reportid;
};

static u8 mxt_buckshot_read_chg(struct mxt_data *data)
{
	return gpio_get_value_cansleep(data->pdata->irq_gpio);
}

static int mxt_switch_to_bootloader_address(struct mxt_data *data)
{
	int i;
	struct i2c_client *client = data->client;

	if (data->state == BOOTLOADER) {
		dev_err(&client->dev, "Already in BOOTLOADER state\n");
		return -EINVAL;
	}

	for (i = 0; mxt_slave_addresses[i].application != 0;  i++) {
		if (mxt_slave_addresses[i].application == client->addr) {
			dev_info(&client->dev, "Changing to bootloader address: "
				"0x%02x -> 0x%02x",
				client->addr,
				mxt_slave_addresses[i].bootloader);

			client->addr = mxt_slave_addresses[i].bootloader;
			data->state = BOOTLOADER;
			return 0;
		}
	}

	dev_err(&client->dev, "Address 0x%02x not found in address table", client->addr);
	return -EINVAL;
}

static int mxt_switch_to_appmode_address(struct mxt_data *data)
{
	int i;
	struct i2c_client *client = data->client;

	if (data->state == APPMODE) {
		dev_err(&client->dev, "Already in APPMODE state\n");
		return -EINVAL;
	}

	for (i = 0; mxt_slave_addresses[i].application != 0;  i++) {
		if (mxt_slave_addresses[i].bootloader == client->addr) {
			dev_info(&client->dev, "Changing to application mode address: "
				"0x%02x -> 0x%02x",
				client->addr,
				mxt_slave_addresses[i].application);

			client->addr = mxt_slave_addresses[i].application;
			data->state = APPMODE;
			return 0;
		}
	}

	dev_err(&client->dev, "Address 0x%02x not found in address table", client->addr);
	return -EINVAL;
}

static int mxt_get_bootloader_version(struct i2c_client *client, u8 val)
{
	u8 buf[3];

	if (val | MXT_BOOT_EXTENDED_ID)
	{
		dev_dbg(&client->dev, "Retrieving extended mode ID information");

		if (i2c_master_recv(client, &buf[0], 3) != 3) {
			dev_err(&client->dev, "%s: i2c recv failed\n", __func__);
			return -EIO;
		}

		dev_info(&client->dev, "Bootloader ID:%d Version:%d",
			buf[1], buf[2]);

		return buf[0];
	}
	else
	{
		dev_info(&client->dev, "Bootloader ID:%d",
			val & MXT_BOOT_ID_MASK);

		return val;
	}
}

static int mxt_check_bootloader(struct i2c_client *client,
				unsigned int state)
{
	u8 val;

recheck:
	if (i2c_master_recv(client, &val, 1) != 1) {
		dev_err(&client->dev, "%s: i2c recv failed\n", __func__);
		return -EIO;
	}

	switch (state) {
	case MXT_WAITING_BOOTLOAD_CMD:
		val = mxt_get_bootloader_version(client, val);
		val &= ~MXT_BOOT_STATUS_MASK;
		break;
	case MXT_WAITING_FRAME_DATA:
	case MXT_APP_CRC_FAIL:
		val &= ~MXT_BOOT_STATUS_MASK;
		break;
	case MXT_FRAME_CRC_PASS:
		if (val == MXT_FRAME_CRC_CHECK)
			goto recheck;
		if (val == MXT_FRAME_CRC_FAIL) {
			dev_err(&client->dev, "Bootloader CRC fail\n");
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	if (val != state) {
		dev_err(&client->dev, "Invalid bootloader mode state 0x%X\n",
			val);
		return -EINVAL;
	}

	return 0;
}

static int mxt_unlock_bootloader(struct i2c_client *client)
{
	u8 buf[2];

	buf[0] = MXT_UNLOCK_CMD_LSB;
	buf[1] = MXT_UNLOCK_CMD_MSB;

	if (i2c_master_send(client, buf, 2) != 2) {
		dev_err(&client->dev, "%s: i2c send failed\n", __func__);
		return -EIO;
	}

	return 0;
}

static int mxt_fw_write(struct i2c_client *client,
			const u8 *data, unsigned int frame_size)
{
	if (i2c_master_send(client, data, frame_size) != frame_size) {
		dev_err(&client->dev, "%s: i2c send failed\n", __func__);
		return -EIO;
	}

	return 0;
}

static int __mxt_read_reg(struct i2c_client *client,
			       u16 reg, u16 len, void *val)
{
	struct i2c_msg xfer[2];
	u8 buf[2];
	int ret = 0;
	int retries = 3;

	buf[0] = reg & 0xff;
	buf[1] = (reg >> 8) & 0xff;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 2;
	xfer[0].buf = buf;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = len;
	xfer[1].buf = val;

	do {
		ret = i2c_transfer(client->adapter, xfer, 2);
		if (ret != 2)
		{
			dev_err(&client->dev, "%s: i2c transfer failed\n", __func__);
			//return -EIO;
		}
		else
		{
			//dev_err(&client->dev, "%s: i2c transfer succeeded\n", __func__);
			break;
		}
		retries--;
	} while (retries > 0);

	return 0;
}

static int mxt_read_reg(struct i2c_client *client, u16 reg, u8 *val)
{
	return __mxt_read_reg(client, reg, 1, val);
}

static int mxt_write_reg(struct i2c_client *client, u16 reg, u8 val)
{
	u8 buf[3];

	buf[0] = reg & 0xff;
	buf[1] = (reg >> 8) & 0xff;
	buf[2] = val;

	if (i2c_master_send(client, buf, 3) != 3) {
		dev_err(&client->dev, "%s: i2c send failed\n", __func__);
		return -EIO;
	}

	return 0;
}

static int mxt_read_object_table(struct i2c_client *client,
				      u16 reg, u8 *object_buf)
{
	return __mxt_read_reg(client, reg, MXT_OBJECT_SIZE,
				   object_buf);
}

static struct mxt_object *
mxt_get_object(struct mxt_data *data, u8 type)
{
	struct mxt_object *object;
	int i;

	for (i = 0; i < data->info.object_num; i++) {
		object = data->object_table + i;
		if (object->type == type)
			return object;
	}

	dev_err(&data->client->dev, "Invalid object type T%d\n", type);
	return NULL;
}

static int mxt_read_message(struct mxt_data *data,
				 struct mxt_message *message)
{
	int ret;

	/* Do not read last byte which contains CRC */
	ret = __mxt_read_reg(data->client, data->T5_address,
			     data->T5_msg_size, message);

	if (message->reportid != MXT_RPTID_NOMSG && data->debug_enabled)
		print_hex_dump(KERN_INFO, "MXT MSG:", DUMP_PREFIX_NONE,
			16, 1, message, data->T5_msg_size, false);

	return ret;
}

static int mxt_read_message_reportid(struct mxt_data *data,
	struct mxt_message *message, u8 reportid)
{
	int try = 0;
	int error;

	while (++try < 20) {
		error = mxt_read_message(data, message);
		if (error)
			return error;

		if (message->reportid == 0xff)
			return -EINVAL;

		if (message->reportid == reportid)
			return 0;
	}

	return -EINVAL;
}

static int mxt_read_object(struct mxt_data *data,
				u8 type, u8 offset, u8 *val)
{
	struct mxt_object *object;
	u16 reg;

	object = mxt_get_object(data, type);
	if (!object)
		return -EINVAL;

	reg = object->start_address;
	return __mxt_read_reg(data->client, reg + offset, 1, val);
}

static int mxt_write_object(struct mxt_data *data,
				 u8 type, u8 offset, u8 val)
{
	struct mxt_object *object;
	u16 reg;

	object = mxt_get_object(data, type);
	if (!object)
		return -EINVAL;

	if (offset >= object->size * object->instances) {
		dev_err(&data->client->dev, "Tried to write outside object T%d"
			" offset:%d, size:%d\n", type, offset, object->size);
		return -EINVAL;
	}

	reg = object->start_address;
	return mxt_write_reg(data->client, reg + offset, val);
}

static void mxt_input_report(struct mxt_data *data, int single_id)
{
	struct mxt_finger *finger = data->finger;
	struct input_dev *input_dev = data->input_dev;
	int status = finger[single_id].status;
	int finger_num = 0;
	int id;

	for (id = 0; id < MXT_MAX_FINGER; id++) {
		if (!finger[id].status)
			continue;

		input_mt_slot(input_dev, id);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER,
				finger[id].status != MXT_RELEASE);

		if (finger[id].status != MXT_RELEASE) {
			finger_num++;
			input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR,
					finger[id].area);
			input_report_abs(input_dev, ABS_MT_POSITION_X,
					finger[id].x);
			input_report_abs(input_dev, ABS_MT_POSITION_Y,
					finger[id].y);
			input_report_abs(input_dev, ABS_MT_PRESSURE,
					finger[id].pressure);
		} else {
			finger[id].status = 0;
		}
	}

	input_report_key(input_dev, BTN_TOUCH, finger_num > 0);

	if (status != MXT_RELEASE) {
		input_report_abs(input_dev, ABS_X, finger[single_id].x);
		input_report_abs(input_dev, ABS_Y, finger[single_id].y);
		input_report_abs(input_dev,
				 ABS_PRESSURE, finger[single_id].pressure);
	}

	input_sync(input_dev);
}

static void mxt_reset_fingers(struct mxt_data *data)
{
	struct mxt_finger *finger = data->finger;
	struct input_dev *input_dev = data->input_dev;
	int id;

	for (id = 0; id < MXT_MAX_FINGER; id++) {
		if (finger[id].status != 0) {
			input_mt_slot(input_dev, id);
			input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
			finger[id].status = 0;
		}
	}

	input_sync(input_dev);
}


static void mxt_input_touchevent(struct mxt_data *data,
				      struct mxt_message *message)
{
	struct mxt_finger *finger = data->finger;
	struct device *dev = &data->client->dev;
	u8 status = message->message[0];
	int x;
	int y;
	int area;
	int pressure;
	int id;

	if (data->driver_paused)
		return;

	id = message->reportid - data->T9_reportid_min;

	/* Check the touch is present on the screen */
	if (!(status & MXT_DETECT)) {
		if (status & MXT_RELEASE) {
			dev_dbg(dev, "[%d] released\n", id);

			finger[id].status = MXT_RELEASE;
			mxt_input_report(data, id);
		}
		return;
	}

	/* Check only AMP detection */
	if (!(status & (MXT_PRESS | MXT_MOVE)))
		return;

	x = (message->message[1] << 4) | ((message->message[3] >> 4) & 0xf);
	y = (message->message[2] << 4) | ((message->message[3] & 0xf));
	if (data->max_x < 1024)
		x = x >> 2;
	if (data->max_y < 1024)
		y = y >> 2;

	area = message->message[4];
	pressure = message->message[5];

	dev_dbg(dev, "[%d] %s x: %d, y: %d, area: %d\n", id,
		status & MXT_MOVE ? "moved" : "pressed",
		x, y, area);

	finger[id].status = status & MXT_MOVE ?
				MXT_MOVE : MXT_PRESS;
	finger[id].x = x;
	finger[id].y = y;
	finger[id].area = area;
	finger[id].pressure = pressure;

	mxt_input_report(data, id);
}

static void mxt_keyarray_event(struct mxt_data *data,
				      struct mxt_message *message)
{
	struct input_dev *input_dev = data->input_dev;
	int key;
	int report;

	for (key=0; key < T15_NUM_KEYS; key++) {
		report = message->message[1] & (1 << key);

		if (t15_keystatus[key] && report) {
			printk("Key %d reported!  Forwarding press!!!\n", key);
			t15_keystatus[key] = T15_PRESS;
			input_report_key(input_dev, t15_keycodes[key], 1);
			input_sync(input_dev);
		} else if (!t15_keystatus[key] && !report) {
			printk("Key %d reported!  Forwarding release!!!\n", key);
			t15_keystatus[key] = T15_RELEASE;
			input_report_key(input_dev, t15_keycodes[key], 0);
			input_sync(input_dev);
		}
	}
}

static irqreturn_t mxt_interrupt(int irq, void *dev_id)
{
	struct mxt_data *data = dev_id;
	struct mxt_message message;
	struct device *dev = &data->client->dev;
	u8 reportid;

	if (data->state != APPMODE) {
		dev_err(dev, "Ignoring IRQ - not in APPMODE state\n");
		return IRQ_HANDLED;
	}

	if (data->power_enabled == false)
	{
		//dev_info(dev, "Ignoring IRQ - device is either off or still powering on\n");
		return IRQ_HANDLED;
	}

	do {
		if (mxt_read_message(data, &message)) {
			dev_err(dev, "Failed to read message\n");
			goto end;
		}

		reportid = message.reportid;

		if (reportid == MXT_RPTID_NOMSG) {
			goto end;

		} else if (reportid >= data->T9_reportid_min
			&& reportid <= data->T9_reportid_max) {
			mxt_input_touchevent(data, &message);
		} else if (reportid == data->T15_reportid) {
			mxt_keyarray_event(data, &message);
		} else if (reportid == data->T6_reportid) {
			if (message.message[0] & MXT_STATUS_CFGERROR)
				dev_err(dev, "Configuration error\n");
		}
	} while (reportid != MXT_RPTID_NOMSG);

end:
	return IRQ_HANDLED;
}

static int mxt_make_highchg(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	struct mxt_message message;
	int count;
	int error;

	/* If all objects report themselves then a number of messages equal to
	 * the number of report ids may be generated. Therefore a good safety
	 * heuristic is twice this number */
	count = data->max_reportid * 2;

	/* Read dummy message to make high CHG pin */
	do {
		error = mxt_read_message(data, &message);
		if (error)
			return error;
	} while (message.reportid != MXT_RPTID_NOMSG && --count);

	if (!count) {
		dev_err(dev, "CHG pin isn't cleared\n");
		return -EBUSY;
	}

	return 0;
}

static int mxt_read_current_crc(struct mxt_data *data, unsigned long *crc)
{
	struct device *dev = &data->client->dev;
	int error;
	struct mxt_message message;
	struct mxt_object *object;

	object = mxt_get_object(data, MXT_GEN_COMMAND_T6);
	if (!object)
		return -EIO;

	/* Try to read the config checksum of the existing cfg */
	mxt_write_object(data, MXT_GEN_COMMAND_T6,
		MXT_COMMAND_REPORTALL, 1);

	msleep(30);

	/* Read message from command processor, which only has one report ID */
	error = mxt_read_message_reportid(data, &message, object->max_reportid);
	if (error) {
		dev_err(dev, "Failed to retrieve CRC\n");
		return error;
	}

	/* Bytes 1-3 are the checksum. */
	*crc = message.message[1] | (message.message[2] << 8) |
		(message.message[3] << 16);

	return 0;
}

//This is a version of Atmel's mxt_download_config function modified to load
// the configuration data from a string included in the driver's platform_data
int mxt_download_config_hardcoded_into_board_file(struct mxt_data *data, const char *fn)
{
	struct device *dev = &data->client->dev;
	struct mxt_info cfg_info;
	struct mxt_object *object;
	//const struct firmware *cfg = NULL;
	int ret;
	int offset;
	int pos;
	int i;
	unsigned long current_crc, info_crc, config_crc;
	unsigned int type, instance, size;
	u8 val;
	u16 reg;
	const char *cfgData = data->pdata->config;
	int cfgSize = data->pdata->config_length;

// 	ret = request_firmware(&cfg, fn, dev);
// 	if (ret < 0) {
// 		dev_err(dev, "Failure to request config file %s\n", fn);
// 		return 0;
// 	}

	ret = mxt_read_current_crc(data, &current_crc);
	if (ret)
		return ret;

	if (strncmp(cfgData, MXT_CFG_MAGIC, strlen(MXT_CFG_MAGIC))) {
		dev_err(dev, "Unrecognised config file\n");
		ret = -EINVAL;
		goto release;
	}

	pos = strlen(MXT_CFG_MAGIC);

	/* Load information block and check */
	for (i = 0; i < sizeof(struct mxt_info); i++) {
		ret = sscanf(cfgData + pos, "%hhx%n",
			     (unsigned char *)&cfg_info + i,
			     &offset);
		if (ret != 1) {
			dev_err(dev, "Bad format\n");
			ret = -EINVAL;
			goto release;
		}

		pos += offset;
	}

	if (cfg_info.family_id != data->info.family_id) {
		dev_err(dev, "Family ID mismatch!\n");
		ret = -EINVAL;
		goto release;
	}

	if (cfg_info.variant_id != data->info.variant_id) {
		dev_err(dev, "Variant ID mismatch!\n");
		ret = -EINVAL;
		goto release;
	}

	if (cfg_info.version != data->info.version) {
		dev_err(dev, "Warning: version mismatch!\n");
	}

	if (cfg_info.build != data->info.build) {
		dev_err(dev, "Warning: build num mismatch!\n");
	}

	ret = sscanf(cfgData + pos, "%lx%n", &info_crc, &offset);
	if (ret != 1) {
		dev_err(dev, "Bad format\n");
		ret = -EINVAL;
		goto release;
	}
	pos += offset;

	/* Check config CRC */
	ret = sscanf(cfgData + pos, "%lx%n", &config_crc, &offset);
	if (ret != 1) {
		dev_err(dev, "Bad format\n");
		ret = -EINVAL;
		goto release;
	}
	pos += offset;

	if (current_crc == config_crc) {
		dev_info(dev,
		"Config CRC 0x%X: OK\n", (unsigned int) current_crc);
		ret = 0;
		goto release;
	} else {
		dev_info(dev, "Config CRC 0x%X: does not match 0x%X, "
			"writing config\n",
			(unsigned int) current_crc,
			(unsigned int) config_crc);
	}

	while (pos < cfgSize) {
		/* Read type, instance, length */
		ret = sscanf(cfgData + pos, "%x %x %x%n", &type, &instance, &size, &offset);
		if (ret == 0) {
			/* EOF */
			ret = 1;
			goto release;
		} else if (ret < 0) {
			dev_err(dev, "Bad format\n");
			ret = -EINVAL;
			goto release;
		}
		pos += offset;

		object = mxt_get_object(data, type);
		if (!object) {
			ret = -EINVAL;
			ret = 0;
			continue;
			//goto release;
		}

		if (size > object->size) {
			dev_err(dev, "Object length exceeded!  Object %#x, max size is %#x, specified size is %#x\n", object->type, object->size, size);
			ret = -EINVAL;
			goto release;
		}

		if (instance >= object->instances) {
			dev_err(dev, "Object instances exceeded! Object %#x, instances %d\n", object->type, object->instances);
			ret = -EINVAL;
			goto release;
		}

		reg = object->start_address + object->size * instance;

		for (i = 0; i < size; i++) {
			ret = sscanf(cfgData + pos, "%hhx%n",
				     &val,
				     &offset);
			if (ret != 1) {
				dev_err(dev, "Bad format\n");
				ret = -EINVAL;
				goto release;
			}
			ret = mxt_write_reg(data->client, reg + i, val);
			if (ret)
				goto release;

			pos += offset;
		}

		/* If firmware is upgraded, new bytes may be added to end of
		 * objects. It is generally forward compatible to zero these
		 * bytes - previous behaviour will be retained. However
		 * this does invalidate the CRC and will force a config
		 * download every time until the configuration is updated */
		if (size < object->size) {
			dev_info(dev, "Warning: zeroing %d byte(s) in T%d\n",
				 object->size - size, type);

			for (i = size + 1; i < object->size; i++) {
				ret = mxt_write_reg(data->client, reg + i, 0);
				if (ret)
					goto release;
			}
		}
	}

release:
//	release_firmware(cfg);
	return ret;
}

int mxt_download_config(struct mxt_data *data, const char *fn)
{
	struct device *dev = &data->client->dev;
	struct mxt_info cfg_info;
	struct mxt_object *object;
	const struct firmware *cfg = NULL;
	int ret;
	int offset;
	int pos;
	int i;
	unsigned long current_crc, info_crc, config_crc;
	unsigned int type, instance, size;
	u8 val;
	u16 reg;

	ret = request_firmware(&cfg, fn, dev);
	if (ret < 0) {
		dev_err(dev, "Failure to request config file %s\n", fn);
		return 0;
	}

	ret = mxt_read_current_crc(data, &current_crc);
	if (ret)
		return ret;

	if (strncmp(cfg->data, MXT_CFG_MAGIC, strlen(MXT_CFG_MAGIC))) {
		dev_err(dev, "Unrecognised config file\n");
		ret = -EINVAL;
		goto release;
	}

	pos = strlen(MXT_CFG_MAGIC);

	/* Load information block and check */
	for (i = 0; i < sizeof(struct mxt_info); i++) {
		ret = sscanf(cfg->data + pos, "%hhx%n",
			     (unsigned char *)&cfg_info + i,
			     &offset);
		if (ret != 1) {
			dev_err(dev, "Bad format\n");
			ret = -EINVAL;
			goto release;
		}

		pos += offset;
	}

	if (cfg_info.family_id != data->info.family_id) {
		dev_err(dev, "Family ID mismatch!\n");
		ret = -EINVAL;
		goto release;
	}

	if (cfg_info.variant_id != data->info.variant_id) {
		dev_err(dev, "Variant ID mismatch!\n");
		ret = -EINVAL;
		goto release;
	}

	if (cfg_info.version != data->info.version) {
		dev_err(dev, "Warning: version mismatch!\n");
	}

	if (cfg_info.build != data->info.build) {
		dev_err(dev, "Warning: build num mismatch!\n");
	}

	ret = sscanf(cfg->data + pos, "%lx%n", &info_crc, &offset);
	if (ret != 1) {
		dev_err(dev, "Bad format\n");
		ret = -EINVAL;
		goto release;
	}
	pos += offset;

	/* Check config CRC */
	ret = sscanf(cfg->data + pos, "%lx%n", &config_crc, &offset);
	if (ret != 1) {
		dev_err(dev, "Bad format\n");
		ret = -EINVAL;
		goto release;
	}
	pos += offset;

	if (current_crc == config_crc) {
		dev_info(dev,
		"Config CRC 0x%X: OK\n", (unsigned int) current_crc);
		ret = 0;
		goto release;
	} else {
		dev_info(dev, "Config CRC 0x%X: does not match 0x%X, "
			"writing config\n",
			(unsigned int) current_crc,
			(unsigned int) config_crc);
	}

	while (pos < cfg->size) {
		/* Read type, instance, length */
		ret = sscanf(cfg->data + pos, "%x %x %x%n", &type, &instance, &size, &offset);
		if (ret == 0) {
			/* EOF */
			ret = 1;
			goto release;
		} else if (ret < 0) {
			dev_err(dev, "Bad format\n");
			ret = -EINVAL;
			goto release;
		}
		pos += offset;

		object = mxt_get_object(data, type);
		if (!object) {
			ret = -EINVAL;
			goto release;
		}

		if (size > object->size) {
			dev_err(dev, "Object length exceeded!\n");
			ret = -EINVAL;
			goto release;
		}

		if (instance >= object->instances) {
			dev_err(dev, "Object instances exceeded!\n");
			ret = -EINVAL;
			goto release;
		}

		reg = object->start_address + object->size * instance;

		for (i = 0; i < size; i++) {
			ret = sscanf(cfg->data + pos, "%hhx%n",
				     &val,
				     &offset);
			if (ret != 1) {
				dev_err(dev, "Bad format\n");
				ret = -EINVAL;
				goto release;
			}

			ret = mxt_write_reg(data->client, reg + i, val);
			if (ret)
				goto release;

			pos += offset;
		}

		/* If firmware is upgraded, new bytes may be added to end of
		 * objects. It is generally forward compatible to zero these
		 * bytes - previous behaviour will be retained. However
		 * this does invalidate the CRC and will force a config
		 * download every time until the configuration is updated */
		if (size < object->size) {
			dev_info(dev, "Warning: zeroing %d byte(s) in T%d\n",
				 object->size - size, type);

			for (i = size + 1; i < object->size; i++) {
				ret = mxt_write_reg(data->client, reg + i, 0);
				if (ret)
					goto release;
			}
		}
	}

release:
	release_firmware(cfg);
	return ret;
}

static int mxt_soft_reset(struct mxt_data *data, u8 value)
{
//	int timeout_counter = 0;
	struct device *dev = &data->client->dev;

	dev_info(dev, "Resetting chip\n");

	mxt_write_object(data, MXT_GEN_COMMAND_T6,
			MXT_COMMAND_RESET, value);
//Note: the read_chg logic below may have a problem... any pending message from the controller
	// will cause an issue with it and make the touch screen take 1000ms to wake up
// 	if (data->pdata->read_chg == NULL) {
		msleep(MXT_RESET_NOCHGREAD);
// 	} else {
// 		switch (data->info.family_id) {
// 		case MXT224_ID:
// 			msleep(MXT224_RESET_TIME);
// 			break;
// 		case MXT768E_ID:
// 			msleep(MXT768E_RESET_TIME);
// 			break;
// 		case MXT1386_ID:
// 			msleep(MXT1386_RESET_TIME);
// 			break;
// 		default:
// 			msleep(MXT_RESET_TIME);
// 		}
// 		timeout_counter = 0;
// 		//while ((timeout_counter++ <= 100) && data->pdata->read_chg())
// 		while ((timeout_counter++ <= 100) && mxt_buckshot_read_chg(data))
// 			msleep(10);
// 		if (timeout_counter > 100) {
// 			dev_err(dev, "No response after reset!\n");
// 			return -EIO;
// 		}
//	}

	return 0;
}

static int mxt_set_power_cfg(struct mxt_data *data, u8 mode)
{
	struct device *dev = &data->client->dev;
	int error;
	u8 actv_cycle_time;
	u8 idle_cycle_time;

	if (data->state != APPMODE) {
		dev_err(dev, "Not in APPMODE\n");
		return -EINVAL;
	}

	switch (mode) {
	case MXT_POWER_CFG_DEEPSLEEP:
		actv_cycle_time = 0;
		idle_cycle_time = 0;
	case MXT_POWER_CFG_RUN:
	default:
		actv_cycle_time = data->actv_cycle_time;
		idle_cycle_time = data->idle_cycle_time;
	}

	error = mxt_write_object(data, MXT_GEN_POWER_T7, MXT_POWER_ACTVACQINT,
	actv_cycle_time);
	if (error)
		goto i2c_error;

	error = mxt_write_object(data, MXT_GEN_POWER_T7, MXT_POWER_IDLEACQINT,
				idle_cycle_time);
	if (error)
		goto i2c_error;

	dev_dbg(dev, "%s: Set ACTV %d, IDLE %d", __func__,
		actv_cycle_time, idle_cycle_time);

	data->is_stopped = (mode == MXT_POWER_CFG_DEEPSLEEP) ? 1 : 0;

	return 0;

i2c_error:
	dev_err(dev, "Failed to set power cfg");
	return error;
}

static int mxt_read_power_cfg(struct mxt_data *data, u8 *actv_cycle_time,
				u8 *idle_cycle_time)
{
	int error;

	error = mxt_read_object(data, MXT_GEN_POWER_T7,
				MXT_POWER_ACTVACQINT,
				actv_cycle_time);
	if (error)
		return error;

	error = mxt_read_object(data, MXT_GEN_POWER_T7,
				MXT_POWER_IDLEACQINT,
				idle_cycle_time);
	if (error)
		return error;

	return 0;
}

static int mxt_check_power_cfg_post_reset(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int error;

	error = mxt_read_power_cfg(data, &data->actv_cycle_time, &data->idle_cycle_time);
	if (error)
		return error;

	/* Power config is zero, select free run */
	if (data->actv_cycle_time == 0 || data->idle_cycle_time == 0) {
		dev_dbg(dev, "Overriding power cfg to free run\n");
		data->actv_cycle_time = 255;
		data->idle_cycle_time = 255;

		error = mxt_set_power_cfg(data, MXT_POWER_CFG_RUN);
		if (error)
			return error;
	}

	return 0;
}

static int mxt_probe_power_cfg(struct mxt_data *data)
{
	int error;

	error = mxt_read_power_cfg(data, &data->actv_cycle_time, &data->idle_cycle_time);
	if (error)
		return error;

	/* If in deep sleep mode, attempt reset */
	if (data->actv_cycle_time == 0 || data->idle_cycle_time == 0) {
		error = mxt_soft_reset(data, MXT_RESET_VALUE);
		if (error)
			return error;

		error = mxt_check_power_cfg_post_reset(data);
		if (error)
			return error;
	}

	return 0;
}

static int mxt_check_reg_init(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int timeout_counter = 0;
	int ret;
	u8 command_register;

	ret = mxt_download_config_hardcoded_into_board_file(data, MXT_CFG_NAME);
	//ret = mxt_download_config(data, MXT_CFG_NAME);
	if (ret < 0)
		return ret;
	else if (ret == 0)
		/* CRC matched, or no config file, no need to reset */
		return 0;

	/* Backup to memory */
	mxt_write_object(data, MXT_GEN_COMMAND_T6,
			MXT_COMMAND_BACKUPNV,
			MXT_BACKUP_VALUE);
	msleep(MXT_BACKUP_TIME);
	do {
		ret =  mxt_read_object(data, MXT_GEN_COMMAND_T6,
					MXT_COMMAND_BACKUPNV,
					&command_register);
		if (ret)
			return ret;
		msleep(10);
	} while ((command_register != 0) && (timeout_counter++ <= 100));
	if (timeout_counter > 100) {
		dev_err(dev, "No response after backup!\n");
		return -EIO;
	}

	ret = mxt_soft_reset(data, MXT_RESET_VALUE);
	if (ret)
		return ret;

	ret = mxt_check_power_cfg_post_reset(data);
	if (ret)
		return ret;

	return 0;
}

static int mxt_get_info(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	struct mxt_info *info = &data->info;
	int error;
	u8 val;

	error = mxt_read_reg(client, MXT_FAMILY_ID, &val);
	if (error)
		return error;
	info->family_id = val;

	error = mxt_read_reg(client, MXT_VARIANT_ID, &val);
	if (error)
		return error;
	info->variant_id = val;

	error = mxt_read_reg(client, MXT_VERSION, &val);
	if (error)
		return error;
	info->version = val;

	error = mxt_read_reg(client, MXT_BUILD, &val);
	if (error)
		return error;
	info->build = val;

	error = mxt_read_reg(client, MXT_OBJECT_NUM, &val);
	if (error)
		return error;
	info->object_num = val;

	return 0;
}

static int mxt_get_object_table(struct mxt_data *data)
{
	struct device *dev = &data->client->dev;
	int error;
	int i;
	u16 reg;
	u8 reportid = 0;
	u8 buf[MXT_OBJECT_SIZE];

	for (i = 0; i < data->info.object_num; i++) {
		struct mxt_object *object = data->object_table + i;

		reg = MXT_OBJECT_START + MXT_OBJECT_SIZE * i;
		error = mxt_read_object_table(data->client, reg, buf);
		if (error)
			return error;

		object->type = buf[0];
		object->start_address = (buf[2] << 8) | buf[1];
		object->size = buf[3] + 1;
		object->instances = buf[4] + 1;
		object->num_report_ids = buf[5];

		if (object->num_report_ids) {
			reportid += object->num_report_ids * object->instances;
			object->max_reportid = reportid;
			object->min_reportid = object->max_reportid -
				object->instances * object->num_report_ids + 1;
		}

		/* save data for objects used when processing interrupts */
		switch (object->type) {
		case MXT_TOUCH_MULTI_T9:
			data->T9_reportid_max = object->max_reportid;
			data->T9_reportid_min = object->min_reportid;
			break;
		case MXT_GEN_COMMAND_T6:
			data->T6_reportid = object->max_reportid;
			break;
		case MXT_GEN_MESSAGE_T5:
			if (object->size > MXT_MSG_MAX_SIZE) {
				dev_err(dev, "MXT_MSG_MAX_SIZE exceeded");
				return -EINVAL;
			}

			data->T5_address = object->start_address;
			/* skip CRC */
			data->T5_msg_size = object->size - 1;
			break;
                case MXT_TOUCH_KEYARRAY_T15:
                        data->T15_reportid = object->max_reportid;
                        break;
		}

		dev_dbg(dev, "T%d, start:%d size:%d instances:%d "
			"min_reportid:%d max_reportid:%d\n",
			object->type, object->start_address, object->size,
			object->instances,
			object->min_reportid, object->max_reportid);
	}

	/* Store maximum reportid */
	data->max_reportid = reportid;

	return 0;
}

static int mxt_read_resolution(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	int error;
	unsigned int x_range, y_range;
	unsigned int max_x, max_y;
	unsigned char orient;
	unsigned char val;

	/* Update matrix size in info struct */
	error = mxt_read_reg(client, MXT_MATRIX_X_SIZE, &val);
	if (error)
		return error;
	data->info.matrix_xsize = val;

	error = mxt_read_reg(client, MXT_MATRIX_Y_SIZE, &val);
	if (error)
		return error;
	data->info.matrix_ysize = val;

	/* Read X/Y size of touchscreen */
	error =  mxt_read_object(data, MXT_TOUCH_MULTI_T9,
			       MXT_TOUCH_XRANGE_MSB, &val);
	if (error)
		return error;
	x_range = val << 8;

	error =  mxt_read_object(data, MXT_TOUCH_MULTI_T9,
			       MXT_TOUCH_XRANGE_LSB, &val);
	if (error)
		return error;
	x_range |= val;

	error =  mxt_read_object(data, MXT_TOUCH_MULTI_T9,
			       MXT_TOUCH_YRANGE_MSB, &val);
	if (error)
		return error;
	y_range = val << 8;

	error =  mxt_read_object(data, MXT_TOUCH_MULTI_T9,
			       MXT_TOUCH_YRANGE_LSB, &val);
	if (error)
		return error;
	y_range |= val;

	error =  mxt_read_object(data, MXT_TOUCH_MULTI_T9,
			       MXT_TOUCH_ORIENT, &orient);
	if (error)
		return error;

	/* Handle default values */
	if (x_range == 0) x_range = 1023;
	if (y_range == 0) y_range = 1023;

	max_x = x_range + 1;
	max_y = y_range + 1;

	if (orient & MXT_XY_SWITCH) {
		data->max_x = max_y;
		data->max_y = max_x;
	} else {
		data->max_x = max_x;
		data->max_y = max_y;
	}

	dev_info(&client->dev,
			"Matrix Size X%dY%d Touchscreen size X%dY%d\n",
			data->info.matrix_xsize, data->info.matrix_ysize,
			data->max_x, data->max_y);

	return 0;
}

static int mxt_initialize(struct mxt_data *data)
{
	struct i2c_client *client = data->client;
	struct mxt_info *info = &data->info;
	int error;

	error = mxt_get_info(data);
	if (error) {
		/* Try bootloader mode */
		error = mxt_switch_to_bootloader_address(data);
		if (error)
			return error;

		error = mxt_check_bootloader(client, MXT_APP_CRC_FAIL);
		if (error)
			return error;

		dev_err(&client->dev, "Application CRC failure\n");
		data->state = BOOTLOADER;

		return 0;
	}

	dev_info(&client->dev,
			"Family ID: %d Variant ID: %d Version: %d.%d "
			"Build: 0x%02X Object Num: %d\n",
			info->family_id, info->variant_id,
			info->version >> 4, info->version & 0xf,
			info->build, info->object_num);

	data->state = APPMODE;

	data->object_table = kcalloc(info->object_num,
				     sizeof(struct mxt_object),
				     GFP_KERNEL);
	if (!data->object_table) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	/* Get object table information */
	error = mxt_get_object_table(data);
	if (error) {
		dev_err(&client->dev, "Failed to read object table\n");
		return error;
	}

	error = mxt_probe_power_cfg(data);
	if (error) {
		dev_err(&client->dev, "Failed to initialize power cfg\n");
		return error;
	}

	/* Check register init values */
	error = mxt_check_reg_init(data);
	if (error) {
		dev_err(&client->dev, "Failed to initialize config\n");
		return error;
	}

	error = mxt_read_resolution(data);
	if (error) {
		dev_err(&client->dev, "Failed to initialize screen size\n");
		return error;
	}

	return 0;
}

static ssize_t mxt_object_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct mxt_object *object;
	int count = 0;
	int i, j;
	int error;
	u8 val;

	for (i = 0; i < data->info.object_num; i++) {
		object = data->object_table + i;

		count += snprintf(buf + count, PAGE_SIZE - count,
				"Object[%d] (Type %d)\n",
				i + 1, object->type);
		if (count >= PAGE_SIZE)
			return PAGE_SIZE - 1;

		count += snprintf(buf + count, PAGE_SIZE - count,
				"\n");
		if (count >= PAGE_SIZE)
			return PAGE_SIZE - 1;
		continue;

		for (j = 0; j < object->size; j++) {
			error = mxt_read_object(data,
						object->type, j, &val);
			if (error)
				return error;

			count += snprintf(buf + count, PAGE_SIZE - count,
					"\t[%2d]: %02x (%d)\n", j, val, val);
			if (count >= PAGE_SIZE)
				return PAGE_SIZE - 1;
		}

		count += snprintf(buf + count, PAGE_SIZE - count, "\n");
		if (count >= PAGE_SIZE)
			return PAGE_SIZE - 1;
	}

	return count;
}

static int mxt_load_fw(struct device *dev, const char *fn)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	const struct firmware *fw = NULL;
	unsigned int frame_size;
	unsigned int pos = 0;
	unsigned int retry = 0;
	int ret;

	ret = request_firmware(&fw, fn, dev);
	if (ret < 0) {
		dev_err(dev, "Unable to open firmware %s\n", fn);
		return ret;
	}

	if (data->state != BOOTLOADER) {
		/* Change to the bootloader mode */
		ret = mxt_soft_reset(data, MXT_BOOT_VALUE);
		if (ret)
			goto release_firmware;

		ret = mxt_switch_to_bootloader_address(data);
		if (ret)
			goto release_firmware;
	}

	ret = mxt_check_bootloader(client, MXT_WAITING_BOOTLOAD_CMD);
	if (ret)
	{
		/* Bootloader may still be unlocked from previous update
		 * attempt */
		ret = mxt_check_bootloader(client,
			MXT_WAITING_FRAME_DATA);

		if (ret)
			goto return_to_app_mode;
	} else {
		dev_info(dev, "Unlocking bootloader\n");

		/* Unlock bootloader */
		mxt_unlock_bootloader(client);
	}

	while (pos < fw->size) {
		ret = mxt_check_bootloader(client,
						MXT_WAITING_FRAME_DATA);
		if (ret)
			goto release_firmware;

		frame_size = ((*(fw->data + pos) << 8) | *(fw->data + pos + 1));

		/* We should add 2 at frame size as the the firmware data is not
		 * included the CRC bytes.
		 */
		frame_size += 2;

		/* Write one frame to device */
		mxt_fw_write(client, fw->data + pos, frame_size);

		ret = mxt_check_bootloader(client,
						MXT_FRAME_CRC_PASS);
		if (ret) {
			retry++;

			/* Back off by 20ms per retry */
			msleep(retry * 20);

			if (retry > 20)
				goto release_firmware;
		} else {
			retry = 0;
			pos += frame_size;
			dev_info(dev, "Updated %d/%zd bytes\n", pos, fw->size);
		}
	}

return_to_app_mode:
	mxt_switch_to_appmode_address(data);
release_firmware:
	release_firmware(fw);

	return ret;
}

static ssize_t mxt_update_fw_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int error;

	disable_irq(data->irq);

	error = mxt_load_fw(dev, MXT_FW_NAME);
	if (error) {
		dev_err(dev, "The firmware update failed(%d)\n", error);
		count = error;
	} else {
		dev_info(dev, "The firmware update succeeded\n");

		/* Wait for reset */
		msleep(MXT_FWRESET_TIME);

		data->state = INIT;
		kfree(data->object_table);
		data->object_table = NULL;

		mxt_initialize(data);
	}

	if (data->state == APPMODE) {
		enable_irq(data->irq);

		error = mxt_make_highchg(data);
		if (error)
			return error;
	}

	return count;
}

static ssize_t mxt_pause_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int count = 0;

	count += sprintf(buf + count, "%d", data->driver_paused);
	count += sprintf(buf + count, "\n");

	return count;
}

static ssize_t mxt_pause_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int i;

	if (sscanf(buf, "%u", &i) == 1 && i < 2) {
		data->driver_paused = i;

		dev_dbg(dev, "%s\n", i ? "paused" : "unpaused");
	} else
		dev_dbg(dev, "pause_driver write error\n");

	return count;
}

static ssize_t mxt_debug_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int count = 0;

	count += sprintf(buf + count, "%d", data->debug_enabled);
	count += sprintf(buf + count, "\n");

	return count;
}

static ssize_t mxt_debug_enable_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mxt_data *data = dev_get_drvdata(dev);
	int i;

	if (sscanf(buf, "%u", &i) == 1 && i < 2) {
		data->debug_enabled = i;

		dev_dbg(dev, "%s\n", i ? "debug enabled" : "debug disabled");
	} else
	dev_dbg(dev, "debug_enabled write error\n");

	return count;
}

static ssize_t mxt_mem_access_read(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret = 0;

	if (data->state != APPMODE) {
		dev_err(dev, "Not in APPMODE\n");
		return -EINVAL;
	}

	if (off >= MXT_MEMACCESS_SIZE)
		return -EIO;

	if (off + count > MXT_MEMACCESS_SIZE)
		count = MXT_MEMACCESS_SIZE - off;

	if (count > MXT_I2C_MAX_REQ_SIZE)
		count = MXT_I2C_MAX_REQ_SIZE;

	if (count > 0)
		ret = __mxt_read_reg(data->client, off, count, buf);

	return ret == 0 ? count : ret;
}

int mxt_write_block(struct i2c_client *client, u16 addr, u16 length, u8 *value)
{
	int i;
	struct {
		__le16 le_addr;
		u8  data[MXT_I2C_MAX_REQ_SIZE];
	} i2c_block_transfer;

	if (length > MXT_I2C_MAX_REQ_SIZE)
		return -EINVAL;

	for (i = 0; i < length; i++)
		i2c_block_transfer.data[i] = *value++;

		i2c_block_transfer.le_addr = cpu_to_le16(addr);

		i = i2c_master_send(client, (u8 *) &i2c_block_transfer, length + 2);

	if (i == (length + 2))
		return 0;
	else
		return -EIO;
}

static ssize_t mxt_mem_access_write(struct file *filp, struct kobject *kobj,
	struct bin_attribute *bin_attr, char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct mxt_data *data = dev_get_drvdata(dev);
	int ret = 0;

	if (data->state != APPMODE) {
		dev_err(dev, "Not in APPMODE\n");
		return -EINVAL;
	}

	if (off >= MXT_MEMACCESS_SIZE)
		return -EIO;

	if (off + count > MXT_MEMACCESS_SIZE)
		count = MXT_MEMACCESS_SIZE - off;

	if (count > MXT_I2C_MAX_REQ_SIZE)
		count = MXT_I2C_MAX_REQ_SIZE;

	if (count > 0)
		ret = mxt_write_block(data->client, off, count, buf);

	return ret == 0 ? count : 0;
}

static DEVICE_ATTR(object, 0400, mxt_object_show, NULL);
static DEVICE_ATTR(update_fw, 0600, NULL, mxt_update_fw_store);
static DEVICE_ATTR(pause_driver, 0600, mxt_pause_show, mxt_pause_store);
static DEVICE_ATTR(debug_enable, 0600, mxt_debug_enable_show, mxt_debug_enable_store);

static struct attribute *mxt_attrs[] = {
	&dev_attr_object.attr,
	&dev_attr_update_fw.attr,
	&dev_attr_pause_driver.attr,
	&dev_attr_debug_enable.attr,
	NULL
};

static const struct attribute_group mxt_attr_group = {
	.attrs = mxt_attrs,
};

static void mxt_start(struct mxt_data *data)
{
	int error;
	struct device *dev = &data->client->dev;

	if (data->is_stopped == 0)
		return;

	error = mxt_set_power_cfg(data, MXT_POWER_CFG_RUN);

	if (!error)
		dev_dbg(dev, "MXT started\n");
}

static void mxt_stop(struct mxt_data *data)
{
	int error;
	struct device *dev = &data->client->dev;

	if (data->is_stopped)
		return;

	error = mxt_set_power_cfg(data, MXT_POWER_CFG_DEEPSLEEP);

	mxt_reset_fingers(data);

	if (!error)
		dev_dbg(dev, "MXT suspended\n");
}

static int mxt_input_open(struct input_dev *dev)
{
	struct mxt_data *data = input_get_drvdata(dev);

	mxt_start(data);

	return 0;
}

static void mxt_input_close(struct input_dev *dev)
{
	struct mxt_data *data = input_get_drvdata(dev);

	mxt_stop(data);
}

//TODO: :(
static int reg_set_optimum_mode_check(struct regulator *reg, int load_uA)
{
	return (regulator_count_voltages(reg) > 0) ?
		regulator_set_optimum_mode(reg, load_uA) : 0;
}

static int mxt_power_on(struct mxt_data *data, bool on)
{
	int rc;

	if (on == false)
		goto power_off;

	gpio_set_value_cansleep(data->pdata->reset_gpio, 0);

	rc = reg_set_optimum_mode_check(data->vcc_ana, MXT_ACTIVE_LOAD_UA);
	if (rc < 0) {
		dev_err(&data->client->dev,
			"Regulator vcc_ana set_opt failed rc=%d\n", rc);
		return rc;
	}
	rc = regulator_enable(data->vcc_ana);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vcc_ana enable failed rc=%d\n", rc);
		goto error_reg_en_vcc_ana;
	}

	if (data->pdata->digital_pwr_regulator) {
		rc = reg_set_optimum_mode_check(data->vcc_dig,
					MXT_ACTIVE_LOAD_DIG_UA);
		if (rc < 0) {
			dev_err(&data->client->dev,
				"Regulator vcc_dig set_opt failed rc=%d\n",
				rc);
			goto error_reg_opt_vcc_dig;
		}

		rc = regulator_enable(data->vcc_dig);
		if (rc) {
			dev_err(&data->client->dev,
				"Regulator vcc_dig enable failed rc=%d\n", rc);
			goto error_reg_en_vcc_dig;
		}
	}

	if (data->pdata->i2c_pull_up) {
		rc = reg_set_optimum_mode_check(data->vcc_i2c, MXT_I2C_LOAD_UA);
		if (rc < 0) {
			dev_err(&data->client->dev,
				"Regulator vcc_i2c set_opt failed rc=%d\n", rc);
			goto error_reg_opt_i2c;
		}

		rc = regulator_enable(data->vcc_i2c);
		if (rc) {
			dev_err(&data->client->dev,
				"Regulator vcc_i2c enable failed rc=%d\n", rc);
			goto error_reg_en_vcc_i2c;
		}
	}
	gpio_set_value_cansleep(data->pdata->reset_gpio, 1);

	msleep(MXT336_HW_RESET_TIME);

	//TODO: Factory mode
// 	//Disable touch suppression on the factory image, to enable the full-ground test
// 	if (buckshot_factory_mode())
// 	{
// 		mxt_write_object(data, 42, 0, 0);
// 	}

	data->power_enabled = true;

	if (!mxt_buckshot_read_chg(data))
	{
		//interrupt is pending, but we haven't serviced it yet
		//manually trigger the ISR so we can get back on track
		mxt_interrupt(data->irq, (void*) data);
	}

	return 0;

error_reg_en_vcc_i2c:
	if (data->pdata->i2c_pull_up)
		reg_set_optimum_mode_check(data->vcc_i2c, 0);
error_reg_opt_i2c:
	if (data->pdata->digital_pwr_regulator)
		regulator_disable(data->vcc_dig);
error_reg_en_vcc_dig:
	if (data->pdata->digital_pwr_regulator)
		reg_set_optimum_mode_check(data->vcc_dig, 0);
error_reg_opt_vcc_dig:
	regulator_disable(data->vcc_ana);
error_reg_en_vcc_ana:
	reg_set_optimum_mode_check(data->vcc_ana, 0);
	return rc;

power_off:
	data->power_enabled = false;

	reg_set_optimum_mode_check(data->vcc_ana, 0);
	regulator_disable(data->vcc_ana);
	if (data->pdata->digital_pwr_regulator) {
		reg_set_optimum_mode_check(data->vcc_dig, 0);
		regulator_disable(data->vcc_dig);
	}
	if (data->pdata->i2c_pull_up) {
		reg_set_optimum_mode_check(data->vcc_i2c, 0);
		regulator_disable(data->vcc_i2c);
	}
	msleep(50);
	return 0;
}

static int mxt_regulator_configure(struct mxt_data *data, bool on)
{
	int rc;

	if (on == false)
		goto hw_shutdown;

	data->vcc_ana = regulator_get(&data->client->dev, "vdd_ana");
	if (IS_ERR(data->vcc_ana)) {
		rc = PTR_ERR(data->vcc_ana);
		dev_err(&data->client->dev,
			"Regulator get failed vcc_ana rc=%d\n", rc);
		return rc;
	}

	if (regulator_count_voltages(data->vcc_ana) > 0) {
		rc = regulator_set_voltage(data->vcc_ana, MXT_VTG_MIN_UV,
							MXT_VTG_MAX_UV);
		if (rc) {
			dev_err(&data->client->dev,
				"regulator set_vtg for vcc_ana failed rc=%d\n", rc);
			goto error_set_vtg_vcc_ana;
		}
		//else
		//	dev_err(&data->client->dev,
		//		"regulator set_vtg for vcc_ana succeeded rc=%d\n", rc);
	}
	if (data->pdata->digital_pwr_regulator) {
		data->vcc_dig = regulator_get(&data->client->dev, "vdd_dig");
		if (IS_ERR(data->vcc_dig)) {
			rc = PTR_ERR(data->vcc_dig);
			dev_err(&data->client->dev,
				"Regulator get dig failed rc=%d\n", rc);
			goto error_get_vtg_vcc_dig;
		}

		if (regulator_count_voltages(data->vcc_dig) > 0) {
			rc = regulator_set_voltage(data->vcc_dig,
				MXT_VTG_DIG_MIN_UV, MXT_VTG_DIG_MAX_UV);
			if (rc) {
				dev_err(&data->client->dev,
					"regulator set_vtg for vcc_dig failed rc=%d, c: %d\n", rc, regulator_count_voltages(data->vcc_dig));
				goto error_set_vtg_vcc_dig;
			}
			//else
			//	dev_err(&data->client->dev,
			//		"regulator set_vtg for vcc_dig succeeded rc=%d\n", rc);
		}
	}
	if (data->pdata->i2c_pull_up) {
		data->vcc_i2c = regulator_get(&data->client->dev, "vcc_i2c");
		if (IS_ERR(data->vcc_i2c)) {
			rc = PTR_ERR(data->vcc_i2c);
			dev_err(&data->client->dev,
				"Regulator get failed rc=%d\n",	rc);
			goto error_get_vtg_i2c;
		}
		if (regulator_count_voltages(data->vcc_i2c) > 0) {
			rc = regulator_set_voltage(data->vcc_i2c,
				MXT_I2C_VTG_MIN_UV, MXT_I2C_VTG_MAX_UV);
			if (rc) {
				dev_err(&data->client->dev,
					"regulator set_vtg for vcc_i2c failed rc=%d\n", rc);
				goto error_set_vtg_i2c;
			}
// 			else
// 				dev_err(&data->client->dev,
// 					"regulator set_vtg for vcc_i2c succeeded rc=%d\n", rc);
		}
	}

	return 0;

error_set_vtg_i2c:
	regulator_put(data->vcc_i2c);
error_get_vtg_i2c:
	if (data->pdata->digital_pwr_regulator)
		if (regulator_count_voltages(data->vcc_dig) > 0)
			regulator_set_voltage(data->vcc_dig, 0,
				MXT_VTG_DIG_MAX_UV);
error_set_vtg_vcc_dig:
	if (data->pdata->digital_pwr_regulator)
		regulator_put(data->vcc_dig);
error_get_vtg_vcc_dig:
	if (regulator_count_voltages(data->vcc_ana) > 0)
		regulator_set_voltage(data->vcc_ana, 0, MXT_VTG_MAX_UV);
error_set_vtg_vcc_ana:
	regulator_put(data->vcc_ana);
	return rc;

hw_shutdown:
	if (regulator_count_voltages(data->vcc_ana) > 0)
		regulator_set_voltage(data->vcc_ana, 0, MXT_VTG_MAX_UV);
	regulator_put(data->vcc_ana);
	if (data->pdata->digital_pwr_regulator) {
		if (regulator_count_voltages(data->vcc_dig) > 0)
			regulator_set_voltage(data->vcc_dig, 0,
						MXT_VTG_DIG_MAX_UV);
		regulator_put(data->vcc_dig);
	}
	if (data->pdata->i2c_pull_up) {
		if (regulator_count_voltages(data->vcc_i2c) > 0)
			regulator_set_voltage(data->vcc_i2c, 0,
						MXT_I2C_VTG_MAX_UV);
		regulator_put(data->vcc_i2c);
	}
	return 0;
}

#ifdef CONFIG_PM
static int mxt_regulator_lpm(struct mxt_data *data, bool on)
{
	mxt_power_on(data, !on);

	return 0;
}
#endif

#ifdef CONFIG_OF
static int mxt_parse_dt(struct device *dev, struct mxt_platform_data_buckshot *pdata)
{
	//int rc;
	//struct mxt_config_info *info;
	struct device_node *np = dev->of_node;
	//struct property *prop;
	int config_length;

	/* reset, irq gpio info */
	pdata->reset_gpio = of_get_named_gpio_flags(np, "atmel,reset-gpio",
				0, &pdata->reset_gpio_flags);
	pdata->irq_gpio = of_get_named_gpio_flags(np, "atmel,irq-gpio",
				0, &pdata->irq_gpio_flags);

	pdata->config = of_get_property(np, "atmel,config", &config_length);
	if (!pdata->config) {
		dev_err(dev, "Looking up atmel,config property in node %s failed, length %i",
			np->full_name, config_length);
		return -ENODEV;
//		dev_err(dev, "WARNING: Continuing anyway, without any configuration data\n");
// 		pdata->config = 0;
// 		pdata->config_length = 0;
	} else if (!config_length) {
		dev_err(dev, "Invalid length of configuration data (%i)\n", config_length);
		return -EINVAL;
// 		dev_err(dev, "WARNING: Continuing anyway, without any configuration data\n");
// 		pdata->config = 0;
// 		pdata->config_length = 0;
	} else {
		//Set the pointer that was used by the platform data struct when we
		// were still using board files to point to the new device tree
		// config data
		//pdata->config = prop->value;
		pdata->config_length = config_length;
		dev_err(dev, "Looking up atmel,config property in node %s succeeded, length %i",
			np->full_name, config_length);
	}

	/* regulator info */
	pdata->i2c_pull_up = of_property_read_bool(np, "atmel,i2c-pull-up");
	pdata->digital_pwr_regulator = of_property_read_bool(np,
						"atmel,dig-reg-support");

	return 0;
}
#else
static int mxt_parse_dt(struct device *dev, struct mxt_platform_data_buckshot *pdata)
{
	return -ENODEV;
}
#endif

static int __devinit mxt_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct mxt_platform_data_buckshot *pdata;// = client->dev.platform_data;
	struct mxt_data *data;
	struct input_dev *input_dev;
	int error;
	int key;

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct mxt_platform_data_buckshot), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		error = mxt_parse_dt(&client->dev, pdata);
		if (error)
			return error;
	} else
		pdata = client->dev.platform_data;

	if (!pdata)
		return -EINVAL;

	data = kzalloc(sizeof(struct mxt_data), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!data || !input_dev) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	data->state = INIT;
	data->power_enabled = false;
	data->client = client;
	data->input_dev = input_dev;
	data->pdata = pdata;
	data->irq = client->irq;

	if (gpio_is_valid(pdata->irq_gpio)) {
		/* configure touchscreen irq gpio */
		error = gpio_request(pdata->irq_gpio, "mxt_irq_gpio");
		if (error) {
			dev_err(&client->dev, "unable to request gpio [%d]\n",
				pdata->irq_gpio);
			goto err_free_mem;
		}
		error = gpio_direction_input(pdata->irq_gpio);
		if (error) {
			dev_err(&client->dev,
				"unable to set_direction for gpio [%d]\n",
				pdata->irq_gpio);
			goto err_irq_gpio_req;
		}
	}

	if (gpio_is_valid(pdata->reset_gpio)) {
		/* configure touchscreen reset out gpio */
		error = gpio_request(pdata->reset_gpio, "mxt_reset_gpio");
		if (error) {
			dev_err(&client->dev,
				"unable to request reset gpio %d\n",
				pdata->reset_gpio);
			goto err_irq_gpio_req;
		}

		error = gpio_direction_output(pdata->reset_gpio, 0);
		if (error) {
			dev_err(&client->dev,
				"unable to set direction for gpio %d\n",
				pdata->reset_gpio);
			goto err_reset_gpio_req;
		}
	}

	error = mxt_regulator_configure(data, true);
	if (error) {
		dev_err(&client->dev, "Failed in mxt_regulator_configure\n");
		goto err_reset_gpio_req;
	}

	error = mxt_power_on(data, true);
	if (error) {
		dev_err(&client->dev, "Failed to power on hardware\n");
		goto err_regulator_on;
	}

	/* Initialize i2c device */
	error = mxt_initialize(data);
	if (error)
		goto err_free_object;

	/* Initialize input device */
	input_dev->name = "Atmel maXTouch Touchscreen";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->open = mxt_input_open;
	input_dev->close = mxt_input_close;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);

	for (key = 0; key < T15_NUM_KEYS; key++)
	{
		input_set_capability(input_dev, EV_KEY, t15_keycodes[key]);
		t15_keystatus[key] = T15_RELEASE;
	}

	/* For single touch */
	input_set_abs_params(input_dev, ABS_X,
			     0, data->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_Y,
			     0, data->max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE,
			     0, 255, 0, 0);

	/* For multi touch */
	input_mt_init_slots(input_dev, MXT_MAX_FINGER);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			     0, MXT_MAX_AREA, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
			     0, data->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
			     0, data->max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE,
			     0, 255, 0, 0);

	input_set_drvdata(input_dev, data);
	i2c_set_clientdata(client, data);

	error = request_threaded_irq(client->irq, NULL, mxt_interrupt,
			pdata->irqflags, client->dev.driver->name, data);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		goto err_free_object;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	data->early_suspend.suspend = mxt_early_suspend;
	data->early_suspend.resume = mxt_late_resume;
	register_early_suspend(&data->early_suspend);
	data->suspend_state = 0;
#endif

	if (data->state == APPMODE) {
		error = mxt_make_highchg(data);
		if (error) {
			dev_err(&client->dev, "Failed to make high CHG\n");
			goto err_free_irq;
		}
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&client->dev, "Failed to register input device\n");
		goto err_free_irq;
	}

	error = sysfs_create_group(&client->dev.kobj, &mxt_attr_group);
	if (error) {
		dev_err(&client->dev, "Failed to create sysfs group\n");
		goto err_unregister_device;
	}

	sysfs_bin_attr_init(&data->mem_access_attr);
	data->mem_access_attr.attr.name = "mem_access";
	data->mem_access_attr.attr.mode = S_IRUSR | S_IWUSR;
	data->mem_access_attr.read = mxt_mem_access_read;
	data->mem_access_attr.write = mxt_mem_access_write;
	data->mem_access_attr.size = MXT_MEMACCESS_SIZE;

	if (sysfs_create_bin_file(&client->dev.kobj, &data->mem_access_attr) < 0) {
		dev_err(&client->dev, "Failed to create %s\n", data->mem_access_attr.attr.name);
		goto err_remove_sysfs_group;
	}

	return 0;

err_remove_sysfs_group:
        sysfs_remove_group(&client->dev.kobj, &mxt_attr_group);
err_unregister_device:
	input_unregister_device(input_dev);
	input_dev = NULL;
err_free_irq:
	free_irq(client->irq, data);
err_free_object:
	kfree(data->object_table);
err_regulator_on:
	mxt_regulator_configure(data, false);
err_reset_gpio_req:
	if (gpio_is_valid(pdata->reset_gpio))
		gpio_free(pdata->reset_gpio);
err_irq_gpio_req:
	if (gpio_is_valid(pdata->irq_gpio))
		gpio_free(pdata->irq_gpio);
err_free_mem:
	input_free_device(input_dev);
	kfree(data);
	return error;
}

static int __devexit mxt_remove(struct i2c_client *client)
{
	struct mxt_data *data = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &mxt_attr_group);
	free_irq(data->irq, data);
	input_unregister_device(data->input_dev);
#if defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&data->early_suspend);
#endif

	kfree(data->object_table);
	kfree(data);

	return 0;
}

#ifdef CONFIG_PM
static int mxt_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mxt_data *data = i2c_get_clientdata(client);
	struct input_dev *input_dev = data->input_dev;
	int error=0;

	//Already shut down during an early_suspend call
	if (data->power_enabled == false)
		return 0;

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		mxt_stop(data);

	mutex_unlock(&input_dev->mutex);

	/* put regulators in low power mode */
	error = mxt_regulator_lpm(data, true);
	if (error < 0) {
		dev_err(dev, "failed to enter low power mode\n");
		return error;
	}

	return 0;
}

static int mxt_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mxt_data *data = i2c_get_clientdata(client);
	struct input_dev *input_dev = data->input_dev;
	int error;

	/* put regulators in high power mode */
	error = mxt_regulator_lpm(data, false);
	if (error < 0) {
		dev_err(dev, "failed to enter high power mode\n");
		return error;
	}

	/* Soft reset */
	//(This is unnecessary now that the device is being power-cycled
	//mxt_soft_reset(data, MXT_RESET_VALUE);

	mutex_lock(&input_dev->mutex);

	if (input_dev->users)
		mxt_start(data);

	mutex_unlock(&input_dev->mutex);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void mxt_early_suspend(struct early_suspend *es)
{
	struct mxt_data *mxt;
	mxt = container_of(es, struct mxt_data, early_suspend);

	if (mxt_suspend(&mxt->client->dev) != 0)
		dev_err(&mxt->client->dev, "%s: failed\n", __func__);
}

static void mxt_late_resume(struct early_suspend *es)
{
	struct mxt_data *mxt;
	mxt = container_of(es, struct mxt_data, early_suspend);

	if (mxt_resume(&mxt->client->dev) != 0)
		dev_err(&mxt->client->dev, "%s: failed\n", __func__);
}
#endif

static const struct dev_pm_ops mxt_pm_ops = {
	.suspend	= mxt_suspend,
	.resume		= mxt_resume,
};
#endif

static const struct i2c_device_id mxt_id[] = {
	{ "qt602240_ts", 0 },
	{ "atmel_mxt_ts", 0 },
	{ "mXT224", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mxt_id);
#ifdef CONFIG_OF
static struct of_device_id mxt_match_table[] = {
	{ .compatible = "atmel,mxt-ts",},
	{ },
};
#else
#define mxt_match_table NULL
#endif

static struct i2c_driver mxt_driver = {
	.driver = {
		.name	= "atmel_mxt_ts",
		.owner	= THIS_MODULE,
		.of_match_table = mxt_match_table,
#ifdef CONFIG_PM
		.pm	= &mxt_pm_ops,
#endif
	},
	.probe		= mxt_probe,
	.remove		= __devexit_p(mxt_remove),
	.id_table	= mxt_id,
};

module_i2c_driver(mxt_driver);

/* Module information */
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_DESCRIPTION("Atmel maXTouch Touchscreen driver");
MODULE_LICENSE("GPL");
