#!/bin/bash


#CHECK YO BOARD BOOTS



ENTRY=0x$(arm-none-eabi-nm build/qlonq.elf | awk '/_reset/{print $1}')

# fire and release
pkill -9 openocd 2>/dev/null || true
openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "load_image $PWD/build/qlonq.elf" -c "resume $ENTRY" -c "shutdown"

# burn it down
pkill -9 openocd 2>/dev/null || true
sleep 1

# read canary
openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF8000" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff8000"

sleep 4
openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF8000" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff8000"
