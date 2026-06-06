// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cm-psu.c - Linux driver for Cooler Master power supplies with HID interface
 * Copyright (C) 2023 Jannis Mast <jannis@ctrl-c.xyz>
 *
 * Based on corsair-psu.c
 * Copyright (C) 2020 Wilken Gottwalt <wilken.gottwalt@posteo.net>
 *
 * X SILENT Edge Platinum support (PID 0x020C) added 2026.
 * That model speaks a DIFFERENT protocol than the MasterPlus devices:
 *  - Binary (not ASCII), little-endian values
 *  - Report 3 (23 bytes incl. report-id) = full telemetry, streamed by the
 *    device on its own (no request needed)
 *  - Report 4 (2 bytes incl. report-id)  = hotspot temperature in degC
 * Layout of report 3 payload (data[1..22], data[0] = report id 0x03):
 *   [0,1] u16 Vin/10   [2] u8 Iin/10   [3,4] u16 Pin   [5,6] u16 Pout
 *   [7] u8 V12/10      [8,9] u16 I12/10    [10,11] u16 P12
 *   [12] u8 V3.3/10    [13,14] u16 I3.3/10 [15,16] u16 P3.3
 *   [17] u8 V5/10      [18,19] u16 I5/10   [20,21] u16 P5
 * Verified against MasterCTRL/HWiNFO and via P = U*I on all rails.
 */

#include <linux/errno.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

/*
 * MasterPlus protocol information (unchanged):
 * - The PSU sends HID events without having to send a request first
 * - Events contain human-readable strings of the form [{type}{channel}{value}]
 *   (see original driver comment for full details)
 */

#define DRIVER_NAME "cm-psu"

/* protocol selector, stored per device via hid_device_id.driver_data */
enum cmpsu_proto {
	CMPSU_MASTERPLUS = 0,
	CMPSU_XSILENT    = 1,
};

/* ---- MasterPlus counts (original) ---- */
#define COUNT_VOLTAGE 5
#define COUNT_CURRENT 5
#define COUNT_POWER   2
#define COUNT_TEMP    2
#define COUNT_FAN     1

#define EVENT_LEN 16

/* ---- X SILENT counts ---- */
#define XS_COUNT_VOLTAGE 4	/* V_AC, +12V, +3.3V, +5V */
#define XS_COUNT_CURRENT 4	/* I_AC, I_+12V, I_+3.3V, I_+5V */
#define XS_COUNT_POWER   5	/* P_in, P_out, P_+12V, P_+3.3V, P_+5V */
#define XS_COUNT_TEMP    1

#define XS_REPORT_DATA 0x03
#define XS_REPORT_TEMP 0x04
#define XS_LEN_DATA    23
#define XS_LEN_TEMP    2

struct cmpsu_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	int proto;
	/* MasterPlus values */
	long values_voltage[COUNT_VOLTAGE];
	long values_current[COUNT_CURRENT];
	long values_power[COUNT_POWER];
	long values_temp[COUNT_TEMP];
	long values_fan[COUNT_FAN];
	/* X SILENT values */
	long xs_in[XS_COUNT_VOLTAGE];
	long xs_curr[XS_COUNT_CURRENT];
	long xs_power[XS_COUNT_POWER];
	long xs_temp[XS_COUNT_TEMP];
};

/* =========================== MasterPlus path =========================== */

static const char *cmpsu_labels_voltage[] = {
	"V_AC",
	"+5V",
	"+3.3V",
	"+12V2",
	/* Reverse order because this one is present on single-rail PSUs */
	"+12V1",
};

static const char *cmpsu_labels_current[] = {
	"I_AC",
	"I_+5V",
	"I_+3.3V",
	"I_+12V2",
	"I_+12V1",
};

static const char *cmpsu_labels_power[] = {
	"P_in",
	"P_out",
};

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

