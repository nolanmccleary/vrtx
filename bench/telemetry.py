#!/usr/bin/env python3
"""Load a benchmark image over JTAG, run it, then decode the g_telemetry blob.
All timing is reported in CPU cycles."""

import subprocess, socket, time, re, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF  = os.path.join(ROOT, "build", "qlonq.elf")
OPENOCD_CFG = "openocd/de1soc.cfg"

TELEM_MAGIC = 0x51544C30   # "QTL0"
TELEM_DONE  = 2
RUN_SECONDS = 6            # let the target run this long, then halt + read once


# --------------------------------------------------------------- OpenOCD link
class OpenOCD:
    """Start OpenOCD and talk to it over its Tcl port (6666)."""

    def __init__(self):
        subprocess.run(["pkill", "-9", "openocd"], stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        self.proc = subprocess.Popen(["openocd", "-f", OPENOCD_CFG, "-c", "init"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(2)
        self.sock = socket.socket()
        self.sock.connect(("localhost", 6666))

    def cmd(self, text):
        self.sock.send((text + "\x1a").encode())
        out = b""
        while not out.endswith(b"\x1a"):
            out += self.sock.recv(65536)
        return out[:-1].decode()

    def read(self, addr, nwords):
        """Read nwords 32-bit words at addr; returns a list of ints."""
        resp = self.cmd(f"mdw phys 0x{addr:08x} {nwords}")
        words = []
        for line in resp.splitlines():
            if ":" in line:                        # "0xADDR: w0 w1 w2 ..."
                data = line.split(":", 1)[1]
                words += [int(w, 16) for w in re.findall(r"[0-9a-fA-F]{8}", data)]
        return words[:nwords]

    def close(self):
        self.cmd("resume")
        self.cmd("shutdown")
        self.proc.wait()


def symbol(name):
    """Address of a symbol in the ELF (via nm)."""
    out = subprocess.check_output(["arm-none-eabi-nm", ELF]).decode()
    m = re.search(rf"^([0-9a-fA-F]+)\s+\S+\s+{re.escape(name)}$", out, re.M)
    if not m:
        sys.exit(f"symbol {name} not found in {ELF}")
    return int(m.group(1), 16)


# ------------------------------------------------------------ telemetry decode
# telemetry_t header: 14 u32 fields, then metric_t metric[].
HEADER = ["magic", "version", "cpu_hz", "gtimer_hz", "cal_cycles", "cal_gtimer",
          "read_ovf", "probe_ovf", "state", "bench_id", "n_metrics",
          "subbits", "maxpow", "nbuckets"]

def read_telemetry(ocd, base):
    hdr = dict(zip(HEADER, ocd.read(base, len(HEADER))))
    if hdr["magic"] != TELEM_MAGIC:
        sys.exit(f"bad magic 0x{hdr['magic']:08x}")

    nb = hdr["nbuckets"]
    hist_bytes   = (28 + 4 * nb + 7) & ~7          # count,sum,min,max,overflow,buckets[]
    metric_bytes = 16 + hist_bytes                 # name[16] + hist_t
    metrics = []
    for i in range(hdr["n_metrics"]):
        w = ocd.read(base + len(HEADER) * 4 + i * metric_bytes, metric_bytes // 4)
        name = b"".join(x.to_bytes(4, "little") for x in w[0:4]).split(b"\x00")[0]
        metrics.append({
            "name":     name.decode(errors="replace"),
            "count":    w[4] | (w[5] << 32),
            "sum":      w[6] | (w[7] << 32),
            "min":      w[8],
            "max":      w[9],
            "overflow": w[10],
            "buckets":  w[11:11 + nb],
        })
    return hdr, metrics


# ------------------------------------------------ sub-octave histogram (HdrHist)
def bucket_range(idx, subbits):
    """Cycle range [lo, hi) covered by histogram bucket idx."""
    S = 1 << subbits
    if idx < S:
        return idx, idx + 1
    octave, sub = divmod(idx - S, S)
    return (S + sub) << octave, (S + sub + 1) << octave

def percentile(m, subbits, p):
    """p-th percentile (cycles), interpolated within a bucket, clamped to [min,max]."""
    target, cum = p / 100.0 * m["count"], 0
    for idx, c in enumerate(m["buckets"]):
        if not c:
            continue
        if cum + c >= target:
            lo, hi = bucket_range(idx, subbits)
            return min(max(lo + (hi - lo) * (target - cum) / c, m["min"]), m["max"])
        cum += c
    return m["max"]


# ------------------------------------------------------------------ reporting
def report(hdr, metrics):
    print(f"\n=== telemetry (bench {hdr['bench_id']}, state {hdr['state']}, v{hdr['version']}) ===")
    print(f"read_overhead={hdr['read_ovf']}c  probe_overhead={hdr['probe_ovf']}c")
    if hdr["cal_gtimer"]:
        print(f"cal ratio: {hdr['cal_cycles'] / hdr['cal_gtimer']:.3f} cycles/gtimer-tick")
    sb = hdr["subbits"]
    print(f"histogram: {1 << sb} sub-buckets/octave, <= {100.0 / (1 << sb):.2f}% error\n")

    for m in metrics:
        if m["count"] == 0:
            continue
        mean = m["sum"] / m["count"]
        print(f"[{m['name']}]  n={m['count']}  overflow={m['overflow']}   (cycles)")
        print(f"   min {m['min']:>8}   mean {mean:>9.0f}"
              f"   p50 {percentile(m, sb, 50):>9.0f}"
              f"   p90 {percentile(m, sb, 90):>9.0f}"
              f"   p99 {percentile(m, sb, 99):>9.0f}   max {m['max']:>8}")
        octave_bars(m, sb)
        print()

def octave_bars(m, subbits):
    counts = {}
    for idx, c in enumerate(m["buckets"]):
        if c:
            e = bucket_range(idx, subbits)[0].bit_length() - 1
            counts[e] = counts.get(e, 0) + c
    peak = max(counts.values(), default=1)
    for e in sorted(counts):
        print(f"   2^{e:<2} [{1 << e:>8}..{2 << e:>8})  {counts[e]:>6}  {'#' * max(1, 40 * counts[e] // peak)}")


# ----------------------------------------------------------------------- main
def main():
    entry = symbol("_reset_handler")
    telem = symbol("g_telemetry")
    os.chdir(ROOT)

    ocd = OpenOCD()

    # Load, clear the NOLOAD flag scoreboard, start at the reset vector.
    ocd.cmd("halt")
    ocd.cmd(f"load_image {ELF}")
    ocd.cmd("mww phys 0xffff0000 0 12")
    ocd.cmd("reg cpsr 0x1d3")
    ocd.cmd(f"reg pc 0x{entry:08x}")
    ocd.cmd("resume")

    time.sleep(RUN_SECONDS)                         # run uninterrupted, then read once
    ocd.cmd("halt")

    hdr, metrics = read_telemetry(ocd, telem)
    if hdr["state"] != TELEM_DONE:
        print(f"WARN: state={hdr['state']} (not DONE); decoding current contents")
    ocd.close()

    report(hdr, metrics)


if __name__ == "__main__":
    main()
