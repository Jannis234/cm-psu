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
 * This driver currently supports the ASCII protocol implemented by Cooler
 * Master's MasterPlus software and supported PSUs
 * 
 * The PSU continuously sends HID events when powered on, containing human
 * readable ASCII strings. The format is:
 * [${type}${channel}${value}]
 * The type is encoded with a single character. Valid types are voltage (V),
 * current (I), power (P), temperature (T) and fan speed (F)
 * Power value P2 is a special case and contains two values separated by a /
 * 
 * Quirks/missing features:
 * - Because the driver is passive, it doesn't know how many channels the PSU
 *   acutally has until data arrives. This results in +12V2 always reporting
 *   N/A on single-rail devices
 * - The temperature channels don't have labels (MasterPlus only shows one)
 * - Channel P1 is currently ignored because it is unclear what this value
 *   means (MasterPlus doesn't display it)
 * - Fan control is not implemented
 * - Cooler Master XG650/750/850 PSUs are not supported. These models are
 *   supported by MasterPlus but seem to use a different protocol
 */

#define DRIVER_NAME "cm-psu"

/* Channel count for the ASCII protocol */
#define ASCII_COUNT_VOLTAGE 5
#define ASCII_COUNT_CURRENT 5
#define ASCII_COUNT_POWER   2
#define ASCII_COUNT_TEMP    2
#define ASCII_COUNT_FAN     1

/* Maximum channel count for all protocols */
#define COUNT_VOLTAGE 5
#define COUNT_CURRENT 5
#define COUNT_POWER   2
#define COUNT_TEMP    2
#define COUNT_FAN     1

#define ASCII_EVENT_LEN 16

static const char* cmpsu_labels_ascii_voltage[] = {
	"V_AC",
	"+5V",
	"+3.3V",
	"+12V2",
	/* Reverse order because this one is present on single-rail PSUs */
	"+12V1",
};
static const char* cmpsu_labels_ascii_current[] = {
	"I_AC",
	"I_+5V",
	"I_+3.3V",
	"I_+12V2",
	"I_+12V1",
};
static const char* cmpsu_labels_ascii_power[] = {
	"P_in",
	"P_out",
};

enum cmpsu_protocol {
	CMPSU_PROTO_ASCII,
};

struct cmpsu_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	enum cmpsu_protocol protocol;
	u8 count_voltage;
	u8 count_current;
	u8 count_power;
	u8 count_temp;
	u8 count_fan;
	const char **labels_voltage;
	const char **labels_current;
	const char **labels_power;
	long values_voltage[COUNT_VOLTAGE];
	long values_current[COUNT_CURRENT];
	long values_power[COUNT_POWER];
	long values_temp[COUNT_TEMP];
	long values_fan[COUNT_FAN];
};

static umode_t cmpsu_hwmon_is_visible(const void *data,
			enum hwmon_sensor_types type, u32 attr, int channel)
{
	const struct cmpsu_data *priv = data;
	
