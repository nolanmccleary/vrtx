#!/usr/bin/env python3

from __future__ import annotations

import csv
import re
import socket
import struct
import subprocess
import time

from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Sequence

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt


# =============================================================================
# Configuration
# =============================================================================

ROOT = Path(__file__).resolve().parent

TEST_ELF = ROOT / "build" / "test.elf"
OPENOCD_CFG = ROOT / "openocd" / "de1soc.cfg"
RESULTS_DIR = ROOT / "test_results"

EDF_SECONDS = 12.0

EDF_TASKS = 3
ALLOC_METRICS = 6   # malloc, free, malloc_loaded, free_loaded, mem_walk_8k, matmul_32

TRACE_TICKS = 2400         # g_sched_trace capacity (must match workload_edf.c); 4 hyperperiods (lcm(40,60,100)=600)
GANTT_WINDOW = 720         # ticks shown in the Gantt -- 3x the original 240 (>1 hyperperiod), rest of the 2400-tick capture is unshown
COMFORTABLE_U = 700        # always plot this well-under-capacity trial as a baseline


# =============================================================================
# Target telemetry ABI
# =============================================================================

# telemetry_t:
#
#     uint32_t running;
#     uint32_t read_overhead;
#     metric_t metric[];
#
# metric_t:
#
#     char     name[16];
#     uint64_t count;
#     uint64_t sum;
#     uint32_t min;
#     uint32_t max;

TELEMETRY_HEADER = struct.Struct("<II")
METRIC_STRUCT = struct.Struct("<16sQQII")

TELEMETRY_HEADER_SIZE = TELEMETRY_HEADER.size
METRIC_SIZE = METRIC_STRUCT.size


# =============================================================================
# Fault decode  (bench/fault.h fault_record_t)
# =============================================================================

# fault_record_t: magic, vec, pc, spsr, dfsr, dfar, ifsr, ifar  (8 x u32)
FAULT_STRUCT = struct.Struct("<8I")
FAULT_MAGIC = 0x464C5431  # "FLT1"

FAULT_VEC = {1: "undef", 2: "swi", 3: "prefetch abort", 4: "data abort", 5: "fiq"}

# short-descriptor DFSR/IFSR fault status: FS = {bit10, bits[3:0]}
FAULT_FS = {
    0b00001: "alignment",
    0b00101: "translation (L1)",
    0b00111: "translation (L2)",
    0b01000: "synchronous external abort",
    0b01001: "domain (L1)",
    0b01011: "domain (L2)",
    0b01101: "permission (L1)",
    0b01111: "permission (L2)",
    0b10110: "asynchronous external abort",
}


def _fs_str(status: int) -> str:
    fs = ((status >> 6) & 0x10) | (status & 0x0F)
    return FAULT_FS.get(fs, f"FS=0b{fs:05b}")


def fault_report(words: Sequence[int]) -> str | None:
    if len(words) < 8 or words[0] != FAULT_MAGIC:
        return None

    _, vec, pc, spsr, dfsr, dfar, ifsr, ifar = words[:8]
    name = FAULT_VEC.get(vec, f"vec={vec}")

    lines = [
        f"*** CPU FAULT: {name} ***",
        f"  faulting PC = 0x{pc:08x}",
        f"  SPSR        = 0x{spsr:08x}  (mode 0x{spsr & 0x1f:02x})",
    ]
    if vec == 4:  # data abort
        wnr = "write" if (dfsr >> 11) & 1 else "read"
        lines.append(f"  DFSR        = 0x{dfsr:08x}  ({_fs_str(dfsr)}, {wnr})")
        lines.append(f"  DFAR        = 0x{dfar:08x}")
    elif vec == 3:  # prefetch abort
        lines.append(f"  IFSR        = 0x{ifsr:08x}  ({_fs_str(ifsr)})")
        lines.append(f"  IFAR        = 0x{ifar:08x}")

    return "\n".join(lines)


# =============================================================================
# Result types
# =============================================================================

@dataclass(frozen=True)
class Metric:
    name: str        # metric_t, telemetry_t g_telemetry
    count: int       # metric_t, telemetry_t g_telemetry
    total: int       # metric_t, telemetry_t g_telemetry
    minimum: int     # metric_t, telemetry_t g_telemetry
    maximum: int     # metric_t, telemetry_t g_telemetry

    @property
    def mean(self) -> int:
        return self.total // self.count if self.count else 0


@dataclass(frozen=True)
class EDFResult:
    index: int                  # g_edf_u_index
    u_permille: int             # g_edf_u_permille

    ticks: int                  # gTicks
    misses: int                 # gMissedDeadlines

    c: tuple[int, ...]          # g_edf_C
    periods: tuple[int, ...]    # g_edf_periods

    done: tuple[int, ...]       # g_edf_done
    expected: tuple[int, ...]   # gTicks, g_edf_periods

    sched: Metric               # telemetry_t g_telemetry, metric_t

    @property
    def requested_u(self) -> float:
        return self.u_permille / 1000.0

    @property
    def actual_u(self) -> float:
        return sum(
            c / period
            for c, period in zip(self.c, self.periods)
        )



# =============================================================================
# ELF
# =============================================================================

def elf_symbols(elf: Path) -> dict[str, int]:
    output = subprocess.check_output(
        [
            "arm-none-eabi-nm",
            str(elf),
        ],
        cwd=ROOT,
        text=True,
    )

    symbols: dict[str, int] = {}

    for line in output.splitlines():
        parts = line.split()

        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)

    return symbols


def require_symbols(
    symbols: dict[str, int],
    names: Sequence[str],
) -> None:
    missing = [
        name
        for name in names
        if name not in symbols
    ]

    if missing:
        raise RuntimeError(
            "missing ELF symbols: "
            + ", ".join(missing)
        )


def elf_cache_config(elf: Path) -> str:
    """MMU / D-cache / I-cache enable state, read from the .elf. The Makefile
    records the compile-time enables as absolute symbols (--defsym); nm needs -a
    to list absolute symbols. No runtime field, no loaded-image inflation."""
    output = subprocess.check_output(
        ["arm-none-eabi-nm", "-a", str(elf)],
        cwd=ROOT,
        text=True,
    )

    vals: dict[str, int] = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in ("A", "a") and parts[2].startswith("_cfg_enable_"):
            vals[parts[2]] = int(parts[0], 16)

    onoff = lambda v: "on" if vals.get(v, 0) else "off"
    return (
        f"MMU {onoff('_cfg_enable_mmu')} · "
        f"D {onoff('_cfg_enable_dcache')} · "
        f"I {onoff('_cfg_enable_icache')}"
    )


# =============================================================================
# OpenOCD
# =============================================================================

