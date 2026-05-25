#!/usr/bin/env python3
import subprocess, time, re

OPENOCD_CFG = "openocd/de1soc.cfg"

# all observable registers: name -> address
REGS = {
    "VECTOR_FLAG":       0xFFFF0000,
    "TICK_MIRROR":       0xFFFF0004,
    "ALLOC_CHECK":       0xFFFF0008,
    "SDRAM_TEST_RESULT": 0xFFFF000C,
    "SCHED_COUNT_1":     0xFFFF0010,
    "SCHED_COUNT_2":     0xFFFF0014,
    "GENERAL_FLAG":      0xFFFF0018,
    "THREAD_COUNT_1":    0xFFFF0024,
    "THREAD_COUNT_2":    0xFFFF0028,
}

# expected static values
EXPECTED = {
    "VECTOR_FLAG":       0x18,
    "ALLOC_CHECK":       0x67,
    "SDRAM_TEST_RESULT": 0xDEAD0000,
    "GENERAL_FLAG":      0x69,
}


def openocd(*tcl):
    args = ["openocd", "-f", OPENOCD_CFG]
    for cmd in tcl:
        args += ["-c", cmd]
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout


def read_regs(addr_dict):
    """Read a {name: address} dict via openocd mdw. Returns {name: int_value}."""
    mdw_cmds = [f"mdw 0x{addr:08x}" for addr in addr_dict.values()]
    out = openocd("init", "halt", *mdw_cmds, "resume", "shutdown")
    parsed = {}
    for line in out.splitlines():
        m = re.search(r"(0x[0-9a-f]+):\s+([0-9a-f]+)", line)
        if m:
            parsed[int(m.group(1), 16)] = int(m.group(2), 16)
    return {name: parsed.get(addr, 0) for name, addr in addr_dict.items()}


def read_regset():
    """Read cpsr, r13 (sp), r15 (pc). Returns {reg_name: int_value}."""
    out = openocd("init", "halt", "reg cpsr", "reg sp", "reg pc", "resume", "shutdown")
    result = {}
    for reg in ("cpsr", "sp", "pc"):
        m = re.search(reg + r" \(/32\):\s*(0x[0-9a-f]+)", out)
        if m:
            result[reg] = int(m.group(1), 16)
    return result


def check(got, expected):
    """Compare two dicts entry by entry, print PASS/FAIL."""
    for name, exp in expected.items():
        val = got.get(name)
        if val is None:
            print(f"FAIL  {name}: not found")
        elif val == exp:
            print(f"PASS  {name}: 0x{val:08x}")
        else:
            print(f"FAIL  {name}: got 0x{val:08x}, expected 0x{exp:08x}")


# ---------- loop 1: TICK_MIRROR must increment each read ----------

print("\n=== loop 1: TICK_MIRROR monotonicity ===")
prev = 0
for i in range(1, 4):
    time.sleep(1)
    snap = read_regs({"TICK_MIRROR": REGS["TICK_MIRROR"]})
    val = snap["TICK_MIRROR"]
    if val > prev:
        print(f"PASS  iter {i}: 0x{val:08x} > 0x{prev:08x}")
    else:
        print(f"FAIL  iter {i}: 0x{val:08x} not > 0x{prev:08x}")
    prev = val


# ---------- loop 2: cooperative + preemptive counters must increase ----------

print("\n=== loop 2: scheduler counter progression ===")
sched_regs = {
    "SCHED_COUNT_1":  REGS["SCHED_COUNT_1"],
    "SCHED_COUNT_2":  REGS["SCHED_COUNT_2"],
    "THREAD_COUNT_1": REGS["THREAD_COUNT_1"],
    "THREAD_COUNT_2": REGS["THREAD_COUNT_2"],
}
prev = {k: 0 for k in sched_regs}
for i in range(1, 4):
    time.sleep(1)
    snap = read_regs(sched_regs)
    for name, val in snap.items():
        label = "PASS" if val > prev[name] else "FAIL"
        print(f"{label}  {name} iter {i}: 0x{val:08x}")
        prev[name] = val

sc1, sc2 = prev["SCHED_COUNT_1"], prev["SCHED_COUNT_2"]
if abs(sc1 - 2 * sc2) <= 2:
    print(f"PASS  coop ratio: SCHED_COUNT_1={sc1} ~ 2x SCHED_COUNT_2={sc2}")
else:
    print(f"FAIL  coop ratio: SCHED_COUNT_1={sc1} != ~2x SCHED_COUNT_2={sc2}")


# ---------- loop 3: static register expected values ----------

print("\n=== loop 3: static register sanity ===")
snap = read_regs(REGS)
check(snap, EXPECTED)


# ---------- regset: CPU register state at halt ----------

print("\n=== regset: CPU register state ===")
regs = read_regset()

cpsr = regs.get("cpsr", 0)
mode = cpsr & 0x1f
mode_name = {0x1f: "SYS", 0x13: "SVC", 0x12: "IRQ"}.get(mode, "UNKNOWN")
if mode_name != "UNKNOWN":
    print(f"PASS  CPSR mode: 0x{mode:02x} ({mode_name})")
else:
    print(f"FAIL  CPSR mode: 0x{mode:02x} unexpected (CPSR=0x{cpsr:08x})")

pc = regs.get("pc", 0)
if 0xFFFF0000 <= pc <= 0xFFFFFFFF:
    print(f"PASS  PC: 0x{pc:08x} (in OCRAM)")
else:
    print(f"FAIL  PC: 0x{pc:08x} (outside OCRAM)")

sp = regs.get("sp", 0)
if sp >= 0xFFFF8000:
    print(f"PASS  SP: 0x{sp:08x} (in OCRAM stack region)")
else:
    print(f"FAIL  SP: 0x{sp:08x} (below OCRAM stack base 0xffff8000)")
