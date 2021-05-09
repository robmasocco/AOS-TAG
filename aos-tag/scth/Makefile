# Roberto Masocco <robmasocco@gmail.com>
# March 8, 2021
# Makefile for the "System Call Table Hacker" kernel module.
# NOTE: To activate debugging in this module, set DEBUG=1 from the command line.

MODNAME=scth

ifeq ($(KERNELRELEASE),)
.PHONY: all install clean uninstall
all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
else
obj-m += $(MODNAME).o
$(MODNAME)-y := scth_main.o utils/paging_navigator.o scth_lib.o
ifeq ($(DEBUG), 1)
ccflags-y += -DDEBUG
endif
endif
