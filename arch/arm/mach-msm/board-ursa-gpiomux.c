/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/gpiomux.h>
#include <mach/socinfo.h>
#include <mach/board-detect.h>

static struct gpiomux_setting gpio_pn_2ma_cfg = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_NONE,
};

static struct gpiomux_setting gpio_pd_2ma_cfg = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_DOWN,
};

static struct gpiomux_setting gpio_pu_2ma_cfg = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_UP,
};

static struct gpiomux_setting gpio_ol_2ma_cfg = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_NONE,
	.dir = GPIOMUX_OUT_LOW
};

static struct gpiomux_setting gpio_oh_2ma_cfg = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_NONE,
	.dir = GPIOMUX_OUT_HIGH
};

static struct gpiomux_setting func1_pn_2ma_cfg = {
	.func = GPIOMUX_FUNC_1,
	.drv = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_NONE,
};

static struct gpiomux_setting func1_pd_2ma_cfg = {
	.func = GPIOMUX_FUNC_1,
	.drv = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_DOWN,
};

static struct gpiomux_setting func1_pn_6ma_cfg = {
	.func = GPIOMUX_FUNC_1,
	.drv = GPIOMUX_DRV_6MA,
	.pull = GPIOMUX_PULL_NONE,
};

static struct gpiomux_setting func1_pn_8ma_cfg = {
	.func = GPIOMUX_FUNC_1,
	.drv = GPIOMUX_DRV_8MA,
	.pull = GPIOMUX_PULL_NONE,
};

static struct gpiomux_setting func1_pk_8ma_cfg = {
	.func = GPIOMUX_FUNC_1,
	.drv = GPIOMUX_DRV_8MA,
	.pull = GPIOMUX_PULL_KEEPER,
};

static struct gpiomux_setting func1_pd_8ma_cfg = {
	.func = GPIOMUX_FUNC_1,
	.drv = GPIOMUX_DRV_8MA,
	.pull = GPIOMUX_PULL_DOWN,
};

static struct gpiomux_setting func2_pn_8ma_cfg = {
	.func = GPIOMUX_FUNC_2,
	.drv = GPIOMUX_DRV_8MA,
	.pull = GPIOMUX_PULL_NONE,
};

static struct gpiomux_setting func3_pn_8ma_cfg = {
	.func = GPIOMUX_FUNC_3,
	.drv = GPIOMUX_DRV_8MA,
	.pull = GPIOMUX_PULL_NONE,
};

static struct gpiomux_setting ursa_touch_resout_sus_cfg = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv = GPIOMUX_DRV_6MA,
	.pull = GPIOMUX_PULL_NONE,
	.dir = GPIOMUX_OUT_HIGH,
};

static struct gpiomux_setting ursa_touch_resout_act_cfg = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv = GPIOMUX_DRV_6MA,
	.pull = GPIOMUX_PULL_NONE,
	.dir = GPIOMUX_OUT_HIGH,
};

static struct gpiomux_setting ursa_touch_int_act_cfg = {
	.func = GPIOMUX_FUNC_GPIO,
	.pull = GPIOMUX_PULL_NONE,
	.dir = GPIOMUX_IN,
};

static struct gpiomux_setting ursa_touch_int_sus_cfg = {
	.func = GPIOMUX_FUNC_GPIO,
	.pull = GPIOMUX_PULL_NONE,
	.dir = GPIOMUX_IN,
};

/*
 * GPIO CONFIGURATIONS FOR URSA
 *
 * [GPIOMUX_ACTIVE]	Configured when GPIO is requested
 * [GPIOMUX_SUSPENDED]	Configured on initialization and when GPIO is free and
 *                      has no direct relation with power collapse
 *
 * GPIOs that are not configured in gpiomux retain initialization values from
 * TLMM configuration in SBL. Also cores other than Kraits may configure GPIO
 * as needed, and those configurations are not visible here.
 *
 * The following subsystems are omitted for GPIOMUX configuration
 *	DSP
 *	RF
 *	UIM
 *	HDMI
 */

