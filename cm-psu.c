// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cm-psu.c - Linux driver for Cooler Master power supplies with HID interface
 * Copyright (C) 2023 Jannis Mast <jannis@ctrl-c.xyz>
 * 
 * Based on corsair-psu.c
 * Copyright (C) 2020 Wilken Gottwalt <wilken.gottwalt@posteo.net>
 */

#include <linux/errno.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

/*
 * Protocol information:
 * - The PSU sends HID events without having to send a request first
 * - Events contain human-readable strings
 * - All events appear to be 16 bytes long, padded with zero bytes if needed
 * - The format of each string is:
 *   [{type}{channel}{value}]
 * - Types are a single uppercase letter, channels a single digit
 * - Valid types are:
 *   - V: Voltage (Volt)
 *   - I: Current (Amp)
 *   - P: Power (Watt)
 *   - T: Temperature (°C)
 *   - R: Fan speed (RPM)
 * - Channels start at 1
 * - Values can have different numbers of digits and can include a decimal
 *   point
 * - Special case: Channel P2 includes two values in this format:
 *   [P2{value1}/{value2}]
 *   (No other channels appear to do this)
 * 
 * The protocol has been reverse engineered (if you can even call it that) by
 * looking at the data and referecing with what is displayed by Cooler Master's
 * "MasterPlus" software.
 * The driver has been tested against a V850 Gold i multi PSU. Looking at
 * hardware description files (JSON) included with MasterPlus, all compatible
 * models should use an identical protocol.
 * 
 * Quirks/missing features:
 * - The temperature readings don't have labels because there is no indication
 *   what they acutally are. CM's software only show one temperature.
 * - Channel P1 is currently ignored because it is unclear what value it is
 *   reporting. Looking at the previously mentioned JSON files from MasterPlus,
 *   it is either PFC (Power factor correction?) or EFF (efficiency?). CM's
 *   software doesn't show this value either.
 * - Fan control is not supported. Right now, this driver is entirely passive
 *   and does not send any requests to the PSU. Figuring out the protocol for
 *   setting custom fan curves will likely require sniffing USB traffic from
 *   CM's software (which refuses to run on my machine for some reason).
 * - The XG650/750/850 PSUs may also use the same protocol as they are
 *   supported by MasterPlus - However, those have different sets of channels/
 *   sensors additional code may be needed
 */

#define DRIVER_NAME "cm-psu"

#define COUNT_VOLTAGE 5
#define COUNT_CURRENT 5
#define COUNT_POWER   2
#define COUNT_TEMP    2
#define COUNT_FAN     1

#define EVENT_LEN 16

struct cmpsu_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	long values_voltage[COUNT_VOLTAGE];
	long values_current[COUNT_CURRENT];
	long values_power[COUNT_POWER];
	long values_temp[COUNT_TEMP];
	long values_fan[COUNT_FAN];
};

static const char* cmpsu_labels_voltage[] = {
	"V_AC",
	"+5V",
	"+3.3V",
	"+12V2",
	/* Reverse order because this one is present on single-rail PSUs */
	"+12V1",
};

static const char* cmpsu_labels_current[] = {
	"I_AC",
	"I_+5V",
	"I_+3.3V",
	"I_+12V2",
	"I_+12V1",
};

static const char* cmpsu_labels_power[] = {
	"P_in",
	"P_out",
};

/*long cmpsu_parse_value(u8 *data, int *idx, int fraction_scale,
			bool expect_second);*/

static umode_t cmpsu_hwmon_is_visible(const void *data,
			enum hwmon_sensor_types type, u32 attr, int channel)
{
	switch (type) {
		case hwmon_in:
			if (channel < COUNT_VOLTAGE)
				return 0444;
			break;
		case hwmon_curr:
			if (channel < COUNT_CURRENT)
				return 0444;
			break;
		case hwmon_power:
			if (channel < COUNT_POWER)
				return 0444;
			break;
		case hwmon_temp:
			if (channel < COUNT_TEMP)
				return 0444;
			break;
		case hwmon_fan:
			if (channel < COUNT_FAN)
				return 0444;
			break;
		default:
			break;
	}
	
	return 0;
}

