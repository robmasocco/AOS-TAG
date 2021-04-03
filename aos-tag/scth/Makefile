# Roberto Masocco
# 8/3/2021
# Makefile for the "System Call Table Hacker" kernel module.
# NOTE: To activate debugging in this module, set DEBUG=1 from the command line.

MODNAME=scth

ifeq ($(KERNELRELEASE),)
.PHONY: all install clean uninstall
all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install
	ln -s /lib/modules/$(shell uname -r)/extra/$(MODNAME).ko /lib/modules/$(shell uname -r)
	depmod -a

uninstall:
	rm /lib/modules/$(shell uname -r)/extra/$(MODNAME).ko
	rm /lib/modules/$(shell uname -r)/$(MODNAME).ko
	depmod -a
else
obj-m += $(MODNAME).o
$(MODNAME)-y := scth_main.o utils/paging_navigator.o scth_lib.o
ifeq ($(DEBUG), 1)
ccflags-y += -DDEBUG
endif
endif