	switch (type) {
	case hwmon_in:
		if (channel < priv->count_voltage)
			return 0444;
		break;
	case hwmon_curr:
		if (channel < priv->count_current)
			return 0444;
		break;
	case hwmon_power:
		if (channel < priv->count_power)
			return 0444;
		break;
	case hwmon_temp:
		if (channel < priv->count_temp)
			return 0444;
		break;
	case hwmon_fan:
		if (channel < priv->count_fan)
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
		if (channel < priv->count_voltage) {
			if (priv->values_voltage[channel] == -1) {
				err = -ENODATA;
			} else {
				*val = priv->values_voltage[channel];
				err = 0;
			}
		}
		break;
	case hwmon_curr:
		if (channel < priv->count_current) {
			if (priv->values_current[channel] == -1) {
				err = -ENODATA;
			} else {
				*val = priv->values_current[channel];
				err = 0;
			}
		}
		break;
	case hwmon_power:
		if (channel < priv->count_power) {
			if (priv->values_power[channel] == -1) {
				err = -ENODATA;
			} else {
				*val = priv->values_power[channel];
				err = 0;
			}
		}
		break;
	case hwmon_temp:
		if (channel < priv->count_temp) {
			if (priv->values_temp[channel] == -1) {
				err = -ENODATA;
			} else {
				*val = priv->values_temp[channel];
				err = 0;
			}
		}
		break;
	case hwmon_fan:
		if (channel < priv->count_fan) {
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
	struct cmpsu_data *priv = dev_get_drvdata(dev);
	
	if (type == hwmon_in && attr == hwmon_in_label \
	    && channel < priv->count_voltage) {
		*str = priv->labels_voltage[channel];
		return 0;
	} else if (type == hwmon_curr && attr == hwmon_curr_label
	           && channel < priv->count_current) {
		*str = priv->labels_current[channel];
		return 0;
	} else if (type == hwmon_power && attr == hwmon_power_label
	           && channel < priv->count_power) {
		*str = priv->labels_power[channel];
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
	
	priv->protocol = id->driver_data;
	if (id->driver_data == CMPSU_PROTO_ASCII) {
		priv->count_voltage = ASCII_COUNT_VOLTAGE;
		priv->count_current = ASCII_COUNT_CURRENT;
		priv->count_power = ASCII_COUNT_POWER;
		priv->count_temp = ASCII_COUNT_TEMP;
		priv->count_fan = ASCII_COUNT_FAN;
		priv->labels_voltage = cmpsu_labels_ascii_voltage;
		priv->labels_current = cmpsu_labels_ascii_current;
		priv->labels_power = cmpsu_labels_ascii_power;
	}
	
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

static int cmpsu_parse_ascii(struct cmpsu_data *priv,
			struct hid_report *report, u8 *data, int size)
{
	char type;
	unsigned int channel;
	unsigned int value1;
	unsigned int value2;
	
	if (size != ASCII_EVENT_LEN)
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

static int cmpsu_raw_event(struct hid_device *hdev, struct hid_report *report,
			u8 *data, int size)
{
	struct cmpsu_data *priv = hid_get_drvdata(hdev);
	
	switch (priv->protocol) {
	case CMPSU_PROTO_ASCII:
		return cmpsu_parse_ascii(priv, report, data, size);
	}
	return 0;
}

static const struct hid_device_id cmpsu_idtable[] = {
	/* Device names and IDs extracted from DeviceList.cfg in MasterPlus */
	/* MasterWatt 1200 */
	{ HID_USB_DEVICE(0x2516, 0x0030), .driver_data = CMPSU_PROTO_ASCII },
	/* V550 GOLD i MULTI */
	{ HID_USB_DEVICE(0x2516, 0x018D), .driver_data = CMPSU_PROTO_ASCII },
	/* V650 GOLD i MULTI */
	{ HID_USB_DEVICE(0x2516, 0x018F), .driver_data = CMPSU_PROTO_ASCII },
	/* V750 GOLD i MULTI */
	{ HID_USB_DEVICE(0x2516, 0x0191), .driver_data = CMPSU_PROTO_ASCII },
	/* V850 GOLD i MULTI */
	{ HID_USB_DEVICE(0x2516, 0x0193), .driver_data = CMPSU_PROTO_ASCII },
	/* V550 GOLD i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x0195), .driver_data = CMPSU_PROTO_ASCII },
	/* V650 GOLD i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x0197), .driver_data = CMPSU_PROTO_ASCII },
	/* V750 GOLD i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x0199), .driver_data = CMPSU_PROTO_ASCII },
	/* V850 GOLD i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x019B), .driver_data = CMPSU_PROTO_ASCII },
	/* V650 PLATINUM i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x019D), .driver_data = CMPSU_PROTO_ASCII },
	/* V750 PLATINUM i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x019F), .driver_data = CMPSU_PROTO_ASCII },
	/* V850 PLATINUM i 12VO */
	{ HID_USB_DEVICE(0x2516, 0x01A1), .driver_data = CMPSU_PROTO_ASCII },
	/* FANLESS 1300 */
	{ HID_USB_DEVICE(0x2516, 0x01A5), .driver_data = CMPSU_PROTO_ASCII },
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
