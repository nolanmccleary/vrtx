#!/usr/bin/env python3
# Load a benchmark image, run it to completion, then read and decode the
# g_telemetry blob over JTAG. All timing is reported in CPU cycles.
import subprocess, socket, time, re, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OPENOCD_CFG = "openocd/de1soc.cfg"
ELF = os.path.join(ROOT, "build", "qlonq.elf")

STATUS_BASE = 0xFFFF0000            # legacy flag region, zeroed on load
TELEM_MAGIC = 0x51544C30            # "QTL0"
TELEM_DONE  = 2
HDR_WORDS   = 14                    # u32 fields before metric[]
RUN_SECONDS = 6                     # uninterrupted target run before halt+read

os.chdir(ROOT)

def sym(name):
    out = subprocess.check_output(['arm-none-eabi-nm', ELF]).decode()
    m = re.search(r'^([0-9a-fA-F]+)\s+\S+\s+' + re.escape(name) + r'$', out, re.M)
    if not m:
        sys.exit(f"symbol {name} not found in {ELF}")
    return int(m.group(1), 16)

entry = sym('_reset_handler')
telem = sym('g_telemetry')

subprocess.run(['pkill', '-9', 'openocd'], stderr=subprocess.DEVNULL)
time.sleep(0.5)
proc = subprocess.Popen(['openocd', '-f', OPENOCD_CFG, '-c', 'init'],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)

s = socket.socket()
s.connect(('localhost', 6666))

def ocd(cmd):
    s.send((cmd + '\x1a').encode())
    r = b''
    while not r.endswith(b'\x1a'):
        r += s.recv(65536)
    return r[:-1].decode()

def read_words(addr, n):
    resp = ocd(f'mdw phys 0x{addr:08x} {n}')
    words = []
    for line in resp.splitlines():
        if ':' not in line:
            continue
        _, rest = line.split(':', 1)
        words += [int(t, 16) for t in re.findall(r'[0-9a-fA-F]{8}', rest)]
    return words[:n]

def u64(words, i):
    return words[i] | (words[i + 1] << 32)

# --- load + run ---
ocd('halt')
ocd(f'load_image {ELF}')
ocd(f'mww phys 0x{STATUS_BASE:08x} 0 12')
ocd('reg cpsr 0x1d3')
ocd(f'reg pc 0x{entry:08x}')
ocd('resume')

# Let the bench run UNINTERRUPTED to completion, then halt once. Polling by
# halt/resume over the slow USB-Blaster starves the target of run time (and would
# perturb timing), so we wait a fixed window and read once at the end.
STATE_OFF = 8 * 4
time.sleep(RUN_SECONDS)
ocd('halt')
st = read_words(telem + STATE_OFF, 1)
if not (st and st[0] == TELEM_DONE):
    print(f"WARN: state={st[0] if st else '?'} (not DONE); decoding current contents")

# --- read header (target stays halted) ---
hdr = read_words(telem, HDR_WORDS)
(magic, version, cpu_hz, gtimer_hz, cal_cycles, cal_gtimer,
 read_ovf, probe_ovf, state, bench_id, n_metrics, k, maxp, nb) = hdr

if magic != TELEM_MAGIC:
    ocd('resume'); ocd('shutdown'); proc.wait()
    sys.exit(f"bad magic 0x{magic:08x} (expected 0x{TELEM_MAGIC:08x})")

S = 1 << k
hist_bytes = (28 + 4 * nb + 7) & ~7          # hist_t, 8-aligned
metric_bytes = 16 + hist_bytes               # name[16] + hist_t
metric_words = metric_bytes // 4

metrics = []
for i in range(n_metrics):
    base = telem + HDR_WORDS * 4 + i * metric_bytes
    w = read_words(base, metric_words)
    name = bytes(b for word in w[0:4] for b in word.to_bytes(4, 'little'))
    name = name.split(b'\x00', 1)[0].decode(errors='replace')
    count = u64(w, 4); ssum = u64(w, 6)
    mn, mx, ov = w[8], w[9], w[10]
    buckets = w[11:11 + nb]
    metrics.append((name, count, ssum, mn, mx, ov, buckets))
ocd('resume')
ocd('shutdown')
proc.wait()

# --- decode ---
def bucket_range(idx):
    if idx < S:
        return idx, idx + 1
    o, sub = divmod(idx - S, S)
    return (S + sub) << o, (S + sub + 1) << o

def pct(buckets, count, mn, mx, p):
    # Bucketed percentile with within-bucket linear interpolation, clamped to the
    # exact [min,max]. When the true spread is narrower than one bucket, the estimate
    # collapses toward the exact bounds (which then carry the signal).
    target = p / 100.0 * count
    cum = 0
    for idx, c in enumerate(buckets):
        if not c:
            continue
        if cum + c >= target:
            lo, hi = bucket_range(idx)
            v = lo + (hi - lo) * ((target - cum) / c)
            return min(max(v, mn), mx)
        cum += c
    return mx        # remainder is in the overflow/max tail

print(f"\n=== telemetry (bench_id={bench_id}, state={state}, v{version}) ===")
print(f"cycle counter: read_overhead={read_ovf}c  probe_overhead={probe_ovf}c")
if cal_gtimer:
    print(f"cal ratio: {cal_cycles}/{cal_gtimer} = {cal_cycles/cal_gtimer:.3f} cycles/gtimer-tick")
print(f"histogram: {S} sub-buckets/octave, max 2^{maxp} cyc, {nb} buckets (<= {100.0/S:.2f}% rel err)\n")

for (name, count, ssum, mn, mx, ov, buckets) in metrics:
    if count == 0:
        continue
    mean = ssum / count
    p50 = pct(buckets, count, mn, mx, 50)
    p90 = pct(buckets, count, mn, mx, 90)
    p99 = pct(buckets, count, mn, mx, 99)
    spread = mx - mn
    tight = " (spread < 1 bucket; percentiles bounded by min/max)" if spread and \
            spread < ((mx >> k) if mx >= S else 1) else ""
    print(f"[{name}]  n={count}  overflow={ov}{tight}")
    print(f"   min={mn:>8}  mean={mean:>10.1f}  p50={p50:>10.1f}  "
          f"p90={p90:>10.1f}  p99={p99:>10.1f}  max={mx:>8}   (cycles)")
    # compact per-octave bar
    oct_counts = {}
    for idx, c in enumerate(buckets):
        if c:
            lo, _ = bucket_range(idx)
            e = lo.bit_length() - 1
            oct_counts[e] = oct_counts.get(e, 0) + c
    peak = max(oct_counts.values()) if oct_counts else 1
    for e in sorted(oct_counts):
        bar = '#' * max(1, int(40 * oct_counts[e] / peak))
        print(f"   2^{e:<2} [{1<<e:>8}..{2<<e:>8})  {oct_counts[e]:>6}  {bar}")
    print()
