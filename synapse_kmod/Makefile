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
    src/synapse_evchr.o \
    src/synapse_sched.o

# Include paths
ccflags-y := -I$(src)/include -Wall -Wextra -Wno-unused-parameter

# Debug build (adds verbose kprobe logging)
ifdef DEBUG
ccflags-y += -DDEBUG -g
endif

# ── Targets ───────────────────────────────────────────────────

all:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

# ── the check that would have caught the 7.2 break ────────────────────────
#
# ⚠ THIS MACHINE'S KERNEL IS NOT CI'S KERNEL, AND FOR A MODULE THAT MATTERS.
# Linux 7.2 REMOVED strncpy() from <linux/string.h>. The module built fine here
# on 7.1 — where the declaration is still there — and failed in CI with
# `implicit declaration of function 'strncpy'`, which for a kernel build is an
# error and not a warning. "It builds on my box" says nothing at all about a
# kernel API that has been deleted upstream.
#
# So ask the BUILT MODULE what it needs from the kernel, which is a question
# with an answer on any kernel: `nm -u` lists exactly the symbols the loader
# will have to resolve. A name on this list is one the kernel has removed or is
# removing, and a module that references it is already broken on a kernel
# somebody is running — it just has not reached them yet.
#
# ⚠ THE LIST IS OF SYMBOLS, NOT OF SOURCE TEXT. Grepping the .c files finds the
# word in a comment and misses a call that arrived through a macro; this sees
# what the linker sees. Verified to fail: putting the strncpy() back makes
# `U strncpy` reappear and this target reports it.
#
# strncpy_from_user() is a DIFFERENT function, still present and still correct
# for copying a path in from userspace — hence the word boundaries.
DEPRECATED_SYMS := strcpy strncpy strlcpy strcat strncat

check-symbols: $(MODULE_NAME).ko
	@bad=""; \
	for s in $(DEPRECATED_SYMS); do \
	    if nm -u $(MODULE_NAME).ko | grep -qE "^ +U $$s$$"; then \
	        bad="$$bad $$s"; \
	    fi; \
	done; \
	if [ -n "$$bad" ]; then \
	    echo "synapse_kmod: needs kernel symbol(s) the kernel has removed:$$bad"; \
	    echo "  strncpy/strcpy went in 7.2, strlcpy earlier. Use strscpy()," ; \
	    echo "  or strscpy_pad() where the zero padding was load-bearing."; \
	    exit 1; \
	fi; \
	echo "synapse_kmod: no removed kernel string API referenced"

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