static struct msm_gpiomux_config ursa_audio_configs_common[] __initdata = {
	/* Codec control */
	{
		.gpio	  = 63,		/* CODEC_RESET_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_ol_2ma_cfg,
		},
	},
	{
		.gpio	  = 72,		/* CODEC_INT1_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pd_2ma_cfg,
		},
	},

	/* Slimbus */
	{
		.gpio     = 70,		/* SLIMBUS_CLK */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func1_pk_8ma_cfg,
		},
	},
	{
		.gpio     = 71,		/* SLIMBUS_DATA */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func1_pk_8ma_cfg,
		},
	},
	/* Speaker Amp I2C */
        {
                .gpio     = 48,         /* I2C_CLK */
                .settings = {
                        [GPIOMUX_SUSPENDED] = &func3_pn_8ma_cfg,
                },
        },
        {
                .gpio     = 47,         /* I2C_DATA */
                .settings = {
                        [GPIOMUX_SUSPENDED] = &func3_pn_8ma_cfg,
                },
        },

	/* The following GPIOs are omitted from gpiomux configuration
	 * 	93	CODEC_INT2_N
	 */
};

static struct msm_gpiomux_config ursa_audio_configs_p1[] __initdata = {
	/* Speaker Amp I2S */
	{
		.gpio     = 79,		/* SPKR_AMP_I2S_CLK */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_8ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_8ma_cfg,
		},
	},
	{
		.gpio     = 80,		/* SPKR_AMP_I2S_WS */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func1_pd_8ma_cfg,
		},
	},
	{
		.gpio     = 81,		/* SPKR_AMP_I2S_SDO */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func1_pd_8ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_bt_wifi_configs_p1[] = {
	/* WCN 5 Wire Interface */
	{
		.gpio     = 36,		/* WLAN_WCN_CMD_DATA2 */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_8ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_2ma_cfg,
		},
	},
	{
		.gpio     = 37,		/* WLAN_WCN_CMD_DATA1 */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_8ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_2ma_cfg,
		},
	},
	{
		.gpio     = 38,		/* WLAN_WCN_CMD_DATA0 */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_8ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_2ma_cfg,
		},
	},
	{
		.gpio     = 39,		/* WLAN_WCN_CMD_SET */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_8ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_2ma_cfg,
		},
	},
	{
		.gpio     = 40,		/* WLAN_WCN_CMD_CLK */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_8ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_2ma_cfg,
		},
	},

	/* WCN BT */
	{
		.gpio     = 35,		/* WCN_BT_SSBI */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_8ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_8ma_cfg,
		},
	},
	{
		.gpio     = 43,		/* WCN_BT_CTL */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_8ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_8ma_cfg,
		},
	},
	{
		.gpio     = 44,		/* WCN_BT_DATA */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_8ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_8ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_camera_configs_common[] __initdata = {
	/* RFC FFC */
	{
		.gpio     = 15,		/* CAM_MCLK0_MSM */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_8ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_2ma_cfg,
		},
	},
	{
		.gpio     = 19,		/* CAM_I2C_SDA */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_2ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pn_2ma_cfg,
		},
	},
	{
		.gpio     = 20,		/* CAM_I2C_SCL */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_2ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pn_2ma_cfg,
		},
	},

	/* 4CC */
	{
		.gpio     = 16,		/* CAM_MCLK1_MSM */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_6ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_2ma_cfg,
		},
	},
	{
		.gpio     = 24,		/* CAM_4CC_ULPM_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pn_2ma_cfg,
		},
	},

	/* ISP Control */
	{
		.gpio     = 23,		/* CAM_ISP_RST_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_oh_2ma_cfg,
		},
	},
	{
		.gpio     = 25, 	/* CAM_ISP_PWDN */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_oh_2ma_cfg,
		},
	},
	{
		.gpio     = 26,		/* CAM_ISP_XPWDN */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_oh_2ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_camera_configs_p1p2common[] __initdata = {
	{
		.gpio     = 31,		/* CAM_FFC_PWDN_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_ol_2ma_cfg,
		},
	},
	{
		.gpio     = 32,		/* CAM_4CC_LED_STAT0 */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pd_2ma_cfg,
		},
	},
	{
		.gpio     = 89,		/* CAM_RFC_PWDN_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_ol_2ma_cfg,
		},
	},

	/* 4CC */
	{
		.gpio     = 18, 	/* CAM_4CC_ASIC_RDY_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pu_2ma_cfg,
		},
	},
	{
		.gpio     = 56,		/* CAM_4CC_LED_QUAD */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_ol_2ma_cfg,
		},
	},
	{
		.gpio     = 57,		/* CAM_4CC_LED_SEL1 */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_ol_2ma_cfg,
		},
	},
	{
		.gpio     = 91,		/* CAM_4CC_LED_SEL0 */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_ol_2ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_camera_configs_p2[] __initdata = {
	/* 4CC */
	{
		.gpio     = 21, 	/* CAM_4CC_I2C_MSM_SDA */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func1_pn_8ma_cfg,
		},
	},
	{
		.gpio     = 22,		/* CAM_4CC_I2C_MSM_SCL */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func1_pn_8ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_camera_configs_p1p2evt_common[] __initdata = {
	{
		.gpio     = 17,		/* CAM_MCLK2_MSM */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_2ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_2ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_camera_configs_pre_dvt[] __initdata = {
	{
		.gpio     = 17,		/* CAM_MCLK2_MSM */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_6ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_2ma_cfg,
		},
	},

};

static struct msm_gpiomux_config ursa_camera_configs_dvt[] __initdata = {
	{
		.gpio     = 17,		/* CAM_MCLK2_MSM */
		.settings = {
			[GPIOMUX_ACTIVE]    = &func1_pn_6ma_cfg,
			[GPIOMUX_SUSPENDED] = &func1_pd_2ma_cfg,
		},
	},
	/* 4CC */
	{
		.gpio     = 21, 	/* CAM_4CC_I2C_MSM_SDA */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func1_pn_2ma_cfg,
		},
	},
	{
		.gpio     = 22,		/* CAM_4CC_I2C_MSM_SCL */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func1_pn_2ma_cfg,
		},
	},
};

static struct gpiomux_setting lcd_en_act_cfg = {
	.func = GPIOMUX_FUNC_GPIO,
	.drv = GPIOMUX_DRV_2MA,
	.pull = GPIOMUX_PULL_NONE,
	.dir = GPIOMUX_OUT_HIGH,
};

static struct msm_gpiomux_config ursa_display_configs[] __initdata = {
	{
		.gpio    = 12,		/* LCD_TE_EN */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pn_2ma_cfg,
		},
	},
	{
		.gpio     = 58,		/* LCD_PWR_EN */
		.settings = {
			[GPIOMUX_ACTIVE]    = &lcd_en_act_cfg,
			[GPIOMUX_SUSPENDED] = &gpio_ol_2ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_nfc_configs[] __initdata = {
	/* I2C */
	{
		.gpio     = 29,		/* NFC_I2C_SDA */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func3_pn_8ma_cfg,
		},
	},
	{
		.gpio     = 30,		/* NFC_I2C_SCL */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func3_pn_8ma_cfg,
		},
	},

	/* Control */
	{
		.gpio     = 59,		/* NFC_IRQ */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pd_2ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_nfc_configs_p2[] __initdata = {
	{
		.gpio     = 86,		/* NFC_CLK_REQ */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pd_2ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_sensor_configs_common[] __initdata = {
	{
		.gpio      = 10,	/* SENSORS2_I2C_SDA */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func3_pn_8ma_cfg,
		},
	},
	{
		.gpio     = 11,		/* SENSORS2_I2C_SCL */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func3_pn_8ma_cfg,
		},
	},
	{
		.gpio     = 64,		/* SS_RESET_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_ol_2ma_cfg,
		},
	},
	{
		.gpio     = 66,		/* SS_INT_XG */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pd_2ma_cfg,
		},
	},
	{
		.gpio     = 67,		/* SS_INT_MAG_DRDY */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pd_2ma_cfg,
		},
	},
	{
		.gpio     = 68,		/* SS_PROX_INT_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pn_2ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_sensor_configs_p1_pre_evt[] __initdata = {
	{
		.gpio     = 28,		/* SS_SAR_PROX_INT_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pu_2ma_cfg,
		},
	},
	{
		.gpio     = 69,		/* SS_SAR_PROX_EN */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_oh_2ma_cfg,
		},
	},
	{
		.gpio     = 73,		/* SS_SAR_PROX_KEEPALIVE */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pd_2ma_cfg,
		},
	},

};

