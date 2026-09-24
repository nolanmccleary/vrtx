#!/usr/bin/env bash
# Build the QEMU target and launch it halted, waiting for a debugger on :1234.
#
#   ./qemu/run.sh            # dual-core, MMU on, halted; connect gdb (qemu/debug.gdb)
#   ./qemu/run.sh --run      # free-run (no -S), quick "does it boot" smoke test
#   SMP=1 ./qemu/run.sh      # single-core, MMU off (simpler, deterministic)
#
# Default is dual-core + MMU + SCU on -- QEMU starts both A9 cores at the ELF entry
# (no pen/PSCI), the g_cpu1_ready handshake brings CPU1 up, and both schedulers run.
# Note: QEMU (TCG) does NOT model A9 weak memory order / SCU coherency, so a
# coherency/barrier bug may not reproduce here even with SMP on. Ctrl-A X quits.
set -e
cd "$(dirname "$0")/.."

SMP="${SMP:-2}"
if [ "$SMP" = "1" ]; then
    make BOARD=qemu ENABLE_MMU=0 ENABLE_DCACHE=0 ENABLE_ICACHE=0 ENABLE_L2=0 ENABLE_SMP=0 ENABLE_NULL_GUARD=0 build/test.elf
else
    make BOARD=qemu ENABLE_MMU=1 ENABLE_DCACHE=1 ENABLE_ICACHE=0 ENABLE_L2=0 ENABLE_SMP=1 ENABLE_NULL_GUARD=0 build/test.elf
fi

HALT="-S"
[ "$1" = "--run" ] && HALT=""

exec qemu-system-arm \
    -M vexpress-a9 -cpu cortex-a9 -smp "$SMP" -m 256M \
    -nographic \
    -kernel build/test.elf \
    -gdb tcp::1234 $HALT
