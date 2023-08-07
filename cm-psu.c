#include <linux/errno.h>
#include <linux/hid.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

#define DRIVER_NAME "cm-psu"

struct cmpsu_data {
	struct hid_device *hdev;
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
	
	return 0;
	
}

static void cmpsu_remove(struct hid_device *hdev) {
	
	struct cmpsu_data *priv = hid_get_drvdata(hdev);
	
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
