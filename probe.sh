#!/usr/bin/env bash
# Concise runtime probe: build EDF @ U=700, flash, let it run, halt, and dump the
# thread_t of "task 1" (period 60 -- the one your Gantt shows never re-arming).
# Reads the debug capture g_dbg_tasks[] in kernel/preempt_sched.c. No test.py.
set -e
ELF=build/edf.elf
make TLSF=1 U_PERMILLE=700 "$ELF" >/dev/null
sym(){ arm-none-eabi-nm "$ELF" | awk -v n="$1" '$3==n{print "0x"$1}'; }
ENTRY=$(sym _reset_handler)
T1PTR=$(printf '0x%08x' $(( $(sym g_dbg_tasks) + 4 )))    # &g_dbg_tasks[1]
TICKS=$(sym gTicks); MISS=$(sym gMissedDeadlines)
pkill -9 openocd 2>/dev/null || true; sleep 0.5
openocd -f openocd/de1soc.cfg \
  -c "init" -c "halt" \
  -c "load_image $ELF" -c "mww phys 0xffff0000 0 12" \
  -c "reg cpsr 0x1d3" -c "reg pc $ENTRY" -c "resume" \
  -c "sleep 3000" -c "halt" \
  -c "set t [lindex [read_memory $T1PTR 32 1] 0]" \
  -c "echo \"== task 1 (period 60) thread_t @ [format 0x%08x \$t] ==\"" \
  -c "set f [read_memory \$t 32 6]" \
  -c "echo \"periodicity=[lindex \$f 0]   (0=PERIODIC 1=APERIODIC)\"" \
  -c "echo \"period     =[lindex \$f 1]\"" \
  -c "echo \"release_tm =[lindex \$f 2]\"" \
  -c "echo \"deadline   =[lindex \$f 3]\"" \
  -c "echo \"dirty      =[lindex \$f 4]\"" \
  -c "echo \"status     =[lindex \$f 5]   (0=PENDING 1=RUNNING 2=FINISHED)\"" \
  -c "echo \"gTicks=[lindex [read_memory $TICKS 32 1] 0]  misses=[lindex [read_memory $MISS 32 1] 0]\"" \
  -c "shutdown"
