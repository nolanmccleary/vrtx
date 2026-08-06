#!/usr/bin/env python3
import subprocess, socket, time, re, os

OPENOCD_CFG = "openocd/de1soc.cfg"

STATUS_BASE       = 0xFFFF0000
VECTOR_FLAG       = 0xFFFF0000
TICK_MIRROR       = 0xFFFF0004
ALLOC_CHECK       = 0xFFFF0008
SDRAM_TEST_RESULT = 0xFFFF000C
GENERAL_FLAG      = 0xFFFF0018
THREAD_COUNT_1    = 0xFFFF0024
THREAD_COUNT_2    = 0xFFFF0028
THREAD_COUNT_3    = 0xFFFF002C

ELF = os.path.abspath('build/qlonq.elf')

entry_hex = subprocess.check_output(
    ['arm-none-eabi-nm', ELF]
).decode()
entry_hex = re.search(r'([0-9a-f]+) . _reset', entry_hex).group(1)

subprocess.run(['pkill', '-9', 'openocd'], stderr=subprocess.DEVNULL)
time.sleep(0.5)

proc = subprocess.Popen(
    ['openocd', '-f', OPENOCD_CFG, '-c', 'init'],
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
)
time.sleep(2)

s = socket.socket()
s.connect(('localhost', 6666))

def ocd(cmd):
    s.send((cmd + '\x1a').encode())
    r = b''
    while not r.endswith(b'\x1a'):
        r += s.recv(4096)
    return r[:-1].decode()

def mdw(addr):
    resp = ocd(f'mdw 0x{addr:08x}')
    m = re.search(r'[0-9a-f]{8}:\s+([0-9a-f]+)', resp)
    return int(m.group(1), 16) if m else None

def reg(name):
    resp = ocd(f'reg {name}')
    m = re.search(name + r' \(/32\):\s*(0x[0-9a-f]+)', resp)
    return int(m.group(1), 16) if m else None


ocd('sleep 1000')
ocd('halt')
ocd(f'load_image {ELF}')
# The .status flags live in a NOLOAD section, so load_image never clears them and
# a warm reset preserves them. Zero them here or stale values masquerade as results.
ocd(f'mww phys 0x{STATUS_BASE:08x} 0 12')
ocd('reg cpsr 0x1d3')
ocd(f'reg pc 0x{entry_hex}')
ocd('resume')
time.sleep(2)


# --- loop 1: TICK_MIRROR must increment ---

print("\n=== loop 1: TICK_MIRROR ===")
prev = 0
for i in range(1, 4):
    time.sleep(1)
    ocd('halt')
    val = mdw(TICK_MIRROR)
    ocd('resume')
    if val > prev:
        print(f"PASS  iter {i}: 0x{val:08x} > 0x{prev:08x}")
    else:
        print(f"FAIL  iter {i}: 0x{val:08x} not > 0x{prev:08x}")
    prev = val


# --- loop 2: preemptive thread counters must increase ---

print("\n=== loop 2: preemptive thread counters ===")
prev_t1 = 0
prev_t2 = 0
for i in range(1, 4):
    time.sleep(1)
    ocd('halt')
    t1 = mdw(THREAD_COUNT_1)
    t2 = mdw(THREAD_COUNT_2)
    ocd('resume')
    if t1 > prev_t1:
        print(f"PASS  THREAD_COUNT_1 iter {i}: 0x{t1:08x}")
    else:
        print(f"FAIL  THREAD_COUNT_1 iter {i}: 0x{t1:08x} not > 0x{prev_t1:08x}")
    if t2 > prev_t2:
        print(f"PASS  THREAD_COUNT_2 iter {i}: 0x{t2:08x}")
    else:
        print(f"FAIL  THREAD_COUNT_2 iter {i}: 0x{t2:08x} not > 0x{prev_t2:08x}")
    prev_t1 = t1
    prev_t2 = t2


# --- loop 3: static expected values ---

