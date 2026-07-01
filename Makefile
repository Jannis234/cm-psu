KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD ?= $(shell pwd)

obj-m := cm-psu.o

all:
	make -C $(KERNELDIR) M=$(PWD) modules

install:
	make -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	make -C $(KERNELDIR) M=$(PWD) clean

.PHONY: all
