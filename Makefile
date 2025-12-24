obj-m += vtfs.o

vtfs-objs := \
  source/vtfs.o \
  source/ops.o \
  source/ram_store.o \
  source/inode.o \
  source/dir.o \
  source/file.o

PWD := $(CURDIR)
KDIR = /lib/modules/$(shell uname -r)/build

EXTRA_CFLAGS = -Wall -g
EXTRA_CFLAGS += -I$(PWD)/include

MODULE_NAME := vtfs
MOUNT_POINT := /mnt/vtfs
TOKEN := ""

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -rf .cache

umount:
	@cd / && sudo umount -l $(MOUNT_POINT) 2>/dev/null || true

rmmod:
	@if lsmod | grep -q "^$(MODULE_NAME)"; then sudo rmmod $(MODULE_NAME); else echo "Module not loaded"; fi

insmod:
	sudo insmod $(MODULE_NAME).ko

mount:
	sudo mkdir -p $(MOUNT_POINT)
	sudo mount -t vtfs none $(MOUNT_POINT)

reload: umount rmmod all insmod mount

test:
	sudo ./scripts/test_ram.sh

logs:
	sudo dmesg | grep "\[vtfs\]" | tail -n 80

status:
	@echo "=== Module ==="
	@lsmod | grep $(MODULE_NAME) || echo "not loaded"
	@echo "=== Mount ==="
	@mount | grep vtfs || echo "not mounted"
	@echo "=== Filesystems ==="
	@cat /proc/filesystems | grep vtfs || echo "not registered"

force-reset:
	@sudo umount -l $(MOUNT_POINT) 2>/dev/null || true
	@sudo rmmod $(MODULE_NAME) 2>/dev/null || true