static int cmpsu_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct cmpsu_data *priv = dev_get_drvdata(dev);
	int err = -EOPNOTSUPP;
	
	switch (type) {
		case hwmon_in:
			if (channel < COUNT_VOLTAGE) {
				if (priv->values_voltage[channel] == -1) {
					err = -ENODATA;
				} else {
					*val = priv->values_voltage[channel];
					err = 0;
				}
			}
			break;
		case hwmon_curr:
			if (channel < COUNT_CURRENT) {
				if (priv->values_current[channel] == -1) {
					err = -ENODATA;
				} else {
					*val = priv->values_current[channel];
					err = 0;
				}
			}
			break;
		case hwmon_power:
			if (channel < COUNT_POWER) {
				if (priv->values_power[channel] == -1) {
					err = -ENODATA;
				} else {
					*val = priv->values_power[channel];
					err = 0;
				}
			}
			break;
		case hwmon_temp:
			if (channel < COUNT_TEMP) {
				if (priv->values_temp[channel] == -1) {
					err = -ENODATA;
				} else {
					*val = priv->values_temp[channel];
					err = 0;
				}
			}
			break;
		case hwmon_fan:
			if (channel < COUNT_FAN) {
				if (priv->values_fan[channel] == -1) {
					err = -ENODATA;
				} else {
					*val = priv->values_fan[channel];
					err = 0;
				}
			}
			break;
		default:
			break;
	}
	
	return err;
}

static int cmpsu_hwmon_read_string(struct device *dev,
			enum hwmon_sensor_types type, u32 attr,
			int channel, const char **str)
{
	if (type == hwmon_in && attr == hwmon_in_label \
	    && channel < COUNT_VOLTAGE) {
		*str = cmpsu_labels_voltage[channel];
		return 0;
	} else if (type == hwmon_curr && attr == hwmon_curr_label
	           && channel < COUNT_CURRENT) {
		*str = cmpsu_labels_current[channel];
		return 0;
	} else if (type == hwmon_power && attr == hwmon_power_label
	           && channel < COUNT_POWER) {
		*str = cmpsu_labels_power[channel];
		return 0;
	}
	
	return -EOPNOTSUPP;
}

static const struct hwmon_ops cmpsu_hwmon_ops = {
	.is_visible = cmpsu_hwmon_is_visible,
	.read = cmpsu_hwmon_read,
	.read_string = cmpsu_hwmon_read_string,
};

