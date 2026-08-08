CROSS   := arm-none-eabi
CC      := $(CROSS)-gcc
OBJCOPY := $(CROSS)-objcopy
NM      := $(CROSS)-nm

# Knobs (override on the command line, e.g. `make MODE=schedbench`).
BOARD      ?= de1-soc
MODE       ?= demo          # demo | selftest | schedbench | stress | allocbench | edf
U_PERMILLE ?= 900           # edf: target utilization x1000

# Source groups.
CORE := main.c \
        bsp/startup.s bsp/bsp.c bsp/boot.c bsp/sequencer.c \
        kernel/allocator.c kernel/preempt_sched.c kernel/deque.c kernel/min_heap.c
INSTR := bench/pmu.c bench/telemetry.c

# Compile flags: cortex-a9, freestanding, our include dirs, feature defines.
CFLAGS := -mcpu=cortex-a9 -marm -O1 -g -ffreestanding -nostdlib -I. -Ibsp -Ikernel -Ibench
CFLAGS += -DBOARD_DE1_SOC -DFLAGS_ENABLED -DENABLE_MMU=1 -DENABLE_DCACHE=1 -DENABLE_ICACHE=1

# Build mode selects the one workload and its define (see main.c).
ifeq ($(MODE),demo)
    CFLAGS += -DMODE_DEMO
    SRCS   := $(CORE) bench/workload_demo.c
else ifeq ($(MODE),selftest)
    CFLAGS += -DMODE_SELFTEST
    SRCS   := $(CORE) bench/workload_selftest.c $(INSTR)
else ifeq ($(MODE),schedbench)
    CFLAGS += -DMODE_SCHEDBENCH
    SRCS   := $(CORE) bench/workload_schedbench.c $(INSTR)
else ifeq ($(MODE),stress)
    CFLAGS += -DMODE_STRESS
    SRCS   := $(CORE) bench/workload_stress.c
else ifeq ($(MODE),allocbench)
    CFLAGS += -DMODE_ALLOCBENCH
    SRCS   := $(CORE) bench/workload_allocbench.c $(INSTR)
else ifeq ($(MODE),edf)
    CFLAGS += -DMODE_EDF -DU_PERMILLE=$(U_PERMILLE)
    SRCS   := $(CORE) bench/workload_edf.c $(INSTR)
endif

LIBGCC := $(shell $(CC) -mcpu=cortex-a9 -marm -print-libgcc-file-name)

# The image name is fixed, so `make clean` when you change MODE.
all: build/qlonq.elf build/qlonq.bin

build/qlonq.elf: $(SRCS) | build
	$(CC) $(CFLAGS) -T linker/$(BOARD).ld -Wl,--build-id=none -o $@ $(SRCS) $(LIBGCC)
	$(CROSS)-objdump -d $@ > build/qlonq.dis

build/qlonq.bin: build/qlonq.elf
	$(OBJCOPY) -O binary $< $@

build:
	mkdir -p build

clean:
	rm -rf build

# --- run on the DE1-SoC over JTAG (board in a clean, non-Linux boot) ---
flash: build/qlonq.elf
	@ENTRY=0x$$($(NM) build/qlonq.elf | awk '$$3=="_reset_handler"{print $$1}'); \
	pkill -9 openocd 2>/dev/null || true; sleep 0.5; \
	openocd -f openocd/de1soc.cfg -c "init" -c "halt" \
	  -c "load_image build/qlonq.elf" -c "mww phys 0xffff0000 0 12" \
	  -c "reg cpsr 0x1d3" -c "reg pc $$ENTRY" -c "resume" -c "shutdown"

test: build/qlonq.elf            # demo regression (flag scoreboard)
	python3 test.py

bench-run: build/qlonq.elf       # selftest/schedbench (decode telemetry)
	python3 bench/telemetry.py

.PHONY: all clean flash test bench-run
