#!/usr/bin/env python3
"""Host-side test driver for the DE1-SoC benchmark images.

`make test` runs the whole suite over JTAG in one OpenOCD session:
  - allocbench: allocator op timings (malloc/free, empty vs loaded)
  - edf sweep : rebuild the EDF image across a range of target utilizations and
                record deadline misses + per-tick scheduler cost -> the U* knee
  - edf trace : a per-tick schedule trace at one representative U -> textbook Gantt

Artifacts (CSV + PNG) land in test_results/<timestamp>/ (gitignored). Reads use
`mdw phys`; with the MMU/caches off (the default build) that is always coherent.
"""
import subprocess, socket, time, re, os, sys, csv
from datetime import datetime

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

OPENOCD_CFG  = "openocd/de1soc.cfg"
STATUS_BASE  = 0xFFFF0000
GENERAL_FLAG = 0xFFFF0018

# telemetry_t layout (bench/telemetry.h): 13-word header + 4 pad (metric_t is 8-aligned).
TELEM_HDR   = 56
HIST_NBUCK  = 16 * (20 - 4 + 1)
_HIST_RAW   = 8 + 8 + 4 + 4 + 4 + HIST_NBUCK * 4
HIST_BYTES  = (_HIST_RAW + 7) & ~7               # hist_t pads to 8 (uint64 members)
METRIC_STRIDE = 16 + HIST_BYTES

U_SWEEP      = [500, 700, 850, 900, 950, 975, 1000, 1025, 1050, 1100]
GANTT_U      = 700          # representative U for the schedule trace
TRACE_TICKS  = 1200         # must match workload_edf.c
GANTT_WINDOW = 240          # ticks shown in the Gantt (readability)
TASK_PERIODS = [40, 60, 100]
EDF_SECONDS  = 12
ALLOC_SECONDS = 8


def elf_unpack(elf):
    out = subprocess.check_output(['arm-none-eabi-nm', elf]).decode()
    return {p[2]: int(p[0], 16) for p in (l.split() for l in out.splitlines()) if len(p) == 3}


