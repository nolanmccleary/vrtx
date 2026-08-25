#!/usr/bin/env python3
from __future__ import annotations

import argparse, csv, re, socket, struct, subprocess, time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Sequence

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


ROOT        = Path(__file__).resolve().parent
TEST_ELF    = ROOT / "build" / "test.elf"
OPENOCD_CFG = ROOT / "openocd" / "de1soc.cfg"
RESULTS_DIR = ROOT / "test_results"

EDF_SECONDS   = 12.0
EDF_TASKS     = 3
ALLOC_METRICS = 7
TRACE_TICKS   = 2400      # g_sched_trace capacity (must match workload_edf.c)
GANTT_WINDOW  = 720       # ticks shown in the Gantt (>1 hyperperiod)
COMFORTABLE_U = 700       # always plot this under-capacity trial as a baseline


# --- target ABI -------------------------------------------------------------
# g_metrics[]  metric_t : min, max, sum, count   (mean = sum/count)
METRIC_STRUCT = struct.Struct("<IIII")
METRIC_SIZE   = METRIC_STRUCT.size
ALLOC_NAMES   = ["malloc", "free", "malloc_loaded", "free_loaded",
                 "mem_walk_8k", "mem_walk_256k", "matmul_32"]

# g_edf_metrics[NTASKS] metrics_t : ci, ci_av, prev_cycles, delta_sum, ti, ti_av, t0
EDF_METRIC_STRUCT = struct.Struct("<7I")
EDF_METRIC_SIZE   = EDF_METRIC_STRUCT.size

# g_fault[NUM_CPUS] fault_record_t : magic, vec, pc, spsr, dfsr, dfar, ifsr, ifar
FAULT_STRUCT = struct.Struct("<8I")
FAULT_MAGIC  = 0x464C5431   # "FLT1"
FAULT_VEC    = {1: "undef", 2: "swi", 3: "prefetch abort", 4: "data abort", 5: "fiq"}
FAULT_FS     = {0b00001: "alignment", 0b00101: "translation (L1)", 0b00111: "translation (L2)",
                0b01000: "sync external abort", 0b01001: "domain (L1)", 0b01011: "domain (L2)",
                0b01101: "permission (L1)", 0b01111: "permission (L2)", 0b10110: "async external abort"}


def fault_report(words: Sequence[int], core: int | None = None) -> str | None:
    if len(words) < 8 or words[0] != FAULT_MAGIC:
        return None
    _, vec, pc, spsr, dfsr, dfar, ifsr, ifar = words[:8]
    fs = lambda s: FAULT_FS.get(((s >> 6) & 0x10) | (s & 0xF), f"FS=0b{((s>>6)&0x10)|(s&0xF):05b}")
    tag = f" (CPU{core})" if core is not None else ""
    out = [f"*** CPU FAULT{tag}: {FAULT_VEC.get(vec, f'vec={vec}')} ***",
           f"  faulting PC = 0x{pc:08x}",
           f"  SPSR        = 0x{spsr:08x}  (mode 0x{spsr & 0x1f:02x})"]
    if vec == 4:
        out += [f"  DFSR        = 0x{dfsr:08x}  ({fs(dfsr)}, {'write' if (dfsr>>11)&1 else 'read'})",
                f"  DFAR        = 0x{dfar:08x}"]
    elif vec == 3:
        out += [f"  IFSR        = 0x{ifsr:08x}  ({fs(ifsr)})", f"  IFAR        = 0x{ifar:08x}"]
    return "\n".join(out)


# --- result types -----------------------------------------------------------
@dataclass(frozen=True)
class Metric:
    name: str
    count: int
    total: int
    minimum: int
    maximum: int

    @property
    def mean(self) -> int:
        return self.total // self.count if self.count else 0


@dataclass(frozen=True)
class EDFResult:
    index: int
    u_permille: int
    ticks: int
    misses: int
    c: tuple[int, ...]
    periods: tuple[int, ...]
    done: tuple[int, ...]
    expected: tuple[int, ...]
    ci_av: tuple[int, ...]
    ti_av: tuple[int, ...]
    ci: tuple[int, ...]
    sched_overhead: tuple[int, ...]   # per-CPU EWMA scheduler cost, cycles

    @property
    def requested_u(self) -> float:
        return self.u_permille / 1000.0

    @property
    def configured_u(self) -> float:
        # what the firmware set up (sum C_i/T_i in ticks), not a measurement
        return sum(c / p for c, p in zip(self.c, self.periods))

    @property
    def measured_u_per_task(self) -> tuple[float, ...]:
        return tuple((ci / ti) if ti else 0.0 for ci, ti in zip(self.ci_av, self.ti_av))

    @property
    def measured_u(self) -> float:
        return sum(self.measured_u_per_task)


# --- ELF --------------------------------------------------------------------
def elf_symbols(elf: Path) -> dict[str, int]:
    out = subprocess.check_output(["arm-none-eabi-nm", str(elf)], cwd=ROOT, text=True)
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    return syms


def require_symbols(symbols: dict[str, int], names: Sequence[str]) -> None:
    missing = [n for n in names if n not in symbols]
    if missing:
        raise RuntimeError("missing ELF symbols: " + ", ".join(missing))


def elf_cache_config(elf: Path) -> str:
    # Makefile records compile-time enables as absolute symbols (--defsym); nm -a lists them.
    out = subprocess.check_output(["arm-none-eabi-nm", "-a", str(elf)], cwd=ROOT, text=True)
    vals = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in ("A", "a") and parts[2].startswith("_cfg_enable_"):
            vals[parts[2]] = int(parts[0], 16)
    onoff = lambda v: "on" if vals.get(v, 0) else "off"
    return (f"MMU {onoff('_cfg_enable_mmu')} · D {onoff('_cfg_enable_dcache')} · "
            f"I {onoff('_cfg_enable_icache')} · L2 {onoff('_cfg_enable_l2')} · SMP {onoff('_cfg_enable_smp')}")


