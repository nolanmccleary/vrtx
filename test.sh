#!/bin/bash


#CHECK YO BOARD BOOTS



ENTRY=0x$(arm-none-eabi-nm build/qlonq.elf | awk '/_reset/{print $1}')

# fire and release
pkill -9 openocd 2>/dev/null || true
openocd -f openocd/de1soc.cfg \
  -c "init" \
  -c "sleep 1000" \
  -c "halt" \
  -c "load_image $PWD/build/qlonq.elf" \
  -c "resume $ENTRY" \
  -c "shutdown"

# burn it down
pkill -9 openocd 2>/dev/null || true
sleep 1

# read canary
openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0004" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0004"

sleep 1
openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0004" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0004"

openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0008" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0008"


openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0000" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0000"

# SDRAM test result: 0xDEAD0000 = pass, any address = first failing word
openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF000C" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff000c"
