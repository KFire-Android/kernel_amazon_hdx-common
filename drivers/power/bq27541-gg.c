/* Copyright (C) 2012 Jeff Loucks <loucks@amazon.com>

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/**
 * The gas gauge is on an I2C bus. It does not have an interrupt line, so it is
 * polled. The default polling period is 3 seconds, while awake/charging, and 15
 * minutes, while suspended.
 *
 * Direct gas gauge access is managed by a mutex, which must be acquired and
 * released. Generally, only the polling thread directly accesses the gas gauge.
 *
 * Gas gauge data, such as SOC, are read at the polling rate, and stored in a
 * cache. The cache access is managed by a mutex, which must be acquired and
 * released. Most access to gas gauge data is via the cache, not directly from
 * the device.
 *
 * The data is evaluated after each poll, and actions are taken, as necessary.
 * For example, if certain data changes, a power supply event is triggered. If
 * certain data exceed limits, the platform may be shut down.
 */

/**
 * CONFIG_BQ27541_FLASHSTREAM enables Flashstream protocol for BQ27541 updates.
 */
#define CONFIG_BQ27541_FLASHSTREAM

/**
 * Work arounds for QC bustedness.
 */
#define CONFIG_BQ27541_NO_GG_ACTIVITY_ON_SUSPEND
#define CONFIG_BQ27541_NO_POWER_EVENTS_ON_SUSPEND

/**
 * These enable legacy bug patches from the BQ27520, which had broken silicon.
 * The patches have not been required for the BQ27541, but remain, just in case.
 */
#undef CONFIG_BQ27541_400KHZ_COMPATIBLE
#undef CONFIG_BQ27541_SINGLE_BYTE_WRITES
#undef CONFIG_BQ27541_MAC_COMMAND_FIX

/**
 * Maintain some statistics, such as power_supply update frequency.
 */
#define CONFIG_BQ27541_STATISTICS

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <asm/unaligned.h>
#include <linux/time.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/byteorder/generic.h>
#include <asm/system_info.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <mach/board.h> //
#include <mach/socinfo.h> //
#include <mach/board-detect.h> //
#include <linux/qpnp/qpnp-adc.h>
#include <linux/spmi.h>
#include <linux/reboot.h>
#include <linux/syscalls.h>
#include <linux/rtc.h>
#if defined(CONFIG_AMAZON_METRICS_LOG)
#include <linux/metricslog.h>
#endif

/* BQ27541 data commands */
#define BQ27541_REG_CNTL		0x00 //USED
#define BQ27541_REG_AR			0x02
#define BQ27541_REG_UFSOC		0x04
#define BQ27541_REG_TEMP		0x06 //USED
#define BQ27541_REG_VOLT		0x08 //USED
#define BQ27541_REG_FLAGS		0x0A //USED
#define BQ27541_REG_NAC			0x0C //USED
#define BQ27541_REG_FAC			0x0E
#define BQ27541_REG_RM			0x10 //USED
#define BQ27541_REG_FCC			0x12 //USED
#define BQ27541_REG_AI			0x14 //USED
#define BQ27541_REG_TTE			0x16
#define BQ27541_REG_FFCC		0x18
#define BQ27541_REG_SI			0x1A
#define BQ27541_REG_UFFCC		0x1C
#define BQ27541_REG_MLI			0x1E //USED
#define BQ27541_REG_UFRM		0x20
#define BQ27541_REG_FRM			0x22
#define BQ27541_REG_AP			0x24
#define BQ27541_REG_INTTEMP		0x28
#define BQ27541_REG_CC			0x2A //USED
#define BQ27541_REG_SOC			0x2C //USED
#define BQ27541_REG_SOH			0x2E //USED
#define BQ27541_REG_PCHG		0x34
#define BQ27541_REG_DOD0		0x36
#define BQ27541_REG_SDSG		0x38

/* Extended commands */
#define BQ27541_REG_PCR			0x3A //USED
#define BQ27541_REG_DCAP		0x3C //USED
#define BQ27541_REG_DFCLS		0x3E //USED
#define BQ27541_REG_DFBLK		0x3F //USED
#define BQ27541_REG_DFD			0x40 // 0x40..0x5f //USED
#define BQ27541_REG_ADF			0x40 // 0x40..0x53
#define BQ27541_REG_ACKS		0x54
#define BQ27541_REG_DFDCKS		0x60 //USED
#define BQ27541_REG_DFDCNTL		0x61 //USED
#define BQ27541_REG_DNAMELEN		0x62
#define BQ27541_REG_DNAME		0x63 // 0x63..0x6C

/* Control status bits */
#define BQ27541_CS_QEN			BIT(0)
#define BQ27541_CS_VOK			BIT(1)
#define BQ27541_CS_RUP_DIS		BIT(2)
#define BQ27541_CS_LDMD			BIT(3)
#define BQ27541_CS_SLEEP		BIT(4)
#define BQ27541_CS_FULLSLEEP		BIT(5)
#define BQ27541_CS_HIBERNATE		BIT(6)
#define BQ27541_CS_SHUTDOWN		BIT(7)
#define BQ27541_CS_HDQHOSTIN		BIT(8)
#define BQ27541_CS_RSVD9		BIT(9)
#define BQ27541_CS_BCA			BIT(10)
#define BQ27541_CS_CCA			BIT(11)
#define BQ27541_CS_CALMODE		BIT(12)
#define BQ27541_CS_SS			BIT(13)
#define BQ27541_CS_FAS			BIT(14)
#define BQ27541_CS_SE			BIT(15)

/* Flag bits */
#define BQ27541_FLAG_DSG		BIT(0)
#define BQ27541_FLAG_SOCF		BIT(1)
#define BQ27541_FLAG_SOC1		BIT(2)
#define BQ27541_FLAG_HW0		BIT(3)
#define BQ27541_FLAG_HW1		BIT(4)
#define BQ27541_FLAG_TDD		BIT(5)
#define BQ27541_FLAG_ISD		BIT(6)
#define BQ27541_FLAG_OCVTAKEN		BIT(7)
#define BQ27541_FLAG_CHG		BIT(8)
#define BQ27541_FLAG_FC			BIT(9)
#define BQ27541_FLAG_RSVD10		BIT(10)
#define BQ27541_FLAG_CHG_INH		BIT(11)
#define BQ27541_FLAG_BATLOW		BIT(12)
#define BQ27541_FLAG_BATHI		BIT(13)
#define BQ27541_FLAG_OTD		BIT(14)
#define BQ27541_FLAG_OTC		BIT(15)

/* Control subcommands */
#define BQ27541_SUBCMD_CNTL_STATUS		0x0000 //USED
#define BQ27541_SUBCMD_DEVICE_TYPE		0x0001 //USED
#define BQ27541_SUBCMD_FW_VERSION		0x0002 //USED
#define BQ27541_SUBCMD_HW_VERSION		0x0003 //USED
#define BQ27541_SUBCMD_RESET_DATA		0x0005
#define BQ27541_SUBCMD_PREV_MACWRITE		0x0007
#define BQ27541_SUBCMD_CHEM_ID			0x0008 //USED
#define BQ27541_SUBCMD_BOARD_OFFSET		0x0009
#define BQ27541_SUBCMD_CC_OFFSET		0x000A
#define BQ27541_SUBCMD_CC_OFFSET_SAVE		0x000B
#define BQ27541_SUBCMD_DF_VERSION		0x000C //USED
#define BQ27541_SUBCMD_SET_FULLSLEEP		0x0010
#define BQ27541_SUBCMD_SET_HIBERNATE		0x0011
#define BQ27541_SUBCMD_CLEAR_HIBERNATE		0x0012
#define BQ27541_SUBCMD_SET_SHUTDOWN		0x0013
#define BQ27541_SUBCMD_CLEAR_SHUTDOWN		0x0014
#define BQ27541_SUBCMD_SET_HDQINTEN		0x0015
#define BQ27541_SUBCMD_CLEAR_HDQINTEN		0x0016
#define BQ27541_SUBCMD_STATIC_CHEM_CHKSUM	0x0017
#define BQ27541_SUBCMD_SEALED			0x0020 //USED
#define BQ27541_SUBCMD_IT_ENABLE		0x0021
#define BQ27541_SUBCMD_CAL_ENABLE		0x002D
#define BQ27541_SUBCMD_RESET			0x0041
#define BQ27541_SUBCMD_EXIT_CAL			0x0080
#define BQ27541_SUBCMD_ENTER_CAL		0x0081
#define BQ27541_SUBCMD_OFFSET_CAL		0x0082

/* data flash subclass IDs */
#define BQ27541_DFD_CLASS_MIB		58
/* data flash data block size */
#define BQ27541_REG_DFD_SIZE		32

// some device configuration constants
#define BQ27541_I2C_ADDRESS		0x55	// 7 bit version of 0xAA/0xAB
#define BQ27541_DEVICE_TYPE		0x0541	// BQ27541_SUBCMD_DEVICE_TYPE
#define BQ27541_FW_VERSION		0x0224	// BQ27541_SUBCMD_FW_VERSION
#define BQ27541_HW_VERSION		0x0060	// BQ27541_SUBCMD_HW_VERSION

// current to reliably indicate charging/discharging
#define BQ27541_AI_CHARGE_THRESHOLD	30	// milliamps (WAG)
#define BQ27541_AI_DISCHARGE_THRESHOLD	2	// milliamps (WAG)

#define BQ27541_DEFAULT_MAX_MV		4350	// millivolts
#define BQ27541_DEFAULT_TAPER_MA	80	// milliamps
#define BQ27541_DEFAULT_FCC_MAH		2360	// milliamp hours
#define BQ27541_DEFAULT_CHARGE_RATE	7	// tenths C
#define BQ27541_DEFAULT_SHUTOFF_MV	3400	// millivolts
#define BQ27541_DEFAULT_ALT_SHUTOFF_MV	3200	// millivolts
#define BQ27541_DEFAULT_SHUTOFF_COUNT	3	// samples before 0% SOC
#define BQ27541_DEFAULT_DISCHARGE_RATE	18	// tenths C
#define BQ27541_DEFAULT_MAX_TEMP_DC	600	// tenths C
#define BQ27541_DEFAULT_MIN_TEMP_DC	0	// tenths C

#define BQ27541_MIN_MV_LIMIT		3000	// sanity check
#define BQ27541_MAX_MV_LIMIT		4800	// sanity check
#define BQ27541_TAPER_MA_LIMIT		350	// sanity check
#define BQ27541_DCAP_MAH_MIN_LIMIT	800	// sanity check
#define BQ27541_DCAP_MAH_MAX_LIMIT	4000	// sanity check
#define BQ27541_CHARGE_RATE_MIN_LIMIT	1	// sanity check
#define BQ27541_CHARGE_RATE_MAX_LIMIT	50	// sanity check

#define BQ27541_WARM_CHG_RESUME_TEMP	440	// decidegC
#define BQ27541_WARM_CHG_RESUME_SOC	3	// delta
#define BQ27541_WARM_CHG_DONE_TEMP	450	// decidegC
#define BQ27541_WARM_CHG_DONE_VOLT	4040	// millivolts
#define BQ27541_WARM_CHG_DONE_CURRENT	100	// milliamps
#define BQ27541_WARM_CHG_DONE_COUNT	3	// smooth over three samples

#define BQ27541_OFFSET_DEG_K_TO_DEG_C	(-2732)	// offset degK to degC
#define DK_TO_DC(dK) (dK + BQ27541_OFFSET_DEG_K_TO_DEG_C) // conversion macro
#define BQ27541_ROOM_TEMP_DC		230	// room temp in decidegC

#define BQ27541_STATUS_WAKE_SECONDS	(3)		// every 3 seconds
#define BQ27541_STATUS_SLEEP_SECONDS	(60*15)		// every 15 real minutes
#define BQ27541_SNAPSHOT_SECONDS	(60*5)		// every 5 minutes

#define BQ27541_SCREEN_ON_DELAY		5		// 5 status cycles (15s)

#define BQ27541_DEFAULT_I2C_MAX_RETRIES	1		// then fail i2c
#define BQ27541_DEFAULT_I2C_HEX_DUMP	0		// enable i2c hex dump?
#define BQ27541_I2C_INTER_CMD_UDELAY	66		// (us) SLUSAP0, sec 7.3
#define BQ27541_I2C_MAC_CMD_UDELAY	100		// (us) fix sub_cmd read
#define BQ27541_I2C_RETRY_MSLEEP	150		// (ms)
#define BQ27541_I2C_RECOVERY_MSLEEP	100		// (ms)
#define BQ27541_I2C_EXEC_FMWR_MSLEEP	4000		// (ms)
#define BQ27541_I2C_MAX_PING_RETRIES	0		// then fail ping

#define BQ27541_ROM_MODE_ADDRESS	0x16		// while in ROM mode

enum {
	BQ27541_DELTA_MIB_e,
	BQ27541_DELTA_INFO_e,
	BQ27541_DELTA_CNTL_STATUS_e,
	BQ27541_DELTA_TEMP_e,
	BQ27541_DELTA_VOLT_e,
	BQ27541_DELTA_FLAGS_e,
	BQ27541_DELTA_NAC_e,
	BQ27541_DELTA_RM_e,
	BQ27541_DELTA_FCC_e,
	BQ27541_DELTA_AI_e,
	BQ27541_DELTA_CC_e,
	BQ27541_DELTA_SOC_e,
	BQ27541_DELTA_VSOC_e,
	BQ27541_DELTA_SOH_e,
	BQ27541_DELTA_STATUS_e,
	BQ27541_DELTA_I2C_ERR_e,
	BQ27541_DELTA_BAT_PRES_e,
	BQ27541_DELTA_HEALTH_e,
	BQ27541_DELTA_MLI_e,
	BQ27541_DELTA_FYI_e,
	BQ27541_DELTA_e_MAX,
};
#define BQ27541_DELTA_e_MASK (0xFFFFFFFF >> (32 - BQ27541_DELTA_e_MAX))

#if defined(CONFIG_BQ27541_STATISTICS)
enum {
	BQ27541_STATS_TOTAL_PSY_e,
	BQ27541_STATS_MIB_e,
	BQ27541_STATS_INFO_e,
	BQ27541_STATS_TEMP_e,
	BQ27541_STATS_SOC_e,
	BQ27541_STATS_VSOC_e,
	BQ27541_STATS_SOH_e,
	BQ27541_STATS_STATUS_e,
	BQ27541_STATS_I2C_ERR_e,
	BQ27541_STATS_BAT_PRES_e,
	BQ27541_STATS_HEALTH_e,
	BQ27541_STATS_FYI_e,
	BQ27541_STATS_e_MAX,
};
#define BQ27541_STATS_e_MASK (0xFFFFFFFF >> (32 - BQ27541_STATS_e_MAX))
#endif

/**
 * Bits assigned in status.status_delta to indicate a specific driver status or
 * chip status bit has recently changed.
 */
#define BQ27541_DELTA_MIB		(1<<BQ27541_DELTA_MIB_e)
#define BQ27541_DELTA_INFO		(1<<BQ27541_DELTA_INFO_e)
#define BQ27541_DELTA_CNTL_STATUS	(1<<BQ27541_DELTA_CNTL_STATUS_e)
#define BQ27541_DELTA_TEMP		(1<<BQ27541_DELTA_TEMP_e)
#define BQ27541_DELTA_VOLT		(1<<BQ27541_DELTA_VOLT_e)
#define BQ27541_DELTA_FLAGS		(1<<BQ27541_DELTA_FLAGS_e)
#define BQ27541_DELTA_NAC		(1<<BQ27541_DELTA_NAC_e)
#define BQ27541_DELTA_RM		(1<<BQ27541_DELTA_RM_e)
#define BQ27541_DELTA_FCC		(1<<BQ27541_DELTA_FCC_e)
#define BQ27541_DELTA_AI		(1<<BQ27541_DELTA_AI_e)
#define BQ27541_DELTA_CC		(1<<BQ27541_DELTA_CC_e)
#define BQ27541_DELTA_SOC		(1<<BQ27541_DELTA_SOC_e)
#define BQ27541_DELTA_VSOC		(1<<BQ27541_DELTA_VSOC_e)
#define BQ27541_DELTA_SOH		(1<<BQ27541_DELTA_SOH_e)
#define BQ27541_DELTA_STATUS		(1<<BQ27541_DELTA_STATUS_e)
#define BQ27541_DELTA_I2C_ERR		(1<<BQ27541_DELTA_I2C_ERR_e)
#define BQ27541_DELTA_BAT_PRES		(1<<BQ27541_DELTA_BAT_PRES_e)
#define BQ27541_DELTA_HEALTH		(1<<BQ27541_DELTA_HEALTH_e)
#define BQ27541_DELTA_MLI		(1<<BQ27541_DELTA_MLI_e)
#define BQ27541_DELTA_FYI		(1<<BQ27541_DELTA_FYI_e)

#define BQ27541_GG_DEV_NAME		"bq27541-gg"
#define BQ27541_GG_FULL_DEV_NAME	"ursa," BQ27541_GG_DEV_NAME

enum bq27541_boot_mode {
	BOOT_MODE_RETAIL = 1,
	BOOT_MODE_FACTORY = 2,
};

enum bq27541_battery_pack_id {
	BATTERY_PACK_ID_MISSING = -1,
	BATTERY_PACK_ID_UNKNOWN = 0,
	BATTERY_PACK_ID_1 = 1,
	BATTERY_PACK_ID_2 = 2,
	BATTERY_PACK_ID_3 = 3,
	BATTERY_PACK_ID_4 = 4,
	BATTERY_PACK_ID_5 = 5,
	BATTERY_PACK_ID_6 = 6,
	BATTERY_PACK_ID_7 = 7,
	BATTERY_PACK_ID_8 = 8,
	BATTERY_PACK_ID_9 = 9,
	BATTERY_PACK_ID_BIF_SMART = 10,
	BATTERY_PACK_ID_ADAPTER = 11,
	BATTERY_PACK_ID_SAMSUNG_MCNAIR = BATTERY_PACK_ID_1,
	BATTERY_PACK_ID_ATL_DESAY = BATTERY_PACK_ID_2,
	BATTERY_PACK_ID_COSLIGHT_SUNWODA = BATTERY_PACK_ID_3,
	BATTERY_PACK_ID_LGC_DESAY = BATTERY_PACK_ID_4,
};

static enum power_supply_property bq27541_bms_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_MAX,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
//	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
//	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
};

enum {
	BATT_STATUS_TEMPERATURE,
	BATT_STATUS_VOLTAGE,
	BATT_STATUS_CURRENT,
	BATT_STATUS_FLAGS,
	BATT_STATUS_NAC,
	BATT_STATUS_RM,
	BATT_STATUS_FCC,
	BATT_STATUS_CAPACITY,
	BATT_STATUS_SOH,
	BATT_STATUS_STATUS,
	BATT_STATUS_HEALTH,
	BATT_STATUS_CNTL,
	BATT_STATUS_CC,
	BATT_STATUS_MLI,
};

