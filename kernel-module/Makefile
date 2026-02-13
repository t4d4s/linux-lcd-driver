obj-m := lcd_driver.o

KDIR := /home/studentas/Desktop/pagal_notes/linux-6.14
CROSS_COMPILE := arm-unknown-linux-gnueabi-
ARCH := arm

all:
	make -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) modules

clean:
	make -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean

