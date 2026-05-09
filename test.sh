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
  -c "init" -c "halt" -c "mdw 0xFFFF0004" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0004" | sed 's/^/TICK_MIRROR: /'

sleep 1
openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0004" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0004" | sed 's/^/TICK_MIRROR: /'

openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0008" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0008" | sed 's/^/ALLOC_CHECK: /'


openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0000" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0000" | sed 's/^/VECTOR_FLAG: /'

# SDRAM test result: 0xDEAD0000 = pass, any address = first failing word
openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF000C" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff000c" | sed 's/^/SDRAM_TEST_RESULT: /'

sleep 2
openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0010" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0010" | sed 's/^/SCHED_COUNT_1: /'

openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0014" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0014" | sed 's/^/SCHED_COUNT_2: /'

openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0000" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0000" | sed 's/^/VECTOR_FLAG: /'

openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" -c "mdw 0xFFFF0018" -c "resume" -c "shutdown" 2>&1 | grep "^0xffff0018" | sed 's/^/GENERAL_FLAG: /'