/**
 * struct bq27541_chip - device information
 * @dev:			this device pointer
 * @client:			this i2c client pointer
 * @batt_psy:			battery power supply
 * @bms_psy:			bms power supply to reference
 * @this_bms_psy:		local bms power supply
 * @suspended:			boolean - driver has been suspended
 * @bootmode:			HLOS bootmode
 * @suspend_time:		wall time at full suspend
 * @suspend_soc:		soc at full suspend
 * @rtc:			RTC device pointer
 * @access.mutex:		mutex guarding chip access
 * @access.errors:		count of chip errors during access
 * @access.errno:		most recent chip errno during access
 * @mitigation.i2c_mac_cmd_udelay: I2C protocol delay for MAC fix
 * @mitigation.i2c_max_retries:	maximum I2C protocol retries
 * @mitigation.i2c_protocol_errors: running I2C protocol errors
 * @mitigation.i2c_inter_cmd_udelay: I2C protocol delay for 400Khz fix
 * @mitigation.i2c_hex_dump:	boolean - enable I2C protocol trace dump
 * @mitigation.suppress_errors:	boolean - suppress I2C protocol error logging
 * @recovery.lock:		spinlock guarding recovery.in_progress
 * @recovery.in_progress:	boolean - recovery in progress
 * @recovery.not_okay:		device is not okay - recovery not successful
 * @recovery.next_ping		next time for recovery ping attempt
 * @status.work:		delayed work context for chip status work
 * @status.count:		count of chip status work executions
 * @status.sleep_real_seconds:	status work period while asleep (wall)
 * @status.wake_seconds:	status work period while awake
 * @status.status_delta:	bit field indicates a driver status has changed
 * @status.deferred_status_delta: bit field of driver status to force next update
 * @status.use_voltage_soc:	boolean - compute SOC from vBat
 * @status.rtc_wakeup_set:	RTC alarm has been set for wake up
 * @status.battery_present:	boolean - battery id okay and responds on I2C
 * @status.watch_status_work:	boolean - report status work results in log
 * @status.pmic_snapshot:	boolean - report PMIC registers in log
 * @status.full_count:		count of reporting full
 * @status.zero_soc_count:	count of vbat < vmin, until 0% SOC
 * @status.cache.mutex:		mutex guarding cache access
 * @status.cache.cntl_status:	most recent chip CONTROL STATUS
 * @status.cache.status:	most recent chip STATUS
 * @status.cache.temp:		most recent chip TEMP
 * @status.cache.volt:		most recent chip VOLT
 * @status.cache.flags:		most recent chip FLAGS
 * @status.cache.flags_delta:	flag bits changed since previous cache
 * @status.cache.nac:		most recent chip NAC
 * @status.cache.rm:		most recent chip RM
 * @status.cache.fcc:		most recent chip FCC
 * @status.cache.ai:		most recent chip AI (negated for compatibility)
 * @status.cache.cc:		most recent chip cycle count
 * @status.cache.soc:		most recent chip SOC
 * @status.cache.vsoc:		most recent computed voltage based SOC
 * @status.cache.soh:		most recent chip control status
 * @status.cache.health:	most recent health determination
 * @status.cache.mli:		most recent maximum load current
 * @status.warm_charge.done_count: count done detect samples
 * @status.warm_charge.done:	warm charge done flag
 * @status.warm_charge.resume_soc: warm charge resume SOC
 * @status.info.is_loaded:	boolean - device info has been loaded
 * @status.info.no_match:	boolean - device does not match driver
 * @status.info.dev_type:	retrieved device type
 * @status.info.fw_ver:		retrieved firmware version
 * @status.info.hw_ver:		retrieved hardware version
 * @status.info.chem_id:	retrieved chemistry id
 * @status.info.df_ver:		retrieved data flash version
 * @status.info.pack_config:	retrieved chip operation configuration
 * @status.info.design_cap:	retrieved design FCC
 * @status.info.battery_id:	battery id ordinal from battery id resistor
 * @status.info.shutoff_mv:	retrieved/assigned shutoff voltage
 * @status.info.alt_shutoff_mv:	shutoff voltage with adapter (alt setup)
 * @status.info.battery_man:	battery manufacturer
 * @status.info.battery_mod:	synthesised battery model (see code)
 * @status.mib.is_loaded:	boolean - mib data has been loaded
 * @status.mib.capacity_mah:	manufacturer specified charge capacity
 * @status.mib.charge_mv:	manufacturer specified charge voltage
 * @status.mib.charge_rate:	manufacturer specified charge rate
 * @status.mib.taper_ma:	manufacturer specified charge taper current
 * @status.mib.pack_serial_number: manufacturer specified pack serial
 * @status.mib.pcm_serial_number: manufacturer specified protection board serial
 * @status.limits.temp_hi_dk:	overides GG OTC, OTD, and CHG_INH high
 * @status.limits.temp_lo_dk:	overides GG CHG_INH low
 * @status.limits.volt_hi_mv:	overides GG BATHI
 * @status.limits.volt_lo_mv:	overides GG BATLOW
 * @status.limits.temp_hi_shutdown_dk: temp to immediate shutdown
 * @status.limits.volt_lo_shutdown_mv: voltage to orderly shutdowm
 * @status.snapshot.next:	wall seconds for next snapshot
 * @status.snapshot.seconds:	snapshot period
 * @charge_monitor.charging:	current charging state
 * @charge_monitor.start_time:	start wall time
 * @charge_monitor.start_soc:	start SOC
 * @charge_monitor.ai_sum:	sum of ai for average
 * @charge_monitor.ai_count:	count of ai samples for average
 * @charge_monitor.ai_max:	maximum ai
 * @charge_monitor.skip_count:	count of unreported episodes
 * @screen_monitor.on:		previous screen state
 * @screen_monitor.soc:		previous SOC
 * @screen_monitor.time:	previous wall time
 * @screen_monitor.temp_soc:	temporary new SOC
 * @screen_monitor.temp_time:	temporary new wall time
 * @screen_monitor.spurious:	count of spurious triggers
 * @screen_monitor.delay:	delay counter
 * @procfs.class:		data flash class sector ordinal
 * @procfs.offset:		data flash offset within class sector
 * @procfs.length:		data flash i/o length
 * @procfs.local:		data flash command (local copy)
 * @procfs.buffer:		data flash command data buffer
 * @flashstream.registered:	boolean - device successfully registered
 * @flashstream.acquired:	boolean - busy has been acquired
 * @flashstream.writing:	boolean - still writing
 * @flashstream.written:	boolean - chip written, so update device context
 * @flashstream.failed:		boolean - process failed at some point
 * @flashstream.prev_failed:	boolean - previous failed status
 * @flashstream.prev_sequence:	previous command sequence
 * @flashstream.sequence:	latest command sequence
 * @flashstream.offset:		rx buffer offset while parsing
 * @flashstream.buffer:		buffers one complete command
 * @stats.start_time:		start time in wall seconds
 * @stats.count:		array of stat counters
 */
struct bq27541_chip {
	struct device			*dev;
	struct qpnp_vadc_chip		*vadc_dev;
	struct qpnp_iadc_chip		*iadc_dev;
	struct i2c_client		*client;
	struct power_supply		*batt_psy;
	struct power_supply		*bms_psy;
	struct power_supply		this_bms_psy;
	int				suspended;
	int 				bootmode;
	unsigned long			suspend_time;
	int				suspend_soc;
	struct rtc_device		*rtc;

	struct {
		struct mutex		mutex;
		int			errors;
		int			errno;
	} access;

	struct {
		int			i2c_mac_cmd_udelay;
		int			i2c_max_retries;
		int			i2c_protocol_errors;
		int			i2c_inter_cmd_udelay;
		int			i2c_hex_dump;
		int			suppress_errors;
	} mitigation;

	struct {
		spinlock_t		lock;
		int			in_progress;
		int			not_okay;
		int			next_ping;
	} recovery;

	struct {
		struct delayed_work	work;
		unsigned long		count;
		int			sleep_real_seconds;
		int			wake_seconds;
		int			status_delta;
		int			deferred_status_delta;
		int			use_voltage_soc;
		int			rtc_wakeup_set;
		int			battery_present;
		int			watch_status_work;
		int			pmic_snapshot;
		int			full_count;
		int			zero_soc_count;
		struct {
			struct mutex	mutex;
			int		cntl_status;
			int		status;
			int		temp;
			int		volt;
			int		flags;
			int		flags_delta;
			int		nac;
			int		rm;
			int		fcc;
			int		ai;
			int		cc;
			int		soc;
			int		vsoc;
			int		soh;
			int		health;
			int		mli;
		} cache;
		struct {
			int		done_count;
			int		done;
			int		resume_soc;
		} warm_charge;
		struct {
			int		is_loaded;
			int		no_match;
			int		dev_type;
			int		fw_ver;
			int		hw_ver;
			int		chem_id;
			int		df_ver;
			int		pack_config;
			int		design_cap;
			int		battery_id;
			int		shutoff_mv;
			int		alt_shutoff_mv;
			char		*battery_man;
			char		*battery_mod;
		} info;
		struct {
			int		is_loaded;
			int		capacity_mah;
			int		charge_mv;
			int		charge_rate;
			int		taper_ma;
			unsigned char	pack_serial_number[16+1];
			unsigned char	pcm_serial_number[16+1];
		} mib;
		struct {
			int		temp_hi_dk;
			int		temp_lo_dk;
			int		volt_hi_mv;
			int		volt_lo_mv;
			int		temp_hi_shutdown_dk;
			int		volt_lo_shutdown_mv;
		} limits;
		struct {
			unsigned long	next;
			int		seconds;
		} snapshot;
	} status;

	struct {
		int			charging;
		unsigned long		start_time;
		int			start_soc;
		long			ai_sum;
		int			ai_count;
		int			ai_max;
		int			skip_count;
	} charge_monitor;

	struct {
		int			on;
		int			soc;
		unsigned long		time;
		int			temp_soc;
		unsigned long		temp_time;
		int			spurious;
		int			delay;
	} screen_monitor;

	struct {
		int			class;
		int			offset;
		int			length;
		char			local[1024];
		u8			buffer[BQ27541_REG_DFD_SIZE*4];
		u8			command;
		u16			control;
		int			response;
	} procfs;

#if defined(CONFIG_BQ27541_FLASHSTREAM)
	struct {
		int			registered;
		int			acquired;
		char			writing;
		char			written;
		char			failed;
		char			prev_failed;
		int			prev_sequence;
		int			sequence;
		int			offset;
		char 			buffer[300];
					//"W: XX XX[ xx]*96" = 8 + 3 * 96 = 296
					// (command is parsed in place)
	} flashstream;
#endif

#if defined(CONFIG_BQ27541_STATISTICS)
	struct {
		unsigned long		start_time;
		unsigned long		count[BQ27541_STATS_e_MAX];
	} stats;
#endif
};

/**
 * Forward definitions.
 */
static void bq27541_status_work_reschedule(struct bq27541_chip * chip);

/**
 * Helper to determine which shutoff_mv to use.
 */
static int bq27541_get_shutoff_mv(struct bq27541_chip * chip)
{
	return (chip->status.info.battery_id == BATTERY_PACK_ID_ADAPTER ||
				!chip->status.battery_present) ?
			chip->status.info.alt_shutoff_mv :
			chip->status.info.shutoff_mv;
}

/**
 * Parse helper.
 * Skips white space (eg. spaces, tabs, EOL). The ptr is advanced.
 * Returns count of characters skipped.
 */
static int bq27541_parse_skip_white(char ** ptr)
{
	int count = 0;
	for (count=0;**ptr && **ptr <= ' ';count++) (*ptr)++;
	return count;
}

/**
 * Parse helper.
 * Decodes hex nibbles, and shifts into value.  The ptr is advanced.
 * Value is initialized to 0. Returns count of nibbles.
 */
static int bq27541_parse_decode_hex(char ** ptr, unsigned long * value)
{
	int count = 0;
	char ch;
	*value = 0;
	while ((ch = **ptr) != 0) {
		if (ch >= 'a') ch -= 'a' - 'A';
		if (ch < '0' || ch > 'F' || (ch > '9' && ch < 'A')) break;
		if (ch > '9') ch -= 'A' - '9' - 1;
		*value = (*value << 4) + (ch - '0');
		(*ptr)++;
		count++;
	}
	return count;
}

/**
 * Parse helper.
 * Decodes decimal digits, and shifts into value.  The ptr is advanced.
 * Value is initialized to 0. Returns count of digits.
 */
static int bq27541_parse_decode_dec(char ** ptr, unsigned long * value)
{
	int count = 0;
	char ch;
	*value = 0;
	while ((ch = **ptr) != 0) {
		if (ch < '0' || ch > '9') break;
		*value = (*value * 10) + (ch - '0');
		(*ptr)++;
		count++;
	}
	return count;
}

static int bq27541_find_device_cb(struct device *dev, /*const*/ void *name) { return 1; }
static struct rtc_device * bq27541_get_rtc(struct bq27541_chip * chip)
{
	if (!chip->rtc) {
		struct device *dev = class_find_device(rtc_class, NULL, NULL,
					bq27541_find_device_cb);
		if (dev) {
			chip->rtc = rtc_class_open((char *)dev_name(dev));
			put_device(dev);
		}
		if (!chip->rtc) pr_err("problem with RTC\n");
	}
	return chip->rtc;
}

static void bq27541_close_rtc(struct bq27541_chip * chip)
{
	if (chip->rtc) {
		rtc_class_close(chip->rtc);
		chip->rtc = 0;
	}
}

/**
 * Get wall time from RTC.
 */
static int bq27541_get_wall_time(struct bq27541_chip * chip,
		struct rtc_time * time)
{
	struct rtc_device *rtc = bq27541_get_rtc(chip);

	if (!rtc) return -1;
	rtc_read_time(rtc, time);
	return 0;
}

/**
 * Get wall time, in seconds.
 */
static unsigned long bq27541_get_wall_seconds(struct bq27541_chip * chip)
{
	unsigned long now = 0;
	struct rtc_time time;

	if (!bq27541_get_wall_time(chip, &time))
		rtc_tm_to_time(&time, &now);
	return now;
}

/**
 * Get wall time, as a simple string.
 * YYYY-MM-DD-hh-mm-ss
 */
static int bq27541_get_wall_string(struct bq27541_chip * chip, char * buffer)
{
	struct rtc_time time;
	const int len = 19;

	if (bq27541_get_wall_time(chip, &time))
		memset(&time, 0, sizeof(time));
	snprintf(buffer, len+1, "%04d-%02d-%02d-%02d-%02d-%02d",
			time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
			time.tm_hour, time.tm_min, time.tm_sec);
	buffer[len] = 0;
	return len;
}

/**
 * Set/clear wall alarm. Used during suspend.
 */
static void bq27541_wall_alarm_set(struct bq27541_chip * chip, int seconds)
{
	struct rtc_device *rtc = bq27541_get_rtc(chip);

	if (rtc) {
		unsigned long now;
		struct rtc_wkalrm alm;
		if (seconds > 0) {
			rtc_read_time(rtc, &alm.time);
			rtc_tm_to_time(&alm.time, &now);
			memset(&alm, 0, sizeof(alm));
			rtc_time_to_tm(now + seconds, &alm.time);
			alm.enabled = true;
		} else {
			memset(&alm, 0, sizeof(alm));
		}
		rtc_set_alarm(rtc, &alm);
	}
}

/**
 * Helper to log a metric, and echo to kernel log.
 */
static void bq27541_log_metric(char * domain, char * entry)
{
	pr_info("%s: %s\n", domain, entry);
#if defined(CONFIG_AMAZON_METRICS_LOG)
	log_to_metrics(ANDROID_LOG_INFO, domain, entry);
#endif
}

/**
 * Power off the system.
 */
static void bq27541_system_power_off(char * message, int immediate)
{
	pr_err("%s\n", message);
	if (immediate) {
		machine_power_off();
	} else {
		sys_sync();
		sys_sync();
		orderly_poweroff(true);
	}
}

/**
 * Helper to read a sysfs node.
 */
static int bq27541_read_sysfs_node(const char * path, char * buffer, int size)
{
	mm_segment_t old_fs;
	int fd, count = -1;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	fd = sys_open(path, O_RDONLY, 0);
	if (fd >= 0) {
		count = sys_read(fd, buffer, size);
		sys_close(fd);
	}
	set_fs(old_fs);
	return count;
}

/**
 * Helper to determine if screen is on.
 * The backlight LED brightness will be 0, if the screen is off, else not 0.
 */
static int bq27541_is_display_on(void)
{
	unsigned long on = 0;
	char buf[16];
	int count;

	count = bq27541_read_sysfs_node(
			"/sys/class/leds/lcd-backlight/brightness",
			buf, sizeof(buf)-1);
	if (count > 0) {
		buf[count] = 0;
		on = simple_strtoul(buf, NULL, 10);
	}
	return on != 0;
}

/**
 * Helper for bms power supply notification.
 */
static void bq27541_bms_supply_changed(struct bq27541_chip * chip)
{
	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("bms");
	if (chip->bms_psy)
		power_supply_changed(chip->bms_psy);
}

/**
 * Wrapper function for battery present decision
 */
static int bq27541_is_battery_present(struct bq27541_chip * chip)
{
	if (chip->bootmode == BOOT_MODE_FACTORY)
		return chip->status.info.battery_id > 0;
	return chip->status.battery_present;
}

/**
 * Wrapper function for decision to use voltage SOC
 */
static int bq27541_use_voltage_soc(struct bq27541_chip * chip)
{
	return chip->status.use_voltage_soc || !chip->status.battery_present;
}

/**
 * Wrapper for qpnp_vadc_read() - returns result.physical
 */
static int bq27541_vadc_read(struct bq27541_chip * chip,
		enum qpnp_vadc_channels channel, int * value)
{
	struct qpnp_vadc_result result;
	int rc = qpnp_vadc_read(chip->vadc_dev, channel, &result);
	if (rc) {
		pr_err("error %d, channel %d\n", rc, channel);
		return rc;
	}
	*value = (int)result.physical;
	return 0;
}

/**
 * Wrapper for qpnp_iadc_read() - returns result.result_ua
 */
static int bq27541_iadc_read(struct bq27541_chip * chip,
		enum qpnp_iadc_channels channel, int * value)
{
	struct qpnp_iadc_result result;
	int rc = qpnp_iadc_read(chip->iadc_dev, channel, &result);
	if (rc) {
		pr_err("error %d, channel %d\n", rc, channel);
		return rc;
	}
	*value = (int)result.result_ua;
	return 0;
}

/**
 * Wrapper for spmi_ext_register_readl()
 */
static int bq27541_pmic_read(struct bq27541_chip * chip,
			u16 address, u8 * buffer, int count)
{
	int rc = spmi_ext_register_readl(spmi_busnum_to_ctrl(0), 0,
			address, buffer, count);
	if (rc) pr_err("PMIC read failed, @0x%04x, rc=%d\n", address, rc);
	return rc;
}

/**
 * Wrapper for the cache mutex (see bq27541_release_cache).
 */
static void bq27541_acquire_cache(struct bq27541_chip * chip)
{
	mutex_lock(&chip->status.cache.mutex);
}

/**
 * Wrapper for the cache mutex (see bq27541_acquire_cache).
 */
static void bq27541_release_cache(struct bq27541_chip * chip)
{
	mutex_unlock(&chip->status.cache.mutex);
}

/**
 * Wrapper for the access mutex (see bq27541_release_busy). It includes an
 * error indicator, which triggers a recovery procedure, on check or release.
 */
static void bq27541_acquire_busy(struct bq27541_chip * chip)
{
	mutex_lock(&chip->access.mutex);
	chip->access.errors = 0;
	chip->access.errno = 0;
}

/**
 * If bus errors are encountered while acquired, bus recovery will be attempted.
 */
static int bq27541_i2c_recovery(struct bq27541_chip * chip);
static int bq27541_release_check(struct bq27541_chip * chip)
{
	int okay = 1;
	if (chip->access.errors) {
		pr_crit("errors: %d (%d)\n",
				chip->access.errors, chip->access.errno);
		chip->access.errors = 0;
		okay = bq27541_i2c_recovery(chip);
	}
	return okay;
}

/**
 * Wrapper for the access mutex (see bq27541_acquire_busy).
 * Performs release check first.
 */
static int bq27541_release_busy(struct bq27541_chip * chip)
{
	int okay = bq27541_release_check(chip);
	mutex_unlock(&chip->access.mutex);
	return okay;
}

/**
 * Access wrapper for reading battery status.
 */
static int bq27541_get_status(struct bq27541_chip * chip, int which)
{
	int value = 0;

	bq27541_acquire_cache(chip);
	switch (which) {
	case BATT_STATUS_TEMPERATURE:
		value = chip->status.cache.temp;
		break;
	case BATT_STATUS_VOLTAGE:
		value = chip->status.cache.volt;
		break;
	case BATT_STATUS_FLAGS:
		value = chip->status.cache.flags;
		break;
	case BATT_STATUS_NAC:
		value = chip->status.cache.nac;
		break;
	case BATT_STATUS_RM:
		value = chip->status.cache.rm;
		break;
	case BATT_STATUS_FCC:
		value = chip->status.cache.fcc;
		break;
	case BATT_STATUS_CURRENT:
		value = chip->status.cache.ai;
		break;
	case BATT_STATUS_CAPACITY:
		value = bq27541_use_voltage_soc(chip) ?
				chip->status.cache.vsoc :
				chip->status.cache.soc;
		break;
	case BATT_STATUS_SOH:
		value = chip->status.cache.soh;
		break;
	case BATT_STATUS_STATUS:
		value = chip->status.cache.status;
		break;
	case BATT_STATUS_HEALTH:
		value = chip->status.cache.health;
		break;
	case BATT_STATUS_CNTL:
		value = chip->status.cache.cntl_status;
		break;
	case BATT_STATUS_CC:
		value = chip->status.cache.cc;
		break;
	case BATT_STATUS_MLI:
		value = chip->status.cache.mli;
		break;
	}
	bq27541_release_cache(chip);
	return value;
}

/**
 * Shorthand for the print_hex_dump_bytes() shorthand.
 */
static void bq27541_hex_dump(char * tag, void * data, int length)
{
	print_hex_dump(KERN_CRIT, tag, DUMP_PREFIX_NONE, 16, 1, data,
			(size_t)length, 0);
}

/**
 * Produce formatted hex dump of I2C exchange.
 */
static void bq27541_i2c_hex_dump(char * pre, u8 addr, u8 reg, void * data,
		int length)
{
	char prefix[32];
	sprintf(prefix,"I2C_%s_%02X_%02X: ", pre, addr, reg);
	bq27541_hex_dump(prefix, data, length);
}

/**
 * Basic I2C transfer function, with retries.
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_transfer(struct bq27541_chip * chip,
		struct i2c_msg * msg, int count)
{
	int rc, retries = chip->mitigation.i2c_max_retries;

	while (1) {
		rc = i2c_transfer(chip->client->adapter, msg, count);
		if (rc >= 0) {
			if (rc == count) return 0;
			rc = -1;
		}
		chip->mitigation.i2c_protocol_errors++;
		if (retries-- <= 0) return rc;
		msleep(BQ27541_I2C_RETRY_MSLEEP);
	}
}

/**
 * Basic I2C read function, with retries.
 * 'addr' is the 7 bit device address.
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_transfer_read(struct bq27541_chip * chip,
		u8 addr, u8 reg, u8 * data, int length)
{
	struct i2c_msg msg[2];
	int rc;

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &reg;
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = length;
	msg[1].buf = data;
#if defined(CONFIG_BQ27541_400KHZ_COMPATIBLE)
	if (chip->mitigation.i2c_inter_cmd_udelay)
		udelay(chip->mitigation.i2c_inter_cmd_udelay);
#endif
	rc = bq27541_i2c_transfer(chip, msg, 2);
	if (!rc && chip->mitigation.i2c_hex_dump)
		bq27541_i2c_hex_dump("R", addr, reg, data, length);
	return rc;
}

/**
 * Basic I2C write function, with retries.
 * 'addr' is the 7 bit device address.
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_transfer_write(struct bq27541_chip * chip,
		u8 addr, u8 * data, int length)
{
	struct i2c_msg msg[1];

	if (chip->mitigation.i2c_hex_dump)
		bq27541_i2c_hex_dump("W", addr, data[0], data+1, length-1);
	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].len = length;
	msg[0].buf = data;
#if defined(CONFIG_BQ27541_400KHZ_COMPATIBLE)
	if (chip->mitigation.i2c_inter_cmd_udelay)
		udelay(chip->mitigation.i2c_inter_cmd_udelay);
#endif
	return bq27541_i2c_transfer(chip, msg, 1);
}

/**
 * Try to make contact with the GG. The chip may be in user, ROM, or broken
 * mode. If user mode, it should answer to a read of the flags register. If in
 * ROM mode, try to switch to user mode, then read flags register. If broken,
 * well, it's broken.
 */
