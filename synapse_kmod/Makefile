# Makefile — synapse_kmod
#
# Build against the running kernel (development):
#   make
#
# Build against SynapseOS kernel source tree:
#   make KERNELDIR=/path/to/synapseos-kernel
#
# Install:
#   sudo make modules_install
#   sudo depmod -a
#   sudo modprobe synapse_kmod
#
# SynapseOS Project — GPLv2

MODULE_NAME := synapse_kmod

# Kernel build directory
KERNELDIR   ?= /lib/modules/$(shell uname -r)/build
PWD         := $(shell pwd)

# Module source files
obj-m += $(MODULE_NAME).o

$(MODULE_NAME)-objs := \
    src/synapse_main.o  \
    src/synapse_sysfs.o \
    src/synapse_probe.o \
    src/synapse_sched.o \
    src/synapse_ctx.o

# Include paths
ccflags-y := -I$(src)/include -Wall -Wextra -Wno-unused-parameter

# Debug build (adds verbose kprobe logging)
ifdef DEBUG
ccflags-y += -DDEBUG -g
endif

# ── Targets ───────────────────────────────────────────────────

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install
	depmod -a

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c .*.cmd modules.order Module.symvers
	rm -rf .tmp_versions

# ── Load / unload helpers ─────────────────────────────────────

load: all
	@echo "Loading synapse_kmod..."
	@rmmod synapse_kmod 2>/dev/null || true
	@insmod $(MODULE_NAME).ko \
	    synapse_events=1 \
	    synapse_sched=1 \
	    synapse_daemon_timeout=30
	@echo "synapse_kmod loaded."
	@cat /sys/kernel/synapse/version

unload:
	@echo "Unloading synapse_kmod..."
	@rmmod synapse_kmod
	@echo "Done."

reload: unload load

status:
	@echo "=== Module status ==="
	@lsmod | grep synapse || echo "(not loaded)"
	@echo ""
	@if [ -d /sys/kernel/synapse ]; then \
	    echo "=== /sys/kernel/synapse/ ==="; \
	    echo "version:"; cat /sys/kernel/synapse/version; \
	    echo "status:";  cat /sys/kernel/synapse/status; \
	    echo "config:";  cat /sys/kernel/synapse/config; \
	    echo "stats:";   cat /sys/kernel/synapse/stats; \
	else \
	    echo "(sysfs not present — module not loaded)"; \
	fi

log:
	@dmesg | grep synapse | tail -50

.PHONY: all clean modules_install load unload reload status log
