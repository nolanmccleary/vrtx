# QEMU target (vexpress-a9)

Runs the exact same firmware under QEMU so you get gdb + hardware watchpoints —
the thing JTAG on the DE1 can't give you (no `reg pc` blindness, no USB-Blaster
wedging, no watchdog reboot). Board selection is `BOARD=qemu`; the scheduler /
mutex / heap / thread code is byte-identical to the DE1 build.

## What runs
First bring-up config is **single core, MMU/caches off** (`ENABLE_MMU=0 …
ENABLE_SMP=0`). That exercises the scheduler + EDF and the core-local
free/lifetime corruption without touching any Cyclone-V map-specific hardware
(SCU, PL310, `build_tables`, reset-manager secondary release). SMP + MMU on QEMU
is a follow-on (needs the vexpress PERIPHBASE map wired through and a PSCI/mailbox
secondary-release path — not done yet).

Fidelity caveat: QEMU (TCG) does **not** model the A9 weak memory order / SCU
coherency. It will catch a **logic / lifetime** bug (which is what we're chasing)
but can give a false green on a **barrier / coherency** bug. Confirm any fix on
the DE1 before believing it.

## Prereqs
- `qemu-system-arm` (`brew install qemu`)
- a cross gdb that speaks ARM: `brew install arm-none-eabi-gdb`
  (or `gdb-multiarch`; lldb works too via `gdb-remote 1234`, less convenient)

## Use
Terminal 1 — build + launch, halted at reset:
```
./qemu/run.sh
```
Quick "does it even boot" smoke test (free-run, no debugger):
```
./qemu/run.sh --run
```
Terminal 2 — attach:
```
arm-none-eabi-gdb build/test.elf -x qemu/debug.gdb
(gdb) continue
```
Every corruption path traps through `raise_error_ctx` / `fault_capture`, so gdb
stops there with a live backtrace. Then:
- `wd` — print the EDF watchdog report (thread/core/task/expected/actual/tick)
- `backtrace` — the C call stack that led to the trap
- to catch the *writer* of a corrupt field: get a thread address, then
  `wpstatus <addr>` sets a hardware watchpoint on its `thread_status` byte;
  `continue`, and gdb halts on whoever writes it.

Ctrl-A X quits QEMU; Ctrl-A C is the QEMU monitor (`info registers`, `xp/…`).