# --- OpenOCD ----------------------------------------------------------------
class OCD:
    def __init__(self, spawn_tries: int = 3):
        self.proc = None
        self.sock = None
        self.owns_daemon = False
        self.fault_addr: int | None = None   # g_fault[], set by main() for crash decode
        self.num_cpus: int = 1

        # Reuse a running daemon (openocd/ocd) rather than spawn+SIGKILL churn, which
        # is the main cause of USB-Blaster wedging (stale libusb endpoints).
        if self._connect():
            return
        for attempt in range(spawn_tries):
            if attempt > 0:
                subprocess.run(["pkill", "-9", "openocd"], stderr=subprocess.DEVNULL)
                time.sleep(0.5)
            self.proc = subprocess.Popen(["openocd", "-f", str(OPENOCD_CFG), "-c", "init"],
                                         cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.owns_daemon = True
            for _ in range(30):
                if self.proc.poll() is not None:
                    break
                if self._connect():
                    return
                time.sleep(0.5)
            if self.proc.poll() is None:
                self.proc.kill()
        raise RuntimeError("OpenOCD/JTAG did not come up (USB-Blaster wedged? power-cycle it)")

    def _connect(self) -> bool:
        try:
            sock = socket.socket()
            sock.connect(("localhost", 6666))
            sock.settimeout(45)
            self.sock = sock
            self.cmd("cortex_a maskisr on")
            self.cmd("rbp all")          # clear stale breakpoints on reuse
            return True
        except (ConnectionRefusedError, OSError):
            self.sock = None
            return False

    def __enter__(self) -> "OCD":
        return self

    def __exit__(self, exc_type, exc, tb) -> bool:
        self.close()
        return False

    def cmd(self, command: str) -> str:
        if self.sock is None:
            raise RuntimeError("OpenOCD socket is closed")
        self.sock.sendall((command + "\x1a").encode())
        resp = b""
        while not resp.endswith(b"\x1a"):
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("OpenOCD connection closed")
            resp += chunk
        return resp[:-1].decode()

    # memory
    def read_words(self, addr: int, count: int) -> list[int]:
        out = self.cmd(f"mdw phys 0x{addr:08x} {count}")
        vals: list[int] = []
        for line in out.splitlines():
            if ":" in line:
                vals += [int(w, 16) for w in re.findall(r"\b[0-9a-fA-F]{8}\b", line.split(":", 1)[1])]
        if len(vals) < count:
            raise RuntimeError(f"short JTAG read at 0x{addr:08x}: wanted {count}, got {len(vals)}\n{out}")
        return vals[:count]

    def read_u32(self, addr: int) -> int:
        return self.read_words(addr, 1)[0]

    def read_bytes(self, addr: int, size: int) -> bytes:
        words = self.read_words(addr, (size + 3) // 4)
        return b"".join(w.to_bytes(4, "little") for w in words)[:size]

    def write_u32(self, addr: int, value: int) -> None:
        self.cmd(f"mww phys 0x{addr:08x} 0x{value:08x}")

    # run control
    def load_image(self, elf: Path, entry: int) -> None:
        self.cmd("halt")
        self.cmd(f"load_image {elf}")
        self.cmd("reg cpsr 0x1d3")
        self.cmd(f"reg pc 0x{entry:08x}")

    def resume(self) -> None:
        self.cmd("resume")

    def halt(self) -> None:
        self.cmd("halt")

    def step(self) -> None:
        self.cmd("step")

    def pc(self) -> int:
        # CPU0 control-flow read. Halt first: reg pc only yields on a halted core.
        self.cmd("halt")
        out = self.cmd("reg pc")
        m = re.search(r"0x([0-9a-fA-F]+)", out)
        if m is None:
            raise RuntimeError(f"cannot parse PC: {out}")
        return int(m.group(1), 16)

    def dump_pcs(self) -> str:
        # Headshot + read PC on every core. A core in reset reports "unavailable".
        out = []
        for core, tgt in enumerate(["cv_hps.cpu", "cv_hps.cpu1"][:self.num_cpus]):
            self.cmd(f"targets {tgt}")
            self.cmd("halt")
            m = re.search(r"0x([0-9a-fA-F]+)", self.cmd("reg pc"))
            out.append(f"CPU{core}=" + (f"0x{int(m.group(1), 16):08x}" if m else "unavailable"))
        self.cmd("targets cv_hps.cpu")
        return ", ".join(out)

    def read_fault(self) -> str | None:
        if self.fault_addr is None:
            return None
        try:
            words = self.read_words(self.fault_addr, 8 * self.num_cpus)
        except Exception:
            return None
        reports = [r for core in range(self.num_cpus)
                   if (r := fault_report(words[core * 8:core * 8 + 8], core))]
        return "\n".join(reports) if reports else None

    def diagnose(self) -> str:
        # The one crash tap. ALWAYS every core's live PC; fault decode added on top,
        # never instead. You never get one core when the machine is angry.
        parts = [self.dump_pcs()]
        fault = self.read_fault()
        if fault:
            parts.append(fault)
        return "\n".join(parts)

    def wait_halt(self, timeout: float = 30.0) -> int:
        out = self.cmd(f"wait_halt {int(timeout * 1000)}").lower()
        if "timeout" in out or "timed out" in out:
            raise RuntimeError(f"no halt in {timeout:.0f}s: {self.diagnose()}")
        return self.pc()

    def add_hw_breakpoint(self, addr: int) -> None:
        out = self.cmd(f"bp 0x{addr:08x} 4 hw").lower()
        if "error" in out or "resource not available" in out:
            raise RuntimeError(f"cannot install hw breakpoint at 0x{addr:08x}:\n{out}")

    def expect_breakpoint(self, expected_addr: int, *, timeout: float = 30.0) -> None:
        pc = self.wait_halt(timeout)
        if pc != expected_addr:
            raise RuntimeError(f"halt 0x{pc:08x} != expected 0x{expected_addr:08x}: {self.diagnose()}")

    def continue_from_breakpoint(self) -> None:
        # OpenOCD steps over the managed hw breakpoint, then we continue.
        self.step()
        self.resume()

    def close(self) -> None:
        if self.sock is not None:
            if self.owns_daemon:                    # leave a reused external daemon running
                try: self.cmd("shutdown")
                except Exception: pass
            try: self.sock.close()
            except Exception: pass
            self.sock = None
        if self.owns_daemon and self.proc is not None and self.proc.poll() is None:
            self.proc.kill()


# --- telemetry collection ---------------------------------------------------
def read_metric(ocd: OCD, base: int, index: int) -> Metric:
    minimum, maximum, total, count = METRIC_STRUCT.unpack(
        ocd.read_bytes(base + index * METRIC_SIZE, METRIC_SIZE))
    name = ALLOC_NAMES[index] if index < len(ALLOC_NAMES) else f"metric_{index}"
    return Metric(name, count, total, minimum, maximum)


def collect_alloc(ocd: OCD, base: int) -> tuple[Metric, ...]:
    return tuple(read_metric(ocd, base, i) for i in range(ALLOC_METRICS))


def expected_jobs(ticks: int, period: int) -> int:
    # First release is on tick 1: 1, 1+T, 1+2T, ...
    return ((ticks - 1) // period) + 1 if ticks else 0


def collect_edf_trial(ocd: OCD, symbols: dict[str, int], periods: tuple[int, ...], num_cpus: int) -> EDFResult:
    ticks = ocd.read_u32(symbols["g_ticks_m"])
    raw = ocd.read_bytes(symbols["g_edf_metrics"], len(periods) * EDF_METRIC_SIZE)
    mets = [EDF_METRIC_STRUCT.unpack_from(raw, i * EDF_METRIC_SIZE) for i in range(len(periods))]
    return EDFResult(
        index=ocd.read_u32(symbols["g_edf_u_index"]),
        u_permille=ocd.read_u32(symbols["g_edf_u_permille"]),
        ticks=ticks,
        misses=ocd.read_u32(symbols["g_misses_m"]),
        c=tuple(ocd.read_words(symbols["g_edf_C"], len(periods))),
        periods=periods,
        done=tuple(ocd.read_words(symbols["g_edf_done"], len(periods))),
        expected=tuple(expected_jobs(ticks, p) for p in periods),
        ci=tuple(m[0] for m in mets),
        ci_av=tuple(m[1] for m in mets),
        ti_av=tuple(m[5] for m in mets),
        sched_overhead=tuple(ocd.read_words(symbols["g_overhead_m"], num_cpus)),
    )


# --- reporting --------------------------------------------------------------
def print_alloc(metrics: Sequence[Metric]) -> None:
    print("[allocbench]")
    for m in metrics:
        print(f"    {m.name:<14} n={m.count:<5} mean={m.mean:<6} min={m.minimum:<6} max={m.maximum}")


def print_edf_header() -> None:
    print(f"\n{'reqU':>6} {'cfgU':>6} {'measU':>6} {'ticks':>8} {'miss':>8} "
          f"{'sched c/c':>10}   per-task measured U (ci_av/ti_av)")


def print_edf_result(r: EDFResult) -> None:
    per_task = "  ".join(f"t{i}={u:.3f}" for i, u in enumerate(r.measured_u_per_task))
    print(f"{r.requested_u:>6.3f} {r.configured_u:>6.3f} {r.measured_u:>6.3f} {r.ticks:>8} "
          f"{r.misses:>8} {'/'.join(map(str, r.sched_overhead)):>10}   {per_task}")


# --- CSV --------------------------------------------------------------------
def write_alloc_csv(path: Path, metrics: Sequence[Metric]) -> None:
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["op", "count", "mean_cyc", "min_cyc", "max_cyc"])
        for m in metrics:
            w.writerow([m.name, m.count, m.mean, m.minimum, m.maximum])


def write_cache_csv(path: Path, metrics: Sequence[Metric]) -> None:
    # For mem_walk, min_cyc = warm (cache-hit) pass, max_cyc = cold (fill) pass.
    by_name = {m.name: m for m in metrics}
    config = elf_cache_config(TEST_ELF)
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["workload", "cache_config", "count", "mean_cyc", "min_cyc", "max_cyc"])
        for name in ("mem_walk_8k", "mem_walk_256k", "matmul_32"):
            m = by_name.get(name)
            if m:
                w.writerow([m.name, config, m.count, m.mean, m.minimum, m.maximum])


def write_edf_csv(path: Path, rows: Sequence[EDFResult]) -> None:
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["requested_u", "configured_u", "measured_u", "ticks", "misses",
                    "sched_overhead_c0_cyc", "sched_overhead_c1_cyc",
                    "C0", "C1", "C2", "T0", "T1", "T2", "done0", "done1", "done2",
                    "expected0", "expected1", "expected2",
                    "ci_av0", "ci_av1", "ci_av2", "ti_av0", "ti_av1", "ti_av2"])
        for r in rows:
            w.writerow([r.requested_u, round(r.configured_u, 6), round(r.measured_u, 6),
                        r.ticks, r.misses,
                        r.sched_overhead[0], r.sched_overhead[1] if len(r.sched_overhead) > 1 else 0,
                        *r.c, *r.periods, *r.done, *r.expected, *r.ci_av, *r.ti_av])


