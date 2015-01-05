/* Copyright (c) 2011, The Linux Foundation. All rights reserved.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>

int msm_show_resume_irq_mask = 1;

/**
 * Hacked routine for Thor/Apollo to map IRQs
 * with their corresponding wakeup events. Ignore hardcoded
 * numbers here, this is only for PoC on Wifi-only devices.
 * This change will actually be made into device tree,
 * thus completely removing the need for BSP specific
 * irq_to_wakeup_ev() function.
 *
 * FIXME: This mapping really should be specified in device tree
 */
wakeup_event_t irq_to_wakeup_ev(int irq)
{
	wakeup_event_t ev;

	switch (irq) {
	case 291:
		ev = WEV_PWR;
		break;
	case 320:
		ev = WEV_RTC;
		break;
	/* for WFO devices */
#ifdef CONFIG_ARCH_MSM8974_THOR
	case 335:
		ev = WEV_WIFI;
		break;
	case 628:
		ev = WEV_CHARGER;
		break;
#endif
	/* For apollo */
#ifdef CONFIG_ARCH_MSM8974_APOLLO
	case 338:
		ev = WEV_WIFI;
		break;
	case 630:
		ev = WEV_CHARGER;
		break;
#endif
	default:
		ev = WEV_NONE;
		break;
	}

	return ev;
}
EXPORT_SYMBOL(irq_to_wakeup_ev);

module_param_named(
	debug_mask, msm_show_resume_irq_mask, int, S_IRUGO | S_IWUSR | S_IWGRP
);
