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
ENABLE_SMP    ?= 1


# Self-boot test gate: compile the spin at the top of main() that hangs until
# JTAG writes g_boot_release. Needed only for the no-debugger SD boot flow --
# enable with `make boot`, drive with `python test.py --bootable`.
BOOT_TEST     ?= 0

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
	-DBOOT_TEST=$(BOOT_TEST) \
	-Wa,--defsym,ENABLE_SMP=$(ENABLE_SMP)


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


# ---------------------------------------------------------------------------
# Self-boot build + flash: the ONE image + the BOOT_TEST gate, wrapped for SD
# boot and written to the microSD's 0xA2 partition (scripts/flash_sd.sh locates
# the card safely and writes only that slice -- no reformat). Then power-cycle
# and drive with `python test.py --bootable`.
#
# Force-rebuilds test.elf because only a -D flag changes (make can't see that in
# the timestamps). After running this, `make clean` before a normal `make test`
# so the gate isn't left compiled in. `make boot FLASH_DRYRUN=1` detects the card
# and stops before writing.
# ---------------------------------------------------------------------------

boot:
	rm -f build/test.elf
	$(MAKE) BOOT_TEST=1 build/test.elf preloader
	FLASH_DRYRUN=$(FLASH_DRYRUN) bash scripts/flash_sd.sh build/preloader.img


# ---------------------------------------------------------------------------
# Self-boot preloader image  (experimental)
#
# NOT a separate build: the ONE test.elf is already boot-ROM-shaped (vectors at
# the OCRAM base + a 0x40 mkpimage-header hole -- see linker/de1-soc.ld and the
# ".text" split in kernel/startup.s). 
# ---------------------------------------------------------------------------

preloader: build/test.elf
	$(OBJCOPY) -O binary $< build/preloader.bin

	@echo "preloader.bin: $$(wc -c < build/preloader.bin) bytes (must fit the boot ROM OCRAM budget)"
	mkimage -T socfpgaimage -d build/preloader.bin build/preloader.img && echo "wrote build/preloader.img (mkimage -T socfpgaimage)"; \

	@echo "then (DE1-SoC boots SD only) flash to the raw 0xA2 partition:"
	@echo "   sudo dd if=build/preloader.img of=/dev/<sd-A2-partition> bs=64k conv=fsync"


clean:
	rm -rf build


.PHONY: build clean flash test preloader boot