static int bq27541_i2c_ping_device(struct bq27541_chip * chip)
{
	static const u8 from_rom_mode_1[] = { 0x00, 0x0F };
	static const u8 from_rom_mode_2[] = { 0x64, 0x0F, 0x00 };
	int retries = BQ27541_I2C_MAX_PING_RETRIES;
	u8 data[2];

	while (1) {
		// try user addressing
		if (bq27541_i2c_transfer_read(chip, (u8)chip->client->addr,
				BQ27541_REG_FLAGS, data, 2) >= 0) {
			return 1;
		}
		// try rom addressing - if okay, switch to user mode
		if (bq27541_i2c_transfer_write(chip,
				BQ27541_ROM_MODE_ADDRESS >> 1,
				(u8*)from_rom_mode_1,
				sizeof(from_rom_mode_1)) >= 0 &&
			bq27541_i2c_transfer_write(chip,
					BQ27541_ROM_MODE_ADDRESS >> 1,
					(u8*)from_rom_mode_2,
					sizeof(from_rom_mode_2)) >= 0) {
			msleep(BQ27541_I2C_EXEC_FMWR_MSLEEP);
			if (bq27541_i2c_transfer_read(chip,
					(u8)chip->client->addr,
					BQ27541_REG_FLAGS,
					data, 2) >= 0) {
				return 1;
			}
		}
		if (retries-- <= 0) break;
		msleep(BQ27541_I2C_RECOVERY_MSLEEP);
	}
	return 0;
}

/**
 * We're here because of a preseumed i2c error, while talking to the BQ27541.
 * Attempt to re-establish communication with the part. If we can't talk to the
 * BQ27541, then presume it isn't present (or alive), set battery not present,
 * and disable battery activity. Otherwise, set present.
 *
 * If successful, then re-enable and return 1, to indicate success.
 * If not successful, then disable and return 0, to indicate failure.
 */
static int bq27541_i2c_recovery(struct bq27541_chip * chip)
{
	int okay;
	pr_err("attempting recovery\n");
	msleep(BQ27541_I2C_RECOVERY_MSLEEP);
	okay = bq27541_i2c_ping_device(chip);
	if (okay) {
		pr_err("recovery successful - enabling battery\n");
		chip->status.battery_present = chip->status.info.battery_id > 0;
		if (chip->recovery.not_okay) {
			chip->recovery.not_okay = 0;
			// other post-recovery actions
		}
	} else {
		pr_err("recovery unsuccessful - disabling battery\n");
		chip->status.battery_present = 0;
		if (chip->recovery.not_okay++ == 0) {
			// other first time failure actions
		}
	}
	chip->status.status_delta |= BQ27541_DELTA_I2C_ERR;
	chip->status.status_delta |= BQ27541_DELTA_BAT_PRES;
	return okay;
}

/**
 * Failures in this function will invoke the recovery process, which may
 * disable the BQ27541. Use bq27541_i2c_transfer_write() to avoid recovery.
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_write(struct bq27541_chip * chip,
		u8 * data, int length)
{
	int err;

	err = bq27541_i2c_transfer_write(chip, (u8)chip->client->addr,
			data, length);
	if (err >= 0) return 0;
	if (!chip->mitigation.suppress_errors)
		dev_err(chip->dev, "write err %d\n", err);
	chip->access.errors++;
	chip->access.errno = err;
	return err;
}

/**
 * Write a single byte to device. (does not acquire busy)
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_write_byte(struct bq27541_chip * chip, u8 reg, u8 value)
{
	u8 data[2];
	data[0] = reg;
	data[1] = value;
	return bq27541_i2c_write(chip, data, 2);
}

/**
 * Write multiple bytes to device. (does not acquire busy)
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_write_bytes(struct bq27541_chip * chip,
		u8 reg, u8 * data, int length)
{
#if defined(CONFIG_BQ27541_SINGLE_BYTE_WRITES)
	int rc = 0;
	while (rc == 0 && length-- > 0)
		rc = bq27541_i2c_write_byte(chip, reg++, *data++);
	return rc;
#else
	u8 buffer[BQ27541_REG_DFD_SIZE+1]; // sized for max write length
	buffer[0] = reg;
	memcpy(buffer+1,data,length);
	return bq27541_i2c_write(chip, buffer, length + 1);
#endif
}

#if !defined(CONFIG_BQ27541_MAC_COMMAND_FIX)
/**
 * Write a register word to device. (does not acquire busy)
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_write_word(struct bq27541_chip * chip, u8 reg, u16 value)
{
#if defined(CONFIG_BQ27541_SINGLE_BYTE_WRITES)
	u8 data[2];
	data[0] = value;
	data[1] = value >> 8;
	return bq27541_i2c_write_bytes(chip, reg, data, 2);
#else
	u8 data[3];
	data[0] = reg;
	data[1] = value;
	data[2] = value >> 8;
	return bq27541_i2c_write(chip, data, sizeof(data));
#endif
}
#endif

/**
 * Read bytes from device. (does not acquire busy)
 * Failures in this function will invoke the recovery process, which may
 * disable the BQ27541. Use bq27541_i2c_transfer_read() to avoid recovery.
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_read(struct bq27541_chip * chip,
		u8 reg, u8 * data, int length)
{
	int err;

	err = bq27541_i2c_transfer_read(chip, (u8)chip->client->addr, reg,
			data, length);
	if (err >= 0) return 0;
	if (!chip->mitigation.suppress_errors)
		dev_err(chip->dev, "read err %d\n", err);
	chip->access.errors++;
	chip->access.errno = err;
	return err;
}

/**
 * Reads register word over i2c. (does not acquire busy)
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_read_word(struct bq27541_chip * chip,
		u8 reg, int * value)
{
	u8 data[2];
	int rc = bq27541_i2c_read(chip, reg, data, 2);
	if (rc == 0 && value) *value = get_unaligned_le16(data);
	return rc;
}

/**
 * Acquires busy, then reads register word over i2c.
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_read_word_acquire(struct bq27541_chip * chip,
		u8 reg, int * value)
{
	int rc;
	bq27541_acquire_busy(chip);
	rc = bq27541_i2c_read_word(chip, reg, value);
	bq27541_release_busy(chip);
	return rc;
}

/**
 * Reads current directly, bypassing cache.
 */
static int bq27541_read_current_now_ua(struct bq27541_chip * chip)
{
	int val;
	if (chip->status.battery_present &&
			bq27541_i2c_read_word_acquire(
				chip, BQ27541_REG_AI, &val) == 0)
		return -(int)(short)val * 1000;
	bq27541_iadc_read(chip, EXTERNAL_RSENSE, &val);
	return -val;
}

/**
 * Reads voltage directly, bypassing cache.
 */
static int bq27541_read_voltage_now_uv(struct bq27541_chip * chip)
{
	int val;
	if (chip->status.battery_present &&
			bq27541_i2c_read_word_acquire(
				chip, BQ27541_REG_VOLT, &val) == 0)
		return val * 1000;
	bq27541_vadc_read(chip, VBAT_SNS, &val);
	return val;
}

/**
 * Returns 0, or -ERROR.
 */
static int bq27541_i2c_read_ctrl_word(struct bq27541_chip * chip,
		int ctrl_cmd, int * value)
{
#if defined(CONFIG_BQ27541_MAC_COMMAND_FIX)
	int rc = bq27541_i2c_write_byte(chip, BQ27541_REG_CNTL, (u8)ctrl_cmd);
	if (rc == 0) {
		if (chip->mitigation.i2c_mac_cmd_udelay)
			udelay(chip->mitigation.i2c_mac_cmd_udelay);
		rc = bq27541_i2c_write_byte(chip, BQ27541_REG_CNTL + 1,
				(u8)(ctrl_cmd >> 8));
		if (rc == 0) {
			if (chip->mitigation.i2c_mac_cmd_udelay)
				udelay(chip->mitigation.i2c_mac_cmd_udelay);
			rc = bq27541_i2c_read_word(chip,
					BQ27541_REG_CNTL, value);
		}
	}
	return rc;
#else
	int rc = bq27541_i2c_write_word(chip, BQ27541_REG_CNTL, ctrl_cmd);
	if (rc == 0) {
		if (chip->mitigation.i2c_mac_cmd_udelay)
			udelay(chip->mitigation.i2c_mac_cmd_udelay);
		rc = bq27541_i2c_read_word(chip, BQ27541_REG_CNTL, value);
	}
	return rc;
#endif
}

/**
 * Returns 1 == sealed, else 0.
 * -1 on error (can imply sealed)
 */
static int bq27541_is_chip_sealed(struct bq27541_chip * chip)
{
	int status = 0;
	if (bq27541_i2c_read_ctrl_word(chip,
			BQ27541_SUBCMD_CNTL_STATUS, &status))
		return -1;
	return !!(status & BQ27541_CS_SS);
}

/**
 * Returns 1 == full access sealed, else 0.
 * -1 on error (can imply sealed)
 */
static int bq27541_is_chip_fa_sealed(struct bq27541_chip * chip)
{
	int status = 0;
	if (bq27541_i2c_read_ctrl_word(chip,
			BQ27541_SUBCMD_CNTL_STATUS, &status))
		return -1;
	return !!(status & BQ27541_CS_FAS);
}

/**
 * Seal or unseal the chip. The key will be used to unseal.
 * The two 16 bit keys are extracted from the single 32 bit key.
 */
static int bq27541_chip_seal(struct bq27541_chip * chip,
		int seal, unsigned int key)
{
	int rc = 0, sealed, fa_sealed;
	unsigned int k0 = (key >> 16) & 0xFFFF;
	unsigned int k1 = key & 0xFFFF;

	sealed = bq27541_is_chip_sealed(chip);
	fa_sealed = bq27541_is_chip_fa_sealed(chip);
	if (seal) {
		rc = bq27541_i2c_write_word(chip, BQ27541_REG_CNTL,
				BQ27541_SUBCMD_SEALED);
	} else {
		rc = bq27541_i2c_write_word(chip, BQ27541_REG_CNTL, k1);
		if (rc == 0)
			rc = bq27541_i2c_write_word(chip, BQ27541_REG_CNTL, k0);
	}
	pr_crit("%d (%d), %d, k0 = %04x, k1 = %04x, %d (%d)\n",
			sealed, fa_sealed, seal, k0, k1,
			bq27541_is_chip_sealed(chip),
			bq27541_is_chip_fa_sealed(chip));
	return rc;
}

/**
 * Returns 0, or -ERROR.
 */
static int bq27541_dfd_read_block(struct bq27541_chip * chip,
		int class, int block, int offset, u8 * data, int length)
{
	if (offset < 0 || length < 0 || (offset+length) > BQ27541_REG_DFD_SIZE)
		return -1;
	if (length == 0)
		return 0;
	if (bq27541_i2c_write_byte(chip, BQ27541_REG_DFDCNTL, 0)) return -1;
	if (bq27541_i2c_write_byte(chip, BQ27541_REG_DFCLS, class)) return -1;
	if (bq27541_i2c_write_byte(chip, BQ27541_REG_DFBLK, block)) return -1;
	msleep(2);
	return bq27541_i2c_read(chip, BQ27541_REG_DFD + offset, data, length);
}

/**
 * Returns 0, or -ERROR.
 */
static int bq27541_dfd_write_block(struct bq27541_chip * chip,
		int class, int block, int offset, u8 * data, int length)
{
	u8 buffer[BQ27541_REG_DFD_SIZE];
	int i,csm;
	if (offset < 0 || length < 0 || (offset+length) > BQ27541_REG_DFD_SIZE)
		return -1;
	if (length == 0)
		return 0;
	if (bq27541_dfd_read_block(chip, class, block, 0, buffer,
			BQ27541_REG_DFD_SIZE))
		return -1;
	memcpy(buffer + offset, data, length);
	for (csm=i=0;i < BQ27541_REG_DFD_SIZE; )
		csm += (int)buffer[i++];
	csm = ~csm & 0xFF;
	if (bq27541_i2c_write_bytes(chip, BQ27541_REG_DFD, buffer,
			BQ27541_REG_DFD_SIZE))
		return -1;
	if (bq27541_i2c_write_byte(chip, BQ27541_REG_DFDCKS, (u8)csm))
		return -1;
	msleep(2);
	return 0;
}

/**
 * Returns 0, or -ERROR.
 */
static int bq27541_dfd_blocked_op(struct bq27541_chip * chip,
	int class, int offset, u8 * data, int length,
	int (blocked_op)(struct bq27541_chip *, int, int, int, u8 *, int))
{
	int rc, block, len;
	if (offset < 0 || length < 0)
		return -1;
	block = offset / BQ27541_REG_DFD_SIZE;
	offset -= block * BQ27541_REG_DFD_SIZE;
	while (length > 0) {
		len = BQ27541_REG_DFD_SIZE - offset;
		if (len > length) len = length;
		rc = blocked_op(chip, class, block, offset, data, len);
		if (rc) return rc;
		length -= len;
		data += len;
		block++;
		offset = 0;
	}
	return 0;
}

/**
 * Returns 0, or -ERROR.
 */
static int bq27541_dfd_read(struct bq27541_chip * chip,
		int class, int offset, u8 * data, int length)
{
	return bq27541_dfd_blocked_op(chip, class, offset, data, length,
			bq27541_dfd_read_block);
}

/**
 * Returns 0, or -ERROR.
 */
static int bq27541_dfd_write(struct bq27541_chip * chip,
		int class, int offset, u8 * data, int length)
{
	return bq27541_dfd_blocked_op(chip, class, offset, data, length,
			bq27541_dfd_write_block);
}

#if defined(CONFIG_BQ27541_FLASHSTREAM)
/**
 * The /proc/bq27541-gg/fs node accepts TI FlashStream update files.
 *
 * (refer to SLUA541A, Updating the bq275xx Firmware at Production)
 *
 * TI BMS FlashStream (.bqfs) and Data Flash FlashStream (.dffs)
 *
 * The files contain instructions for I2C operations required the instruction
 * flash (IF) and/or data flash (DF).
 *
 * The device must be in ROM mode. While in ROM mode, the target device responds
 * to the I2C address of 0x16 (8 bit) or 0x0B (7 bit) if using I2C. From here,
 * the 8-bit I2C address reference is used. To enter ROM mode, 0x0F00 must be
 * written to register address 0x00 of the target device if in the I2C mode, or
 * 0x00 into register 0x00 and 0x0F into register 0x01 if in the HDQ mode.
 * Remember that the I2C address of the device is 0xAA while it is in normal gas
 * gauge mode (default). This step of the process is not part of the bqfs or
 * dffs command sequence. It is up to the user to provide the Enter ROM mode
 * command before using the bqfs or dffs file.
 *
 * Firmware Programming Flow
 *
 * 1. Driver - Enter ROM mode
 * 2. FlashStream - Erase Data Flash
 * 3. FlashStream - Erase Instruction flash
 * 4. FlashStream - Program Instruction flash
 * 5. FlashStream - Program Data flash
 * 6. Driver - Exit ROM mode
 * 7. Driver - Wait at least 250ms
 *
 * Before entering the ROM mode, it must be confirmed that the device is not
 * sealed from a firmware perspective. To verify that the device is not sealed,
 * read Control Status by first writing 0x00 into register 0x00 and 0x01, and
 * then read back register 0x01. If bit 5 is set, then sending the Unseal keys
 * is required. If bit 6 is set, then it requires sending the Full Access Unseal
 * key. These keys are sent by writing the respective keys into the Control
 * register. The keys are 32-bit each. If both keys are required, then they must
 * be entered in the order of Unseal key first and then Full Access Unseal key.
 * For any 32-bit key, no intermittent communication is allowed other than the
 * actual communication to write the keys.
 *
 * Once in ROM mode, the user test setup must be able to open the bqfs or dffs
 * file and perform each one of the I2C or HDQ transactions in strictly the same
 * order as given with the FlashStream file. On completing all the commands
 * within the FlashStream file, the user test system sends the Exit ROM mode
 * procedure. At least a 250-ms delay must occur before attempting to proceed
 * with I2C communication to the target device using the I2C address 0xAA after
 * exiting ROM Mode.
 *
 * The file is ASCII text, and contains both commands and data. Each line
 * represents one command and potentially 96 bytes of data. No row contains more
 * than 96 data bytes. The first two characters of each row represent the
 * command, followed by a ":".
 *   W: [write one or more bytes of data]
 *   R: [read one or more bytes of data]
 *   C: [read and compare one or more bytes of data]
 *   X: [wait at least a given number of milliseconds before proceeding]
 *
 * W: [I2CAddr] [RegAddr] [Byte0 ... ByteN]
 *   eg. W: [AA] [55] [AB CD EF 00]
 *   Writes 0xAB to register 0x55, 0xCD to register 0x56. 0xEF to register 0x57,
 *   and 0x00 to register 0x58.
 *
 * R: [I2CAddr] [RegAddr] [NumBytes]
 *   eg. R: [AA] [55] [100]
 *   Reads 100 (decimal) bytes of data at register address 0x55.
 *
 * C: [I2CAddr] [RegAddr] [Byte0 ... ByteN]
 *   eg. C: [AA] [55] [AB CD EF 00]
 *   Read and compare bytes. Error, if they don't match.
 *
 * X: [Milliseconds]
 *   eg. X: [200]
 *   Wait 200 (decimal) milliseconds.
 *
 */

/**
 * Advance the parse offset past white space.
 */
static char bq27541_fs_skip_white(struct bq27541_chip * chip)
{
	while (1) {
		char c = chip->flashstream.buffer[chip->flashstream.offset];
		if (!c || c > ' ') return c;
		chip->flashstream.offset++;
	}
}

/**
 * Advance the parse offset past a two character hex value and return the value.
 * Skip leading white space.
 */
static int bq27541_fs_get_hex(struct bq27541_chip * chip)
{
	int value = 0, check = 0;
	char c;
	bq27541_fs_skip_white(chip);
	while (((c = chip->flashstream.buffer[chip->flashstream.offset]) >= '0'
			&& c <= '9') || (c >= 'A' && c <= 'F')) {
		if (c > '9') c -= 'A' - '9' - 1;
		value = value * 16 + (c - '0');
		chip->flashstream.offset++;
		check++;
	}
	return (check == 2) ? value : -EPROTO;
}

/**
 * Advance the parse offset past a list of two character hex values and return
 * the values in the parse buffer. As usual, there is a reserved byte at the
 * beginning of the buffer.
 * Skips white space between values.
 */
static int bq27541_fs_get_hex_list(struct bq27541_chip * chip)
{
	int value, count = 1;	// leave an empty byte at the beginning
	while (1) {
		if (!bq27541_fs_skip_white(chip)) break;
		value = bq27541_fs_get_hex(chip);
		if (value < 0) return value;
		chip->flashstream.buffer[count++] = (char)value;
	}
	return count - 1;
}

/**
 * Advance the parse offset past a decimal value and return the value.
 * Skip leading white space.
 */
static int bq27541_fs_get_dec(struct bq27541_chip * chip)
{
	int value = 0, check = 0;
	char c;
	bq27541_fs_skip_white(chip);
	while ((c = chip->flashstream.buffer[chip->flashstream.offset]) >= '0'
			&& c <= '9') {
		value = value * 10 + (c - '0');
		chip->flashstream.offset++;
		check++;
	}
	return check ? value : -EPROTO;
}

/**
 * Advance the parse offset past a single non-white character and return it.
 * Skip leading white space.
 */
static char bq27541_fs_get_char(struct bq27541_chip * chip)
{
	char c = bq27541_fs_skip_white(chip);
	if (c) chip->flashstream.offset++;
	return c;
}

/**
 * Each command line is received into context.buffer, beginning at offset 0.
 * When EOL is detected, the command is parsed in place (the result will be
 * shorter), beginning at offset 1, leaving room for the leading register byte
 * required during single message I2C write.
 */
