CROSS   := arm-none-eabi
CC      := $(CROSS)-gcc
OBJCOPY := $(CROSS)-objcopy
NM      := $(CROSS)-nm

BOARD      ?= de1-soc
U_PERMILLE ?= 900           # edf: target utilization x1000

# Shared sources: kernel + bsp + the instrumentation both benchmarks read (pmu, telemetry).
CORE := main.c \
        bsp/startup.s bsp/bsp.c bsp/boot.c bsp/sequencer.c \
        kernel/allocator.c kernel/preempt_sched.c kernel/deque.c kernel/min_heap.c \
        bench/pmu.c bench/telemetry.c

CFLAGS := -mcpu=cortex-a9 -marm -O1 -g -ffreestanding -nostdlib -I. -Ibsp -Ikernel -Ibench
# MMU + caches OFF by default: a deterministic, uncached baseline to benchmark against.
CFLAGS += -DBOARD_DE1_SOC -DFLAGS_ENABLED -DENABLE_MMU=0 -DENABLE_DCACHE=0 -DENABLE_ICACHE=0

LIBGCC := $(shell $(CC) -mcpu=cortex-a9 -marm -print-libgcc-file-name)

# One image per benchmark, each with its own name so `make test` builds both at once.
edf_SRC   := $(CORE) bench/workload_edf.c
alloc_SRC := $(CORE) bench/workload_allocbench.c

all: build/edf.elf build/allocbench.elf

build/edf.elf: $(edf_SRC) | build
	$(CC) $(CFLAGS) -DMODE_EDF -DU_PERMILLE=$(U_PERMILLE) -T linker/$(BOARD).ld -Wl,--build-id=none -o $@ $(edf_SRC) $(LIBGCC)
	$(CROSS)-objdump -d $@ > build/edf.dis

build/allocbench.elf: $(alloc_SRC) | build
	$(CC) $(CFLAGS) -DMODE_ALLOCBENCH -T linker/$(BOARD).ld -Wl,--build-id=none -o $@ $(alloc_SRC) $(LIBGCC)
	$(CROSS)-objdump -d $@ > build/allocbench.dis

build:
	mkdir -p build

clean:
	rm -rf build

# Flash one image to the DE1-SoC over JTAG:  make flash IMG=edf
IMG ?= edf
flash: build/$(IMG).elf
	@ENTRY=0x$$($(NM) $< | awk '$$3=="_reset_handler"{print $$1}'); \
	pkill -9 openocd 2>/dev/null || true; sleep 0.5; \
	openocd -f openocd/de1soc.cfg -c "init" -c "halt" \
	  -c "load_image $<" -c "mww phys 0xffff0000 0 12" \
	  -c "reg cpsr 0x1d3" -c "reg pc $$ENTRY" -c "resume" -c "shutdown"

# Build both images and drive the full suite (allocbench + EDF sweep + schedule
# trace) on hardware; artifacts land in test_results/<timestamp>/.
test: all
	python3 test.py

.PHONY: all clean flash test
