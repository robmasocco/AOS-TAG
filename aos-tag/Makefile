# Roberto Masocco <robmasocco@gmail.com>
# May 2, 2021
# Makefile for the "AOS-TAG" kernel module.
# This module depends on the "SCTH" module.
# NOTE: To activate debugging in this module, set DEBUG=1 from the command line.

MODNAME=aos_tag

ifeq ($(KERNELRELEASE),)
.PHONY: all install clean uninstall
all:
	cd scth && $(MAKE)
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	cd scth && $(MAKE) clean
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	cd scth && $(MAKE) install
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install
	ln -s /lib/modules/$(shell uname -r)/extra/$(MODNAME).ko /lib/modules/$(shell uname -r)
	depmod -a

uninstall:
	cd scth && $(MAKE) uninstall
	rm /lib/modules/$(shell uname -r)/extra/$(MODNAME).ko
	rm /lib/modules/$(shell uname -r)/$(MODNAME).ko
	depmod -a
else
obj-m += $(MODNAME).o
$(MODNAME)-y := aos-tag_main.o aos-tag_syscalls.o aos-tag_dev-driver.o splay-trees_int-keys/splay-trees_int-keys.o
KBUILD_EXTRA_SYMBOLS := $(PWD)/scth/Module.symvers
ifeq ($(DEBUG), 1)
ccflags-y += -DDEBUG
endif
endif