static int bq27541_fs_execute(struct bq27541_chip * chip)
{
	int rc = 0, value, addr, reg, count;
	u8 data[100];
	char opcode, *buffer = chip->flashstream.buffer;
	chip->flashstream.offset = 0;
	opcode = bq27541_fs_get_char(chip);
	if (!opcode || opcode == ';') return 0;
	if (bq27541_fs_get_char(chip) != ':') return -EPROTO;
	switch (opcode) {
	case 'W':	// W: [I2CAddr] [RegAddr] [Byte0 ... ByteN]
		if ((addr = bq27541_fs_get_hex(chip)) < 0) return addr;
		if ((reg = bq27541_fs_get_hex(chip)) < 0) return reg;
		if ((count = bq27541_fs_get_hex_list(chip)) < 0) return count;
		addr >>= 1;
		buffer[0] = (char)reg;
		bq27541_i2c_hex_dump("FSW", addr, reg, buffer+1, count);
		rc = bq27541_i2c_transfer_write(chip, (u8)addr, (u8*)buffer, count+1);
		break;
	case 'R':	// R: [I2CAddr] [RegAddr] [NumBytes]
		if ((addr = bq27541_fs_get_hex(chip)) < 0) return addr;
		if ((reg = bq27541_fs_get_hex(chip)) < 0) return reg;
		if ((count = bq27541_fs_get_dec(chip)) < 0) return count;
		addr >>= 1;
		rc = bq27541_i2c_transfer_read(chip, (u8)addr, reg, data, count);
		bq27541_i2c_hex_dump("FSR", addr, reg, data, count);
		break;
	case 'C':	// C: [I2CAddr] [RegAddr] [Byte0 ... ByteN]
		if ((addr = bq27541_fs_get_hex(chip)) < 0) return addr;
		if ((reg = bq27541_fs_get_hex(chip)) < 0) return reg;
		if ((count = bq27541_fs_get_hex_list(chip)) < 0) return count;
		addr >>= 1;
		rc = bq27541_i2c_transfer_read(chip, (u8)addr, reg, data, count);
		bq27541_i2c_hex_dump("FSC", addr, reg, data, count);
		bq27541_i2c_hex_dump("FS?", addr, reg, buffer+1, count);
		if (rc == 0 && memcmp(data, buffer+1, count)) rc = -1;
		break;
	case 'X':	// X: [Milliseconds]
		if ((value = bq27541_fs_get_dec(chip)) < 0) return value;
		pr_crit("X: %d\n", value);
		msleep((unsigned int)value);
		break;
	default:
		return -EPROTO;
	}
	pr_crit("%d\n", rc);
	return rc;
}

/**
 * Return status of the previous operation.
 */
static ssize_t bq27541_procfs_fs_read(struct file * file, char * buffer,
		size_t length, loff_t * offset)
{
	struct bq27541_chip * chip = file->private_data;
	char scratch[64];
	ssize_t rc = 0;

	if (*offset == 0) {
		rc = sprintf(scratch, "lines: %d\nstatus: %d\n",
				chip->flashstream.prev_sequence,
				chip->flashstream.prev_failed);
		if (rc > 0 && copy_to_user(buffer, scratch, rc))
			rc = -EFAULT;
		*offset = 1;
	}
	return rc;
}

/**
 * Feed character data to the flashstream interpreter.
 */
static ssize_t bq27541_procfs_fs_write(struct file * file,
		const char __user * buffer, size_t length, loff_t * offset)
{
	struct bq27541_chip * chip = file->private_data;
	char *local_buffer, *bufp;
	ssize_t consumed = 0;
	int rc = 0;

	if (chip->flashstream.failed) return -1;
	local_buffer = bufp = kzalloc(length, GFP_KERNEL);
	if (!bufp) {
		pr_err("failed to allocate buffer\n");
		return -ENOMEM;
	}
	if (copy_from_user(local_buffer, buffer, length)) {
		pr_err("failed to copy buffer\n");
		return -EFAULT;
	}
	chip->flashstream.writing = 1;
	chip->flashstream.written = 1;
	while (length > 0) {
		char c = *bufp++;
		length--;
		consumed++;
		if (c == '\r') {
			// ignore cr - wait for the newline
		} else if (c == '\n') {
			chip->flashstream.buffer[chip->flashstream.offset++] = 0;
			rc = bq27541_fs_execute(chip);
			chip->flashstream.offset = 0;
			chip->flashstream.sequence++;
		} else if (chip->flashstream.offset
				< sizeof(chip->flashstream.buffer)-1) {
			chip->flashstream.buffer[chip->flashstream.offset++] = c;
		} else {
			// this should not happen - protocol error
			rc = -EPROTO;
		}
		if (rc) {
			chip->flashstream.failed = 1;
			break;
		}
	}
	kfree(local_buffer);
	chip->flashstream.writing = 0;
	return rc ? rc : consumed;
}

static int bq27541_procfs_fs_open(struct inode * inode, struct file * file)
{
	struct bq27541_chip * chip = PDE(inode)->data;
	file->private_data = chip;

	cancel_delayed_work(&chip->status.work);
	bq27541_acquire_busy(chip);
	chip->flashstream.acquired = 1;
	chip->flashstream.writing = 0;
	chip->flashstream.written = 0;
	chip->flashstream.failed = 0;
	chip->flashstream.sequence = 0;
	chip->flashstream.offset = 0;
	msleep(100); // chip settling time from whatever it may have been doing
	pr_crit("\n");
	return 0;
}

static int bq27541_procfs_fs_release(struct inode * inode, struct file * file)
{
	struct bq27541_chip * chip = PDE(inode)->data;

	while (chip->flashstream.writing) msleep(250);
	if (chip->flashstream.written) {
		chip->flashstream.prev_failed = chip->flashstream.failed;
		chip->flashstream.prev_sequence = chip->flashstream.sequence;
	}
	chip->flashstream.acquired = 0;
	bq27541_release_busy(chip); // will process any i2c issues
	pr_crit("%d, %d, %d\n", chip->flashstream.written,
			chip->flashstream.prev_sequence,
			chip->flashstream.prev_failed);
	bq27541_status_work_reschedule(chip);
	return 0;
}

static struct file_operations bq27541_procfs_fs_fops = {
	.owner = THIS_MODULE,
	.read = bq27541_procfs_fs_read,
	.write = bq27541_procfs_fs_write,
	.open = bq27541_procfs_fs_open,
	.release = bq27541_procfs_fs_release,
};
#endif // CONFIG_BQ27541_FLASHSTREAM

static int bq27541_procfs_status_read(struct seq_file * seq, void * offset)
{
	struct bq27541_chip * chip = seq->private;
	seq_printf(seq, "mVolts: %d\n"
			"mAmps: %d\n"
			"temp: %d\n"
			"rsoc: %d\n"
			"fcc: %d\n"
			"rm: %d\n"
			"soh: %d\n"
			"stat: %d\n"
			"flgs: %04x\n"
			"ctrl: %04x\n"
			"cyc: %d\n"
			"mli: %d\n",
			bq27541_get_status(chip, BATT_STATUS_VOLTAGE),
			bq27541_get_status(chip, BATT_STATUS_CURRENT),
			bq27541_get_status(chip, BATT_STATUS_TEMPERATURE),
			bq27541_get_status(chip, BATT_STATUS_CAPACITY),
			bq27541_get_status(chip, BATT_STATUS_FCC),
			bq27541_get_status(chip, BATT_STATUS_RM),
			bq27541_get_status(chip, BATT_STATUS_SOH),
			bq27541_get_status(chip, BATT_STATUS_STATUS),
			bq27541_get_status(chip, BATT_STATUS_FLAGS),
			bq27541_get_status(chip, BATT_STATUS_CNTL),
			bq27541_get_status(chip, BATT_STATUS_CC),
			bq27541_get_status(chip, BATT_STATUS_MLI)
		);
	return 0;
}

static int bq27541_procfs_status_open(struct inode * inode, struct file * file)
{
	return single_open(file, bq27541_procfs_status_read, PDE(inode)->data);
}

