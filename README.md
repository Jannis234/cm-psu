# cm-psu Linux kernel module
This driver adds hwmon support for certain Cooler Master power supplies with a USB interface, allowing voltage, current, temperature, etc. to be displayed in linux.

## Supported hardware
The driver was developed and tested with a V850 Gold i multi PSU, but any similar supply that is supported by CM's MasterPlus software should work, with the exception being the XG650/750/850 line.

## Installation and usage
Assuming that you have the required packages to build kernel modules installed, installing this driver should be as simple as
```
make
sudo make install
```
After that, you can load the module using
```
modprobe cm-psu
```
and get readings from your PSU using the regular `sensors` command.

Here is an example of the readings produced by this driver:
```
cmpsu-hid-3-f
Adapter: HID adapter
V_AC:        226.20 V
+5V:           5.10 V
+3.3V:         3.30 V
+12V2:            N/A
+12V1:        11.90 V
fan1:         650 RPM
temp1:        +37.0°C
temp2:        +41.0°C
P_in:        145.00 W
P_out:       137.00 W
I_AC:        700.00 mA
I_+5V:         8.70 A
I_+3.3V:       4.50 A
I_+12V2:          N/A
I_+12V1:       6.40 A
```

## Limitations
* **This driver is new and experimental!** Please open an issue if you encounter any issues (especially with PSU models I haven't tested). I plan to submit this upstream eventually once I can consider it stable enough.
* The XG650/750/850 line is not supported as those units use a different protocol (see issue [#1](https://github.com/Jannis234/cm-psu/issues/1))
* The two temperature readings are unlabeled because I don't know what sensor they belong to (CM's software only shows one of them)
* There is one unidentified value (called `P1` in the PSU's data) that is currently not being reported (also doesn't show up in MasterPlus)
* Manual fan control is not supported. In its current state, the driver is entirely passive and simply parses data that is constantly being sent by the PSU which means that I haven't made any attempts to reverse engineer the protocol used for setting fan curves.

## Credits
This driver is heavily based on the `corsair-psu` kernel driver by Wilken Gottwalt.
