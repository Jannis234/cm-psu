#include <linux/errno.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

#define DRIVER_NAME "cm-psu"

#define COUNT_VOLTAGE 5
#define COUNT_CURRENT 5
#define COUNT_POWER 2
#define COUNT_TEMP 2
#define COUNT_FAN 1

#define EVENT_LEN_MIN 5  /* Minimum that can include data */
#define EVENT_LEN_MAX 64 /* Prevent long parsing on obviously invalid data */

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
	"+12V1", /* Reverse order because this one is present on single-rail models */
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

static umode_t cmpsu_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel) {
	
	switch (type) {
		case hwmon_in:
			if (channel < COUNT_VOLTAGE) {
				return 0444;
			}
			break;
		case hwmon_curr:
			if (channel < COUNT_CURRENT) {
				return 0444;
			}
			break;
		case hwmon_power:
			if (channel < COUNT_POWER) {
				return 0444;
			}
			break;
		case hwmon_temp:
			if (channel < COUNT_TEMP) {
				return 0444;
			}
			break;
		case hwmon_fan:
			if (channel < COUNT_FAN) {
				return 0444;
			}
			break;
		default:
			break;
	}
	
	return 0;
	
}

static int cmpsu_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val) {
	
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

static int cmpsu_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, const char **str) {
	
	if (type == hwmon_in && attr == hwmon_in_label && channel < COUNT_VOLTAGE) {
		*str = cmpsu_labels_voltage[channel];
		return 0;
	} else if (type == hwmon_curr && attr == hwmon_curr_label && channel < COUNT_CURRENT) {
		*str = cmpsu_labels_current[channel];
		return 0;
	} else if (type == hwmon_power && attr == hwmon_power_label && channel < COUNT_POWER) {
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

static int cmpsu_probe(struct hid_device *hdev, const struct hid_device_id *id) {
	
	struct cmpsu_data *priv;
	int ret;
	
	priv = devm_kzalloc(&hdev->dev, sizeof(struct cmpsu_data), GFP_KERNEL);
	if (!priv) {
		return -ENOMEM;
	}
	
	ret = hid_parse(hdev);
	if (ret) {
		return ret;
	}
	
	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		return ret;
	}
	
	ret = hid_hw_open(hdev);
	if (ret) {
		return ret;
	}
	
	priv->hdev = hdev;
	hid_set_drvdata(hdev, priv);
	hid_device_io_start(hdev);
	
	for (int i = 0; i < COUNT_VOLTAGE; i++) {
		priv->values_voltage[i] = -1;
	}
	for (int i = 0; i < COUNT_CURRENT; i++) {
		priv->values_current[i] = -1;
	}
	for (int i = 0; i < COUNT_POWER; i++) {
		priv->values_power[i] = -1;
	}
	for (int i = 0; i < COUNT_TEMP; i++) {
		priv->values_temp[i] = -1;
	}
	for (int i = 0; i < COUNT_FAN; i++) {
		priv->values_fan[i] = -1;
	}
	
	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "corsairpsu", priv, &cmpsu_chip_info, NULL);
	if (IS_ERR(priv->hwmon_dev)) {
		ret = PTR_ERR(priv->hwmon_dev);
		hid_hw_close(hdev);
		hid_hw_stop(hdev);
		return ret;
	}
	
	return 0;
	
}

static void cmpsu_remove(struct hid_device *hdev) {
	
	struct cmpsu_data *priv = hid_get_drvdata(hdev);
	
	hwmon_device_unregister(priv->hwmon_dev);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	
}

long cmpsu_parse_value(u8 *data, int *idx, int fraction_scale, bool expect_second) {
	
	long ret = 0;
	int fraction_scale_current = 0;
	
	/* Make sure there is at least one digit */
	if (data[*idx] < '0' || data[*idx] > '9') {
		return -1;
	}
	
	while (data[*idx] >= '0' && data[*idx] <= '9') {
		ret *= 10;
		ret += data[*idx] - '0';
		(*idx)++;
	}
	
	if (data[*idx] == '.') {
		(*idx)++;
		
		/* Check for at least one digit after a decimal point */
		if (data[*idx] < '0' || data[*idx] > '9') {
			return -1;
		}
		
		while (data[*idx] >= '0' && data[*idx] <= '9') {
			/* Even if we are expecting an integer, we still run this loop to move idx past this number. Also skip digits if there are more than 3 since those wouldn't fit */
			if (fraction_scale_current < fraction_scale) {
				ret *= 10;
				ret += data[*idx] - '0';
				fraction_scale_current++;
			}
			(*idx)++;
		}
	}
	
	/* Add remaining zeros after the decimal point if needed */
	while (fraction_scale_current < fraction_scale) {
		ret *= 10;
		fraction_scale_current++;
	}
	
	/* Now check if the following character is valid */
	if (data[*idx] == ']' || (data[*idx] == '/' && expect_second)) {
		return ret;
	}
	return -1;
	
}

static int cmpsu_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size) {
	
	struct cmpsu_data *priv = hid_get_drvdata(hdev);
	char type;
	int channel;
	int idx;
	long value, value2;
	
	/* Events appear to always be 16 bytes, we support any length just in case */
	if (size < EVENT_LEN_MIN || size > EVENT_LEN_MAX) {
		return 0;
	}
	/* Make sure the string is terminated correctly */
	if (data[size - 1] != 0 && data[size - 1] != '/') {
		return 0;
	}
	if (data[0] != '[') {
		return 0;
	}
	
	type = data[1];
	if (data[2] < '1' || data[2] > '9') {
		return 0;
	}
	channel = data[2] - '1'; /* Channel index starts at 1 */
	idx = 3;
	switch (type) {
		case 'V':
			if (channel >= COUNT_VOLTAGE) {
				return 0;
			}
			value = cmpsu_parse_value(data, &idx, 3, false);
			if (value >= 0) {
				priv->values_voltage[channel] = value;
			}
			break;
		case 'I':
			if (channel >= COUNT_CURRENT) {
				return 0;
			}
			value = cmpsu_parse_value(data, &idx, 3, false);
			if (value >= 0) {
				priv->values_current[channel] = value;
			}
			break;
		case 'P':
			/* Special case: Channel 1 contains two values, channel 0 is ignored */
			if (channel != 1) {
				return 0;
			}
			value = cmpsu_parse_value(data, &idx, 6, true);
			if (value == -1) {
				return 0;
			}
			idx++; /* Skip past the '/' */
			value2 = cmpsu_parse_value(data, &idx, 6, false);
			if (value2 >= 0) {
				priv->values_power[0] = value;
				priv->values_power[1] = value2;
			}
			break;
		case 'T':
			if (channel >= COUNT_TEMP) {
				return 0;
			}
			value = cmpsu_parse_value(data, &idx, 3, false);
			if (value >= 0) {
				priv->values_temp[channel] = value;
			}
			break;
		case 'R':
			if (channel >= COUNT_FAN) {
				return 0;
			}
			value = cmpsu_parse_value(data, &idx, 0, false);
			if (value >= 0) {
				priv->values_fan[channel] = value;
			}
			break;
		default:
			return 0; /* Unknown type */
	}
	
	return 0;
	
}

static const struct hid_device_id cmpsu_idtable[] = {
	{ HID_USB_DEVICE(0x2516, 0x0193) },
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
