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

#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/module.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>

#include "msm_led_flash.h"
#include "msm.h"

#define FLASH_NAME "camera-led-flash-max8834"

//#define CONFIG_MSMB_CAMERA_DEBUG
#undef CDBG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

static struct msm_led_flash_ctrl_t fctrl;

static int32_t msm_led_max8834_get_subdev_id(struct msm_led_flash_ctrl_t *fctrl,
	void *arg)
{
	uint32_t *subdev_id = (uint32_t *)arg;
	if (!subdev_id) {
		pr_err("%s:%d failed\n", __func__, __LINE__);
		return -EINVAL;
	}
	*subdev_id = fctrl->pdev->id;
	CDBG("%s:%d subdev_id %d\n", __func__, __LINE__, *subdev_id);
	return 0;
}

static int32_t msm_led_max8834_config(struct msm_led_flash_ctrl_t *fctrl,
	void *data)
{
	int rc = 0;
	struct msm_camera_led_cfg_t *cfg = (struct msm_camera_led_cfg_t *)data;
	struct v4l2_subdev* ov680_subdev;
	struct v4l2_control ctrl;
	ctrl.id = MSM_V4L2_PID_OV680_SENSOR_STROBE_MODE;

	CDBG("called led_state %d\n", cfg->cfgtype);

	ov680_subdev = msm_sd_find("ov680");
	if(!ov680_subdev){
		pr_err("%s:%d failed\n", __func__, __LINE__);
		return -EINVAL;
	}

	switch (cfg->cfgtype) {
	case MSM_CAMERA_LED_OFF:
		//Turn LED off
		ctrl.value = 0;
		rc = v4l2_subdev_call(ov680_subdev, core, ioctl,
				VIDIOC_OV680_SENSOR_SET_CTRL,
				&ctrl);
		break;

	case MSM_CAMERA_LED_LOW:
		// Turn LED on
		ctrl.value = 1;
		rc = v4l2_subdev_call(ov680_subdev, core, ioctl,
				VIDIOC_OV680_SENSOR_SET_CTRL,
				&ctrl);
		break;

	// Do nothing
	case MSM_CAMERA_LED_HIGH:
		break;
	case MSM_CAMERA_LED_INIT:
	case MSM_CAMERA_LED_RELEASE:
		//Turn LED off
		ctrl.value = 0;
		rc = v4l2_subdev_call(ov680_subdev, core, ioctl,
				VIDIOC_OV680_SENSOR_SET_CTRL,
				&ctrl);
		break;

	default:
		rc = -EFAULT;
		break;
	}
	CDBG("flash_set_led_state: return %d\n", rc);
	return rc;
}

static const struct of_device_id msm_led_max8834_dt_match[] = {
	{.compatible = "qcom,max8834"},
	{}
};

MODULE_DEVICE_TABLE(of, msm_led_max8834_dt_match);

static struct platform_driver msm_led_max8834_driver = {
	.driver = {
		.name = FLASH_NAME,
		.owner = THIS_MODULE,
		.of_match_table = msm_led_max8834_dt_match,
	},
};

static int32_t msm_led_max8834_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	struct device_node *of_node = pdev->dev.of_node;

	if (!of_node) {
		pr_err("of_node NULL\n");
		return -EINVAL;
	}

	fctrl.pdev = pdev;

	rc = of_property_read_u32(of_node, "cell-index", &pdev->id);
	if (rc < 0) {
		pr_err("failed\n");
		return -EINVAL;
	}
	CDBG("pdev id %d\n", pdev->id);
	rc = msm_led_flash_create_v4lsubdev(pdev, &fctrl);
	return rc;
}

static int __init msm_led_max8834_add_driver(void)
{
	return platform_driver_probe(&msm_led_max8834_driver,
		msm_led_max8834_probe);
}

static struct msm_flash_fn_t msm_led_max8834_func_tbl = {
	.flash_get_subdev_id = msm_led_max8834_get_subdev_id,
	.flash_led_config = msm_led_max8834_config,
};

static struct msm_led_flash_ctrl_t fctrl = {
	.func_tbl = &msm_led_max8834_func_tbl,
};

module_init(msm_led_max8834_add_driver);
MODULE_DESCRIPTION("MAX8834 FLASH");
MODULE_LICENSE("GPL v2");