static struct msm_gpiomux_config ursa_system_configs_common[] __initdata = {
	/* DEBUG */
	{
		.gpio     = 94,		/* JTAG_DEBUG_2 */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pd_2ma_cfg,
		},
	},
	{
		.gpio     = 95,		/* JTAG_DEBUG_1*/
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pd_2ma_cfg,
		},
	},

	/* The following GPIOs are omitted from gpiomux configuration
	 * 	103	FORCE_USB_BOOT
	 */
};

static struct msm_gpiomux_config ursa_system_configs_p1[] __initdata = {
	/* DEBUG */
	{
		.gpio     = 4,		/* UART2_DEBUG_TX */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func2_pn_8ma_cfg,
		},
	},
	{
		.gpio     = 5,		/* UART2_DEBUG_RX */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func2_pn_8ma_cfg,
		},
	},

	/* MISC */
	{
		.gpio     = 83,		/* SYS_I2C_SDA */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func3_pn_8ma_cfg,
		},
	},
	{
		.gpio     = 84,		/* SYS_I2C_SCL */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func3_pn_8ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_system_configs_p2[] __initdata = {
	/* MISC */
	{
		.gpio     = 92,		/* DEVICE_PRODUCTION */
		.settings = {
			[GPIOMUX_ACTIVE]    = &gpio_pd_2ma_cfg,
			[GPIOMUX_SUSPENDED] = &gpio_pu_2ma_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_touch_configs_common[] __initdata = {
	/* Touch I2C */
	{
		.gpio     = 6,		/* TS_I2C_SDA */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func3_pn_8ma_cfg,
		},
	},
	{
		.gpio     = 7,		/* TS_I2C_SCL */
		.settings = {
			[GPIOMUX_SUSPENDED] = &func3_pn_8ma_cfg,
		},
	},

	{
		.gpio      = 60,		/* TOUCH RESET */
		.settings = {
			[GPIOMUX_ACTIVE] = &ursa_touch_resout_act_cfg,
			[GPIOMUX_SUSPENDED] = &ursa_touch_resout_sus_cfg,
		},
	},
	{
		.gpio      = 61,		/* TOUCH IRQ */
		.settings = {
			[GPIOMUX_ACTIVE] = &ursa_touch_int_act_cfg,
			[GPIOMUX_SUSPENDED] = &ursa_touch_int_sus_cfg,
		},
	},
};

static struct msm_gpiomux_config ursa_touch_configs_p1[] __initdata = {
	/* Touch control */
	{
		.gpio     = 61,		/* TOUCH_INT_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pn_2ma_cfg,
		},
	},
};

/* P0.5 has additional GPIOs not in P1 */
static struct msm_gpiomux_config ursa_configs_p0_5[] __initdata = {
	/* Touch */
	{
		.gpio     = 54,		/* TOUCH_INT_N_CYP */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pn_2ma_cfg,
		},
	},

	/* MISC */
	{
		.gpio     = 62,		/* SD_CARD_DET_N */
		.settings = {
			[GPIOMUX_SUSPENDED] = &gpio_pn_2ma_cfg,
		},
	},
};

void __init ursa_init_gpiomux(void)
{
	int rc;

	rc = msm_gpiomux_init_dt();
	if (rc) {
		pr_err("%s failed %d\n", __func__, rc);
		return;
	}

	/* shared with all board revisions P0 - EVT */
	msm_gpiomux_install(ursa_audio_configs_common, ARRAY_SIZE(ursa_audio_configs_common));
	msm_gpiomux_install(ursa_camera_configs_common, ARRAY_SIZE(ursa_camera_configs_common));
	msm_gpiomux_install(ursa_sensor_configs_common, ARRAY_SIZE(ursa_sensor_configs_common));
	msm_gpiomux_install_nowrite(ursa_display_configs, ARRAY_SIZE(ursa_display_configs));
	msm_gpiomux_install(ursa_nfc_configs, ARRAY_SIZE(ursa_nfc_configs));
	msm_gpiomux_install(ursa_system_configs_common, ARRAY_SIZE(ursa_system_configs_common));
	msm_gpiomux_install(ursa_touch_configs_common, ARRAY_SIZE(ursa_touch_configs_common));

	if (ursa_board_revision() == URSA_REVISION_P1)
	{
		msm_gpiomux_install(ursa_audio_configs_p1, ARRAY_SIZE(ursa_audio_configs_p1));
		msm_gpiomux_install(ursa_bt_wifi_configs_p1, ARRAY_SIZE(ursa_bt_wifi_configs_p1));
		msm_gpiomux_install(ursa_camera_configs_p1p2common, ARRAY_SIZE(ursa_camera_configs_p1p2common));
		msm_gpiomux_install(ursa_sensor_configs_p1_pre_evt, ARRAY_SIZE(ursa_sensor_configs_p1_pre_evt));
		msm_gpiomux_install(ursa_system_configs_p1, ARRAY_SIZE(ursa_system_configs_p1));
		msm_gpiomux_install(ursa_touch_configs_p1, ARRAY_SIZE(ursa_touch_configs_p1));
		/* before pre DVT */
		msm_gpiomux_install(ursa_camera_configs_p1p2evt_common, ARRAY_SIZE(ursa_camera_configs_p1p2evt_common));
	}
	else if (ursa_board_revision() == URSA_REVISION_P0_5)
	{
		/* shared with P1 family*/
		msm_gpiomux_install(ursa_audio_configs_p1, ARRAY_SIZE(ursa_audio_configs_p1));
		msm_gpiomux_install(ursa_bt_wifi_configs_p1, ARRAY_SIZE(ursa_bt_wifi_configs_p1));
		msm_gpiomux_install(ursa_camera_configs_p1p2common, ARRAY_SIZE(ursa_camera_configs_p1p2common));
		msm_gpiomux_install(ursa_sensor_configs_p1_pre_evt, ARRAY_SIZE(ursa_sensor_configs_p1_pre_evt));
		msm_gpiomux_install(ursa_system_configs_p1, ARRAY_SIZE(ursa_system_configs_p1));
		msm_gpiomux_install(ursa_touch_configs_p1, ARRAY_SIZE(ursa_touch_configs_p1));
		/* different for P0.5 */
		msm_gpiomux_install(ursa_configs_p0_5, ARRAY_SIZE(ursa_configs_p0_5));
		/* before pre DVT */
		msm_gpiomux_install(ursa_camera_configs_p1p2evt_common, ARRAY_SIZE(ursa_camera_configs_p1p2evt_common));
	}
	else if ((ursa_board_revision() == URSA_REVISION_P2) ||
		 (ursa_board_revision() == URSA_REVISION_PRE_EVT))
	{
		/* shared with P1 family */
		msm_gpiomux_install(ursa_audio_configs_p1, ARRAY_SIZE(ursa_audio_configs_p1));
		msm_gpiomux_install(ursa_bt_wifi_configs_p1, ARRAY_SIZE(ursa_bt_wifi_configs_p1));
		msm_gpiomux_install(ursa_camera_configs_p1p2common, ARRAY_SIZE(ursa_camera_configs_p1p2common));
		msm_gpiomux_install(ursa_sensor_configs_p1_pre_evt, ARRAY_SIZE(ursa_sensor_configs_p1_pre_evt));
		msm_gpiomux_install(ursa_system_configs_p1, ARRAY_SIZE(ursa_system_configs_p1));
		msm_gpiomux_install(ursa_touch_configs_p1, ARRAY_SIZE(ursa_touch_configs_p1));
		/* different for P2 */
		msm_gpiomux_install(ursa_camera_configs_p2, ARRAY_SIZE(ursa_camera_configs_p2));
		msm_gpiomux_install(ursa_nfc_configs_p2, ARRAY_SIZE(ursa_nfc_configs_p2));
		msm_gpiomux_install(ursa_system_configs_p2, ARRAY_SIZE(ursa_system_configs_p2));
		/* before pre DVT */
		msm_gpiomux_install(ursa_camera_configs_p1p2evt_common, ARRAY_SIZE(ursa_camera_configs_p1p2evt_common));
	}
	else if ((ursa_board_revision() == URSA_REVISION_EVT)  ||
		 (ursa_board_revision() == URSA_REVISION_P0_E))
	{
		/* shared with P1 family */
		msm_gpiomux_install(ursa_audio_configs_p1, ARRAY_SIZE(ursa_audio_configs_p1));
		msm_gpiomux_install(ursa_bt_wifi_configs_p1, ARRAY_SIZE(ursa_bt_wifi_configs_p1));
		msm_gpiomux_install(ursa_camera_configs_p1p2common, ARRAY_SIZE(ursa_camera_configs_p1p2common));
		msm_gpiomux_install(ursa_system_configs_p1, ARRAY_SIZE(ursa_system_configs_p1));
		msm_gpiomux_install(ursa_touch_configs_p1, ARRAY_SIZE(ursa_touch_configs_p1));
		/* shared with P2 */
		msm_gpiomux_install(ursa_camera_configs_p2, ARRAY_SIZE(ursa_camera_configs_p2));
		msm_gpiomux_install(ursa_nfc_configs_p2, ARRAY_SIZE(ursa_nfc_configs_p2));
		msm_gpiomux_install(ursa_system_configs_p2, ARRAY_SIZE(ursa_system_configs_p2));
		/* before pre DVT */
		msm_gpiomux_install(ursa_camera_configs_p1p2evt_common, ARRAY_SIZE(ursa_camera_configs_p1p2evt_common));
	}
	else if ((ursa_board_revision() == URSA_REVISION_PRE_DVT) )
	{
		/* shared with P1 family */
		msm_gpiomux_install(ursa_audio_configs_p1, ARRAY_SIZE(ursa_audio_configs_p1));
		msm_gpiomux_install(ursa_bt_wifi_configs_p1, ARRAY_SIZE(ursa_bt_wifi_configs_p1));
		msm_gpiomux_install(ursa_camera_configs_p1p2common, ARRAY_SIZE(ursa_camera_configs_p1p2common));
		msm_gpiomux_install(ursa_system_configs_p1, ARRAY_SIZE(ursa_system_configs_p1));
		msm_gpiomux_install(ursa_touch_configs_p1, ARRAY_SIZE(ursa_touch_configs_p1));
		/* shared with P2 */
		msm_gpiomux_install(ursa_nfc_configs_p2, ARRAY_SIZE(ursa_nfc_configs_p2));
		msm_gpiomux_install(ursa_system_configs_p2, ARRAY_SIZE(ursa_system_configs_p2));
		/* pre DVT */
		msm_gpiomux_install(ursa_camera_configs_pre_dvt, ARRAY_SIZE(ursa_camera_configs_pre_dvt));
	}
	else if ((ursa_board_revision() == URSA_REVISION_DVT))
	{
		/* shared with P1 family */
		msm_gpiomux_install(ursa_audio_configs_p1, ARRAY_SIZE(ursa_audio_configs_p1));
		msm_gpiomux_install(ursa_bt_wifi_configs_p1, ARRAY_SIZE(ursa_bt_wifi_configs_p1));
		msm_gpiomux_install(ursa_camera_configs_p1p2common, ARRAY_SIZE(ursa_camera_configs_p1p2common));
		msm_gpiomux_install(ursa_system_configs_p1, ARRAY_SIZE(ursa_system_configs_p1));
		msm_gpiomux_install(ursa_touch_configs_p1, ARRAY_SIZE(ursa_touch_configs_p1));
		/* shared with P2 */
		msm_gpiomux_install(ursa_nfc_configs_p2, ARRAY_SIZE(ursa_nfc_configs_p2));
		msm_gpiomux_install(ursa_system_configs_p2, ARRAY_SIZE(ursa_system_configs_p2));
		/* DVT */
		msm_gpiomux_install(ursa_camera_configs_dvt, ARRAY_SIZE(ursa_camera_configs_dvt));
	}
	else
	{
		pr_crit("%s - Invalid board revision\n", __func__);
		panic("ursa-gpiomux: Invalid board revision");
	}

	// Switch camera MCLK source to alternate clocks
	if (socinfo_get_version() >= 0x20000) {
		msm_tlmm_misc_reg_write(TLMM_SPARE_REG, 0x5);
	}
}
