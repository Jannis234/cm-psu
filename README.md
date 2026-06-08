# cm-psu Linux kernel module
This driver adds hwmon support for certain Cooler Master power supplies with a USB interface, allowing voltage, current, temperature, etc. to be displayed in linux.

## Supported hardware
The driver currently supports the following hardware:
* Most PSUs supported by Cooler Master's MasterPlus software, except the XG650/750/850 line (tested with a V850 Gold i multi)
* Cooler Master X SILENT Edge Platinum 1100
If you have a unit that is currently not supported and you want to help adding it, please feel free to open an issue or pull request on GitHub!

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
* MasterPlus: Although supported, the XG650/750/850 line uses a different protocol than other supported units that has not yet been reverse engineered
* MasterPlus: Fan control is not implemented
* MasterPlus: Power supplies report an unidentified value (labelled `P1`) that is currently ignored by the driver

## Credits
This driver is heavily based on the `corsair-psu` kernel driver by Wilken Gottwalt.
