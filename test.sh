#!/bin/bash

ENTRY=0x$(arm-none-eabi-nm build/qlonq.elf | awk '/_reset/{print $1}')

pkill -9 openocd 2>/dev/null || true
openocd -f openocd/de1soc.cfg \
  -c "init" \
  -c "sleep 1000" \
  -c "halt" \
  -c "load_image $PWD/build/qlonq.elf" \
  -c "resume $ENTRY" \
  -c "shutdown"

pkill -9 openocd 2>/dev/null || true
sleep 2

openocd -f openocd/de1soc.cfg -c "init" -c "profile 10 gmon.out" -c "shutdown"

gprof build/qlonq.elf gmon.out