print("\n=== loop 3: static sanity ===")
ocd('halt')
vf   = mdw(VECTOR_FLAG)
ac   = mdw(ALLOC_CHECK)
sdram = mdw(SDRAM_TEST_RESULT)
gf   = mdw(GENERAL_FLAG)
ocd('resume')

if vf == 0x18:
    print(f"PASS  VECTOR_FLAG: 0x{vf:08x}")
else:
    print(f"FAIL  VECTOR_FLAG: got 0x{vf:08x}, expected 0x00000018")

if ac == 0x67:
    print(f"PASS  ALLOC_CHECK: 0x{ac:08x}")
else:
    print(f"FAIL  ALLOC_CHECK: got 0x{ac:08x}, expected 0x00000067")

if sdram == 0xDEAD0000:
    print(f"PASS  SDRAM_TEST_RESULT: 0x{sdram:08x}")
else:
    print(f"FAIL  SDRAM_TEST_RESULT: got 0x{sdram:08x}, expected 0xdead0000")

if gf == 0x69:
    print(f"PASS  GENERAL_FLAG: 0x{gf:08x}")
else:
    print(f"FAIL  GENERAL_FLAG: got 0x{gf:08x}, expected 0x00000069")


# --- regset ---

print("\n=== regset ===")
ocd('halt')
cpsr = reg('cpsr')
pc   = reg('pc')
sp   = reg('sp')
ocd('resume')

mode = cpsr & 0x1f
if mode in (0x1f, 0x13, 0x12):
    print(f"PASS  CPSR mode: 0x{mode:02x}")
else:
    print(f"FAIL  CPSR mode: 0x{mode:02x}")
if pc >= 0xFFFF0000:
    print(f"PASS  PC:   0x{pc:08x}")
else:
    print(f"FAIL  PC:   0x{pc:08x}")
if sp != 0 and (sp & 3) == 0:
    print(f"PASS  SP:   0x{sp:08x}")
else:
    print(f"FAIL  SP:   0x{sp:08x}")


# --- loop 4: aperiodic pthread3 runs exactly once, then is reaped (EDF model) ---
#   pthread3 is APERIODIC: it runs one job (THREAD_COUNT_3 -> 1) and is freed, so its
#   counter must reach 1 and then hold while the periodics keep advancing. (The old
#   NUM_RUNNING==2 check was from the linear scheduler and no longer applies.)

def sample3():
    ocd('halt')
    t1 = mdw(THREAD_COUNT_1)
    t2 = mdw(THREAD_COUNT_2)
    t3 = mdw(THREAD_COUNT_3)
    ocd('resume')
    return t1, t2, t3

print("\n=== loop 4: pthread3 aperiodic run-once + reap ===")
print("waiting for pthread3 to complete its single job...")
ran = False
for i in range(15):
    time.sleep(1)
    _, _, t3 = sample3()
    print(f"      iter {i+1}: THREAD_COUNT_3=0x{t3:08x}")
    if t3 and t3 >= 1:
        ran = True
        break

if ran:
    print(f"PASS  pthread3 completed once: THREAD_COUNT_3={t3}")
else:
    print("FAIL  pthread3 never ran within 15s")

# reaped: THREAD_COUNT_3 must hold steady while the periodics keep climbing
t1a, t2a, t3a = sample3()
time.sleep(3)
t1b, t2b, t3b = sample3()

if t3a is not None and t3b == t3a:
    print(f"PASS  pthread3 reaped (counter stable at {t3b})")
else:
    print(f"FAIL  pthread3 re-ran: 0x{t3a:08x} -> 0x{t3b:08x}")

if t1b > t1a and t2b > t2a:
    print(f"PASS  periodics still alive: T1 {t1a}->{t1b}, T2 {t2a}->{t2b}")
else:
    print(f"FAIL  periodics stalled: T1 {t1a}->{t1b}, T2 {t2a}->{t2b}")


ocd('shutdown')
proc.wait()
