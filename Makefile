CROSS   := arm-none-eabi
CC      := $(CROSS)-gcc
OBJCOPY := $(CROSS)-objcopy

FLAGS   := -mcpu=cortex-a9 -marm -O1 -g -ffreestanding -nostdlib -I. -Iboot
LIBGCC  := $(shell $(CC) -mcpu=cortex-a9 -marm -print-libgcc-file-name)

SRCS    := startup.s main.c allocator.c preempt_sched.c boot/boot.c boot/sequencer.c

all: build/qlonq.elf build/qlonq.bin

build/qlonq.elf: $(SRCS) | build
	$(CC) $(FLAGS) -T linker/de1soc.ld -Wl,--build-id=none -o $@ $^ $(LIBGCC)
	$(CROSS)-objdump -d $@ > build/qlonq.dis

build/qlonq.bin: build/qlonq.elf
	$(OBJCOPY) -O binary $< $@

build:
	mkdir -p build

clean:
	rm -rf build
