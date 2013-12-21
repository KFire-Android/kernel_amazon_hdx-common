/**
 * virtualinput.c
 *
 * Copyright (c) 2012, Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <linux/platform_device.h>

#include "virtualinput.h"

struct input_dev *virtual_input_dev;        /* Representation of an input device */
static struct platform_device *virtual_plat_dev; /* Device structure */

/* Sysfs method to input simulated
   coordinates to the virtual
   touch driver */
static ssize_t write_virtual_events(struct device *dev,
	struct device_attribute *attr,
	const char *buffer, size_t count)
{
	static int previous_touch;
	static int previous_x = -1;
	static int previous_y = -1;
	int type;
	const unsigned char* charPtr;
	const int* intPtr;

	int x, y;
	unsigned char touch;
	int key;
	int size;

	if (count < 4)
		return count;

	intPtr = (int*) buffer;
	type = *intPtr;
	++intPtr;

	switch (type) {
	case 1:
		// Keyboard input
		size = 2 * sizeof(int) + 1;

		if (count != size) {
			printk("wrong number of bytes allocated: expected %d, got %d.\n", count, size);
			break;
		}

		key = *intPtr;
		intPtr++;

		charPtr = (unsigned char *) intPtr;
		touch = *charPtr;

		if (touch == 1) {  // press
			input_report_key(virtual_input_dev, key, 1);
		}
		else if (touch == 0 && previous_touch == 1) {   //touchdown
			input_report_key(virtual_input_dev, key, 0);
		}

		input_sync(virtual_input_dev);
		previous_touch = touch;

		break;
	case 2:
		// Touch event
		size = 3 * sizeof(int) + 1;

		if (count != size) {
			printk("wrong number of bytes allocated: expected %d, got %d.\n", count, size);
		break;
	}

			x = *intPtr;
			intPtr++;

			y = *intPtr;
			intPtr++;

			charPtr = (unsigned char *) intPtr;
			touch = *charPtr;

			if (touch == 0 && previous_touch == 1) {  //unpress
				input_report_key(virtual_input_dev, BTN_TOUCH, 0);
				previous_x = -1;
				previous_y = -1;
			}
			else if (touch == 1) {   //touchdown
			    if(previous_x != x) {
					input_report_abs(virtual_input_dev, ABS_X, x);
					previous_x = x;
				}
				if(previous_y != y) {
					input_report_abs(virtual_input_dev, ABS_Y, y);
					previous_y = y;
				}
				if(previous_touch == 0) {
					input_report_key(virtual_input_dev, BTN_TOUCH, 1);
				}
			}

			input_sync(virtual_input_dev);
			previous_touch = touch;

			break;
		default:
			break;
	}

	return count;
}

/* Attach the sysfs write method */
/*
 * This allows an application running on device to simply write data to
 * /sys/devices/platform/vms/virtualevents
 */
DEVICE_ATTR(virtualevents, 0644, NULL, write_virtual_events);

/* Attribute Descriptor */
static struct attribute *virtual_input_attrs[] = {
	&dev_attr_virtualevents.attr,
	NULL
};

/* Attribute group */
static struct attribute_group virtual_input_attr_group = {
	.attrs = virtual_input_attrs,
};

/* Driver Initialization */
int __init virtual_input_init(void)
{
	size_t i;

	/* Register a platform device */
	virtual_plat_dev = platform_device_register_simple("vms", -1, NULL, 0);
	if (IS_ERR(virtual_plat_dev)) {
		printk("virtual_input_init: error\n");
		return PTR_ERR(virtual_plat_dev);
	}

	/* Create a sysfs node to read simulated events */
	if (sysfs_create_group(&virtual_plat_dev->dev.kobj, &virtual_input_attr_group)) {
		printk("virtual_input_init: Failed to create sysfs group\n");
	}

	/* Allocate an input device data structure */
	virtual_input_dev = input_allocate_device();
	if (!virtual_input_dev) {
		printk("input_alloc_device() failed\n");
		return -ENOMEM;
	}
	virtual_input_dev->name = "virtual_input";

	/* Announce that the virtual input device will generate absolute
	 * coordinates, and keyboard events */
	set_bit(EV_ABS, virtual_input_dev->evbit);
	set_bit(EV_KEY, virtual_input_dev->evbit);

	set_bit(ABS_X, virtual_input_dev->absbit);
	set_bit(ABS_Y, virtual_input_dev->absbit);
	set_bit(BTN_TOUCH, virtual_input_dev->keybit);

	// Set every key
	for (i = 0; i < KEY_CNT; ++i) {
		set_bit(i, virtual_input_dev->keybit);
	}

	input_set_abs_params(virtual_input_dev, ABS_X, 0, 400, 0, 0);
	input_set_abs_params(virtual_input_dev, ABS_Y, 0, 400, 0, 0);


	/* Register with the input subsystem */
	if(input_register_device(virtual_input_dev)) {
		printk("virtual_input_init: Failed to register input device\n");
	}

	printk("Virtual Input Driver Initialized.\n");
	return 0;
}

/* Driver Exit */
void virtual_input_cleanup(void)
{

	/* Unregister from the input subsystem */
	input_unregister_device(virtual_input_dev);

	/* Cleanup sysfs node */
	sysfs_remove_group(&virtual_plat_dev->dev.kobj, &virtual_input_attr_group);

	/* Unregister driver */
	platform_device_unregister(virtual_plat_dev);

	return;
}

module_init(virtual_input_init);
module_exit(virtual_input_cleanup);

MODULE_AUTHOR("Cullen Logan <cullenl@amazon.com>");
MODULE_DESCRIPTION("Amazon Virtual Input Driver");
MODULE_LICENSE("GPL");
