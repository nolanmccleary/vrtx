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
BOARD_SRCS := boot/boot.c boot/sequencer.c
DEFS += -DBOARD_DE1_SOC
endif

FLAGS   := -mcpu=cortex-a9 -marm -O1 -g -ffreestanding -nostdlib -I. -Iboot
LIBGCC  := $(shell $(CC) -mcpu=cortex-a9 -marm -print-libgcc-file-name)

SRCS := startup.s main.c allocator.c preempt_sched.c deque.c min_heap.c $(BOARD_SRCS)

all: build/qlonq.elf build/qlonq.bin

build/qlonq.elf: $(SRCS) | build
	$(CC) $(FLAGS) $(DEFS) -T $(LD_SCRIPT) -Wl,--build-id=none -o $@ $^ $(LIBGCC)
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

clean:
	rm -rf build

.PHONY: all flash test clean
