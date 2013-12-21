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

#include <linux/of.h>
#include <mach/board-detect.h>


bool board_is_thor(void)
{
#if defined(CONFIG_IDME)
	struct device_node *ap;
	int len;

	ap = of_find_node_by_path("/idme/board_id");
	if (ap) {
		const char *boardid = of_get_property(ap, "value", &len);
		if (len >= 2)
			if (boardid[0] == '0' && boardid[1] == 'c')
				return true;
	}
#endif
	return false;
}

bool board_is_apollo(void)
{
#if defined(CONFIG_IDME)
	struct device_node *ap;
	int len;

	ap = of_find_node_by_path("/idme/board_id");
	if (ap) {
		const char *boardid = of_get_property(ap, "value", &len);
		if (len >= 2)
			if (boardid[0] == '0' && boardid[1] == 'd')
				return true;
	}
#endif
	return false;
}

bool board_has_qca6234()
{
	if (machine_is_thor() || machine_is_apollo() || machine_is_galvajem())
		return true;
#if defined(CONFIG_IDME)
	else {
		struct device_node *ap;
		int len;

		ap = of_find_node_by_path("/idme/board_id");
		if (ap) {
			const char *boardid = of_get_property(ap, "value", &len);
			if (len >= 2)
				// f00010 indicates msm8974 + qca
				if (boardid[0] == 'f' && boardid[1] == '0' &&
					boardid[4] == '1')
					return true;
		}
	}
#endif

	return false;
}
