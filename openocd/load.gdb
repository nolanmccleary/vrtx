target extended-remote localhost:3333

monitor halt

# Disable MMU (bit 0), D-cache (bit 2), I-cache (bit 12) in SCTLR
# so JTAG writes to OCRAM aren't blocked by the MMU
monitor arm mcr 15 0 1 0 0 0x00000000

# Invalidate I-cache and TLBs
monitor arm mcr 15 0 7 5 0 0
monitor arm mcr 15 0 8 7 0 0

load

set $pc = _reset

# detach (extended-remote): flushes register writes including $pc, then
# signals OpenOCD to resume the target before GDB exits
detach