static struct file_operations bq27541_procfs_status_fops = {
	.owner = THIS_MODULE,
	.open = bq27541_procfs_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int bq27541_procfs_config_read(struct seq_file * seq, void * offset)
{
	struct bq27541_chip * chip = seq->private;
	seq_printf(seq, "id: %d\n"
			"board: %d\n"
			"mode: %d\n"
			"chem: %04x\n"
			"type: %04x\n"
			"frmv: %04x\n"
			"hrdv: %04x\n"
			"flav: %04x\n"
			"pcr: %04x\n"
			"dcap: %d\n"
			"mib_cap: %d\n"
			"mib_chg: %d\n"
			"mib_rate: %d\n"
			"mib_tap: %d\n"
			"mib_pack_ser: \"%s\"\n"
			"mib_pcm_ser: \"%s\"\n"
			"manuf: \"%s\"\n"
			"model: \"%s\"\n"
			"shut: %d\n"
			"alt_shut: %d\n"
			"pres: %d\n",
			chip->status.info.battery_id,
			ursa_board_revision(),
			chip->bootmode,
			chip->status.info.chem_id,
			chip->status.info.dev_type,
			chip->status.info.fw_ver,
			chip->status.info.hw_ver,
			chip->status.info.df_ver,
			chip->status.info.pack_config,
			chip->status.info.design_cap,
			chip->status.mib.capacity_mah,
			chip->status.mib.charge_mv,
			chip->status.mib.charge_rate,
			chip->status.mib.taper_ma,
			chip->status.mib.pack_serial_number,
			chip->status.mib.pcm_serial_number,
			chip->status.info.battery_man,
			chip->status.info.battery_mod,
			chip->status.info.shutoff_mv,
			chip->status.info.alt_shutoff_mv,
			chip->status.battery_present
		);
	return 0;
}

static int bq27541_procfs_config_open(struct inode * inode, struct file * file)
{
	return single_open(file, bq27541_procfs_config_read, PDE(inode)->data);
}

static struct file_operations bq27541_procfs_config_fops = {
	.owner = THIS_MODULE,
	.open = bq27541_procfs_config_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int bq27541_procfs_dfd_read(struct seq_file * seq, void * offset)
{
	struct bq27541_chip * chip = seq->private;
	int rc;

	bq27541_acquire_busy(chip);
	rc = bq27541_dfd_read(chip, chip->procfs.class, chip->procfs.offset,
			chip->procfs.buffer, chip->procfs.length);
	bq27541_release_busy(chip);
	if (!rc) {
		int i;
		seq_printf(seq, "%d %d %d =", chip->procfs.class,
				chip->procfs.offset, chip->procfs.length);
		for (i=0;i < chip->procfs.length;i++)
			seq_printf(seq, " %02x", chip->procfs.buffer[i]);
		seq_printf(seq, "\n");
	}
	return rc;
}

/**
 * Write a formatted string to this device, to perform or prepare to perform
 * a DFD read or write.
 * class [ offset [ length ] ] [ '=' byte ... ]
 * '=' clause specifies write
 * length is byte count, on write
 */
static int bq27541_procfs_dfd_write(struct file * file,
		const char __user * buffer, size_t count, loff_t * ppos)
{
	struct seq_file *seq = file->private_data;
	struct bq27541_chip * chip = seq->private;
	char *p, prefix[64];
	unsigned long v;
	int i, rc;

	if (count < 1 || count >= sizeof(chip->procfs.local))
		return -EINVAL;
	if (copy_from_user(chip->procfs.local, buffer, count))
		return -EFAULT;
	chip->procfs.local[count] = 0;
	chip->procfs.class = chip->procfs.offset = 0;
	chip->procfs.length = 1;
	p = chip->procfs.local;
	// parse 0..3 unsigned decimal numbers
	bq27541_parse_skip_white(&p);
	if (bq27541_parse_decode_dec(&p, &v)) {
		chip->procfs.class = (u8)v;
		bq27541_parse_skip_white(&p);
		if (bq27541_parse_decode_dec(&p, &v)) {
			chip->procfs.offset = (u8)v;
			bq27541_parse_skip_white(&p);
			if (bq27541_parse_decode_dec(&p, &v)) {
				chip->procfs.length =
					(v > sizeof(chip->procfs.buffer)) ?
						sizeof(chip->procfs.buffer) :
						(u8)v;
			}
		}
	}
	bq27541_parse_skip_white(&p);
	if (*p && *p != '=')
		return -EFAULT;
	if (*p++ == 0)
		return (int)count;
	// parse 0..n hexidecimal numbers
	for (i=0;i < sizeof(chip->procfs.buffer); ) {
		bq27541_parse_skip_white(&p);
		if (*p == 0) break;
		if (!bq27541_parse_decode_hex(&p, &v))
			return -EFAULT;
		chip->procfs.buffer[i++] = (u8)v;
	}
	chip->procfs.length = i;
	rc = (int)count;
	if (i) {
		snprintf(prefix, sizeof(prefix),
				"bq27541_procfs_dfd_write: %d %d %d = ",
				chip->procfs.class, chip->procfs.offset,
				chip->procfs.length);
		bq27541_hex_dump(prefix, chip->procfs.buffer,
				chip->procfs.length);
		bq27541_acquire_busy(chip);
		if (bq27541_dfd_write(chip, chip->procfs.class,
				chip->procfs.offset, chip->procfs.buffer,
				chip->procfs.length))
			rc = -EFAULT;
		bq27541_release_busy(chip);
	}
	return rc;
}

static int bq27541_procfs_dfd_open(struct inode * inode, struct file * file)
{
	return single_open(file, bq27541_procfs_dfd_read, PDE(inode)->data);
}

static struct file_operations bq27541_procfs_dfd_fops = {
	.owner	= THIS_MODULE,
	.open = bq27541_procfs_dfd_open,
	.read	= seq_read,
	.write	= bq27541_procfs_dfd_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int bq27541_procfs_sealed_read(struct seq_file * seq, void * offset)
{
	struct bq27541_chip * chip = seq->private;
	int sealed, full_access;

	bq27541_acquire_busy(chip);
	sealed = bq27541_is_chip_sealed(chip);
	full_access = bq27541_is_chip_fa_sealed(chip);
	bq27541_release_busy(chip);
	seq_printf(seq, "sealed: %s, full access: %s\n",
			sealed ? "yes" : "no", full_access ? "no" : "yes");
	return 0;
}

/**
 * Write "seal" to the device to seal flash, or one or two hex keys to unseal.
 */
static int bq27541_procfs_sealed_write(struct file * file,
		const char __user * buffer, size_t count, loff_t * ppos)
{
	struct seq_file *seq = file->private_data;
	struct bq27541_chip * chip = seq->private;
	unsigned long k1 = 0, k2 = 0;
	int rc, keys = 0;
	char *p;

	if (count < 1 || count >= sizeof(chip->procfs.local))
		return -EINVAL;
	if (copy_from_user(chip->procfs.local, buffer, count))
		return -EFAULT;
	chip->procfs.local[count] = 0;
	p = chip->procfs.local;
	bq27541_parse_skip_white(&p);
	if (memcmp(p, "seal", 4) == 0) {
		p += 4;
		bq27541_parse_skip_white(&p);
		if (*p)
			return -EINVAL;
	} else {
		if (!bq27541_parse_decode_hex(&p, &k1))
			return -EINVAL;
		keys++;
		bq27541_parse_skip_white(&p);
		if (*p) {
			if (!bq27541_parse_decode_hex(&p, &k2))
				return -EINVAL;
			bq27541_parse_skip_white(&p);
			if (*p)
				return -EINVAL;
			keys++;
		}
	}
	bq27541_acquire_busy(chip);
	if (keys == 0) {
		rc = bq27541_chip_seal(chip, 1, 0);
	} else {
		rc = bq27541_chip_seal(chip, 0, (unsigned int)k1);
		if (rc == 0 && keys == 2)
			rc = bq27541_chip_seal(chip, 0, (unsigned int)k2);
	}
	bq27541_release_busy(chip);
	return rc ? -EFAULT : (int)count;
}

static int bq27541_procfs_sealed_open(struct inode * inode, struct file * file)
{
	return single_open(file, bq27541_procfs_sealed_read, PDE(inode)->data);
}

static struct file_operations bq27541_procfs_sealed_fops = {
	.owner	= THIS_MODULE,
	.open = bq27541_procfs_sealed_open,
	.read	= seq_read,
	.write	= bq27541_procfs_sealed_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int bq27541_procfs_data_read(struct seq_file * seq, void * offset)
{
	struct bq27541_chip * chip = seq->private;

	seq_printf(seq, "%02x %04x = %04x %u %d\n",
			chip->procfs.command, chip->procfs.control,
			(unsigned int)chip->procfs.response,
			(unsigned int)chip->procfs.response,
			(int)(short)chip->procfs.response);
	return 0;
}

/**
 * Write a formatted string to this device, to perform a data command.
 * <hex command byte> [ <hex control byte> ]
 * Result is returned by reading the device.
 */
static int bq27541_procfs_data_write(struct file * file,
		const char __user * buffer, size_t count, loff_t * ppos)
{
	struct seq_file *seq = file->private_data;
	struct bq27541_chip * chip = seq->private;
	unsigned long value;
	char *p;
	int rc;

	if (count < 1 || count >= sizeof(chip->procfs.local))
		return -EINVAL;
	if (copy_from_user(chip->procfs.local, buffer, count))
		return -EFAULT;
	chip->procfs.local[count] = 0;
	chip->procfs.command = 0;
	chip->procfs.control = 0;
	chip->procfs.response = 0;
	p = chip->procfs.local;
	bq27541_parse_skip_white(&p);
	if (!bq27541_parse_decode_hex(&p, &value))
		return -EFAULT;
	chip->procfs.command = (u8)value;
	bq27541_parse_skip_white(&p);
	bq27541_parse_decode_hex(&p, &value);
	chip->procfs.control = (u16)value;
	bq27541_parse_skip_white(&p);
	if (*p)
		return -EFAULT;
	rc = (int)count;
	bq27541_acquire_busy(chip);
	if (chip->procfs.command <= 1) {
		if (bq27541_i2c_read_ctrl_word(chip, (int)chip->procfs.control,
				&chip->procfs.response))
			rc = -EFAULT;
	} else if (bq27541_i2c_read_word(chip, chip->procfs.command,
			&chip->procfs.response)) {
		rc = -EFAULT;
	}
	bq27541_release_busy(chip);
	return rc;
}

static int bq27541_procfs_data_open(struct inode * inode, struct file * file)
{
	return single_open(file, bq27541_procfs_data_read, PDE(inode)->data);
}

static struct file_operations bq27541_procfs_data_fops = {
	.owner	= THIS_MODULE,
	.open = bq27541_procfs_data_open,
	.read	= seq_read,
	.write	= bq27541_procfs_data_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int bq27541_procfs_help_read(struct seq_file *seq, void *offset)
{
	static const char help[] =
	"The BQ27541 is a gas gauge chip on the battery pack.\n"
	"It is wired to I2C bus 0, at address 0x55 (0x16 in ROM mode).\n"
	"Device attributes are visible at '/sys/devi*/*i2c/i*/*55/'.\n"
	"Power supply properties are visible at '/sys/class/power_supply/bms/'.\n"
	"The following configuration, status, and chip access is available at /proc/bq27541-gg/.\n"
	" 'cat config' returns chip configuration (mostly static).\n"
	" 'cat status' returns current chip status (cached every n seconds).\n"
	" 'cat sealed' returns the sealed and full access status. Sealed status can be changed;\n"
	" 'echo <k1> [ <k2> ] >sealed' to unseal and permit greater chip access (k1, k2 in hex)\n"
	" 'echo seal >sealed' to reseal the chip.\n"
	" 'echo <cmd> [ <ctrl> ] >data' to execute data/control commands (cmd/ctrl in hex).\n"
	" 'cat data' to see any results in hex, unsigned, and signed.\n"
	" 'echo <class> [ <offset> [ <length> ] ] [ = <byte> ... ] >dfd' to access flash.\n"
	"    The '=' clause will write bytes (class/offset/length in decimal, bytes in hex).\n"
	" 'cat dfd' returns the contents of flash at 'class,offset,length'.\n"
	" 'cat statistics' returns counts of power supply updates, by reason, and rate per hour.\n"
	" 'echo clear >statistics' to clear statistics.\n"
	" 'cat file.dffs >fs' to update flash on BQ27541.\n"
	" 'cat 'fs' to read status of Flashstream operation.\n";
	seq_puts(seq, help); //seq_printf(seq, "%s", help);
	return 0;
}

static int bq27541_procfs_help_open(struct inode * inode, struct file * file)
{
	return single_open(file, bq27541_procfs_help_read, PDE(inode)->data);
}

static struct file_operations bq27541_procfs_help_fops = {
	.owner = THIS_MODULE,
	.open = bq27541_procfs_help_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

#if defined(CONFIG_BQ27541_STATISTICS)
static int bq27541_procfs_stats_read(struct seq_file * seq, void * offset)
{
	struct bq27541_chip * chip = seq->private;
	static const char * stat_names[] = {
		[BQ27541_STATS_TOTAL_PSY_e] = "TOTAL_PSY",
		[BQ27541_STATS_MIB_e] = "MIB",
		[BQ27541_STATS_INFO_e] = "INFO",
		[BQ27541_STATS_TEMP_e] = "TEMP",
		[BQ27541_STATS_SOC_e] = "SOC",
		[BQ27541_STATS_VSOC_e] = "VSOC",
		[BQ27541_STATS_SOH_e] = "SOH",
		[BQ27541_STATS_STATUS_e] = "STATUS",
		[BQ27541_STATS_I2C_ERR_e] = "I2C_ERR",
		[BQ27541_STATS_BAT_PRES_e] = "BAT_PRES",
		[BQ27541_STATS_HEALTH_e] = "HEALTH",
		[BQ27541_STATS_FYI_e] = "FYI",
	};
	int e = bq27541_get_wall_seconds(chip) - chip->stats.start_time;
	int i, h, m, s;
	h = e / 3600;
	s = e - h * 3600;
	m = s / 60;
	s -= m * 60;
	seq_printf(seq, "ELAPSED_TIME = %d:%02d:%02d\n", h, m, s);
	for (i=0;i < BQ27541_STATS_e_MAX;i++) {
		unsigned long c = chip->stats.count[i];
		unsigned long d = (c * 3600 * 100) / e;
		seq_printf(seq, "%s = %lu (%lu.%02lu)\n",
				stat_names[i], c, d / 100, d % 100);
	}
	return 0;
}

static void bq27541_procfs_stats_clear(struct bq27541_chip * chip)
{
	memset(&chip->stats, 0, sizeof(chip->stats));
	chip->stats.start_time = bq27541_get_wall_seconds(chip);
}

static ssize_t bq27541_procfs_stats_write(struct file * file,
		const char __user * buffer, size_t count, loff_t * ppos)
{
	struct seq_file *seq = file->private_data;
	struct bq27541_chip * chip = seq->private;
	static const char clear_token[] = { 'c', 'l', 'e', 'a', 'r' };
	char buf[sizeof(clear_token)];
	if (count >= sizeof(clear_token) &&
			copy_from_user(buf, buffer, sizeof(clear_token)) == 0 &&
			memcmp(buf, clear_token, sizeof(clear_token)) == 0)
		bq27541_procfs_stats_clear(chip);
	return count;
}

static int bq27541_procfs_stats_open(struct inode * inode, struct file * file)
{
	return single_open(file, bq27541_procfs_stats_read, PDE(inode)->data);
}

static struct file_operations bq27541_procfs_stats_fops = {
	.owner = THIS_MODULE,
	.open = bq27541_procfs_stats_open,
	.read = seq_read,
	.write = bq27541_procfs_stats_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static void bq27541_procfs_init(struct bq27541_chip * chip)
{
	struct proc_dir_entry *dir = proc_mkdir(BQ27541_GG_DEV_NAME, NULL);
	if (dir) {
		proc_create_data("status", S_IFREG | 0400, dir,
				&bq27541_procfs_status_fops, chip);
		proc_create_data("config", S_IFREG | 0400, dir,
				&bq27541_procfs_config_fops, chip);
		proc_create_data("dfd", S_IFREG | 0600, dir,
				&bq27541_procfs_dfd_fops, chip);
		proc_create_data("sealed", S_IFREG | 0600, dir,
				&bq27541_procfs_sealed_fops, chip);
		proc_create_data("data", S_IFREG | 0600, dir,
				&bq27541_procfs_data_fops, chip);
		proc_create_data("help", S_IFREG | 0400, dir,
				&bq27541_procfs_help_fops, chip);
#if defined(CONFIG_BQ27541_STATISTICS)
		proc_create_data("statistics", S_IFREG | 0600, dir,
				&bq27541_procfs_stats_fops, chip);
		bq27541_procfs_stats_clear(chip);
#endif
#if defined(CONFIG_BQ27541_FLASHSTREAM)
		if (chip->status.battery_present) {
			proc_create_data("fs", S_IFREG | 0600, dir,
					&bq27541_procfs_fs_fops, chip);
			chip->flashstream.registered = 1;
		}
#endif
	}
	chip->procfs.class = 0;
	chip->procfs.offset = 0;
	chip->procfs.length = BQ27541_REG_DFD_SIZE;
}

static void bq27541_procfs_deinit(struct bq27541_chip * chip)
{
#if defined(CONFIG_BQ27541_FLASHSTREAM)
	if (chip->flashstream.registered) {
		if (chip->flashstream.acquired) {
			// should not happen!
			// release busy and continue
			chip->flashstream.acquired = 0;
			bq27541_release_busy(chip);
		}
		chip->flashstream.registered = 0;
	}
#endif
}

static void bq27541_pmic_smbb_snapshot(struct bq27541_chip * chip)
{
	u8 vddmax, vddsafe, ibatmax, ibatsafe, vinmin;
	u8 vbattrkl, vbatweak, ibatatca, ibatatcb;
	u8 vbatdet, ttrklmax, tchgmax, wdogtime, tempthresh, iusbmax;

	bq27541_pmic_read(chip, 0x1040, &vddmax, 1);
	bq27541_pmic_read(chip, 0x1041, &vddsafe, 1);
	bq27541_pmic_read(chip, 0x1044, &ibatmax, 1);
	bq27541_pmic_read(chip, 0x1045, &ibatsafe, 1);
	bq27541_pmic_read(chip, 0x1047, &vinmin, 1);
	bq27541_pmic_read(chip, 0x1050, &vbattrkl, 1);
	bq27541_pmic_read(chip, 0x1052, &vbatweak, 1);
	bq27541_pmic_read(chip, 0x1054, &ibatatca, 1);
	bq27541_pmic_read(chip, 0x1055, &ibatatcb, 1);
	bq27541_pmic_read(chip, 0x105D, &vbatdet, 1);
	bq27541_pmic_read(chip, 0x105F, &ttrklmax, 1);
	bq27541_pmic_read(chip, 0x1061, &tchgmax, 1);
	bq27541_pmic_read(chip, 0x1062, &wdogtime, 1);
	bq27541_pmic_read(chip, 0x1066, &tempthresh, 1);
	bq27541_pmic_read(chip, 0x1344, &iusbmax, 1);

	pr_crit("X %4dmv %4dmv %4dmv %4dma %4dma %4dma"
		" %4dmv %4dmv %4dmv %3dma %4dma %4dmv %2dm"
		" %3dm %2ds %3dc %3dc\n",
		bq27541_get_status(chip, BATT_STATUS_VOLTAGE),
		3240 + (int)vddmax * 10,
		3240 + (int)vddsafe * 10,
		100 + ((int)ibatmax & 0x3F) * 50,
		100 + ((int)ibatsafe & 0x3F) * 50,
		iusbmax == 0 ? 100 : iusbmax == 1 ? 150 : iusbmax * 100,
		3400 + ((int)vinmin & 0x3F) * 50,
		2050 + ((int)vbattrkl & 0x0F) * 50,
		2100 + ((int)vbatweak & 0x3F) * 100,
		50 + ((int)ibatatca & 0x0F) * 10,
		100 + ((int)ibatatcb & 0x3F) * 50,
		3240 + ((int)vbatdet & 0x7F) * 20,
		((int)ttrklmax & 0x7F) + 1,
		(((int)tchgmax & 0x7F) + 1) * 4,
		(int)wdogtime & 0x7F,
		75 + ((int)tempthresh >> 4) * 5,
		75 + ((int)tempthresh & 0x0F) * 5
		);
}

/**
 * Take a snapshot of interesting GG data, and write to a log.
 */
static void bq27541_snapshot(struct bq27541_chip * chip)
{
	u8 pmic_pwr, usb_sts;
	char tbuf[32];
	bq27541_get_wall_string(chip, tbuf);
	bq27541_pmic_read(chip, 0x1308, &pmic_pwr, 1);
	bq27541_pmic_read(chip, 0x1309, &usb_sts, 1);
	pr_crit("%s: "
		"V:%d "
		"I:%d "
		"T:%d "
		"SOC:%d "
		"FLG:%04x "
		"FCC:%d "
		"CNT:%lu "
		"NOK:%d "
		"PWR:%02x "
		"USB:%02x "
		"MLI:%d\n",
		tbuf,
		bq27541_get_status(chip, BATT_STATUS_VOLTAGE),
		bq27541_get_status(chip, BATT_STATUS_CURRENT),
		bq27541_get_status(chip, BATT_STATUS_TEMPERATURE),
		bq27541_get_status(chip, BATT_STATUS_CAPACITY),
		/*(unsigned int)*/bq27541_get_status(chip, BATT_STATUS_FLAGS),
		bq27541_get_status(chip, BATT_STATUS_FCC),
		chip->status.count,
		chip->recovery.not_okay,
		pmic_pwr,
		usb_sts,
		bq27541_get_status(chip, BATT_STATUS_MLI)
		);
}

/**
 * The battery is identified by a resistor (Rid) from ground, connected to a
 * 100.0k resistor pulling up to a 1.80v rail, forming a voltage divider. The
 * resistor value, thus the ID, is implied by the voltage reading. The ID
 * voltage reading is in micro-volts (0..1800000)
 *
 * Additionally, it's possible the battery is not installed. The voltage will
 * read nearly that of the rail, since there is a pull up and no battery id
 * resistor pulling down. Return an -ENOENT error to indicate no battery.
 *
 * Vref		1.8V
 * Rp		100k
 *
 * -----------------------------------------------------------------------------
 * V1 batteries are obsolete, and not covered here.
 * -----------------------------------------------------------------------------
 * V2/P1/P2 ID scheme
 *
 * Packs (as of P2)
 *
 * Prefix	VPN			Pack Vendor	Cell Vendor
 * ------	---			-----------	-----------
 * BMC		L83-4951-422-00-4	McNair		Samsung (SDI)
 * BDS or BDL	26S1003(?)		Desay		LGC
 * BSW		S12-M1-C		Sunwoda		Coslight
 *
 * Pack ID resistors
 *
 * The resulting voltages do not form a linear progression, so the scaled ID
 * voltage is simply compared with the center of each interval.
 *
 * ID	Rid	Vid	Cell		Pack		Code
 * --	---	---	----		----		----
 * -1			<missing>
 * 0			<unknown>
 * 10	330k	1.381	<BIF> Smart
 * 1	137k	1.041	Samsung		McNair		MC
 * 2	107k	0.930	ATL		Desay		DS
 * 3	84.5k	0.824	Coslight	Sunwoda		SW
 * 4	66.5k	0.719	LGC		Desay		DL
 * 5	52.3k	0.618
 * 6	41.2k	0.525
 * 7	32.4k	0.440
 * 8	25.5k	0.366
 * 9	20.0k	0.300
 * 11	0k	0.000	<adapter>
 *
 * -----------------------------------------------------------------------------
 * PMIC rev3.0 has a bug. There is a 10k resistor in series with a diode as a
 * permanent pullup on the BATT_ID pin (in parallel with whatever external
 * pullup we have), which corrupts the battery id computation. We'll simply
 * assume Samsung.
 * Update: The v3.1 PMIC fixes the BATT_ID issues, but still identifies as rev3.
 * Added a check for rev3 AND board earlier than P2. Rev 3.1 part or better used
 * on and after P2.
 *
 * -----------------------------------------------------------------------------
 * EVT and later ID scheme
 *
 * The ID mechanism remains the same, but the meaning has changed.
 *
 * Rid    Vid    Vid range    ID
 * -----  ----   ----------   -------------------------
 *               1750..1800   BATTERY_PACK_ID_MISSING
 * 330k   1381   1300..1750   BATTERY_PACK_ID_BIF_SMART
 *               1100..1300   BATTERY_PACK_ID_UNKNOWN
 * 137k   1041    986..1100   BATTERY_PACK_ID_1
 * 107k    930    877.. 986   BATTERY_PACK_ID_2
 * 84.5k   824    772.. 877   BATTERY_PACK_ID_3
 * 66.5k   719    668.. 772   BATTERY_PACK_ID_4
 * 52.3k   618    572.. 668   BATTERY_PACK_ID_5
 * 41.2k   525    483.. 572   BATTERY_PACK_ID_6
 * 32.4k   440    403.. 483   BATTERY_PACK_ID_7
 * 25.5k   366    333.. 403   BATTERY_PACK_ID_8
 * 20.0k   300    271.. 333   BATTERY_PACK_ID_9
 *                 50.. 271   BATTERY_PACK_ID_UNKNOWN
 * 0k        0      0..  50   BATTERY_PACK_ID_ADAPTER

 * The battery ID is used to detect presence in NON-HLOS, and as an implicator
 * of battery system revision, beginning with ID '1' (except EVT, which is '2').
 */
static int bq27541_battery_id(struct bq27541_chip * chip)
{
	int id, rc, ping;
	u8 pmic_rev = 0;

	chip->status.info.battery_id = id = BATTERY_PACK_ID_UNKNOWN;
	chip->status.info.battery_man = chip->status.info.battery_mod = "";
	chip->status.battery_present = 0;

	ping = bq27541_i2c_ping_device(chip);
	if (!ping) pr_err("device ping failed - battery not present?\n");

	bq27541_pmic_read(chip, 0x103, &pmic_rev, 1);
	rc = bq27541_vadc_read(chip, LR_MUX2_BAT_ID, &id);
	if (rc) {
		pr_err("error %d reading battery id\n", rc);
		return rc;
	}
	id = (id + 500) / 1000; // convert to mv

	if (pmic_rev == 3 && ursa_board_revision() < URSA_REVISION_P2) {
		// There's an extra pullup (100k || (10k + diode)). Use default.
		id = 1041;
		pr_err("PMIC rev3 problem - defaulting to ID 1\n");
	}
	if (id > 1750) {
		id = BATTERY_PACK_ID_MISSING;
		pr_err("battery not present?\n");
	} else if (id > 1300) {
		id = BATTERY_PACK_ID_BIF_SMART;
		chip->status.info.battery_man = "<BIF/Smart>";
	} else if (id > 1100) {
		id = BATTERY_PACK_ID_UNKNOWN;
		pr_err("battery unknown!\n");
	}
	else if (id > 986) id = BATTERY_PACK_ID_1;
	else if (id > 877) id = BATTERY_PACK_ID_2;
	else if (id > 772) id = BATTERY_PACK_ID_3;
	else if (id > 668) id = BATTERY_PACK_ID_4;
	else if (id > 572) id = BATTERY_PACK_ID_5;
	else if (id > 483) id = BATTERY_PACK_ID_6;
	else if (id > 403) id = BATTERY_PACK_ID_7;
	else if (id > 333) id = BATTERY_PACK_ID_8;
	else if (id > 271) id = BATTERY_PACK_ID_9;
	else if (id > 50) {
		id = BATTERY_PACK_ID_UNKNOWN;
		pr_err("battery unknown!\n");
	} else {
		id = BATTERY_PACK_ID_ADAPTER;
		chip->status.info.battery_man = "<adapter>";
	}

	if (ursa_board_revision() < URSA_REVISION_EVT) {
		if (id == 1)
			chip->status.info.battery_man = "Samsung/McNair";
		else if (id == 2)
			chip->status.info.battery_man = "ATL/Desay";
		else if (id == 3)
			chip->status.info.battery_man = "Coslight/Sunwoda";
		else if (id == 4)
			chip->status.info.battery_man = "LGC/Desay";
	}

	pr_crit("mode = %d, board = %d, batt_id = %d, batt_man = \"%s\"\n",
			chip->bootmode, ursa_board_revision(), id,
			chip->status.info.battery_man);
	chip->status.info.battery_id = id;
	chip->status.battery_present = (id > 0) && ping;
	return 0;
}

/**
 * Read the manufacturers info blocks and decode the contents. Observe version
 * byte. As of v2, all versions are backward compatible, so data is extracted
 * from the mibs from newest to oldest. If at any point the data is not
 * appropriate, extraction stops, a message is generated, and the is_loaded flag
 * is not set. However, data extracted to that point is retained.
 *
 * There are two 32 byte MIBs, A and B. A is readonly (when part is sealed). B
 * is always read/write. They contain packed structures. The first byte of A is
 * the MIB format/version. The remaining bytes are either UTFZ, or integers,
 * sized and signed as indicated, and stored big-endian.
 *
 * -----------------------------------------------------------------------------
 * Version 0
 *	// MIB_A:
 *	0	u8	version;		// 0
 *	1-31					// reserved
 *	// MIB_B:
 *	0-31					// reserved
 *
 * -----------------------------------------------------------------------------
 * Version 1
 *	// MIB_A:
 *	0	u8	version;		// 1
 *	1-2	u16	capacity_mah;		// eg. 2410
 *	3-4	u16	charge_mv;		// eg. 4350
 *	5	u8	charge_rate;		// eg. 15 is 1.5C
 *	6-7	u16	taper_ma;		// eg. 80
 *	8-15					// reserved
 *	16-31	utfz	pack_serial_number	// eg. "BDS338172B3A"
 *	// MIB_B: (EVT)
 *	0-15					// reserved
 *	16-31	utfz	pcm_serial_number	// proprietary
 *	// MIB_B: (pre-EVT)
 *	0-15	utfz	pack_serial_number	// instead of A.16
 *	16-31					// reserved
 * -----------------------------------------------------------------------------
 * NOTE: Some (pre-EVT) cells have the pack serial number right justified at
 * A.16, left filled with '\0'. Will strip leading and trailing '\0' from string
 * fields, assuming any of the strings may be stored the same way.
 */

#define MIB_SIZE			32		// size of a MIB block
#define MIB_A_BLK_SEALED		1		// MIB_A block, sealed
#define MIB_B_BLK_SEALED		2		// MIB_B block, sealed
#define MIB_VERSION			0		// version
#define MIB_V1_CAPACITY			1		// capacity
#define MIB_V1_CHARGE			3		// charge
#define MIB_V1_CHARGE_RATE		5		// charge_rate
#define MIB_V1_TAPER			6		// taper
#define MIB_V1_PACK_SERIAL		16		// pack serial
#define MIB_V1_PACK_SERIAL_ALT		32		// alternate pack serial
#define MIB_V1_PCM_SERIAL		48		// pcm serial
#define MIB_V1_SERIAL_SIZE		16		// size of a serial

static int bq27541_strip_and_copy_serial(u8 * dst, u8 * src, int length)
{
	int len = 0;
	while (length-- > 0) {
		if (*src >= 32 && *src <= 126) {
			*dst++ = *src;
			len++;
		}
		src++;
	}
	*dst = 0;
	return len;
}

static void bq27541_read_mib(struct bq27541_chip * chip)
{
	u8 mib[MIB_SIZE * 2];
	int len, capacity_mah, charge_mv, charge_rate, taper_ma;

	if (bq27541_is_chip_sealed(chip)) {
		if (bq27541_i2c_write_byte(chip, BQ27541_REG_DFBLK,
				MIB_A_BLK_SEALED))
			goto mib_error;
		msleep(2);
		if (bq27541_i2c_read(chip, BQ27541_REG_DFD, mib, MIB_SIZE))
			goto mib_error;
		if (bq27541_i2c_write_byte(chip, BQ27541_REG_DFBLK,
				MIB_B_BLK_SEALED))
			goto mib_error;
		msleep(2);
		if (bq27541_i2c_read(chip, BQ27541_REG_DFD,
				mib + MIB_SIZE, MIB_SIZE))
			goto mib_error;
	} else {
		if (bq27541_dfd_read(chip, BQ27541_DFD_CLASS_MIB,
				0, mib, sizeof(mib)))
			goto mib_error;
	}

	chip->status.mib.pack_serial_number[0] = 0;
	chip->status.mib.pcm_serial_number[0] = 0;
	switch (mib[MIB_VERSION]) {
	case 2: // some packs may be mis-versioned - fall through to v1
	case 1:
		if (!bq27541_strip_and_copy_serial(
				chip->status.mib.pack_serial_number,
				mib + MIB_V1_PACK_SERIAL,
				MIB_V1_SERIAL_SIZE)) {
			bq27541_strip_and_copy_serial(
					chip->status.mib.pack_serial_number,
					mib + MIB_V1_PACK_SERIAL_ALT,
					MIB_V1_SERIAL_SIZE);
		}
		bq27541_strip_and_copy_serial(
				chip->status.mib.pcm_serial_number,
				mib + MIB_V1_PCM_SERIAL,
				MIB_V1_SERIAL_SIZE);

		capacity_mah = get_unaligned_be16(mib + MIB_V1_CAPACITY);
		charge_mv = get_unaligned_be16(mib + MIB_V1_CHARGE);
		charge_rate = mib[MIB_V1_CHARGE_RATE];
		taper_ma = get_unaligned_be16(mib + MIB_V1_TAPER);
		if (capacity_mah < BQ27541_DCAP_MAH_MIN_LIMIT ||
				capacity_mah > BQ27541_DCAP_MAH_MAX_LIMIT ||
				charge_mv < BQ27541_MIN_MV_LIMIT ||
				charge_mv > BQ27541_MAX_MV_LIMIT ||
				charge_rate < BQ27541_CHARGE_RATE_MIN_LIMIT ||
				charge_rate > BQ27541_CHARGE_RATE_MAX_LIMIT ||
				taper_ma > BQ27541_TAPER_MA_LIMIT)
			goto goofy_mib_data;

		chip->status.mib.capacity_mah = capacity_mah;
		chip->status.mib.charge_mv = charge_mv;
		chip->status.mib.charge_rate = charge_rate;
		chip->status.mib.taper_ma = taper_ma;
	case 0:
		break;
	default:
		pr_err("undefined mib version\n");
		return;
	}

	chip->status.mib.is_loaded = 1;
	chip->status.status_delta |= BQ27541_DELTA_MIB;

	// decode manufacturer and model
	if (chip->status.mib.pack_serial_number[0] == 0 ||
			chip->status.mib.pack_serial_number[0] != 'B')
		return;

	len = strlen(chip->status.mib.pack_serial_number);
	if (len < 6)
		return;

	if (ursa_board_revision() < URSA_REVISION_EVT) {
		// Based on just the prefix: BD, BM, BS
		char pre = chip->status.mib.pack_serial_number[1];
		if (pre == 'D')
			chip->status.info.battery_man = "LGC/Desay";
		else if (pre == 'M')
			chip->status.info.battery_man = "Samsung/McNair";
		else if (pre == 'S')
			chip->status.info.battery_man = "Coslight/Sunwoda";
	} else {
/*
 * Amazon Part Number: 58-000068
 * Model Code: B3 (3rd and 2nd characters from the end of the serial number)
 * Revision Code: last character of the serial number.
 * Serial	Rev	VPN		Pack		Cell
 * Prefix	Code			Vendor		Vendor
 * ------	----	---		------		------
 * BDS		1	26S1003-A	Desay		ATL
 * BDS		2	26S1003-G	Desay		LGC
 * BSW		1	S12-M1-D	Sunwoda		Samsung
 * BSW		2	S12-M1-C	Sunwoda		Coslight
 */
		char pre = chip->status.mib.pack_serial_number[1];
		char rev = chip->status.mib.pack_serial_number[len-1];
		if (pre == 'D') {
			if (rev == '1')
				chip->status.info.battery_man = "ATL/Desay";
			else if (rev == '2')
				chip->status.info.battery_man = "LGC/Desay";
		} else if (pre == 'S') {
			if (rev == '1')
				chip->status.info.battery_man =
						"Samsung/Sunwoda";
			else if (rev == '2')
				chip->status.info.battery_man =
						"Coslight/Sunwoda";
		}
	}

	if (chip->status.info.battery_man[0]) {
		static char model[3];
		model[0] = chip->status.mib.pack_serial_number[len-3];
		model[1] = chip->status.mib.pack_serial_number[len-2];
		model[2] = 0;
		chip->status.info.battery_mod = model;
	}

	return;

goofy_mib_data:
	bq27541_hex_dump("goofy mib data", mib, sizeof(mib));
	return;
mib_error:
	bq27541_release_check(chip);
	pr_err("problems reading mib\n");
	return;
}

/**
 * Read GG identity information. Read once, since it's constant data.
 */
static void bq27541_read_info(struct bq27541_chip * chip)
{
	if (bq27541_i2c_read_ctrl_word(chip, BQ27541_SUBCMD_DEVICE_TYPE,
				&chip->status.info.dev_type) ||
			bq27541_i2c_read_ctrl_word(chip,
					BQ27541_SUBCMD_FW_VERSION,
					&chip->status.info.fw_ver) ||
			bq27541_i2c_read_ctrl_word(chip,
					BQ27541_SUBCMD_HW_VERSION,
					&chip->status.info.hw_ver) ||
			bq27541_i2c_read_ctrl_word(chip,
					BQ27541_SUBCMD_CHEM_ID,
					&chip->status.info.chem_id) ||
			bq27541_i2c_read_ctrl_word(chip,
					BQ27541_SUBCMD_DF_VERSION,
					&chip->status.info.df_ver) ||
			bq27541_i2c_read_word(chip,
					BQ27541_REG_PCR,
					&chip->status.info.pack_config) ||
			bq27541_i2c_read_word(chip,
					BQ27541_REG_DCAP,
					&chip->status.info.design_cap)) {
		bq27541_release_check(chip);
		return;
	}
	chip->status.info.is_loaded = 1;
	chip->status.status_delta |= BQ27541_DELTA_INFO;
	chip->status.info.no_match =
			chip->status.info.dev_type != BQ27541_DEVICE_TYPE ||
			chip->status.info.fw_ver != BQ27541_FW_VERSION ||
			(chip->status.info.hw_ver &&
			chip->status.info.hw_ver != BQ27541_HW_VERSION);
}

/**
 * Rough estimate of SOC from vBat. A ratiometric percentage of vBat between
 * vShutoff and vCharge (vMin and vMax). Should we use a non-linear conversion?
 */
static int bq27541_mv_to_vsoc(struct bq27541_chip * chip, int mv)
{
	int min = bq27541_get_shutoff_mv(chip);
	int max = chip->status.mib.charge_mv;
	int vsoc;
	if (min >= max) {
		vsoc = 50;
	} else {
		vsoc = ((mv - min) * 1005) / ((max - min) * 10);
		if (vsoc < 0) vsoc = 0;
		else if (vsoc > 100) vsoc = 100;
		// apply curve here
	}
	return vsoc;
}

/**
 * The procedure here is;
 * 1. Copy from status to local variables.
 * 2. Update the local variables from GG or fabricated values.
 * 3. Process the local variables, making them consistent and sensible.
 * 4. Determine what has changed, and copy from local variables back to status.
 * If I2C fails, bail as soon as possible after I2C recovery disables the
 * battery. Bailing from here is unlikely, since the I2C will probably already
 * have failed (if it will) and recovery will have had a chance to disable the
 * battery before we get here.
 */
static void bq27541_update_status(struct bq27541_chip * chip)
{
	int cntl_status, temp , volt, flags, nac, rm, fcc, ai, cc, soc;
	int vsoc, soh, status, health, mli, vbat;

	bq27541_acquire_cache(chip);
	cntl_status = chip->status.cache.cntl_status;
	temp = chip->status.cache.temp;
	volt = chip->status.cache.volt;
	flags = chip->status.cache.flags;
	nac = chip->status.cache.nac;
	rm = chip->status.cache.rm;
	fcc = chip->status.cache.fcc;
	ai = chip->status.cache.ai;
	cc = chip->status.cache.cc;
	soc = chip->status.cache.soc;
	vsoc = chip->status.cache.vsoc;
	soh = chip->status.cache.soh;
	status = chip->status.cache.status;
	health = chip->status.cache.health;
	mli = chip->status.cache.mli;
	bq27541_release_cache(chip);

	//==== Step 1 - read data from chip

	vbat = 0;
	bq27541_vadc_read(chip, VBAT_SNS, &vbat);
	vbat /= 1000;

	do {
		if (!chip->status.battery_present)
			break;
		if (bq27541_i2c_read_ctrl_word(chip,
				BQ27541_SUBCMD_CNTL_STATUS, &cntl_status) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_TEMP, &temp) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_VOLT, &volt) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_FLAGS, &flags) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_NAC, &nac) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_RM, &rm) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_FCC, &fcc) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_AI, &ai) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_CC, &cc) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_SOC, &soc) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_SOH, &soh) ||
				bq27541_i2c_read_word(chip,
						BQ27541_REG_MLI, &mli)) {
			bq27541_release_check(chip);
			break;
		}

		temp = DK_TO_DC(temp);
		ai = -(int)(short)ai; // negated for OS compatibility
		mli = (int)(short)mli;
		vsoc = bq27541_mv_to_vsoc(chip, volt);
		if (vbat == 0) vbat = volt;

		/* Do not allow SOC to be 100%, unless it makes sense.
		 * While the GG is learning FCC, RM will increase to, and then
		 * drive up FCC, near EOC. During this time, SOC will be 100%,
		 * because it is simply RM/FCC. However, SOC is not really 100%,
		 * so if it is in this state, cap it at 99%.
		 */
		if (soc == 100 && !(flags & BQ27541_FLAG_FC))
			soc = 99;
	} while (0);

	//==== Step 2 - if the chip communication has failed, estimate data

	if (!chip->status.battery_present) {
		// read from PMIC and just make things look reasonable
		cntl_status = flags = cc = 0;
		fcc = chip->status.mib.capacity_mah;
		soh = 100;
		temp = 250;
		ai = 0;
		bq27541_vadc_read(chip, LR_MUX1_BATT_THERM, &temp);
		bq27541_iadc_read(chip, EXTERNAL_RSENSE, &ai);
		ai = -ai / 1000;
		volt = vbat ? vbat : (chip->status.mib.charge_mv -
					bq27541_get_shutoff_mv(chip)) / 2;
		soc = vsoc = bq27541_mv_to_vsoc(chip, volt);
		nac = rm = (soc * fcc) / 100;
		mli = 0;
	}

	/* It's difficult to make 0% SOC coincide with shutoff_mv, under
	 * varying loads, so we've decided to report 0% SOC, only if vbat
	 * (from PMIC) drops below the appropriate shutoff_mv. If SOC is 0%,
	 * and vbat >= shutoff_mv, then hold SOC at 1%, until vbat drops below
	 * shutoff_mv for <n+> samples.
	 * Pros:
	 * - allows time on battery to be extended under light loads
	 * Cons:
	 * - under heavy load, SOC can drop sharply to 0%, from well above 0%
	 * - under light load, SOC can be held at 1% for a long time
	 * For smoothing, simply wait for <n+> consecutive readings below
	 * shutoff, then declare 0% SOC.
	 */
	/**
	 * Addendum: No longer override the GG SOC at 0%. Doing so allowed low
	 * loads to fully deplete the cell, but a high load to trip the lower
	 * guard limit, bypassing graceful shut down.
	 * if (soc == 0) soc = 1;
	 * if (vsoc == 0) vsoc = 1;
	 */
	if (vbat >= bq27541_get_shutoff_mv(chip)) {
		chip->status.zero_soc_count = 0;
	} else if (chip->status.zero_soc_count++ >=
			BQ27541_DEFAULT_SHUTOFF_COUNT) {
		soc = vsoc = 0;
	}

	//==== Step 3 - process new data - update cache - update delta bits

	/* Determine status based on AI and capped SOC.
	 * If AI > threshold, then DISCHARGING.
	 * If AI < -threshold, then CHARGING.
	 * If capped SOC == 100, then FULL.
	 * Else, NOT_CHARGING.
	 */
	if (ai > BQ27541_AI_DISCHARGE_THRESHOLD)
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	else if (ai < -BQ27541_AI_CHARGE_THRESHOLD)
		status = POWER_SUPPLY_STATUS_CHARGING;
	else if (soc == 100)
		status = POWER_SUPPLY_STATUS_FULL;
	else
		status = POWER_SUPPLY_STATUS_NOT_CHARGING;

	/* High temperature charge termination check.
	 * If warm_charge_done is true, and (Tbat < 44degC or soc <=
	 * warm_charge_resume_soc), then clear warm_charge_done.
	 * If warm_charge_done is false, and (Tbat >= 45degC and Vbat > 4040mv
	 * and Ibat < 100ma), for <n> consecutive samples, then
	 * set warm_charge_done. Also, set warm_charge_resume_soc to SOC - 3.
	 * Reflect warm_charge_done as POWER_SUPPLY_HEALTH_OVERVOLTAGE, which
	 * will inhibit charging.
	 */
	if (chip->status.warm_charge.done) {
		if (temp < BQ27541_WARM_CHG_RESUME_TEMP ||
				soc <= chip->status.warm_charge.resume_soc) {
			chip->status.warm_charge.done = 0;
			chip->status.warm_charge.done_count = 0;
		}
	}
	if (!chip->status.warm_charge.done) {
		if (temp >= BQ27541_WARM_CHG_DONE_TEMP &&
				volt > BQ27541_WARM_CHG_DONE_VOLT &&
				ai > -BQ27541_WARM_CHG_DONE_CURRENT) {
			if (++chip->status.warm_charge.done_count >=
					BQ27541_WARM_CHG_DONE_COUNT) {
				chip->status.warm_charge.done = 1;
				chip->status.warm_charge.resume_soc = soc -
						BQ27541_WARM_CHG_RESUME_SOC;
			}
		} else {
			chip->status.warm_charge.done_count = 0;
		}
	}

	/* Health includes battery presence. Limit sometimes overides GG for
	 * testing. For testing and verification, some GG limits may be
	 * overidden by DTS (or device attribute) limit.
	 *
	 * POWER_SUPPLY_HEALTH_UNSPEC_FAILURE	fault, battery not present
	 * POWER_SUPPLY_HEALTH_OVERHEAT		temp too high, inhibit high
	 * POWER_SUPPLY_HEALTH_OVERVOLTAGE	voltage too high, warm chg done
	 * POWER_SUPPLY_HEALTH_DEAD		voltage too low
	 * POWER_SUPPLY_HEALTH_COLD		temp too low, inhibit low
	 * POWER_SUPPLY_HEALTH_GOOD		no problems
	 */
	do {
		// fault, or battery not present
		if (!bq27541_is_battery_present(chip) || (flags &
				(BQ27541_FLAG_ISD | BQ27541_FLAG_TDD))) {
			health = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			break;
		}
		// <temp_hi_dk> limit overides GG over temp, or inhibit hot
		if (chip->status.limits.temp_hi_dk) {
			if (temp >= DK_TO_DC(chip->status.limits.temp_hi_dk)) {
				health = POWER_SUPPLY_HEALTH_OVERHEAT;
				break;
			}
		} else if ((flags & (BQ27541_FLAG_OTC | BQ27541_FLAG_OTD)) ||
				((flags & (BQ27541_FLAG_CHG_INH)) &&
					temp > BQ27541_ROOM_TEMP_DC)) {
			health = POWER_SUPPLY_HEALTH_OVERHEAT;
			break;
		}
		// High temperature charge termination
		if (chip->status.warm_charge.done) {
			health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
			break;
		}
		// <volt_hi_mv> limit overides GG
		if (chip->status.limits.volt_hi_mv) {
			if (volt >= chip->status.limits.volt_hi_mv) {
				health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
				break;
			}
		} else if (flags & (BQ27541_FLAG_BATHI)) {
			health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
			break;
		}
		// <volt_lo_mv> limit overides GG
		if (chip->status.limits.volt_lo_mv) {
			if (volt <= chip->status.limits.volt_lo_mv) {
				health = POWER_SUPPLY_HEALTH_DEAD;
				break;
			}
		} else if (flags & (BQ27541_FLAG_BATLOW)) {
			health = POWER_SUPPLY_HEALTH_DEAD;
			break;
		}
		// <temp_lo_dk> limit overides GG inhibit low
		if (chip->status.limits.temp_lo_dk) {
			if (temp <= DK_TO_DC(chip->status.limits.temp_lo_dk)) {
				health = POWER_SUPPLY_HEALTH_COLD;
				break;
			}
		} else if ((flags & (BQ27541_FLAG_CHG_INH)) &&
				temp < BQ27541_ROOM_TEMP_DC) {
			health = POWER_SUPPLY_HEALTH_COLD;
			break;
		}
		health = POWER_SUPPLY_HEALTH_GOOD;
	} while (0);

	/* Check to see which cached data items may have changed.
	 * Set delta bits accordingly.
	 */
	bq27541_acquire_cache(chip);
	if (cntl_status != chip->status.cache.cntl_status) {
		chip->status.cache.cntl_status = cntl_status;
		chip->status.status_delta |= BQ27541_DELTA_CNTL_STATUS;
	}
	if (temp != chip->status.cache.temp) {
		chip->status.cache.temp = temp;
		chip->status.status_delta |= BQ27541_DELTA_TEMP;
	}
	if (volt != chip->status.cache.volt) {
		chip->status.cache.volt = volt;
		chip->status.status_delta |= BQ27541_DELTA_VOLT;
	}
	chip->status.cache.flags_delta = chip->status.cache.flags ^ flags;
	if (chip->status.cache.flags_delta) {
		chip->status.cache.flags = flags;
		chip->status.status_delta |= BQ27541_DELTA_FLAGS;
	}
	if (nac != chip->status.cache.nac) {
		chip->status.cache.nac = nac;
		chip->status.status_delta |= BQ27541_DELTA_NAC;
	}
	if (rm != chip->status.cache.rm) {
		chip->status.cache.rm = rm;
		chip->status.status_delta |= BQ27541_DELTA_RM;
	}
	if (fcc != chip->status.cache.fcc) {
		chip->status.cache.fcc = fcc;
		chip->status.status_delta |= BQ27541_DELTA_FCC;
	}
	if (ai != chip->status.cache.ai) {
		chip->status.cache.ai = ai;
		chip->status.status_delta |= BQ27541_DELTA_AI;
	}
	if (cc != chip->status.cache.cc) {
		chip->status.cache.cc = cc;
		chip->status.status_delta |= BQ27541_DELTA_CC;
	}
	if (soc != chip->status.cache.soc) {
		chip->status.cache.soc = soc;
		chip->status.status_delta |= BQ27541_DELTA_SOC;
	}
	if (vsoc != chip->status.cache.vsoc) {
		chip->status.cache.vsoc = vsoc;
		chip->status.status_delta |= BQ27541_DELTA_VSOC;
	}
	if (soh != chip->status.cache.soh) {
		chip->status.cache.soh = soh;
		chip->status.status_delta |= BQ27541_DELTA_SOH;
	}
	if (status != chip->status.cache.status) {
		chip->status.cache.status = status;
		chip->status.status_delta |= BQ27541_DELTA_STATUS;
	}
	if (health != chip->status.cache.health) {
		chip->status.cache.health = health;
		chip->status.status_delta |= BQ27541_DELTA_HEALTH;
	}
	if (mli != chip->status.cache.mli) {
		chip->status.cache.mli = mli;
		chip->status.status_delta |= BQ27541_DELTA_MLI;
	}
	bq27541_release_cache(chip);

	chip->status.count++;
}