class OCD:
    def __init__(self, spawn_tries=4):
        for _ in range(spawn_tries):
            subprocess.run(['pkill', '-9', 'openocd'], stderr=subprocess.DEVNULL)
            time.sleep(1.5)
            self.p = subprocess.Popen(['openocd', '-f', OPENOCD_CFG, '-c', 'init'],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.s = None
            for _ in range(30):
                if self.p.poll() is not None:          # openocd exited (USB error) -> respawn
                    break
                try:
                    self.s = socket.socket()
                    self.s.connect(('localhost', 6666))
                    self.s.settimeout(45)          # so a mid-run USB drop can't hang us
                    return
                except ConnectionRefusedError:
                    self.s = None
                    time.sleep(0.5)
            self.p.kill()
        raise RuntimeError("openocd/JTAG did not come up -- power-cycle the board + USB-Blaster")

    def cmd(self, c):
        self.s.send((c + '\x1a').encode())
        r = b''
        while not r.endswith(b'\x1a'):
            r += self.s.recv(4096)
        return r[:-1].decode()

    def mdw(self, addr, n=1):
        out = self.cmd(f'mdw phys 0x{addr:08x} {n}')
        vals = []
        for m in re.finditer(r'[0-9a-f]{8}:\s+((?:[0-9a-f]{8}\s*)+)', out):
            vals += [int(x, 16) for x in m.group(1).split()]
        return vals if n > 1 else (vals[0] if vals else None)

    def u64(self, addr):
        lo, hi = self.mdw(addr, 2)
        return lo | (hi << 32)

    def read_bytes(self, addr, n):
        words = self.mdw(addr, (n + 3) // 4)
        b = bytearray()
        for w in words:
            b += int(w).to_bytes(4, 'little')
        return list(b[:n])

    def metric(self, tel, i):
        h = tel + TELEM_HDR + i * METRIC_STRIDE + 16
        count = self.u64(h)
        total = self.u64(h + 8)
        return count, ((total // count) if count else 0), self.mdw(h + 16), self.mdw(h + 20)

    def run_image(self, elf, entry, seconds):
        self.cmd('halt')
        self.cmd(f'load_image {elf}')
        self.cmd(f'mww phys 0x{STATUS_BASE:08x} 0 12')
        self.cmd('reg cpsr 0x1d3')
        self.cmd(f'reg pc 0x{entry:08x}')
        self.cmd('resume')
        time.sleep(seconds)
        self.cmd('halt')

    def close(self):
        try:
            self.cmd('shutdown')
        except Exception:
            pass
        self.p.kill()


def make(target, **vars):
    args = ['make'] + [f'{k}={v}' for k, v in vars.items()] + [target]
    r = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if r.returncode:
        raise RuntimeError(f"{' '.join(args)} failed:\n{r.stderr.decode()[-500:]}")


def run_alloc(ocd):
    make('build/allocbench.elf')
    sy = elf_unpack('build/allocbench.elf')
    ocd.run_image('build/allocbench.elf', sy['_reset_handler'], ALLOC_SECONDS)
    flag = ocd.mdw(GENERAL_FLAG)
    tel = sy['g_telemetry']
    metrics = {}
    for i, name in enumerate(('malloc', 'free', 'malloc_loaded', 'free_loaded')):
        metrics[name] = ocd.metric(tel, i)      # (count, mean, min, max)
    return flag, metrics


def run_edf(ocd, u, want_trace=False):
    subprocess.run(['rm', '-f', 'build/edf.elf'], check=False)
    make('build/edf.elf', U_PERMILLE=u)
    sy = elf_unpack('build/edf.elf')
    ocd.run_image('build/edf.elf', sy['_reset_handler'], EDF_SECONDS)
    r = sy['g_edf_result']
    magic, state, _u, _nt, rt, miss, cyc = ocd.mdw(r, 7)
    C, Tp = ocd.mdw(r + 28, 3), ocd.mdw(r + 40, 3)
    done, exp = ocd.mdw(r + 52, 3), ocd.mdw(r + 64, 3)
    _, cmean, _, cmax = ocd.metric(sy['g_telemetry'], 0)
    trace = ocd.read_bytes(sy['g_sched_trace'], TRACE_TICKS) if want_trace else None
    return dict(u=u, ok=(magic == 0x45444631 and state == 1),
                actual_u=sum(c / t for c, t in zip(C, Tp)), misses=miss,
                compl=100.0 * sum(done) / max(1, sum(exp)), cost_mean=cmean, cost_max=cmax,
                C=C, Tp=Tp, done=done, exp=exp, run_ticks=rt, cyc=cyc, trace=trace)


# ------------------------- artifacts -------------------------

def write_csv(path, header, rows):
    with open(path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(rows)


def plot_alloc(metrics, path):
    names = list(metrics)
    mean = [metrics[n][1] for n in names]
    lo = [metrics[n][1] - metrics[n][2] for n in names]
    hi = [metrics[n][3] - metrics[n][1] for n in names]
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.bar(range(len(names)), mean, yerr=[lo, hi], capsize=4, color='mediumseagreen')
    ax.set_xticks(range(len(names)))
    ax.set_xticklabels(names, rotation=12)
    ax.set_ylabel('cycles')
    ax.set_title('Allocator op cost (mean, min-max)  [MMU/caches off]')
    ax.grid(axis='y', alpha=0.3)
    fig.tight_layout(); fig.savefig(path, dpi=130); plt.close(fig)


def plot_sweep(rows, path):
    us = [r['actual_u'] for r in rows]
    fig, (a1, a2) = plt.subplots(2, 1, figsize=(8, 7), sharex=True)
    a1.plot(us, [r['misses'] for r in rows], 'o-', color='crimson')
    a1.axvline(1.0, ls='--', color='gray', label='U = 1.0')
    a1.set_ylabel('deadline misses'); a1.grid(alpha=0.3); a1.legend()
    a1.set_title('EDF schedulability & scheduler cost vs utilization')
    a2.plot(us, [r['cost_mean'] for r in rows], 's-', color='steelblue')
    a2.axvline(1.0, ls='--', color='gray')
    a2.set_ylabel('mean sched cost (cyc/tick)')
    a2.set_xlabel(r'actual utilization  $\sum C_i/T_i$'); a2.grid(alpha=0.3)
    fig.tight_layout(); fig.savefig(path, dpi=130); plt.close(fig)


def plot_gantt(trace, periods, path, u, window=GANTT_WINDOW):
    n = min(len(trace), window)
    nt = len(periods)
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c']
    fig, ax = plt.subplots(figsize=(12, 2.6))
    for i in range(nt):
        y = nt - 1 - i                                   # task 0 at top
        segs, t = [], 0
        while t < n:
            if trace[t] == i:
                s = t
                while t < n and trace[t] == i:
                    t += 1
                segs.append((s, t - s))
            else:
                t += 1
        ax.broken_barh(segs, (y + 0.15, 0.7), facecolors=colors[i % len(colors)])
        for k in range(0, n, periods[i]):                # release arrows (textbook up-arrow)
            ax.annotate('', xy=(k, y + 1.0), xytext=(k, y + 0.12),
                        arrowprops=dict(arrowstyle='->', color='black', lw=0.7))
    ax.set_yticks([nt - 1 - i + 0.5 for i in range(nt)])
    ax.set_yticklabels([f"$\\tau_{i}$ (T={periods[i]})" for i in range(nt)])
    ax.set_ylim(0, nt); ax.set_xlim(0, n)
    ax.set_xlabel('time (ticks)')
    ax.set_title(f'EDF schedule  (U={u/1000:.2f}, first {n} ticks; up-arrow = release)')
    ax.grid(axis='x', alpha=0.3)
    fig.tight_layout(); fig.savefig(path, dpi=130); plt.close(fig)


def main():
    outdir = os.path.join('test_results', datetime.now().strftime('%Y%m%d-%H%M%S'))
    os.makedirs(outdir, exist_ok=True)
    print(f"artifacts -> {outdir}\n")

    ocd = OCD()
    try:
        flag, ametrics = run_alloc(ocd)
        alloc_ok = (flag == 0xA11C)
        print(f"[allocbench] complete=0x{flag:x}")
        for name, (c, mean, mn, mx) in ametrics.items():
            print(f"   {name:<14} n={c:<5} mean={mean}cyc  min={mn}  max={mx}")

        print(f"\n[edf sweep]  {'reqU':>5} {'actU':>5} {'miss':>5} {'compl%':>7} {'cost':>6} {'max':>7}")
        rows = []
        for u in U_SWEEP:
            r = run_edf(ocd, u, want_trace=(u == GANTT_U))
            rows.append(r)
            print(f"             {u/1000:>5.2f} {r['actual_u']:>5.2f} {r['misses']:>5} "
                  f"{r['compl']:>7.1f} {r['cost_mean']:>6} {r['cost_max']:>7}"
                  f"{'' if r['ok'] else '  BAD'}")
    finally:
        ocd.close()

    # CSVs
    write_csv(os.path.join(outdir, 'alloc.csv'),
              ['op', 'count', 'mean_cyc', 'min_cyc', 'max_cyc'],
              [[k, *v] for k, v in ametrics.items()])
    write_csv(os.path.join(outdir, 'edf_sweep.csv'),
              ['req_u', 'actual_u', 'misses', 'compl_pct', 'cost_mean_cyc', 'cost_max_cyc',
               'C0', 'C1', 'C2', 'done0', 'done1', 'done2', 'exp0', 'exp1', 'exp2'],
              [[r['u'] / 1000, round(r['actual_u'], 4), r['misses'], round(r['compl'], 2),
                r['cost_mean'], r['cost_max'], *r['C'], *r['done'], *r['exp']] for r in rows])

    # plots
    plot_alloc(ametrics, os.path.join(outdir, 'alloc_timing.png'))
    plot_sweep(rows, os.path.join(outdir, 'edf_sweep.png'))
    gr = next((r for r in rows if r['u'] == GANTT_U and r['trace']), None)
    if gr:
        write_csv(os.path.join(outdir, 'edf_schedule.csv'), ['tick', 'task'],
                  list(enumerate(gr['trace'])))
        plot_gantt(gr['trace'], TASK_PERIODS, os.path.join(outdir, 'edf_schedule.png'), GANTT_U)

    clean = [r for r in rows if r['ok'] and r['misses'] == 0]
    if clean:
        knee = max(clean, key=lambda r: r['actual_u'])
        print(f"\nU* (highest actual U with 0 misses) = {knee['actual_u']:.3f} "
              f"(requested {knee['u']/1000:.2f})")
    ok = alloc_ok and all(r['ok'] for r in rows)
    print(f"\n{'PASS' if ok else 'FAIL'}   artifacts in {outdir}")
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
