#include <linux/errno.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

#define DRIVER_NAME "cm-psu"

struct cmpsu_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;
};

static const char *placeholder = "placeholder";

static umode_t cmpsu_hwmon_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel) {
	
	return 0444;
	
}

static int cmpsu_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, long *val) {
	
	*val = 0;
	return 0;
	
}

static int cmpsu_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel, const char **str) {
	
	*str = placeholder;
	return 0;
	
}

static const struct hwmon_ops cmpsu_hwmon_ops = {
	.is_visible = cmpsu_hwmon_is_visible,
	.read = cmpsu_hwmon_read,
	.read_string = cmpsu_hwmon_read_string,
};

static const struct hwmon_channel_info* cmpsu_info[] = {
	//HWMON_CHANNEL_INFO(chip,
	//				HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
					HWMON_T_INPUT,
					HWMON_T_INPUT),
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

static int cmpsu_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size) {
	
	struct cmpsu_data *priv = hid_get_drvdata(hdev);
	
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