/**
 * Respond to the status delta. Can detect voltage, current and temperature
 * excursions. Can deduce charge is either complete, or required.
 * Presently only generates log entries, and returns notification flag, if
 * power infrastruction should be notified.
 */
static int bq27541_process_status(struct bq27541_chip * chip)
{
	unsigned int flags, flags_delta;
	int ai, soc, notify = 0;

	bq27541_acquire_cache(chip);
	flags = (unsigned int)chip->status.cache.flags;
	flags_delta = (unsigned int)chip->status.cache.flags_delta;
	ai = chip->status.cache.ai;
	soc = chip->status.cache.soc;
	bq27541_release_cache(chip);

	if (flags_delta & BQ27541_FLAG_FC) {
		pr_info("FYI: BATTERY FC %sSET\n",
				(flags & BQ27541_FLAG_FC) ? "" : "NOT ");
	}
	if (flags_delta & BQ27541_FLAG_CHG_INH) {
		pr_info("FYI: BATTERY CHARGING %sINHIBITED\n",
				(flags & BQ27541_FLAG_CHG_INH) ? "" : "NOT ");
	}
	if (flags_delta & (BQ27541_FLAG_OTD | BQ27541_FLAG_OTC)) {
		pr_info("FYI: BATTERY %s\n",
				(flags & (BQ27541_FLAG_OTD | BQ27541_FLAG_OTC))
				? "IS HOT" : "TEMP OKAY");
	}
	if (flags_delta & (BQ27541_FLAG_BATLOW | BQ27541_FLAG_BATHI)) {
		pr_info("FYI: BATTERY VOLTAGE %s\n",
				(flags & BQ27541_FLAG_BATLOW) ? "LOW" :
				(flags & BQ27541_FLAG_BATHI) ? "HIGH" :
				"OKAY");
	}
	if (flags_delta & (BQ27541_FLAG_TDD | BQ27541_FLAG_ISD)) {
		if (flags & (BQ27541_FLAG_TDD | BQ27541_FLAG_ISD)) {
			pr_info("FYI: BATTERY FAULT:%s%s\n",
				(flags & BQ27541_FLAG_TDD) ? " TAB" : "",
				(flags & BQ27541_FLAG_ISD) ? " SHORT" : ""
				);
		} else {
			pr_info("FYI: BATTERY FAULT CLEARED\n");
		}
	}
	if (flags_delta & BQ27541_FLAG_CHG) {
		pr_info("FYI: %sOKAY TO START A NEW CHARGE CYCLE\n",
				(flags & BQ27541_FLAG_CHG) ? "" : "NOT ");
	}
	if (soc == 100) {
		if (chip->status.full_count++ > 1 &&
				ai < -BQ27541_AI_CHARGE_THRESHOLD) {
			notify = 1;
			pr_info("FYI: BATTERY FULL - STOP CHARGING\n");
		}
	} else {
		chip->status.full_count = 0;
	}
	return notify ? BQ27541_DELTA_FYI : 0;
}

static void bq27541_periodic_snapshot(struct bq27541_chip * chip,
		int reschedule)
{
	unsigned long now = get_seconds();

	if (chip->status.snapshot.next && now >= chip->status.snapshot.next) {
		bq27541_snapshot(chip);
		reschedule = chip->status.snapshot.next = 0;
	}
	if (reschedule || chip->status.snapshot.next == 0) {
		chip->status.snapshot.next = chip->status.snapshot.seconds ?
				now + chip->status.snapshot.seconds : 0;
	}
}

static void bq27541_status_reschedule(struct bq27541_chip * chip)
{
	if (chip->status.wake_seconds > 0)
		schedule_delayed_work(&chip->status.work,
				(HZ)*chip->status.wake_seconds);
}

static void bq27541_try_recovery(struct bq27541_chip * chip)
{
	unsigned long now = get_seconds();
	if (chip->recovery.next_ping <= now) {
		chip->recovery.next_ping = bq27541_i2c_recovery(chip) ? 0 :
				now + chip->status.sleep_real_seconds;
	}
}

/**
 * Monitor charging status. Charging is indicated by status ==
 * POWER_SUPPLY_STATUS_CHARGING, else it is considered discharging. When status
 * changes, record the episode, and initialise for a new episode.
 *
 * NOTE: Special case - if SOC reaches zero, terminate this discharge episode
 * now, so it gets recorded prior to shutting down.
 */