# --- plots ------------------------------------------------------------------
def plot_alloc(metrics: Sequence[Metric], path: Path) -> None:
    names  = [m.name for m in metrics]
    means  = [m.mean for m in metrics]
    lo_err = [m.mean - m.minimum for m in metrics]
    hi_err = [m.maximum - m.mean for m in metrics]
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.bar(range(len(metrics)), means, yerr=[lo_err, hi_err], capsize=4)
    ax.set_xticks(range(len(metrics)))
    ax.set_xticklabels(names, rotation=12)
    ax.set_ylabel("cycles")
    ax.set_title(f"Allocator operation cost  ({elf_cache_config(TEST_ELF)})")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def plot_cache(metrics: Sequence[Metric], path: Path) -> None:
    # 8 KB set fits L1, 256 KB exceeds L1 / fits L2. cold = max pass, warm = min pass.
    by_name = {m.name: m for m in metrics}
    matmul = by_name.get("matmul_32")
    fig, (ax_mw, ax_mm) = plt.subplots(1, 2, figsize=(9, 4))
    xs, heights, colors, tick_pos, tick_lbl = [], [], [], [], []
    x = 0.0
    for size_label, name in (("8 KB", "mem_walk_8k"), ("256 KB", "mem_walk_256k")):
        m = by_name.get(name)
        if m is None:
            continue
        xs += [x, x + 1.0]
        heights += [m.maximum, m.minimum]
        colors += ["#c0504d", "#4f81bd"]
        tick_pos += [x, x + 1.0]
        tick_lbl += [f"{size_label}\ncold", f"{size_label}\nwarm"]
        x += 2.7
    if xs:
        ax_mw.bar(xs, heights, width=0.8, color=colors)
        ax_mw.set_yscale("log")
        ax_mw.set_xticks(tick_pos)
        ax_mw.set_xticklabels(tick_lbl, fontsize=8)
        ax_mw.set_title("SDRAM RMW / pass: cold vs warm")
    ax_mw.set_ylabel("cycles (log)")
    ax_mw.grid(axis="y", alpha=0.3, which="both")
    if matmul is not None:
        ax_mm.bar([matmul.name], [matmul.mean],
                  yerr=[[matmul.mean - matmul.minimum], [matmul.maximum - matmul.mean]],
                  capsize=6, color="#9bbb59")
        ax_mm.set_title(f"{matmul.name}: compute time-to-complete")
    ax_mm.set_ylabel("cycles")
    ax_mm.grid(axis="y", alpha=0.3)
    fig.suptitle(f"Cache-sensitive workloads  ({elf_cache_config(TEST_ELF)})")
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def plot_edf(rows: Sequence[EDFResult], path: Path) -> None:
    requested = [r.requested_u for r in rows]
    misses    = [r.misses for r in rows]
    measured  = [r.measured_u for r in rows]
    n_cpus    = len(rows[0].sched_overhead) if rows else 1
    overhead  = [[r.sched_overhead[c] for r in rows] for c in range(n_cpus)]
    fig, (a1, a2, a3) = plt.subplots(3, 1, figsize=(8, 10), sharex=True)
    a1.plot(requested, misses, "o-")
    a1.axvline(1.0, linestyle="--")
    a1.set_ylabel("deadline misses")
    a1.set_title("EDF: schedulability, scheduler cost, measured utilization")
    a1.grid(alpha=0.3)
    for c in range(n_cpus):
        a2.plot(requested, overhead[c], "o-", label=f"CPU{c}")
    a2.axvline(1.0, linestyle="--")
    a2.set_ylabel("scheduler cycles / tick (EWMA)")
    a2.legend()
    a2.grid(alpha=0.3)
    if requested:
        lo, hi = min(requested), max(requested)
        a3.plot([lo, hi], [lo, hi], "--", color="gray", label="y = x")
    a3.plot(requested, measured, "o-", label="measured")
    a3.axvline(1.0, linestyle="--")
    a3.set_ylabel("measured utilization")
    a3.set_xlabel("requested utilization")
    a3.legend()
    a3.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


