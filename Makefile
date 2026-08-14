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
	bsp/startup.s \
	bsp/bsp.c \
	bsp/boot.c \
	bsp/sequencer.c \
	$(ALLOC_SRC) \
	kernel/preempt_sched.c \
	kernel/deque.c \
	kernel/min_heap.c \
	bench/ktrace.c \
	bench/pmu.c \
	bench/telemetry.c \
	bench/workload_edf.c \
	bench/workload_allocbench.c


# ---------------------------------------------------------------------------
# Compiler / linker
# ---------------------------------------------------------------------------

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
	-DENABLE_MMU=0 \
	-DENABLE_DCACHE=0 \
	-DENABLE_ICACHE=0


LDFLAGS := \
	-T linker/$(BOARD).ld \
	-Wl,--build-id=none \
	-Wl,-Map=build/test.map


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