static int bq27541_charge_monitor(struct bq27541_chip * chip, int force)
{
	int charging, abs_ai, soc, notify = 0;

	charging = bq27541_get_status(chip, BATT_STATUS_STATUS) ==
			POWER_SUPPLY_STATUS_CHARGING;
	soc = bq27541_get_status(chip, BATT_STATUS_CAPACITY);
	if ((charging != chip->charge_monitor.charging) || force ||
			(!charging && soc == 0)) {
		unsigned long now = bq27541_get_wall_seconds(chip);
		int duration = now - chip->charge_monitor.start_time;
		int delta_soc = soc - chip->charge_monitor.start_soc;
		if (!charging) delta_soc = -delta_soc;

		if (chip->charge_monitor.start_time && duration && delta_soc) {
			char buf[256];
			int ai_avg = chip->charge_monitor.ai_sum /
					chip->charge_monitor.ai_count;
			snprintf(buf, sizeof(buf),
					"%scharge:def:"
					"duration=%d;TI;1"
					",start_soc=%d;CT;1"
					",stop_soc=%d;CT;1"
					",delta_soc=%d;CT;1"
					",avg_current=%d;CT;1"
					",max_current=%d;CT;1"
					",skipped=%d;CT;1"
//				",start_reason=%d;CT;1" resume,usb,health
//				",stop_reason=%d;CT;1" full,usb,health
					":NR",
					chip->charge_monitor.charging
							? "" : "dis",
					duration,
					chip->charge_monitor.start_soc,
					soc,
					delta_soc,
					ai_avg,
					chip->charge_monitor.ai_max,
					chip->charge_monitor.skip_count);
			bq27541_log_metric(BQ27541_GG_DEV_NAME, buf);
			chip->charge_monitor.skip_count = 0;
		} else {
			chip->charge_monitor.skip_count++;
		}
		// initialize new episode
		chip->charge_monitor.charging = charging;
		chip->charge_monitor.start_time = now;
		chip->charge_monitor.start_soc = soc;
		chip->charge_monitor.ai_sum = 0;
		chip->charge_monitor.ai_count = 0;
		chip->charge_monitor.ai_max = 0;
	}

	abs_ai = bq27541_get_status(chip, BATT_STATUS_CURRENT);
	if (charging) abs_ai = -abs_ai;
	chip->charge_monitor.ai_sum += abs_ai;
	chip->charge_monitor.ai_count++;
	if (abs_ai > chip->charge_monitor.ai_max)
		chip->charge_monitor.ai_max = abs_ai;

	return notify;
}

/**
 * Monitor screen state. Report SOC delta and time, while the screen was off.
 * There is an optional delay for the 'on' transition, to avoid spurious screen
 * wake ups. To report the spurious wake ups, set the delay to 0, and a metric
 * will be generated every time the screen comes on. Otherwise, set the delay,
 * as number of GG status cycles, and a new screen state will require that many
 * cycles to declare screen-on.
 */
static int bq27541_screen_monitor(struct bq27541_chip * chip, int force)
{
	int on = bq27541_is_display_on();

	// if no change, check for spurious trigger, then leave
	if (on == chip->screen_monitor.on) {
		if (chip->screen_monitor.delay) {
			chip->screen_monitor.delay = 0;
			chip->screen_monitor.spurious++;
		}
		return 0;
	}

	// if first time with change, sample soc and time
	if (chip->screen_monitor.delay == 0) {
		chip->screen_monitor.temp_soc = bq27541_get_status(chip,
				BATT_STATUS_CAPACITY);
		chip->screen_monitor.temp_time = bq27541_get_wall_seconds(chip);
	}

	// if delay not met, leave (only delay on transition)
	if (on && ++chip->screen_monitor.delay <= BQ27541_SCREEN_ON_DELAY &&
			!force)
		return 0;

	// if new state is on, we've been off, so report screen-off metric
	if (on && chip->screen_monitor.time) {
		int delta_soc = chip->screen_monitor.temp_soc -
				chip->screen_monitor.soc;
		unsigned long elapsed_ms = 1000 *
				(chip->screen_monitor.temp_time -
					chip->screen_monitor.time);
		char buf[128];
		snprintf(buf, sizeof(buf),
				"screen_off_drain:def:"
				"value=%d;CT;1"
				",elapsed=%lu;TI;1"
				",spurious=%d;CT;1:NR",
				delta_soc, elapsed_ms,
				chip->screen_monitor.spurious);
		bq27541_log_metric("drain_metrics", buf);
	}

	// set new state, copy times, and leave
	chip->screen_monitor.on = on;
	chip->screen_monitor.delay = 0;
	chip->screen_monitor.spurious = 0;
	chip->screen_monitor.soc = chip->screen_monitor.temp_soc;
	chip->screen_monitor.time = chip->screen_monitor.temp_time;
	return 0;
}

/**
 * Check safety limits - may shut down the device
 */
static int bq27541_check_limits(struct bq27541_chip * chip, int status_delta)
{
	int notify_mask = 0;

	// check temp - power off if exceeded
	if (chip->status.limits.temp_hi_shutdown_dk) {
		int temp = bq27541_get_status(chip, BATT_STATUS_TEMPERATURE);
		if (temp > DK_TO_DC(chip->status.limits.temp_hi_shutdown_dk)) {
			char buf[128];
			bq27541_snapshot(chip);
			snprintf(buf, sizeof(buf),
					BQ27541_GG_DEV_NAME ":def:"
					"thermal_shutdown=1;CT;1,"
					"temp=%d;TI;1:NR",
					temp/10);
			bq27541_log_metric("battery", buf);
			bq27541_system_power_off(
					"critical battery temperature", 1);
		}
	}

	// check voltage - orderly shutdown if exceeded
	if (chip->status.limits.volt_lo_shutdown_mv) {
		int volt = bq27541_get_status(chip, BATT_STATUS_VOLTAGE);
		if (volt < chip->status.limits.volt_lo_shutdown_mv) {
			char buf[128];
			bq27541_snapshot(chip);
			snprintf(buf, sizeof(buf),
					BQ27541_GG_DEV_NAME ":def:"
					"critical_shutdown=1;CT;1:NR");
			bq27541_log_metric("battery", buf);
			bq27541_system_power_off("critical battery voltage", 0);
		}
	}

	// check soc
	if (bq27541_use_voltage_soc(chip)) {
		if (status_delta & BQ27541_DELTA_VSOC)
			notify_mask = BQ27541_DELTA_VSOC;
	} else {
		if (status_delta & BQ27541_DELTA_SOC) {
			char buf[128];
			int soc = bq27541_get_status(chip,
					BATT_STATUS_CAPACITY);
			notify_mask = BQ27541_DELTA_SOC;
			if (soc == 100 || soc == 0) {
				bq27541_snapshot(chip);
				if (soc == 0) {
					snprintf(buf, sizeof(buf),
						"soc_zero:def:"
						"voltage=%d;CT;1:NR",
						bq27541_get_status(chip,
							BATT_STATUS_VOLTAGE));
				} else {
					snprintf(buf, sizeof(buf),
						"soc_full:def:"
						"fcc=%d;CT;1:NR",
						bq27541_get_status(chip,
							BATT_STATUS_FCC));
				}
				bq27541_log_metric(BQ27541_GG_DEV_NAME, buf);
			}
		}
	}
	return notify_mask;
}

/**
 * Queries the gas gauge chip on the battery pack for info and status. If no
 * battery pack, it will make do with temp, volt, and ai from PMIC, and compute
 * other stuff. If data has changed, will call bq27541_bms_supply_changed().
 */
static void bq27541_status_work_do_work(struct bq27541_chip * chip)
{
	int status_delta, notify_mask = 0;

	if (chip->status.pmic_snapshot)
		bq27541_pmic_smbb_snapshot(chip);

	bq27541_acquire_busy(chip);

	if (chip->recovery.not_okay)
		bq27541_try_recovery(chip);

	/**
	 * The status.status_delta field should start out clear, since it was
	 * cleared before leaving the last status work. However, it may have
	 * accumulated some bits since then. Set any deferred bits.
	 */
	chip->status.status_delta |= chip->status.deferred_status_delta;
	chip->status.deferred_status_delta = 0;

	if (chip->status.battery_present && !chip->status.mib.is_loaded)
		bq27541_read_mib(chip);

	if (chip->status.battery_present && !chip->status.info.is_loaded)
		bq27541_read_info(chip);

	if (chip->status.status_delta) {
		pr_crit("type:%04x "
			"frmv:%04x "
			"hrdv:%04x "
			"chem:%04x "
			"flav:%04x "
			"pcr:%04x "
			"dcap:%d "
			"mib_cap:%d "
			"mib_chg:%d "
			"mib_rate:%d "
			"mib_tap:%d "
			"mib_pack_ser:\"%s\" "
			"mib_pcm_ser:\"%s\" "
			"shut:%d\n"
			"alt_shut:%d\n",
			chip->status.info.dev_type,
			chip->status.info.fw_ver,
			chip->status.info.hw_ver,
			chip->status.info.chem_id,
			chip->status.info.df_ver,
			chip->status.info.pack_config,
			chip->status.info.design_cap,
			chip->status.mib.capacity_mah,
			chip->status.mib.charge_mv,
			chip->status.mib.charge_rate,
			chip->status.mib.taper_ma,
			chip->status.mib.pack_serial_number,
			chip->status.mib.pcm_serial_number,
			chip->status.info.shutoff_mv,
			chip->status.info.alt_shutoff_mv
			);
		if (chip->status.info.no_match)
			pr_err("device does not match driver!\n");
	}

	bq27541_update_status(chip); // most of the work

	if (chip->status.status_delta) {
		if (chip->status.watch_status_work) {
			bq27541_acquire_cache(chip);
			pr_crit("(%04x) "
				"ctrl:%04x "
				"sta:%d "
				"tmp:%d "
				"v:%d "
				"flgs:%04x(%04x) "
				"nac:%d "
				"rm:%d "
				"fcc:%d "
				"ai:%d "
				"cc:%d "
				"soc:%d "
				"vsoc:%d "
				"soh:%d\n",
				(unsigned int)chip->status.status_delta,
				(unsigned int)chip->status.cache.cntl_status,
				chip->status.cache.status,
				chip->status.cache.temp,
				chip->status.cache.volt,
				(unsigned int)chip->status.cache.flags,
				(unsigned int)chip->status.cache.flags_delta,
				chip->status.cache.nac,
				chip->status.cache.rm,
				chip->status.cache.fcc,
				chip->status.cache.ai,
				chip->status.cache.cc,
				chip->status.cache.soc,
				chip->status.cache.vsoc,
				chip->status.cache.soh
				);
			bq27541_release_cache(chip);
		}
		notify_mask += bq27541_process_status(chip);
	}

	status_delta = chip->status.status_delta;
	chip->status.status_delta = 0;

	bq27541_release_busy(chip);

	notify_mask |= bq27541_charge_monitor(chip, 0);
	notify_mask |= bq27541_screen_monitor(chip, 0);
	notify_mask |= bq27541_check_limits(chip, status_delta);

	notify_mask |= status_delta & (
			BQ27541_DELTA_BAT_PRES | BQ27541_DELTA_I2C_ERR |
			BQ27541_DELTA_STATUS | BQ27541_DELTA_HEALTH |
			BQ27541_DELTA_MIB | BQ27541_DELTA_INFO |
			BQ27541_DELTA_TEMP | BQ27541_DELTA_SOH
			);
#if defined(CONFIG_BQ27541_NO_POWER_EVENTS_ON_SUSPEND)
	if (notify_mask && chip->suspended) {
		chip->status.deferred_status_delta |= notify_mask;
		notify_mask = 0;
	}
#endif
	if (notify_mask) {
#if defined(CONFIG_BQ27541_STATISTICS)
		int mask = 1;
		chip->stats.count[BQ27541_STATS_TOTAL_PSY_e]++;
		while (mask & BQ27541_DELTA_e_MASK) {
			switch (mask & notify_mask) {
			case BQ27541_DELTA_MIB:
				chip->stats.count[BQ27541_STATS_MIB_e]++;
				break;
			case BQ27541_DELTA_INFO:
				chip->stats.count[BQ27541_STATS_INFO_e]++;
				break;
			case BQ27541_DELTA_TEMP:
				chip->stats.count[BQ27541_STATS_TEMP_e]++;
				break;
			case BQ27541_DELTA_SOC:
				chip->stats.count[BQ27541_STATS_SOC_e]++;
				break;
			case BQ27541_DELTA_VSOC:
				chip->stats.count[BQ27541_STATS_VSOC_e]++;
				break;
			case BQ27541_DELTA_SOH:
				chip->stats.count[BQ27541_STATS_SOH_e]++;
				break;
			case BQ27541_DELTA_STATUS:
				chip->stats.count[BQ27541_STATS_STATUS_e]++;
				break;
			case BQ27541_DELTA_I2C_ERR:
				chip->stats.count[BQ27541_STATS_I2C_ERR_e]++;
				break;
			case BQ27541_DELTA_BAT_PRES:
				chip->stats.count[BQ27541_STATS_BAT_PRES_e]++;
				break;
			case BQ27541_DELTA_HEALTH:
				chip->stats.count[BQ27541_STATS_HEALTH_e]++;
				break;
			case BQ27541_DELTA_FYI:
				chip->stats.count[BQ27541_STATS_FYI_e]++;
				break;
			}
			mask <<= 1;
		}
#endif
		bq27541_bms_supply_changed(chip);
	}

	bq27541_periodic_snapshot(chip, 0);
	bq27541_status_reschedule(chip);
}

/**
 * Runs periodically off the work queue.
 */
static void bq27541_status_work(struct work_struct *work)
{
	struct bq27541_chip *chip = container_of(
			work, struct bq27541_chip, status.work.work);
	bq27541_status_work_do_work(chip);
}

/**
 * Internal version that cancels before calling work.
 * Used to kick-start, or reschedule.
 */
static void bq27541_status_work_reschedule(struct bq27541_chip * chip)
{
	cancel_delayed_work(&chip->status.work);
	bq27541_status_work_do_work(chip);
}


static int bq27541_get_bms_property(struct power_supply * ps,
			enum power_supply_property property,
			union power_supply_propval * value)
{
	struct bq27541_chip *chip = container_of(ps,
			struct bq27541_chip, this_bms_psy);
	int rc = 0;

