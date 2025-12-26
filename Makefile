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

# -----------------------
# Build
# -----------------------
all:
	@$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	@$(MAKE) -C $(KDIR) M=$(PWD) clean
	@rm -rf .cache

# -----------------------
# Helpers / debug
# -----------------------
status:
	@echo "=== Module ==="
	@lsmod | grep -E "^$(MODULE_NAME)\b" || echo "not loaded"
	@echo "=== Mount ==="
	@mount | grep -E " on $(MOUNT_POINT) " || echo "not mounted"
	@echo "=== Filesystems ==="
	@cat /proc/filesystems | grep -E "\b$(MODULE_NAME)\b" || echo "not registered"

logs:
	@sudo dmesg | grep "\[vtfs\]" | tail -n 120

debug-busy:
	@echo "=== Who uses $(MOUNT_POINT) ==="
	@sudo fuser -vm $(MOUNT_POINT) 2>/dev/null || true
	@echo "=== Open files under $(MOUNT_POINT) (may be empty if lsof missing) ==="
	@command -v lsof >/dev/null 2>&1 && sudo lsof +D $(MOUNT_POINT) 2>/dev/null | head -n 50 || true

# -----------------------
# Module load/unload
# -----------------------
insmod:
	@sudo insmod $(MODULE_NAME).ko

rmmod:
	@# If still mounted, do not try to remove module (will hang/fail)
	@if mount | grep -qE " on $(MOUNT_POINT) "; then \
		echo "Refuse: $(MOUNT_POINT) is still mounted"; \
		exit 1; \
	fi
	@if lsmod | grep -qE "^$(MODULE_NAME)\b"; then \
		sudo rmmod $(MODULE_NAME) || true; \
	else \
		echo "Module not loaded"; \
	fi

# -----------------------
# Mount/umount (robust)
# -----------------------
mount:
	@sudo mkdir -p $(MOUNT_POINT)
	@sudo mount -t vtfs none $(MOUNT_POINT)

umount:
	@# Make sure we are not inside mount point
	@cd / || true
	@# 1) try normal umount
	@sudo umount $(MOUNT_POINT) 2>/dev/null || true
	@# 2) if still mounted -> try lazy
	@if mount | grep -qE " on $(MOUNT_POINT) "; then \
		echo "Still mounted -> trying lazy umount"; \
		sudo umount -l $(MOUNT_POINT) 2>/dev/null || true; \
	fi
	@# 3) if still mounted -> kill holders then lazy umount again
	@if mount | grep -qE " on $(MOUNT_POINT) "; then \
		echo "Still mounted -> killing processes holding $(MOUNT_POINT)"; \
		sudo fuser -km $(MOUNT_POINT) 2>/dev/null || true; \
		sleep 1; \
		sudo umount -l $(MOUNT_POINT) 2>/dev/null || true; \
	fi
	@# 4) final check
	@if mount | grep -qE " on $(MOUNT_POINT) "; then \
		echo "ERROR: cannot unmount $(MOUNT_POINT). Run: make debug-busy"; \
		exit 1; \
	else \
		echo "Unmounted (or was not mounted)"; \
	fi

# -----------------------
# “Hard reset” that tries to recover from hangs
# -----------------------
reset:
	@cd / || true
	@# unmount with force (kill holders)
	@$(MAKE) umount || true
	@# try remove module a few times
	@for i in 1 2 3; do \
		if lsmod | grep -qE "^$(MODULE_NAME)\b"; then \
			echo "rmmod attempt $$i"; \
			sudo rmmod $(MODULE_NAME) 2>/dev/null || true; \
			sleep 1; \
		fi; \
	done
	@# final status
	@$(MAKE) status

# -----------------------
# Reliable reload
# -----------------------
reload: reset all insmod mount
	@echo "Reload complete"
	@$(MAKE) status

# -----------------------
# Tests
# -----------------------
test:
	sudo ./scripts/test_all.sh

test-ram:
	sudo ./scripts/test_ram.sh

test-hardlink:
	sudo ./scripts/test_hard_link.sh
