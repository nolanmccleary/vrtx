CROSS   := arm-none-eabi

CC      := $(CROSS)-gcc
OBJCOPY := $(CROSS)-objcopy
OBJDUMP := $(CROSS)-objdump
NM      := $(CROSS)-nm

BOARD ?= de1-soc


# ---------------------------------------------------------------------------
# Allocator
# ---------------------------------------------------------------------------

TLSF ?= 1

ifeq ($(TLSF),1)
ALLOC_SRC := kernel/tlsf.c
else
ALLOC_SRC := kernel/allocator.c
endif


# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

CORE := \
	main.c \
	kernel/startup.s \
	bsp/bsp.c \
	bsp/boot.c \
	bsp/sequencer.c \
	$(ALLOC_SRC) \
	kernel/preempt_sched.c \
	kernel/deque.c \
	kernel/min_heap.c \
	kernel/pmu.c \
	kernel/thread.c \
	bench/ktrace.c \
	bench/fault.c \
	bench/telemetry.c \
	bench/workload_edf.c \
	bench/workload_allocbench.c \
	bench/workload_compute.c


# ---------------------------------------------------------------------------
# Compiler / linker
# ---------------------------------------------------------------------------

# Cache/MMU enables -- single source of truth: compiled into the code AND emitted
# as absolute symbols (LDFLAGS below) so the host reads the config straight from
# the .elf symbol table, without any runtime field or loaded-image inflation.
ENABLE_MMU    ?= 1
ENABLE_DCACHE ?= 1
ENABLE_ICACHE ?= 1
ENABLE_L2     ?= 1   # PL310 outer (L2) cache; requires ENABLE_MMU
ENABLE_SMP    ?= 1   # enable the MPCore SCU (L1 D-cache coherency); requires ENABLE_MMU

# Dev convenience: hold the L4 watchdog in reset at boot so a hang/fault never
# resets the HPS (which drops the JTAG-DP and wedges the USB-Blaster). Set to 0
# for a "real" build that wants watchdog protection.
DISABLE_WDT   ?= 1

CFLAGS := \
	-mcpu=cortex-a9 \
	-marm \
	-O1 \
	-g \
	-ffreestanding \
	-nostdlib \
	-I. \
	-Ibsp \
	-Ikernel \
	-Ibench \
	-DBOARD_DE1_SOC \
	-DMODE_TEST \
	-DENABLE_MMU=$(ENABLE_MMU) \
	-DENABLE_DCACHE=$(ENABLE_DCACHE) \
	-DENABLE_ICACHE=$(ENABLE_ICACHE) \
	-DENABLE_L2=$(ENABLE_L2) \
	-DENABLE_SMP=$(ENABLE_SMP) \
	-DDISABLE_WDT=$(DISABLE_WDT)


LDFLAGS := \
	-T linker/$(BOARD).ld \
	-Wl,--build-id=none \
	-Wl,-Map=build/test.map \
	-Wl,--defsym=_cfg_enable_mmu=$(ENABLE_MMU) \
	-Wl,--defsym=_cfg_enable_dcache=$(ENABLE_DCACHE) \
	-Wl,--defsym=_cfg_enable_icache=$(ENABLE_ICACHE) \
	-Wl,--defsym=_cfg_enable_l2=$(ENABLE_L2) \
	-Wl,--defsym=_cfg_enable_smp=$(ENABLE_SMP)


LIBGCC := $(shell $(CC) -mcpu=cortex-a9 -marm -print-libgcc-file-name)


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------

build:
	mkdir -p build


build/test.elf: $(CORE) linker/$(BOARD).ld | build
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(CORE) $(LIBGCC)
	$(OBJDUMP) -d $@ > build/test.dis


# ---------------------------------------------------------------------------
# Flash
# ---------------------------------------------------------------------------

IMG ?= test

flash: build/$(IMG).elf
	@ENTRY=0x$$($(NM) $< | awk '$$3=="_reset_handler"{print $$1}'); \
	pkill -9 openocd 2>/dev/null || true; \
	sleep 0.5; \
	openocd \
		-f openocd/de1soc.cfg \
		-c "init" \
		-c "halt" \
		-c "load_image $<" \
		-c "reg cpsr 0x1d3" \
		-c "reg pc $$ENTRY" \
		-c "resume" \
		-c "shutdown"


# ---------------------------------------------------------------------------
# Test
# ---------------------------------------------------------------------------

test: build/test.elf
	python3 test.py


all: build/test.elf


clean:
	rm -rf build


.PHONY: build clean flash test
