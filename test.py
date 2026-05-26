#!/usr/bin/env python3
import subprocess, socket, time, re

OPENOCD_CFG = "openocd/de1soc.cfg"

TICK_MIRROR       = 0xFFFF0004
SCHED_COUNT_1     = 0xFFFF0010
SCHED_COUNT_2     = 0xFFFF0014
GENERAL_FLAG      = 0xFFFF0018
NUM_THREADS       = 0xFFFF001C
NUM_RUNNING       = 0xFFFF0020
THREAD_COUNT_1    = 0xFFFF0024
THREAD_COUNT_2    = 0xFFFF0028
THREAD_COUNT_3    = 0xFFFF002C
VECTOR_FLAG       = 0xFFFF0000
ALLOC_CHECK       = 0xFFFF0008
SDRAM_TEST_RESULT = 0xFFFF000C

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


# --- loop 4: pthread3 termination ---

print("\n=== loop 4: pthread3 termination ===")
ocd('halt')
nr  = mdw(NUM_RUNNING)
nt  = mdw(NUM_THREADS)
t3  = mdw(THREAD_COUNT_3)
ocd('resume')
print(f"      NUM_THREADS={nt}  NUM_RUNNING={nr}  THREAD_COUNT_3=0x{t3:08x}")

print("waiting for pthread3 to terminate...")
terminated = False
for i in range(60):
    time.sleep(1)
    ocd('halt')
    nr = mdw(NUM_RUNNING)
    nt = mdw(NUM_THREADS)
    t3 = mdw(THREAD_COUNT_3)
    ocd('resume')
    print(f"      iter {i+1}: NUM_THREADS={nt}  NUM_RUNNING={nr}  THREAD_COUNT_3=0x{t3:08x}")
    if nr == 2:
        terminated = True
        break

if terminated:
    print(f"PASS  pthread3 terminated: NUM_RUNNING={nr}  NUM_THREADS={nt}")
else:
    print("FAIL  pthread3 did not terminate within 60s")


ocd('shutdown')
proc.wait()
