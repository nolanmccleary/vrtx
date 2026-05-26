CROSS   := arm-none-eabi
CC      := $(CROSS)-gcc
OBJCOPY := $(CROSS)-objcopy

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

SRCS := startup.s main.c allocator.c preempt_sched.c $(BOARD_SRCS)

all: build/qlonq.elf build/qlonq.bin

build/qlonq.elf: $(SRCS) | build
	$(CC) $(FLAGS) $(DEFS) -T $(LD_SCRIPT) -Wl,--build-id=none -o $@ $^ $(LIBGCC)
	$(CROSS)-objdump -d $@ > build/qlonq.dis

build/qlonq.bin: build/qlonq.elf
	$(OBJCOPY) -O binary $< $@

build:
	mkdir -p build

clean:
	rm -rf build