static const struct hwmon_channel_info *cmpsu_info[] = {
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

/* ============================ X SILENT path ============================ */

static const char *cmpsu_xs_labels_voltage[] = {
	"V_AC", "+12V", "+3.3V", "+5V",
};

static const char *cmpsu_xs_labels_current[] = {
	"I_AC", "I_+12V", "I_+3.3V", "I_+5V",
};

static const char *cmpsu_xs_labels_power[] = {
	"P_in", "P_out", "P_+12V", "P_+3.3V", "P_+5V",
};

static umode_t cmpsu_xs_is_visible(const void *data,
		enum hwmon_sensor_types type, u32 attr, int channel)
{
	switch (type) {
		case hwmon_in:
			if (channel < XS_COUNT_VOLTAGE)
				return 0444;
			break;
		case hwmon_curr:
			if (channel < XS_COUNT_CURRENT)
				return 0444;
			break;
		case hwmon_power:
			if (channel < XS_COUNT_POWER)
				return 0444;
			break;
		case hwmon_temp:
			if (channel < XS_COUNT_TEMP)
				return 0444;
			break;
		default:
			break;
	}

	return 0;
}

static int cmpsu_xs_read(struct device *dev, enum hwmon_sensor_types type,
		u32 attr, int channel, long *val)
{
	struct cmpsu_data *priv = dev_get_drvdata(dev);

	switch (type) {
		case hwmon_in:
			if (channel < XS_COUNT_VOLTAGE) {
				if (priv->xs_in[channel] == -1)
					return -ENODATA;
				*val = priv->xs_in[channel];
				return 0;
			}
			break;
		case hwmon_curr:
			if (channel < XS_COUNT_CURRENT) {
				if (priv->xs_curr[channel] == -1)
					return -ENODATA;
				*val = priv->xs_curr[channel];
				return 0;
			}
			break;
		case hwmon_power:
			if (channel < XS_COUNT_POWER) {
				if (priv->xs_power[channel] == -1)
					return -ENODATA;
				*val = priv->xs_power[channel];
				return 0;
			}
			break;
		case hwmon_temp:
			if (channel < XS_COUNT_TEMP) {
				if (priv->xs_temp[channel] == -1)
					return -ENODATA;
				*val = priv->xs_temp[channel];
				return 0;
			}
			break;
		default:
			break;
	}

	return -EOPNOTSUPP;
}

static int cmpsu_xs_read_string(struct device *dev,
		enum hwmon_sensor_types type, u32 attr,
		int channel, const char **str)
{
	if (type == hwmon_in && attr == hwmon_in_label
	    && channel < XS_COUNT_VOLTAGE) {
		*str = cmpsu_xs_labels_voltage[channel];
		return 0;
	} else if (type == hwmon_curr && attr == hwmon_curr_label
	           && channel < XS_COUNT_CURRENT) {
		*str = cmpsu_xs_labels_current[channel];
		return 0;
	} else if (type == hwmon_power && attr == hwmon_power_label
	           && channel < XS_COUNT_POWER) {
		*str = cmpsu_xs_labels_power[channel];
		return 0;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops cmpsu_xs_hwmon_ops = {
	.is_visible = cmpsu_xs_is_visible,
	.read = cmpsu_xs_read,
	.read_string = cmpsu_xs_read_string,
};

static const struct hwmon_channel_info *cmpsu_xs_info[] = {
	HWMON_CHANNEL_INFO(temp,
				HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(in,
				HWMON_I_INPUT | HWMON_I_LABEL,
				HWMON_I_INPUT | HWMON_I_LABEL,
				HWMON_I_INPUT | HWMON_I_LABEL,
				HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
				HWMON_C_INPUT | HWMON_C_LABEL,
				HWMON_C_INPUT | HWMON_C_LABEL,
				HWMON_C_INPUT | HWMON_C_LABEL,
				HWMON_C_INPUT | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(power,
				HWMON_P_INPUT | HWMON_P_LABEL,
				HWMON_P_INPUT | HWMON_P_LABEL,
				HWMON_P_INPUT | HWMON_P_LABEL,
				HWMON_P_INPUT | HWMON_P_LABEL,
				HWMON_P_INPUT | HWMON_P_LABEL),
	NULL
};

static const struct hwmon_chip_info cmpsu_xs_chip_info = {
	.ops = &cmpsu_xs_hwmon_ops,
	.info = cmpsu_xs_info,
};

/* little-endian u16 from payload */
static inline u16 cmpsu_xs_u16(const u8 *d, int i)
{
	return d[i] | (d[i + 1] << 8);
}

static void cmpsu_xs_parse(struct cmpsu_data *priv, u8 *data, int size)
{
	/* data[0] = report id; payload starts at data[1] */
	if (data[0] == XS_REPORT_DATA && size == XS_LEN_DATA) {
		/* voltages: byte/10 V -> millivolt = byte*100 */
		priv->xs_in[0] = (long)cmpsu_xs_u16(data, 1) * 100; /* V_AC */
		priv->xs_in[1] = (long)data[8] * 100;               /* +12V */
		priv->xs_in[2] = (long)data[13] * 100;              /* +3.3V */
		priv->xs_in[3] = (long)data[18] * 100;              /* +5V */
		/* currents: value/10 A -> milliamp = value*100 */
		priv->xs_curr[0] = (long)data[3] * 100;                 /* I_AC */
		priv->xs_curr[1] = (long)cmpsu_xs_u16(data, 9) * 100;   /* I_+12V */
		priv->xs_curr[2] = (long)cmpsu_xs_u16(data, 14) * 100;  /* I_+3.3V */
		priv->xs_curr[3] = (long)cmpsu_xs_u16(data, 19) * 100;  /* I_+5V */
		/* powers: watt -> microwatt = watt*1000000 */
		priv->xs_power[0] = (long)cmpsu_xs_u16(data, 4) * 1000000;  /* P_in */
		priv->xs_power[1] = (long)cmpsu_xs_u16(data, 6) * 1000000;  /* P_out */
		priv->xs_power[2] = (long)cmpsu_xs_u16(data, 11) * 1000000; /* P_+12V */
		priv->xs_power[3] = (long)cmpsu_xs_u16(data, 16) * 1000000; /* P_+3.3V */
		priv->xs_power[4] = (long)cmpsu_xs_u16(data, 21) * 1000000; /* P_+5V */
	} else if (data[0] == XS_REPORT_TEMP && size == XS_LEN_TEMP) {
		/* hotspot temperature in degC -> millidegree */
		priv->xs_temp[0] = (long)data[1] * 1000;
	}
}

/* ============================== shared =============================== */

static int cmpsu_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct cmpsu_data *priv;
	const struct hwmon_chip_info *chip;
	int ret;
	int i;

	priv = devm_kzalloc(&hdev->dev, sizeof(struct cmpsu_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->proto = id->driver_data;

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

	if (priv->proto == CMPSU_XSILENT) {
		for (i = 0; i < XS_COUNT_VOLTAGE; i++)
			priv->xs_in[i] = -1;
		for (i = 0; i < XS_COUNT_CURRENT; i++)
			priv->xs_curr[i] = -1;
		for (i = 0; i < XS_COUNT_POWER; i++)
			priv->xs_power[i] = -1;
		for (i = 0; i < XS_COUNT_TEMP; i++)
			priv->xs_temp[i] = -1;
		chip = &cmpsu_xs_chip_info;
	} else {
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
		chip = &cmpsu_chip_info;
	}

	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "cmpsu",
				priv, chip, NULL);
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

	if (priv->proto == CMPSU_XSILENT) {
		cmpsu_xs_parse(priv, data, size);
		return 0;
	}

	/* ---- MasterPlus ASCII protocol (unchanged) ---- */
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
	{ HID_USB_DEVICE(0x2516, 0x020C),   /* X SILENT Edge Platinum 1100 */
	  .driver_data = CMPSU_XSILENT },
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
