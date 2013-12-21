/* Copyright (c) 2013, Amazon.com. All rights reserved.
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

#ifndef __ARCH_ARM_MACH_MSM_BOARD_DETECT_H
#define __ARCH_ARM_MACH_MSM_BOARD_DETECT_H

#include <asm/system_info.h>

// Devicetree defines
#ifdef CONFIG_OF
#define early_machine_is_apollo()		\
	of_flat_dt_is_compatible(of_get_flat_dt_root(), "amazon,apollo")
#define early_machine_is_galvajem()		\
	of_flat_dt_is_compatible(of_get_flat_dt_root(), "amazon,galvajem")
#define early_machine_is_thor()		\
	of_flat_dt_is_compatible(of_get_flat_dt_root(), "amazon,thor")
#define machine_is_apollo()		\
	of_machine_is_compatible("amazon,apollo")
#define machine_is_galvajem()		\
	of_machine_is_compatible("amazon,galvajem")
#define machine_is_thor()		\
	of_machine_is_compatible("amazon,thor")
#endif

// Thor/Apollo section
bool board_is_thor(void);
bool board_is_apollo(void);

// msm8974 and mixed section
bool board_has_qca6234(void);

#endif
