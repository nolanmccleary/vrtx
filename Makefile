CROSS   := arm-none-eabi
CC      := $(CROSS)-gcc
OBJCOPY := $(CROSS)-objcopy
NM      := $(CROSS)-nm

OCD_CFG := openocd/de1soc.cfg

BOARD         ?= de1-soc
ENABLE_MMU    ?= 1
ENABLE_DCACHE ?= 1
ENABLE_ICACHE ?= 1
FLAGS_ENABLED ?= 1

ifeq ($(ENABLE_DCACHE),1)
ifeq ($(ENABLE_MMU),0)
$(error ENABLE_DCACHE=1 requires ENABLE_MMU=1)
endif
endif

DEFS := -DENABLE_MMU=$(ENABLE_MMU) -DENABLE_DCACHE=$(ENABLE_DCACHE) -DENABLE_ICACHE=$(ENABLE_ICACHE)
ifeq ($(FLAGS_ENABLED),1)
DEFS += -DFLAGS_ENABLED
endif

LD_SCRIPT := linker/$(BOARD).ld

ifeq ($(BOARD),de1-soc)
BOARD_SRCS := bsp/boot.c bsp/sequencer.c
DEFS += -DBOARD_DE1_SOC
endif

FLAGS   := -mcpu=cortex-a9 -marm -O1 -g -ffreestanding -nostdlib -I. -Ibsp -Ikernel -Ibench
LIBGCC  := $(shell $(CC) -mcpu=cortex-a9 -marm -print-libgcc-file-name)

# One workload (benchmark/demo) per image, selected by WORKLOAD= (default: demo).
# main() dispatches to that workload's g_workload.run(). Non-demo workloads are
# instrumented (pull in the pmu/telemetry layer).
WORKLOAD ?= demo

SRCS := bsp/startup.s main.c bsp/bsp.c \
        kernel/allocator.c kernel/preempt_sched.c kernel/deque.c kernel/min_heap.c \
        $(BOARD_SRCS) bench/workload_$(WORKLOAD).c

ifneq ($(WORKLOAD),demo)
DEFS += -DBENCH_BUILD
SRCS += bench/pmu.c bench/telemetry.c
# If the workload ships a ktrace override, force-include it into every TU so it wins
# over kernel/ktrace.h's no-op defaults and instruments the kernel (Phase 2+).
ifneq ($(wildcard bench/ktrace_$(WORKLOAD).h),)
FLAGS += -include bench/ktrace_$(WORKLOAD).h
endif
endif

# The image name is constant but its contents depend on WORKLOAD (different SRCS).
# At parse time, if the selected workload differs from the last build, drop the image
# so it relinks — otherwise make sees the .elf up-to-date and runs the wrong workload.
LAST_WORKLOAD := $(shell cat build/config.sig 2>/dev/null)
ifneq ($(WORKLOAD),$(LAST_WORKLOAD))
$(shell mkdir -p build && echo '$(WORKLOAD)' > build/config.sig && rm -f build/qlonq.elf build/qlonq.bin)
endif

all: build/qlonq.elf build/qlonq.bin

build/qlonq.elf: $(SRCS) | build
	$(CC) $(FLAGS) $(DEFS) -T $(LD_SCRIPT) -Wl,--build-id=none -o $@ $(SRCS) $(LIBGCC)
	$(CROSS)-objdump -d $@ > build/qlonq.dis

build/qlonq.bin: build/qlonq.elf
	$(OBJCOPY) -O binary $< $@

build:
	mkdir -p build

# Load the image over JTAG and run it (board must be in a clean, non-Linux boot).
# OpenOCD resolves relative paths from the repo root; flags are zeroed since they
# live in a NOLOAD section that load_image does not touch.
flash: build/qlonq.elf
	@ENTRY=0x$$($(NM) build/qlonq.elf | awk '$$3=="_reset_handler"{print $$1}'); \
	pkill -9 openocd 2>/dev/null || true; sleep 0.5; \
	openocd -f $(OCD_CFG) \
	  -c "init" -c "halt" \
	  -c "load_image build/qlonq.elf" \
	  -c "mww phys 0xffff0000 0 12" \
	  -c "reg cpsr 0x1d3" -c "reg pc $$ENTRY" \
	  -c "resume" -c "shutdown"

# Full on-hardware test harness (loads, runs, and checks the flag scoreboard).
test: build/qlonq.elf
	python3 test.py

# Build + run a benchmark image and decode its telemetry over JTAG.
#   make WORKLOAD=selftest bench-run
bench-run: all
	@if [ "$(WORKLOAD)" = "demo" ]; then echo "set WORKLOAD=<name>, e.g. make WORKLOAD=selftest bench-run"; exit 1; fi
	python3 bench/telemetry.py

clean:
	rm -rf build

.PHONY: all flash test bench-run clean
