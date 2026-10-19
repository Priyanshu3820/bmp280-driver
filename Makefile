obj-m += driver.o
PWD := $(CURDIR)

# Dynamically point to the current active kernel headers on the Pi
KDIR ?= /lib/modules/$(shell uname -r)/build

# Build both the kernel module and the user space reader
all: user module

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

user:
	$(MAKE) -C user_space

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(MAKE) -C user_space clean

.PHONY: all module user clean