def plot_gantt(trace: Sequence[int], periods: Sequence[int], path: Path, u_permille: int,
               u_configured: float = 0.0, task_u: Sequence[float] = (), label: str = "",
               window: int = GANTT_WINDOW) -> None:
    n = min(len(trace), window)
    nt = len(periods)
    colors = ["#1f77b4", "#ff7f0e", "#2ca02c"]
    width = min(26.0, max(12.0, n / 130.0))
    fig, ax = plt.subplots(figsize=(width, 2.6))
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
        for k in range(0, n, periods[i]):                # release arrows
            ax.annotate("", xy=(k, y + 1.0), xytext=(k, y + 0.12),
                        arrowprops=dict(arrowstyle="->", color="black", lw=0.7))
    row_label = lambda i: f"$\\tau_{i}$ (T={periods[i]})" + (f"\nU={task_u[i]:.3f}" if i < len(task_u) else "")
    measured_total = sum(task_u) if task_u else 0.0
    ax.set_yticks([nt - 1 - i + 0.5 for i in range(nt)])
    ax.set_yticklabels([row_label(i) for i in range(nt)], fontsize=8)
    ax.set_ylim(0, nt)
    ax.set_xlim(0, n)
    ax.set_xlabel("time (ticks)")
    ax.set_title(f"EDF schedule  (Ureq={u_permille / 1000:.3f}, Ucfg={u_configured:.3f}, "
                 f"Umeas={measured_total:.3f}{', ' + label if label else ''}; first {n} ticks; up-arrow = release)")
    ax.grid(axis="x", alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


# --- phases -----------------------------------------------------------------
def allocbench(ocd: OCD, symbols: dict[str, int]) -> list[Metric]:
    bp = symbols["ktrace_bp_alloc_done"]
    ocd.add_hw_breakpoint(bp)
    ocd.resume()
    ocd.expect_breakpoint(bp, timeout=30.0)
    metrics = collect_alloc(ocd, symbols["g_metrics"])
    print_alloc(metrics)
    return list(metrics)


def edf_test(ocd: OCD, symbols: dict[str, int], num_cpus: int,
             periods: tuple[int, ...], u_values: tuple[int, ...]) -> tuple[list[EDFResult], dict[int, bytes], bytes]:
    ocd.continue_from_breakpoint()          # off bp_alloc (or load); ready/done armed by main()
    bp_edf_ready = symbols["ktrace_bp_edf_ready"]
    bp_edf_done  = symbols["ktrace_bp_edf_done"]
    print_edf_header()

    edf_results: list[EDFResult] = []
    traces: dict[int, bytes] = {}

    for trial, expected_u in enumerate(u_values):
        ocd.expect_breakpoint(bp_edf_ready, timeout=15.0)
        idx = ocd.read_u32(symbols["g_edf_u_index"])
        u   = ocd.read_u32(symbols["g_edf_u_permille"])
        if idx != trial or u != expected_u:
            raise RuntimeError(f"EDF desync: expected trial {trial} U={expected_u}, got trial {idx} U={u}")

        ocd.continue_from_breakpoint()
        time.sleep(EDF_SECONDS)
        ocd.halt()

        if ocd.read_fault():
            raise RuntimeError(ocd.diagnose())

        result = collect_edf_trial(ocd, symbols, periods, num_cpus)
        edf_results.append(result)
        print_edf_result(result)

        # CPU0 schedule trace this trial: g_sched_trace[0][.], g_trace_len[0].
        # (CPU1 is idle during the CPU0 sweep -- its trace comes from phase B below.)
        n = min(ocd.read_u32(symbols["g_trace_len"]), TRACE_TICKS)
        if n:
            traces[result.u_permille] = ocd.read_bytes(symbols["g_sched_trace"], n)

        # Release the sampled trial; let firmware run on to the next trial's bp_edf_ready.
        # It may already be sitting on it (short trial) -- only resume if it isn't.
        ocd.write_u32(symbols["g_test_release"], 1)
        ocd.resume()
        time.sleep(0.05)
        ocd.halt()
        if ocd.pc() != bp_edf_ready:
            ocd.resume()
        ocd.resume()

    ocd.expect_breakpoint(bp_edf_done, timeout=30.0)

    # Phase B: CPU0 has seeded the edf_driver onto CPU1 and parked. CPU1 now runs its
    # own u=0.7 workload alone (no concurrent allocation). Let it fill its trace, then
    # snapshot g_sched_trace[1] / g_trace_len[1] on the CPU1 target for the CPU1 Gantt.
    cpu1_trace = b""
    if num_cpus > 1:
        ocd.resume()                       # CPU0 -> its idle loop (keeps ticking, feeds the WDT)
        time.sleep(3.0)                     # CPU1 accumulates its schedule trace
        ocd.cmd("targets cv_hps.cpu1")
        ocd.cmd("halt")
        n = min(ocd.read_u32(symbols["g_trace_len"] + 4), TRACE_TICKS)   # g_trace_len[1]
        cpu1_trace = ocd.read_bytes(symbols["g_sched_trace"] + TRACE_TICKS, n) if n else b""
        ocd.cmd("targets cv_hps.cpu")

    return edf_results, traces, cpu1_trace


def write_artifacts(outdir: Path, alloc_metrics: list[Metric], edf_results: list[EDFResult],
                    traces: dict[int, bytes], periods: tuple[int, ...], cpu1_trace: bytes = b"") -> None:
    # A skipped phase leaves its list empty and is simply not written.
    if alloc_metrics:
        write_alloc_csv(outdir / "alloc.csv", alloc_metrics[:4])
        write_cache_csv(outdir / "cache_workloads.csv", alloc_metrics)
        plot_alloc(alloc_metrics[:4], outdir / "alloc_timing.png")
        plot_cache(alloc_metrics, outdir / "cache_workloads.png")

    if not edf_results:
        return
    write_edf_csv(outdir / "edf_sweep.csv", edf_results)
    plot_edf(edf_results, outdir / "edf_sweep.png")

    by_u       = {r.u_permille: r for r in edf_results}
    swept      = sorted(by_u)
    no_miss    = [u for u in swept if by_u[u].misses == 0]
    miss       = [u for u in swept if by_u[u].misses > 0]
    labels: dict[int, list[str]] = {}
    if COMFORTABLE_U in by_u: labels.setdefault(COMFORTABLE_U, []).append("comfortable")
    if no_miss:               labels.setdefault(max(no_miss), []).append("last no-miss")
    if miss:                  labels.setdefault(min(miss), []).append("first miss")
    if swept:                 labels.setdefault(max(swept), []).append("max U")

    for u in sorted(labels):
        trace = traces.get(u)
        if not trace:
            continue
        plot_gantt(trace, periods, outdir / f"edf_schedule_u{u / 1000:.3f}.png", u,
                   u_configured=by_u[u].configured_u, task_u=by_u[u].measured_u_per_task,
                   label=" / ".join(labels[u]))

    # CPU1 Gantt at u=0.7 from the phase-B capture (per-core CPU1 metrics not mirrored yet
    # -> no task-U labels). configured_u is the same u=0.7 the driver set up.
    if cpu1_trace and COMFORTABLE_U in by_u:
        plot_gantt(cpu1_trace, periods, outdir / f"edf_schedule_cpu1_u{COMFORTABLE_U / 1000:.3f}.png",
                   COMFORTABLE_U, u_configured=by_u[COMFORTABLE_U].configured_u, label="CPU1")

    clean = [r for r in edf_results if r.misses == 0]
    if clean:
        knee = max(clean, key=lambda r: r.configured_u)
        print(f"\nU* = {knee.configured_u:.3f} (requested {knee.requested_u:.3f}, measured {knee.measured_u:.3f})")


# --- main -------------------------------------------------------------------
def main(bootable: bool = False) -> None:
    if not TEST_ELF.exists():
        raise RuntimeError(f"{TEST_ELF} does not exist")

    symbols  = elf_symbols(TEST_ELF)
    num_cpus = 2 if symbols.get("_cfg_enable_smp") else 1
    require_symbols(symbols, (
        "_reset_handler", "g_metrics", "g_edf_metrics", "g_ticks_m", "g_misses_m", "g_overhead_m",
        "g_edf_u_values", "g_edf_u_count", "g_edf_u_index", "g_edf_u_permille",
        "g_edf_periods", "g_edf_C", "g_edf_done", "g_sched_trace", "g_trace_len", "g_test_release",
        "ktrace_bp_alloc_done", "ktrace_bp_edf_ready", "ktrace_bp_edf_done", "fault_trap", "g_fault"))
    if bootable:
        require_symbols(symbols, ("g_boot_release",))

    bp_edf_ready = symbols["ktrace_bp_edf_ready"]
    bp_edf_done  = symbols["ktrace_bp_edf_done"]
    bp_fault     = symbols["fault_trap"]

    outdir = RESULTS_DIR / datetime.now().strftime("%Y%m%d-%H%M%S")
    outdir.mkdir(parents=True, exist_ok=True)
    print(f"artifacts -> {outdir}\n")

    alloc_metrics: list[Metric] = []
    edf_results: list[EDFResult] = []
    traces: dict[int, bytes] = {}
    cpu1_trace: bytes = b""

    with OCD() as ocd:
        if bootable:
            # Board self-booted from SD, spinning at the BOOT_TEST gate: attach, don't load.
            ocd.halt()
            print(f"bootable: attached to self-booted target, PC=0x{ocd.pc():08x}\n")
        else:
            ocd.load_image(TEST_ELF, symbols["_reset_handler"])

        u_count = ocd.read_u32(symbols["g_edf_u_count"])
        if not 1 <= u_count <= 64:
            raise RuntimeError(f"invalid EDF U count: {u_count}")
        periods  = tuple(ocd.read_words(symbols["g_edf_periods"], EDF_TASKS))
        u_values = tuple(ocd.read_words(symbols["g_edf_u_values"], u_count))
        print(f"EDF U sweep: {list(u_values)}")
        print(f"EDF periods: {list(periods)}\n")

        ocd.fault_addr = symbols["g_fault"]
        ocd.num_cpus   = num_cpus
        ocd.add_hw_breakpoint(bp_edf_ready)
        ocd.add_hw_breakpoint(bp_edf_done)
        ocd.add_hw_breakpoint(bp_fault)
        if bootable:
            ocd.write_u32(symbols["g_boot_release"], 1)

        # Control panel: comment a phase to skip it (also comment its call in main.c).
        alloc_metrics = allocbench(ocd, symbols)
        edf_results, traces, cpu1_trace = edf_test(ocd, symbols, num_cpus, periods, u_values)

    write_artifacts(outdir, alloc_metrics, edf_results, traces, periods, cpu1_trace)
    print(f"\nPASS   artifacts in {outdir}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="DE1-SoC RTOS test driver")
    parser.add_argument("--bootable", "--live", action="store_true", dest="bootable",
                        help="attach to a self-booted target spinning at the BOOT_TEST gate "
                             "and release it, instead of load_image-ing the ELF over JTAG.")
    args = parser.parse_args()
    main(bootable=args.bootable)