	switch (property) {
	case POWER_SUPPLY_PROP_STATUS: /* enum */
		value->intval = bq27541_get_status(chip, BATT_STATUS_STATUS);
		break;
	case POWER_SUPPLY_PROP_CAPACITY: /* % */
		value->intval = bq27541_get_status(chip, BATT_STATUS_CAPACITY);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW: /* uA */
		value->intval = bq27541_read_current_now_ua(chip);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW: /* uV */
		value->intval = bq27541_read_voltage_now_uv(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL: /* uAh */
		value->intval = bq27541_get_status(chip, BATT_STATUS_FCC) * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN: /* uAh */
		value->intval = chip->status.info.design_cap * 1000;
		break;
	case POWER_SUPPLY_PROP_TEMP: /* dC */
		value->intval = bq27541_get_status(chip, BATT_STATUS_TEMPERATURE);
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER: /* str */
		value->strval = chip->status.info.battery_man;
		break;
	case POWER_SUPPLY_PROP_HEALTH: /* enum */
		value->intval = bq27541_get_status(chip, BATT_STATUS_HEALTH);
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY: /* enum */
		value->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW: /* uAh */
		value->intval = bq27541_get_status(chip, BATT_STATUS_RM) * 1000;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME: /* str */
		value->strval = chip->status.info.battery_mod;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER: /* str */
		value->strval = chip->status.mib.pack_serial_number;
		break;
	case POWER_SUPPLY_PROP_PRESENT: /* bool */
		value->intval = bq27541_is_battery_present(chip);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT: /* count */
		value->intval = bq27541_get_status(chip, BATT_STATUS_CC);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED: /* bool */
		value->intval = bq27541_get_status(chip, BATT_STATUS_HEALTH) ==
					POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX: /* uV */
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN: /* uV */
		value->intval = chip->status.mib.charge_mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN: /* uV */
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN: /* uV */
		value->intval = bq27541_get_shutoff_mv(chip) * 1000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL: /* enum */
		value->intval = bq27541_get_status(chip, BATT_STATUS_FLAGS);
		if (value->intval & BQ27541_FLAG_FC)
			value->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (value->intval & BQ27541_FLAG_SOC1)
			value->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else if (value->intval & BQ27541_FLAG_SOCF)
			value->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else
			value->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX: /* uA */
		// returns max discharge current
		// TODO where to get actual max discharge rate - add to MIB?
		value->intval = chip->status.mib.capacity_mah *
					BQ27541_DEFAULT_DISCHARGE_RATE * 100;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX: /* uA */
		// returns max charge current
		value->intval = chip->status.mib.capacity_mah *
					chip->status.mib.charge_rate * 100;
		break;
#if 0
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW: /* sec */
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG: /* sec */
#endif
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static int bq27541_set_bms_property(struct power_supply * ps,
			enum power_supply_property property,
			const union power_supply_propval * value)
{
//	struct bq27541_chip *chip = container_of(ps,
//			struct bq27541_chip, this_bms_psy);

	switch (property) {
	default:
		return -EINVAL;
	}
	return 0;
}

static int bq27541_bms_property_writeable(struct power_supply * psy,
			enum power_supply_property property)
{
//	struct bq27541_chip *chip = container_of(
//			psy, struct bq27541_chip, this_bms_psy);

	switch (property) {
	default:
		return -EINVAL;
	}
	return 0;
}

static void bq27541_external_power_changed(struct power_supply *psy)
{
//	struct bq27541_chip *chip = container_of(
//			psy, struct bq27541_chip, this_bms_psy);
}

static int bq27541_init_context(struct bq27541_chip * chip)
{
	struct device_node *dnp = chip->dev->of_node;
	u32 value;

	chip->bootmode = BOOT_MODE_RETAIL;

	chip->status.deferred_status_delta = BQ27541_DELTA_BAT_PRES;
	chip->status.use_voltage_soc = 0;
	chip->status.battery_present = 0;
	chip->status.watch_status_work = 0;
	chip->status.info.shutoff_mv = BQ27541_DEFAULT_SHUTOFF_MV;
	chip->status.info.alt_shutoff_mv = BQ27541_DEFAULT_ALT_SHUTOFF_MV;
	chip->status.info.design_cap = BQ27541_DEFAULT_FCC_MAH;
	chip->status.mib.capacity_mah = BQ27541_DEFAULT_FCC_MAH;
	chip->status.mib.charge_mv = BQ27541_DEFAULT_MAX_MV;
	chip->status.mib.charge_rate = BQ27541_DEFAULT_CHARGE_RATE;
	chip->status.mib.taper_ma = BQ27541_DEFAULT_TAPER_MA;
	chip->status.snapshot.seconds = BQ27541_SNAPSHOT_SECONDS;
	chip->status.wake_seconds = BQ27541_STATUS_WAKE_SECONDS;
	chip->status.sleep_real_seconds = BQ27541_STATUS_SLEEP_SECONDS;
	chip->charge_monitor.charging = -1;

	chip->mitigation.i2c_mac_cmd_udelay = BQ27541_I2C_MAC_CMD_UDELAY;
	chip->mitigation.i2c_max_retries = BQ27541_DEFAULT_I2C_MAX_RETRIES;
	chip->mitigation.i2c_protocol_errors = 0;
	chip->mitigation.i2c_inter_cmd_udelay = BQ27541_I2C_INTER_CMD_UDELAY;
	chip->mitigation.i2c_hex_dump = BQ27541_DEFAULT_I2C_HEX_DUMP;
	chip->mitigation.suppress_errors = 0;

	chip->status.limits.temp_hi_dk = 0;
	chip->status.limits.temp_lo_dk = 0;
	chip->status.limits.volt_hi_mv = 0;
	chip->status.limits.volt_lo_mv = 0;
	chip->status.limits.temp_hi_shutdown_dk = 0;
	chip->status.limits.volt_lo_shutdown_mv = 0;

	if (dnp) {
		struct device_node *n = of_find_node_by_path("/idme/bootmode");
		if (n) {
			struct property *p = of_find_property(n, "value", 0);
			if (p && p->length == 2) {
				switch (*(char*)p->value) {
				case '1':
					chip->bootmode = BOOT_MODE_RETAIL;
					break;
				case '2':
					chip->bootmode = BOOT_MODE_FACTORY;
					break;
				default:
					pr_err("undefined bootmode '%c'\n",
							*(char*)p->value);
					break;
				}
			} else {
				pr_err("irregular /idme/bootmode element\n");
			}
		} else {
			pr_err("/idme/bootmode not found\n");
		}

		value = !!chip->status.use_voltage_soc;
		of_property_read_u32(dnp, "ursa,use-voltage-soc", &value);
		chip->status.use_voltage_soc = !!value;

		of_property_read_u32(dnp, "ursa,gg-snapshot-sec",
				&chip->status.snapshot.seconds);
		of_property_read_u32(dnp, "ursa,gg-status-wake-sec",
				&chip->status.wake_seconds);
		of_property_read_u32(dnp, "ursa,gg-status-sleep-real-sec",
				&chip->status.sleep_real_seconds);

		of_property_read_u32(dnp, "ursa,info-shutoff-mv",
				&chip->status.info.shutoff_mv);
		of_property_read_u32(dnp, "ursa,info-alt-shutoff-mv",
				&chip->status.info.alt_shutoff_mv);

		of_property_read_u32(dnp, "ursa,mib-capacity-mah",
				&chip->status.mib.capacity_mah);
		of_property_read_u32(dnp, "ursa,mib-charge-mv",
				&chip->status.mib.charge_mv);
		of_property_read_u32(dnp, "ursa,mib-charge-rate",
				&chip->status.mib.charge_rate);
		of_property_read_u32(dnp, "ursa,mib-taper-ma",
				&chip->status.mib.taper_ma);

		of_property_read_u32(dnp, "ursa,limit-temp-hi-dk",
				&chip->status.limits.temp_hi_dk);
		of_property_read_u32(dnp, "ursa,limit-temp-lo-dk",
				&chip->status.limits.temp_lo_dk);
		of_property_read_u32(dnp, "ursa,limit-volt-hi-mv",
				&chip->status.limits.volt_hi_mv);
		of_property_read_u32(dnp, "ursa,limit-volt-lo-mv",
				&chip->status.limits.volt_lo_mv);
		of_property_read_u32(dnp, "ursa,limit-temp-hi-shutdown-dk",
				&chip->status.limits.temp_hi_shutdown_dk);
		of_property_read_u32(dnp, "ursa,limit-volt-lo-shutdown-mv",
				&chip->status.limits.volt_lo_shutdown_mv);
	}

	chip->status.cache.fcc = chip->status.mib.capacity_mah;
	chip->status.cache.temp = 250;
	chip->status.cache.volt = (chip->status.mib.charge_mv +
					chip->status.info.shutoff_mv) / 2;
	chip->status.cache.nac = chip->status.cache.rm =
					chip->status.cache.fcc / 2;
	chip->status.cache.soc = chip->status.cache.vsoc = 50;
	chip->status.cache.soh = 100;

	return 0;
}

enum {
	BQ27541_DA_USE_VOLTAGE_SOC,
	BQ27541_DA_BATTERY_PRESENT,
	BQ27541_DA_WATCH_STATUS_WORK,
	BQ27541_DA_COMPARE_GG_BMS,
	BQ27541_DA_SNAPSHOT,
	BQ27541_DA_STATUS_WAKE,
	BQ27541_DA_STATUS_SLEEP,
	BQ27541_DA_I2C_MAC_CMD_DELAY,
	BQ27541_DA_I2C_MAX_RETRIES,
	BQ27541_DA_I2C_PROTOCOL_ERRORS,
	BQ27541_DA_I2C_INTER_CMD_UDELAY,
	BQ27541_DA_I2C_HEX_DUMP,
	BQ27541_DA_I2C_NOK,
	BQ27541_DA_SUPPRESS_ERRORS,
	BQ27541_DA_PMIC_SNAPSHOT,
	BQ27541_DA_BOOTMODE,
	BQ27541_DA_TEMP_HI_SHUTDOWN,
	BQ27541_DA_VOLT_LO_SHUTDOWN,
	BQ27541_DA_BATTERY_ID,
	BQ27541_DA_SHUTOFF_MV,
	BQ27541_DA_ALT_SHUTOFF_MV,
	BQ27541_DA_TEST,
};

static ssize_t bq27541_show_attr(struct device *dev,
		struct device_attribute *attr, char *buf, int which)
{
	struct bq27541_chip *chip = dev_get_drvdata(dev);
	int value = 0;
	switch (which) {
	case BQ27541_DA_USE_VOLTAGE_SOC:
		value = chip->status.use_voltage_soc;
		break;
	case BQ27541_DA_BATTERY_PRESENT:
		value = chip->status.battery_present;
		break;
	case BQ27541_DA_WATCH_STATUS_WORK:
		value = chip->status.watch_status_work;
		break;
	case BQ27541_DA_SNAPSHOT:
		value = chip->status.snapshot.seconds;
		break;
	case BQ27541_DA_STATUS_WAKE:
		value = chip->status.wake_seconds;
		break;
	case BQ27541_DA_STATUS_SLEEP:
		value = chip->status.sleep_real_seconds;
		break;
	case BQ27541_DA_I2C_MAC_CMD_DELAY:
		value = chip->mitigation.i2c_mac_cmd_udelay;
		break;
	case BQ27541_DA_I2C_MAX_RETRIES:
		value = chip->mitigation.i2c_max_retries;
		break;
	case BQ27541_DA_I2C_PROTOCOL_ERRORS:
		value = chip->mitigation.i2c_protocol_errors;
		break;
	case BQ27541_DA_I2C_INTER_CMD_UDELAY:
		value = chip->mitigation.i2c_inter_cmd_udelay;
		break;
	case BQ27541_DA_I2C_HEX_DUMP:
		value = chip->mitigation.i2c_hex_dump;
		break;
	case BQ27541_DA_I2C_NOK:
		value = chip->recovery.not_okay;
		break;
	case BQ27541_DA_SUPPRESS_ERRORS:
		value = chip->mitigation.suppress_errors;
		break;
	case BQ27541_DA_PMIC_SNAPSHOT:
		value = chip->status.pmic_snapshot;
		break;
	case BQ27541_DA_BOOTMODE:
		value = chip->bootmode;
		break;
	case BQ27541_DA_TEMP_HI_SHUTDOWN:
		value = chip->status.limits.temp_hi_shutdown_dk;
		break;
	case BQ27541_DA_VOLT_LO_SHUTDOWN:
		value = chip->status.limits.volt_lo_shutdown_mv;
		break;
	case BQ27541_DA_BATTERY_ID:
		value = chip->status.info.battery_id;
		break;
	case BQ27541_DA_SHUTOFF_MV:
		value = chip->status.info.shutoff_mv;
		break;
	case BQ27541_DA_ALT_SHUTOFF_MV:
		value = chip->status.info.alt_shutoff_mv;
		break;
	case BQ27541_DA_TEST:
		break;
	default:
		return 0;
	}
	return sprintf(buf, "%d\n", value);
}

static ssize_t bq27541_set_attr(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count,
		int which)
{
	struct bq27541_chip *chip = dev_get_drvdata(dev);
	long value;
	int rc = kstrtol(buf, 0, &value);
	if (rc) return count;
	switch (which) {
	case BQ27541_DA_USE_VOLTAGE_SOC:
		chip->status.use_voltage_soc = !!value;
		bq27541_bms_supply_changed(chip);
		break;
	case BQ27541_DA_BATTERY_PRESENT:
		chip->status.battery_present = !!value;
		bq27541_bms_supply_changed(chip);
		break;
	case BQ27541_DA_WATCH_STATUS_WORK:
		chip->status.watch_status_work = !!value;
		break;
	case BQ27541_DA_SNAPSHOT:
		chip->status.snapshot.seconds = value;
		bq27541_periodic_snapshot(chip, 1);
		break;
	case BQ27541_DA_STATUS_WAKE:
		chip->status.wake_seconds = value;
		bq27541_status_work_reschedule(chip);
		break;
	case BQ27541_DA_STATUS_SLEEP:
		chip->status.sleep_real_seconds = value;
		bq27541_status_work_reschedule(chip);
		break;
	case BQ27541_DA_I2C_MAC_CMD_DELAY:
		chip->mitigation.i2c_mac_cmd_udelay = value;
		break;
	case BQ27541_DA_I2C_MAX_RETRIES:
		chip->mitigation.i2c_max_retries = value;
		break;
	case BQ27541_DA_I2C_PROTOCOL_ERRORS:
		chip->mitigation.i2c_protocol_errors = value;
		break;
	case BQ27541_DA_I2C_INTER_CMD_UDELAY:
		chip->mitigation.i2c_inter_cmd_udelay = value;
		break;
	case BQ27541_DA_I2C_HEX_DUMP:
		chip->mitigation.i2c_hex_dump = value;
		break;
	case BQ27541_DA_I2C_NOK:
		chip->recovery.not_okay = value;
		break;
	case BQ27541_DA_SUPPRESS_ERRORS:
		chip->mitigation.suppress_errors = value;
		break;
	case BQ27541_DA_PMIC_SNAPSHOT:
		chip->status.pmic_snapshot = value;
		break;
	case BQ27541_DA_BOOTMODE:
		if (value == BOOT_MODE_RETAIL || value == BOOT_MODE_FACTORY)
			chip->bootmode = value;
		break;
	case BQ27541_DA_TEMP_HI_SHUTDOWN:
		chip->status.limits.temp_hi_shutdown_dk = value;
		break;
	case BQ27541_DA_VOLT_LO_SHUTDOWN:
		chip->status.limits.volt_lo_shutdown_mv = value;
		break;
	case BQ27541_DA_BATTERY_ID:
		chip->status.info.battery_id = value;
		break;
	case BQ27541_DA_SHUTOFF_MV:
		chip->status.info.shutoff_mv = value;
		break;
	case BQ27541_DA_ALT_SHUTOFF_MV:
		chip->status.info.alt_shutoff_mv = value;
		break;
	case BQ27541_DA_TEST:
		break;
	}
	return count;
}

#define BQ27541_ATTRIBUTE(attr, which) \
static ssize_t bq27541_show_##attr(struct device *dev, \
		struct device_attribute *attr, char *buf) \
{ \
	return bq27541_show_attr(dev, attr, buf, which); \
} \
static ssize_t bq27541_set_##attr(struct device *dev, \
		struct device_attribute *attr, const char *buf, size_t count) \
{ \
	return bq27541_set_attr(dev, attr, buf, count, which); \
} \
static DEVICE_ATTR(attr, S_IWUSR | S_IRUSR, \
		bq27541_show_##attr, bq27541_set_##attr);

BQ27541_ATTRIBUTE(use_voltage_soc, BQ27541_DA_USE_VOLTAGE_SOC)
BQ27541_ATTRIBUTE(battery_present, BQ27541_DA_BATTERY_PRESENT)
BQ27541_ATTRIBUTE(watch_status_work, BQ27541_DA_WATCH_STATUS_WORK)
BQ27541_ATTRIBUTE(snapshot_seconds, BQ27541_DA_SNAPSHOT)
BQ27541_ATTRIBUTE(status_wake_seconds, BQ27541_DA_STATUS_WAKE)
BQ27541_ATTRIBUTE(status_sleep_real_seconds, BQ27541_DA_STATUS_SLEEP)
BQ27541_ATTRIBUTE(i2c_mac_cmd_udelay, BQ27541_DA_I2C_MAC_CMD_DELAY)
BQ27541_ATTRIBUTE(i2c_max_retries, BQ27541_DA_I2C_MAX_RETRIES)
BQ27541_ATTRIBUTE(i2c_protocol_errors, BQ27541_DA_I2C_PROTOCOL_ERRORS)
BQ27541_ATTRIBUTE(i2c_inter_cmd_udelay, BQ27541_DA_I2C_INTER_CMD_UDELAY)
BQ27541_ATTRIBUTE(i2c_hex_dump, BQ27541_DA_I2C_HEX_DUMP)
BQ27541_ATTRIBUTE(i2c_nok, BQ27541_DA_I2C_NOK)
BQ27541_ATTRIBUTE(suppress_errors, BQ27541_DA_SUPPRESS_ERRORS)
BQ27541_ATTRIBUTE(pmic_snapshot, BQ27541_DA_PMIC_SNAPSHOT)
BQ27541_ATTRIBUTE(bootmode, BQ27541_DA_BOOTMODE)
BQ27541_ATTRIBUTE(temp_hi_shutdown, BQ27541_DA_TEMP_HI_SHUTDOWN)
BQ27541_ATTRIBUTE(volt_lo_shutdown, BQ27541_DA_VOLT_LO_SHUTDOWN)
BQ27541_ATTRIBUTE(battery_id, BQ27541_DA_BATTERY_ID)
BQ27541_ATTRIBUTE(shutoff_mv, BQ27541_DA_SHUTOFF_MV)
BQ27541_ATTRIBUTE(alt_shutoff_mv, BQ27541_DA_ALT_SHUTOFF_MV)
BQ27541_ATTRIBUTE(test, BQ27541_DA_TEST)

static void bq27541_create_attributes(struct bq27541_chip * chip)
{
	device_create_file(chip->dev, &dev_attr_use_voltage_soc);
	device_create_file(chip->dev, &dev_attr_battery_present);
	device_create_file(chip->dev, &dev_attr_watch_status_work);
	device_create_file(chip->dev, &dev_attr_snapshot_seconds);
	device_create_file(chip->dev, &dev_attr_status_wake_seconds);
	device_create_file(chip->dev, &dev_attr_status_sleep_real_seconds);
	device_create_file(chip->dev, &dev_attr_i2c_mac_cmd_udelay);
	device_create_file(chip->dev, &dev_attr_i2c_max_retries);
	device_create_file(chip->dev, &dev_attr_i2c_protocol_errors);
	device_create_file(chip->dev, &dev_attr_i2c_inter_cmd_udelay);
	device_create_file(chip->dev, &dev_attr_i2c_hex_dump);
	device_create_file(chip->dev, &dev_attr_i2c_nok);
	device_create_file(chip->dev, &dev_attr_suppress_errors);
	device_create_file(chip->dev, &dev_attr_pmic_snapshot);
	device_create_file(chip->dev, &dev_attr_bootmode);
	device_create_file(chip->dev, &dev_attr_temp_hi_shutdown);
	device_create_file(chip->dev, &dev_attr_volt_lo_shutdown);
	device_create_file(chip->dev, &dev_attr_battery_id);
	device_create_file(chip->dev, &dev_attr_shutoff_mv);
	device_create_file(chip->dev, &dev_attr_alt_shutoff_mv);
	device_create_file(chip->dev, &dev_attr_test);
}

#if defined(CONFIG_BQ27541_GG)
static char *bq27541_bms_supplicants[] = {
	"battery"
};
#define THIS_PSY_NAME			"bms"
#define THIS_PSY_TYPE			POWER_SUPPLY_TYPE_BMS
#define THIS_SUPPLICANT_LIST		bq27541_bms_supplicants
#define THIS_SUPPLICANT_COUNT		ARRAY_SIZE(bq27541_bms_supplicants)
#else
#define THIS_PSY_NAME			"gg"
#define THIS_PSY_TYPE			POWER_SUPPLY_TYPE_UNKNOWN
#define THIS_SUPPLICANT_LIST		0
#define THIS_SUPPLICANT_COUNT		0
#endif

static int bq27541_probe(struct i2c_client * client,
				 const struct i2c_device_id * id)
{
	struct bq27541_chip *chip;
	int rc = 0;
	struct qpnp_vadc_chip *vadc_dev;
	struct qpnp_iadc_chip *iadc_dev;

	vadc_dev = qpnp_get_vadc(&client->dev, "gg");
	iadc_dev = qpnp_get_iadc(&client->dev, "gg");
	if (IS_ERR(vadc_dev)) rc = PTR_ERR(vadc_dev);
	if (rc == 0 && IS_ERR(iadc_dev)) rc = PTR_ERR(iadc_dev);
	if (rc) {
		if (rc == -EPROBE_DEFER)
			pr_info("adc not ready\n");
		else
			pr_err("missing adc property\n");
		return rc;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("check i2c functionality failed\n");
		return -ENODEV;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		pr_err("failed to allocate chip data\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	chip->vadc_dev = vadc_dev;
	chip->iadc_dev = iadc_dev;

	mutex_init(&chip->access.mutex);
	mutex_init(&chip->status.cache.mutex);
	spin_lock_init(&chip->recovery.lock);
	INIT_DELAYED_WORK(&chip->status.work, bq27541_status_work);

	bq27541_init_context(chip);

	i2c_set_clientdata(client, chip);
	dev_set_drvdata(chip->dev, chip);

	bq27541_battery_id(chip);

	chip->status.snapshot.next = 1;
	bq27541_status_work_reschedule(chip);

	bq27541_create_attributes(chip);

	chip->this_bms_psy.name = THIS_PSY_NAME;
	chip->this_bms_psy.type = THIS_PSY_TYPE;
	chip->this_bms_psy.get_property = bq27541_get_bms_property;
	chip->this_bms_psy.set_property = bq27541_set_bms_property;
	chip->this_bms_psy.property_is_writeable = bq27541_bms_property_writeable;
	chip->this_bms_psy.properties = bq27541_bms_properties;
	chip->this_bms_psy.num_properties = ARRAY_SIZE(bq27541_bms_properties);
	chip->this_bms_psy.external_power_changed = bq27541_external_power_changed;
	chip->this_bms_psy.supplied_to = THIS_SUPPLICANT_LIST;
	chip->this_bms_psy.num_supplicants = THIS_SUPPLICANT_COUNT;
	rc = power_supply_register(chip->dev, &chip->this_bms_psy);
	if (rc < 0)
		pr_err("power_supply_register failed, rc = %d\n", rc);

	bq27541_procfs_init(chip);
	return 0;
}

static void bq27541_do_shutdown(struct bq27541_chip * chip)
{
	cancel_delayed_work(&chip->status.work);
	bq27541_procfs_deinit(chip);
	bq27541_charge_monitor(chip, 1);
	bq27541_screen_monitor(chip, 1);
	bq27541_close_rtc(chip);
}

static int bq27541_remove(struct i2c_client *client)
{
	struct bq27541_chip *chip = i2c_get_clientdata(client);
	bq27541_do_shutdown(chip);
	kfree(chip);
	return 0;
}

static void bq27541_shutdown(struct i2c_client *client)
{
	struct bq27541_chip *chip = i2c_get_clientdata(client);
	bq27541_do_shutdown(chip);
}

static int bq27541_pm_suspend(struct device *dev)
{
	struct bq27541_chip *chip = dev_get_drvdata(dev);
	if (!chip) return 0;
	chip->suspended = 1;
#if defined(CONFIG_BQ27541_NO_GG_ACTIVITY_ON_SUSPEND)
	cancel_delayed_work(&chip->status.work);
#else
	bq27541_status_work_reschedule(chip);
#endif
	chip->suspend_soc = bq27541_get_status(chip, BATT_STATUS_CAPACITY);
	chip->suspend_time = bq27541_get_wall_seconds(chip);
	/**
	 * This rtc wakeup scheme will work only part way, unless a wake lock is
	 * held long enough to permit the power_supply infrastructure to
	 * completely process events. E.g. if the GG reports FULL, then this
	 * driver updates 'BMS', but the charger doesn't get a chance to respond
	 * and turn off charging.
	 */
	if (chip->status.sleep_real_seconds) {
		chip->status.rtc_wakeup_set = 1;
		bq27541_wall_alarm_set(chip, chip->status.sleep_real_seconds);
	}
	return 0;
}

static int bq27541_pm_resume(struct device *dev)
{
	struct bq27541_chip *chip = dev_get_drvdata(dev);
	if (!chip) return 0;
	chip->suspended = 0;
	if (chip->status.rtc_wakeup_set) {
		chip->status.rtc_wakeup_set = 0;
		bq27541_wall_alarm_set(chip, 0);
	}
	bq27541_status_work_reschedule(chip);
	if (chip->suspend_time) {
		unsigned long elapsed_ms = (bq27541_get_wall_seconds(chip) -
				chip->suspend_time) * 1000;
		int soc = bq27541_get_status(chip, BATT_STATUS_CAPACITY);
		int voltage = bq27541_get_status(chip, BATT_STATUS_VOLTAGE);
		int temp = bq27541_get_status(chip, BATT_STATUS_TEMPERATURE);
		int rm = bq27541_get_status(chip, BATT_STATUS_RM);
		int nac = bq27541_get_status(chip, BATT_STATUS_NAC);
		int delta_soc = chip->suspend_soc - soc;
		char buf[128];
		snprintf(buf, sizeof(buf),
				"suspend_drain:def:"
				"value=%d;CT;1"
				",elapsed=%lu;TI;1:NR",
				delta_soc, elapsed_ms);
		bq27541_log_metric("drain_metrics", buf);
		snprintf(buf, sizeof(buf),
				"batt:def:cap=%d;CT;1"
				",mv=%d;CT;1"
				",temp_g=%d;CT;1"
				",charge=%d/%d;CT;1:NR",
				soc, voltage, temp, rm, nac);
		bq27541_log_metric(BQ27541_GG_DEV_NAME, buf);
		chip->suspend_time = 0;
	}
	return 0;
}

static const struct dev_pm_ops bq27541_pm_ops = {
	.suspend = bq27541_pm_suspend,
	.resume = bq27541_pm_resume,
};

static const struct i2c_device_id bq27541_id[] = {
	{ BQ27541_GG_FULL_DEV_NAME, 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27541_id);

static unsigned short bq27541_i2c[] = {
	BQ27541_I2C_ADDRESS,
	I2C_CLIENT_END,
};

static const struct of_device_id bq27541_of_device_id_table[] = {
	{ .compatible = BQ27541_GG_FULL_DEV_NAME, },
	{},
};

static struct i2c_driver bq27541_driver = {
	.driver = {
		.name = BQ27541_GG_FULL_DEV_NAME,
		.owner = THIS_MODULE,
		.pm = &bq27541_pm_ops,
		.of_match_table = of_match_ptr(bq27541_of_device_id_table),
        },
	.probe = bq27541_probe,
	.remove = bq27541_remove,
	.shutdown = bq27541_shutdown,
	.id_table = bq27541_id,
	.address_list = bq27541_i2c,
};

static int __init bq27541_init(void)
{
	return i2c_add_driver(&bq27541_driver);
}

static void __exit bq27541_exit(void)
{
	i2c_del_driver(&bq27541_driver);
}

module_init(bq27541_init);
module_exit(bq27541_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jeff Loucks <loucks@amazon.com>");
MODULE_DESCRIPTION("BQ27541 battery gas guage driver");
MODULE_VERSION("V1.0");
MODULE_ALIAS("platform:" BQ27541_GG_FULL_DEV_NAME);