static const struct hwmon_channel_info* cmpsu_info[] = {
	HWMON_CHANNEL_INFO(temp,
					HWMON_T_INPUT,
					HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(fan,
					HWMON_F_INPUT),
	HWMON_CHANNEL_INFO(in,
					HWMON_I_INPUT | HWMON_I_LABEL,
					HWMON_I_INPUT | HWMON_I_LABEL,
					HWMON_I_INPUT | HWMON_I_LABEL,
					HWMON_I_INPUT | HWMON_I_LABEL,
					HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
					HWMON_C_INPUT | HWMON_C_LABEL,
					HWMON_C_INPUT | HWMON_C_LABEL,
					HWMON_C_INPUT | HWMON_C_LABEL,
					HWMON_C_INPUT | HWMON_C_LABEL,
					HWMON_C_INPUT | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(power,
					HWMON_P_INPUT | HWMON_P_LABEL,
					HWMON_P_INPUT | HWMON_P_LABEL),
	NULL
};

static const struct hwmon_chip_info cmpsu_chip_info = {
	.ops = &cmpsu_hwmon_ops,
	.info = cmpsu_info,
};

static int cmpsu_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct cmpsu_data *priv;
	int ret;
	int i;
	
	priv = devm_kzalloc(&hdev->dev, sizeof(struct cmpsu_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	
	ret = hid_parse(hdev);
	if (ret)
		return ret;
	
	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
		return ret;
	
	ret = hid_hw_open(hdev);
	if (ret)
		return ret;
	
	priv->hdev = hdev;
	hid_set_drvdata(hdev, priv);
	hid_device_io_start(hdev);
	
	for (i = 0; i < COUNT_VOLTAGE; i++)
		priv->values_voltage[i] = -1;
	for (i = 0; i < COUNT_CURRENT; i++)
		priv->values_current[i] = -1;
	for (i = 0; i < COUNT_POWER; i++)
		priv->values_power[i] = -1;
	for (i = 0; i < COUNT_TEMP; i++)
		priv->values_temp[i] = -1;
	for (i = 0; i < COUNT_FAN; i++)
		priv->values_fan[i] = -1;
	
	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "cmpsu",
					priv, &cmpsu_chip_info, NULL);
	if (IS_ERR(priv->hwmon_dev)) {
		ret = PTR_ERR(priv->hwmon_dev);
		hid_hw_close(hdev);
		hid_hw_stop(hdev);
		return ret;
	}
	
	return 0;
}

static void cmpsu_remove(struct hid_device *hdev)
{
	struct cmpsu_data *priv = hid_get_drvdata(hdev);
	
	hwmon_device_unregister(priv->hwmon_dev);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static int cmpsu_raw_event(struct hid_device *hdev, struct hid_report *report,
			u8 *data, int size)
{
	struct cmpsu_data *priv = hid_get_drvdata(hdev);
	char type;
	unsigned int channel;
	unsigned int value1;
	unsigned int value2;
	
	if (size != EVENT_LEN)
		return 0;
	
	/* Make sure the data is null-terminated */
	if (data[size - 1] != 0)
		return 0;
	/* Enforce a minimum length
	 * (square brackets + data type + channel index + value) */
	if (size < 5)
		return 0;
	
	/* Pick the correct format string depending on the packet type */
	switch (data[1]) {
		/* Voltage, current, temperature: Single value with one decimal */
		case 'V':
		case 'I':
		case 'T':
			if (sscanf(data, "[%c%1u%03u.%1u]",
						&type, &channel, &value1, &value2) != 4)
				return 0;
			break;
		/* Fan RPM: Single value, no decimal */
		case 'R':
			if (sscanf(data, "[%c%1u%04u]",
						&type, &channel, &value1) != 3)
				return 0;
			break;
		/* Power: Two values, no decimal */
		case 'P':
			/* Ignore packet P1 */
			if (data[2] != '2')
				return 0;
			if (sscanf(data, "[%c%1u%04u/%04u]",
						&type, &channel, &value1, &value2) != 4)
				return 0;
			break;
		default:
			return 0;
	}
	
	if (channel < 0)
		return 0;
	/* Index from the device starts at 1 */
	channel -= 1;
	
	switch (type) {
		case 'V':
			if (channel >= COUNT_VOLTAGE)
				return 0;
			priv->values_voltage[channel] = (value1 * 1000) + (value2 * 100);
			break;
		case 'I':
			if (channel >= COUNT_CURRENT)
				return 0;
			priv->values_current[channel] = (value1 * 1000) + (value2 * 100);
			break;
		case 'T':
			if (channel >= COUNT_TEMP)
				return 0;
			priv->values_temp[channel] = (value1 * 1000) + (value2 * 100);
			break;
		case 'R':
			if (channel >= COUNT_FAN)
				return 0;
			priv->values_fan[channel] = value1;
			break;
		case 'P':
			if (channel != 1)
				return 0;
			priv->values_power[0] = value1 * 1000000;
			priv->values_power[1] = value2 * 1000000;
			break;
	}
	
	return 0;
}

/* Pulled from MasterPlus' DeviceList.cfg (may contain unreleased models) */
static const struct hid_device_id cmpsu_idtable[] = {
	{ HID_USB_DEVICE(0x2516, 0x0030) }, /* MasterWatt 1200 */
	{ HID_USB_DEVICE(0x2516, 0x018D) }, /* V550 GOLD i MULTI */
	{ HID_USB_DEVICE(0x2516, 0x018F) }, /* V650 GOLD i MULTI */
	{ HID_USB_DEVICE(0x2516, 0x0191) }, /* V750 GOLD i MULTI */
	{ HID_USB_DEVICE(0x2516, 0x0193) }, /* V850 GOLD i MULTI */
	{ HID_USB_DEVICE(0x2516, 0x0195) }, /* V550 GOLD i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x0197) }, /* V650 GOLD i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x0199) }, /* V750 GOLD i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x019B) }, /* V850 GOLD i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x019D) }, /* V650 PLATINUM i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x019F) }, /* V750 PLATINUM i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x01A1) }, /* V850 PLATINUM i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x01A5) }, /* FANLESS 1300 */
	{ }
};
MODULE_DEVICE_TABLE(hid, cmpsu_idtable);

static struct hid_driver cmpsu_driver = {
	.name = DRIVER_NAME,
	.id_table = cmpsu_idtable,
	.probe = cmpsu_probe,
	.remove = cmpsu_remove,
	.raw_event = cmpsu_raw_event,
};
module_hid_driver(cmpsu_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jannis Mast <jannis@ctrl-c.xyz>");
MODULE_DESCRIPTION("Driver for Cooler Master power supplies with HID interface");