class OCD:

    def __init__(self, spawn_tries: int = 3):
        self.proc = None
        self.sock = None
        self.owns_daemon = False

        # Set by main() once symbols resolve, so halt/timeout paths can decode a
        # CPU fault (bench/fault.c): breakpoint addr of fault_trap + addr of g_fault.
        self.fault_bp: int | None = None
        self.fault_addr: int | None = None

        # Reuse an already-running daemon (e.g. started by openocd/ocd) rather than
        # spawning + SIGKILL-ing a fresh openocd -- the open/hard-kill churn is the
        # main cause of USB-Blaster wedging (stale libusb endpoints -> timeouts).
        if self._connect():
            return

        for attempt in range(spawn_tries):

            if attempt > 0:
                # Only reap a stale/wedged daemon as a fallback, not by default.
                subprocess.run(
                    ["pkill", "-9", "openocd"],
                    stderr=subprocess.DEVNULL,
                )
                time.sleep(0.5)

            self.proc = subprocess.Popen(
                [
                    "openocd",
                    "-f",
                    str(OPENOCD_CFG),
                    "-c",
                    "init",
                ],
                cwd=ROOT,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            self.owns_daemon = True

            for _ in range(30):

                if self.proc.poll() is not None:
                    break

                if self._connect():
                    return

                time.sleep(0.5)

            if self.proc.poll() is None:
                self.proc.kill()

        raise RuntimeError(
            "OpenOCD/JTAG did not come up "
            "(USB-Blaster wedged? power-cycle it)"
        )


    def _connect(self) -> bool:
        try:
            sock = socket.socket()
            sock.connect(("localhost", 6666))
            sock.settimeout(45)

            self.sock = sock
            self.cmd("cortex_a maskisr on")
            self.cmd("rbp all")           # clear any stale breakpoints on reuse

            return True

        except (ConnectionRefusedError, OSError):
            self.sock = None
            return False


    def __enter__(self) -> "OCD":
        return self


    def __exit__(
        self,
        exc_type,
        exc,
        tb,
    ) -> bool:
        self.close()
        return False


    def cmd(self, command: str) -> str:
        if self.sock is None:
            raise RuntimeError(
                "OpenOCD socket is closed"
            )

        self.sock.sendall(
            (command + "\x1a").encode()
        )

        response = b""

        while not response.endswith(b"\x1a"):

            chunk = self.sock.recv(4096)

            if not chunk:
                raise RuntimeError(
                    "OpenOCD connection closed"
                )

            response += chunk

        return response[:-1].decode()


    # -------------------------------------------------------------------------
    # Memory
    # -------------------------------------------------------------------------

    def read_words(
        self,
        addr: int,
        count: int,
    ) -> list[int]:

        output = self.cmd(
            f"mdw phys 0x{addr:08x} {count}"
        )

        values: list[int] = []

        for line in output.splitlines():

            if ":" not in line:
                continue

            payload = line.split(":", 1)[1]

            values.extend(
                int(word, 16)
                for word in re.findall(
                    r"\b[0-9a-fA-F]{8}\b",
                    payload,
                )
            )

        if len(values) < count:
            raise RuntimeError(
                f"short JTAG read at 0x{addr:08x}: "
                f"wanted {count}, got {len(values)}\n"
                f"OpenOCD: {output}"
            )

        return values[:count]


    def read_u32(self, addr: int) -> int:
        return self.read_words(addr, 1)[0]


    def read_bytes(
        self,
        addr: int,
        size: int,
    ) -> bytes:

        words = self.read_words(
            addr,
            (size + 3) // 4,
        )

        raw = b"".join(
            word.to_bytes(4, "little")
            for word in words
        )

        return raw[:size]


    def write_u32(
        self,
        addr: int,
        value: int,
    ) -> None:

        self.cmd(
            f"mww phys "
            f"0x{addr:08x} "
            f"0x{value:08x}"
        )


    # -------------------------------------------------------------------------
    # Run control
    # -------------------------------------------------------------------------

    def load_image(
        self,
        elf: Path,
        entry: int,
    ) -> None:

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
        output = self.cmd("reg pc")

        match = re.search(
            r"0x([0-9a-fA-F]+)",
            output,
        )

        if match is None:
            raise RuntimeError(
                f"cannot parse PC: {output}"
            )

        return int(match.group(1), 16)


    def wait_halt(
        self,
        timeout: float = 30.0,
    ) -> int:

        output = self.cmd(
            f"wait_halt {int(timeout * 1000)}"
        )

        lowered = output.lower()

        if (
            "timeout" in lowered
            or "timed out" in lowered
        ):
            # A hang is often a captured CPU fault (target spun in fault_halt).
            # Halt and check g_fault before reporting a bare timeout.
            self.cmd("halt")
            report = self.read_fault()
            if report:
                raise RuntimeError(report)

            raise RuntimeError(
                f"target did not halt within "
                f"{timeout:.1f}s:\n"
                f"{output}"
            )

        return self.pc()


    def read_fault(self) -> str | None:
        if self.fault_addr is None:
            return None
        try:
            words = self.read_words(self.fault_addr, 8)
        except Exception:
            return None
        return fault_report(words)


    # -------------------------------------------------------------------------
    # Hardware breakpoints
    # -------------------------------------------------------------------------

    def add_hw_breakpoint(
        self,
        addr: int,
    ) -> None:

        output = self.cmd(
            f"bp 0x{addr:08x} 4 hw"
        )

        lowered = output.lower()

        if (
            "error" in lowered
            or "resource not available" in lowered
        ):
            raise RuntimeError(
                f"cannot install hardware breakpoint "
                f"at 0x{addr:08x}:\n"
                f"{output}"
            )


    def expect_breakpoint(
        self,
        expected_addr: int,
        *,
        timeout: float = 30.0,
    ) -> None:

        pc = self.wait_halt(timeout)

        if self.fault_bp is not None and pc == self.fault_bp:
            report = self.read_fault()
            raise RuntimeError(
                report
                or f"halted at fault_trap 0x{pc:08x} (no fault record)"
            )

        if pc != expected_addr:
            raise RuntimeError(
                f"unexpected target halt: "
                f"PC=0x{pc:08x}, "
                f"expected=0x{expected_addr:08x}"
            )


    def continue_from_breakpoint(self) -> None:
        # OpenOCD steps over the managed hardware breakpoint,
        # returns halted one instruction later, then we continue.
        self.step()
        self.resume()


    def close(self) -> None:

        if self.sock is not None:

            # Only shut the daemon down if WE started it; a reused external daemon
            # (openocd/ocd) is left running for further debugging.
            if self.owns_daemon:
                try:
                    self.cmd("shutdown")
                except Exception:
                    pass

            try:
                self.sock.close()
            except Exception:
                pass

            self.sock = None


        if (
            self.owns_daemon
            and self.proc is not None
            and self.proc.poll() is None
        ):
            self.proc.kill()


# =============================================================================
# Telemetry
# =============================================================================

def read_metric(
    ocd: OCD,
    telemetry_base: int,
    index: int,
) -> Metric:

    addr = (
        telemetry_base
        + TELEMETRY_HEADER_SIZE
        + index * METRIC_SIZE
    )

    raw = ocd.read_bytes(
        addr,
        METRIC_SIZE,
    )

    (
        raw_name,
        count,
        total,
        minimum,
        maximum,
    ) = METRIC_STRUCT.unpack(raw)

    name = (
        raw_name
        .split(b"\0", 1)[0]
        .decode(errors="replace")
    )

    return Metric(
        name=name,
        count=count,
        total=total,
        minimum=minimum,
        maximum=maximum,
    )


# =============================================================================
# Allocator benchmark
# =============================================================================

def collect_alloc(
    ocd: OCD,
    telemetry_base: int,
) -> tuple[Metric, ...]:

    return tuple(
        read_metric(
            ocd,
            telemetry_base,
            i,
        )
        for i in range(ALLOC_METRICS)
    )


# =============================================================================
# EDF benchmark
# =============================================================================

def expected_jobs(
    ticks: int,
    period: int,
) -> int:

    # First release occurs on scheduler tick 1:
    #
    #     1
    #     1 + T
    #     1 + 2T
    #     ...

    if ticks == 0:
        return 0

    return ((ticks - 1) // period) + 1


def collect_edf_trial(
    ocd: OCD,
    symbols: dict[str, int],
    periods: tuple[int, ...],
) -> EDFResult:

    ticks = ocd.read_u32(
        symbols["gTicks"]
    )

    c = tuple(
        ocd.read_words(
            symbols["g_edf_C"],
            len(periods),
        )
    )

    done = tuple(
        ocd.read_words(
            symbols["g_edf_done"],
            len(periods),
        )
    )

    expected = tuple(
        expected_jobs(
            ticks,
            period,
        )
        for period in periods
    )

    sched = read_metric(
        ocd,
        symbols["g_telemetry"],
        0,
    )

    return EDFResult(
        index=ocd.read_u32(
            symbols["g_edf_u_index"]
        ),

        u_permille=ocd.read_u32(
            symbols["g_edf_u_permille"]
        ),

        ticks=ticks,

        misses=ocd.read_u32(
            symbols["gMissedDeadlines"]
        ),

        c=c,
        periods=periods,
        done=done,
        expected=expected,

        sched=sched,
    )


# =============================================================================
# Reporting
# =============================================================================

def print_alloc(
    metrics: Sequence[Metric],
) -> None:

    print("[allocbench]")

    for i, metric in enumerate(metrics):

        name = (
            metric.name
            or f"metric_{i}"
        )

        print(
            f"    {name:<14} "
            f"n={metric.count:<5} "
            f"mean={metric.mean:<6} "
            f"min={metric.minimum:<6} "
            f"max={metric.maximum}"
        )


def print_edf_header() -> None:

    print()

    print(
        f"{'reqU':>6} "
        f"{'actU':>6} "
        f"{'ticks':>8} "
        f"{'miss':>8} "
        f"{'cost':>8} "
        f"{'max':>8}"
    )


def print_edf_result(
    result: EDFResult,
) -> None:

    print(
        f"{result.requested_u:>6.3f} "
        f"{result.actual_u:>6.3f} "
        f"{result.ticks:>8} "
        f"{result.misses:>8} "
        f"{result.sched.mean:>8} "
        f"{result.sched.maximum:>8}"
    )


# =============================================================================
# CSV
# =============================================================================

def write_alloc_csv(
    path: Path,
    metrics: Sequence[Metric],
) -> None:

    with path.open(
        "w",
        newline="",
    ) as file:

        writer = csv.writer(file)

        writer.writerow(
            [
                "op",
                "count",
                "mean_cyc",
                "min_cyc",
                "max_cyc",
            ]
        )

        for i, metric in enumerate(metrics):

            writer.writerow(
                [
                    metric.name or f"metric_{i}",
                    metric.count,
                    metric.mean,
                    metric.minimum,
                    metric.maximum,
                ]
            )


def write_cache_csv(
    path: Path,
    metrics: Sequence[Metric],
) -> None:
    """CSV companion to cache_workloads.png -- the cache-sensitive metrics for
    ONE cache configuration. cache_config is stamped on every row so the file is
    self-documenting; compare configs by diffing two runs' files. For
    mem_walk_8k, min_cyc is the warm (steady, cache-hit) pass and max_cyc is the
    cold (pass-0 fill) pass -- a large gap means the D-cache is live."""

    by_name = {m.name: m for m in metrics}
    config = elf_cache_config(TEST_ELF)

    with path.open(
        "w",
        newline="",
    ) as file:

        writer = csv.writer(file)

        writer.writerow(
            [
                "workload",
                "cache_config",
                "count",
                "mean_cyc",
                "min_cyc",
                "max_cyc",
            ]
        )

        for name in ("mem_walk_8k", "matmul_32"):

            metric = by_name.get(name)

            if metric is None:
                continue

            writer.writerow(
                [
                    metric.name,
                    config,
                    metric.count,
                    metric.mean,
                    metric.minimum,
                    metric.maximum,
                ]
            )


def write_edf_csv(
    path: Path,
    rows: Sequence[EDFResult],
) -> None:

    with path.open(
        "w",
        newline="",
    ) as file:

        writer = csv.writer(file)

        writer.writerow(
            [
                "requested_u",
                "actual_u",

                "ticks",
                "misses",

                "sched_count",
                "sched_mean_cyc",
                "sched_min_cyc",
                "sched_max_cyc",

                "C0",
                "C1",
                "C2",

                "T0",
                "T1",
                "T2",

                "done0",
                "done1",
                "done2",

                "expected0",
                "expected1",
                "expected2",
            ]
        )

        for result in rows:

            writer.writerow(
                [
                    result.requested_u,
                    round(
                        result.actual_u,
                        6,
                    ),

                    result.ticks,
                    result.misses,

                    result.sched.count,
                    result.sched.mean,
                    result.sched.minimum,
                    result.sched.maximum,

                    *result.c,
                    *result.periods,
                    *result.done,
                    *result.expected,
                ]
            )


# =============================================================================
# Plots
# =============================================================================

def plot_alloc(
    metrics: Sequence[Metric],
    path: Path,
) -> None:

    names = [
        metric.name or f"metric_{i}"
        for i, metric in enumerate(metrics)
    ]

    means = [
        metric.mean
        for metric in metrics
    ]

    low_error = [
        metric.mean - metric.minimum
        for metric in metrics
    ]

    high_error = [
        metric.maximum - metric.mean
        for metric in metrics
    ]

    fig, ax = plt.subplots(
        figsize=(7, 4)
    )

    ax.bar(
        range(len(metrics)),
        means,
        yerr=[
            low_error,
            high_error,
        ],
        capsize=4,
    )

    ax.set_xticks(
        range(len(metrics))
    )

    ax.set_xticklabels(
        names,
        rotation=12,
    )

    ax.set_ylabel("cycles")

    ax.set_title(
        f"Allocator operation cost  ({elf_cache_config(TEST_ELF)})"
    )

    ax.grid(
        axis="y",
        alpha=0.3,
    )

    fig.tight_layout()

    fig.savefig(
        path,
        dpi=130,
    )

    plt.close(fig)


def plot_cache(
    metrics: Sequence[Metric],
    path: Path,
) -> None:
    """Cache-sensitive workloads, kept off the allocator chart because their
    cycle scale is 10-200x larger. Each figure is a permanent record of ONE
    cache configuration (stamped in the title via elf_cache_config); compare
    configs by running the battery twice and setting the two figures side by
    side. mem_walk_8k is shown as cold (pass 0 fill) vs warm (steady hits): a
    tall gap means the D-cache is live, a flat pair means it isn't."""

    by_name = {m.name: m for m in metrics}
    memwalk = by_name.get("mem_walk_8k")
    matmul = by_name.get("matmul_32")

    fig, (ax_mw, ax_mm) = plt.subplots(
        1, 2, figsize=(9, 4)
    )

    if memwalk is not None:
        ax_mw.bar(
            ["cold\n(pass 0)", "warm\n(steady)"],
            [memwalk.maximum, memwalk.minimum],
            color=["#c0504d", "#4f81bd"],
        )
        ax_mw.set_title(
            f"{memwalk.name}: SDRAM RMW / 8 KB pass"
        )

    ax_mw.set_ylabel("cycles")
    ax_mw.grid(axis="y", alpha=0.3)

    if matmul is not None:
        ax_mm.bar(
            [matmul.name],
            [matmul.mean],
            yerr=[
                [matmul.mean - matmul.minimum],
                [matmul.maximum - matmul.mean],
            ],
            capsize=6,
            color="#9bbb59",
        )
        ax_mm.set_title(
            f"{matmul.name}: compute time-to-complete"
        )

    ax_mm.set_ylabel("cycles")
    ax_mm.grid(axis="y", alpha=0.3)

    fig.suptitle(
        f"Cache-sensitive workloads  ({elf_cache_config(TEST_ELF)})"
    )

    fig.tight_layout()

    fig.savefig(
        path,
        dpi=130,
    )

    plt.close(fig)


def plot_edf(
    rows: Sequence[EDFResult],
    path: Path,
) -> None:

    utilization = [
        result.actual_u
        for result in rows
    ]

    misses = [
        result.misses
        for result in rows
    ]

    costs = [
        result.sched.mean
        for result in rows
    ]

    fig, (a1, a2) = plt.subplots(
        2,
        1,
        figsize=(8, 7),
        sharex=True,
    )

    a1.plot(
        utilization,
        misses,
        "o-",
    )

    a1.axvline(
        1.0,
        linestyle="--",
    )

    a1.set_ylabel(
        "deadline misses"
    )

    a1.set_title(
        "EDF schedulability and scheduler cost"
    )

    a1.grid(
        alpha=0.3
    )


    a2.plot(
        utilization,
        costs,
        "o-",
    )

    a2.axvline(
        1.0,
        linestyle="--",
    )

    a2.set_ylabel(
        "mean scheduler cycles/tick"
    )

    a2.set_xlabel(
        "actual utilization"
    )

    a2.grid(
        alpha=0.3
    )


    fig.tight_layout()

    fig.savefig(
        path,
        dpi=130,
    )

    plt.close(fig)


def plot_gantt(
    trace: Sequence[int],
    periods: Sequence[int],
    path: Path,
    u_permille: int,
    u_actual: float = 0.0,
    label: str = "",
    window: int = GANTT_WINDOW,
) -> None:

    n = min(len(trace), window)
    nt = len(periods)
    colors = ["#1f77b4", "#ff7f0e", "#2ca02c"]

    width = min(26.0, max(12.0, n / 130.0))          # scale width with the window shown
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

        ax.broken_barh(
            segs,
            (y + 0.15, 0.7),
            facecolors=colors[i % len(colors)],
        )

        for k in range(0, n, periods[i]):                # release arrows (textbook up-arrow)
            ax.annotate(
                "",
                xy=(k, y + 1.0),
                xytext=(k, y + 0.12),
                arrowprops=dict(arrowstyle="->", color="black", lw=0.7),
            )

    ax.set_yticks([nt - 1 - i + 0.5 for i in range(nt)])
    ax.set_yticklabels([f"$\\tau_{i}$ (T={periods[i]})" for i in range(nt)])
    ax.set_ylim(0, nt)
    ax.set_xlim(0, n)
    ax.set_xlabel("time (ticks)")
    ax.set_title(
        f"EDF schedule  (Ureq={u_permille / 1000:.3f}, "
        f"Uact={u_actual:.3f}"
        f"{', ' + label if label else ''}; "
        f"first {n} ticks; up-arrow = release)"
    )
    ax.grid(axis="x", alpha=0.3)

    fig.tight_layout()
    fig.savefig(path, dpi=130)
    plt.close(fig)


# =============================================================================
# Main
# =============================================================================

def main() -> None:

    if not TEST_ELF.exists():
        raise RuntimeError(
            f"{TEST_ELF} does not exist"
        )


    symbols = elf_symbols(
        TEST_ELF
    )


    require_symbols(
        symbols,
        (
            "_reset_handler",

            "g_telemetry",

            "gTicks",
            "gMissedDeadlines",

            "g_edf_u_values",
            "g_edf_u_count",
            "g_edf_u_index",
            "g_edf_u_permille",

            "g_edf_periods",
            "g_edf_C",
            "g_edf_done",

            "g_sched_trace",
            "g_trace_len",

            "g_test_release",

            "ktrace_bp_alloc_done",
            "ktrace_bp_edf_ready",
            "ktrace_bp_edf_done",

            "fault_trap",
            "g_fault",
        ),
    )


    bp_alloc = (
        symbols["ktrace_bp_alloc_done"]
    )

    bp_edf_ready = (
        symbols["ktrace_bp_edf_ready"]
    )

    bp_edf_done = (
        symbols["ktrace_bp_edf_done"]
    )

    bp_fault = (
        symbols["fault_trap"]
    )


    outdir = (
        RESULTS_DIR
        / datetime.now().strftime(
            "%Y%m%d-%H%M%S"
        )
    )


    outdir.mkdir(
        parents=True,
        exist_ok=True,
    )


    print(
        f"artifacts -> {outdir}\n"
    )


    edf_results: list[EDFResult] = []


    with OCD() as ocd:

        # ---------------------------------------------------------------------
        # Load exactly once
        # ---------------------------------------------------------------------

        ocd.load_image(
            TEST_ELF,
            symbols["_reset_handler"],
        )


        # ---------------------------------------------------------------------
        # Read immutable EDF configuration while target is halted
        # ---------------------------------------------------------------------

        u_count = ocd.read_u32(
            symbols["g_edf_u_count"]
        )


        if not 1 <= u_count <= 64:
            raise RuntimeError(
                f"invalid EDF U count: "
                f"{u_count}"
            )


        periods = tuple(
            ocd.read_words(
                symbols["g_edf_periods"],
                EDF_TASKS,
            )
        )


        u_values = tuple(
            ocd.read_words(
                symbols["g_edf_u_values"],
                u_count,
            )
        )


        print(
            f"EDF U sweep: "
            f"{list(u_values)}"
        )


        print(
            f"EDF periods: "
            f"{list(periods)}\n"
        )


        # ---------------------------------------------------------------------
        # Install semantic hardware breakpoints
        # ---------------------------------------------------------------------

        # Let the halt/timeout paths decode a CPU fault instead of reporting a
        # bare timeout (bench/fault.c: fault_trap marker + g_fault record).
        ocd.fault_bp = bp_fault
        ocd.fault_addr = symbols["g_fault"]

        ocd.add_hw_breakpoint(
            bp_alloc
        )

        ocd.add_hw_breakpoint(
            bp_edf_ready
        )

        ocd.add_hw_breakpoint(
            bp_edf_done
        )

        ocd.add_hw_breakpoint(
            bp_fault
        )


        # ---------------------------------------------------------------------
        # Boot
        # ---------------------------------------------------------------------

        ocd.resume()


        # =====================================================================
        # Allocator benchmark
        # =====================================================================

        ocd.expect_breakpoint(
            bp_alloc,
            timeout=30.0,
        )


        alloc_metrics = collect_alloc(
            ocd,
            symbols["g_telemetry"],
        )


        print_alloc(
            alloc_metrics
        )


        # Step over the allocator breakpoint and continue into EDF.
        ocd.continue_from_breakpoint()


        # =====================================================================
        # EDF sweep
        # =====================================================================

        print_edf_header()


        traces: dict[int, Sequence[int]] = {}   # u_permille -> schedule trace


        for (
            trial,
            expected_u,
        ) in enumerate(u_values):

            # -----------------------------------------------------------------
            # Target has configured this U and reached ktrace_bp_edf_ready.
            # -----------------------------------------------------------------

            ocd.expect_breakpoint(
                bp_edf_ready,
                timeout=30.0,
            )


            actual_index = ocd.read_u32(
                symbols["g_edf_u_index"]
            )


            actual_u = ocd.read_u32(
                symbols["g_edf_u_permille"]
            )


            if actual_index != trial:
                raise RuntimeError(
                    f"EDF trial mismatch: "
                    f"expected {trial}, "
                    f"got {actual_index}"
                )


            if actual_u != expected_u:
                raise RuntimeError(
                    f"EDF U mismatch: "
                    f"expected {expected_u}, "
                    f"got {actual_u}"
                )


            # Step over ready breakpoint.
            # Firmware then enters KTRACE_WAIT_RELEASE().
            ocd.continue_from_breakpoint()


            # -----------------------------------------------------------------
            # Measurement window
            # -----------------------------------------------------------------

            time.sleep(
                EDF_SECONDS
            )


            # -----------------------------------------------------------------
            # Freeze exact target state
            # -----------------------------------------------------------------

            ocd.halt()


            result = collect_edf_trial(
                ocd,
                symbols,
                periods,
            )


            if result.index != trial:
                raise RuntimeError(
                    f"EDF trial changed during measurement: "
                    f"expected {trial}, "
                    f"got {result.index}"
                )


            if result.u_permille != expected_u:
                raise RuntimeError(
                    f"EDF U changed during measurement: "
                    f"expected {expected_u}, "
                    f"got {result.u_permille}"
                )


            edf_results.append(
                result
            )


            print_edf_result(
                result
            )


            # -----------------------------------------------------------------
            # Capture this trial's schedule trace. Every trial is traced; we
            # keep them all and choose which to plot once the sweep's miss
            # pattern is known. Target is halted, so the JTAG read is coherent.
            # -----------------------------------------------------------------

            trace_len = min(
                ocd.read_u32(symbols["g_trace_len"]),
                TRACE_TICKS,
            )

            if trace_len:
                traces[result.u_permille] = ocd.read_bytes(
                    symbols["g_sched_trace"],
                    trace_len,
                )


            # -----------------------------------------------------------------
            # Release current trial.
            #
            # Target is halted, so JTAG RAM write is valid.
            # -----------------------------------------------------------------

            ocd.write_u32(
                symbols["g_test_release"],
                1,
            )

            ocd.resume()

            time.sleep(0.05)

            
            ocd.halt()
            probe_pc = ocd.pc()

            if probe_pc != bp_edf_ready:
                ocd.resume()


            ocd.resume()


        # =====================================================================
        # Final target-generated breakpoint
        # =====================================================================

        ocd.expect_breakpoint(
            bp_edf_done,
            timeout=30.0,
        )


    # =========================================================================
    # Artifacts
    # =========================================================================

    write_alloc_csv(
        outdir / "alloc.csv",
        alloc_metrics[:4],                 # malloc / free / *_loaded
    )


    write_cache_csv(
        outdir / "cache_workloads.csv",
        alloc_metrics,                     # picks out mem_walk_8k + matmul_32 by name
    )


    write_edf_csv(
        outdir / "edf_sweep.csv",
        edf_results,
    )


    plot_alloc(
        alloc_metrics[:4],                 # malloc / free / *_loaded -- same ~2k scale
        outdir / "alloc_timing.png",
    )


    plot_cache(
        alloc_metrics,                     # picks out mem_walk_8k + matmul_32 by name
        outdir / "cache_workloads.png",
    )


    plot_edf(
        edf_results,
        outdir / "edf_sweep.png",
    )


    # Gantt charts at the schedulability transition, not a fixed U: the highest
    # utilization that still met every deadline, the lowest that missed one, and
    # the top of the sweep. These are only known once all misses are collected.
    result_by_u = {r.u_permille: r for r in edf_results}
    swept_us = sorted(result_by_u)

    no_miss_us = [u for u in swept_us if result_by_u[u].misses == 0]
    miss_us    = [u for u in swept_us if result_by_u[u].misses > 0]

    gantt_labels: dict[int, list[str]] = {}   # u_permille -> chart labels (deduped)

    if COMFORTABLE_U in result_by_u:
        gantt_labels.setdefault(COMFORTABLE_U, []).append("comfortable")

    if no_miss_us:
        gantt_labels.setdefault(max(no_miss_us), []).append("last no-miss")

    if miss_us:
        gantt_labels.setdefault(min(miss_us), []).append("first miss")

    if swept_us:
        gantt_labels.setdefault(max(swept_us), []).append("max U")

    for u_permille in sorted(gantt_labels):
        trace = traces.get(u_permille)

        if trace is None:
            continue

        plot_gantt(
            trace,
            periods,
            outdir / f"edf_schedule_u{u_permille / 1000:.3f}.png",
            u_permille,
            u_actual=result_by_u[u_permille].actual_u,
            label=" / ".join(gantt_labels[u_permille]),
        )


    # =========================================================================
    # U*
    # =========================================================================

    clean = [
        result
        for result in edf_results
        if result.misses == 0
    ]


    if clean:

        knee = max(
            clean,
            key=lambda result:
                result.actual_u,
        )


        print(
            f"\nU* = "
            f"{knee.actual_u:.3f} "
            f"(requested "
            f"{knee.requested_u:.3f})"
        )


    print(
        f"\nPASS   artifacts in "
        f"{outdir}"
    )


if __name__ == "__main__":
    main()
