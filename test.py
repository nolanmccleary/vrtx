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
ALLOC_METRICS = 4

GANTT_U = 700              # the one U trial whose schedule is traced (matches workload_edf.c)
TRACE_TICKS = 1200         # g_sched_trace capacity (must match workload_edf.c)
GANTT_WINDOW = 240         # ticks shown in the Gantt (readability)


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
# Result types
# =============================================================================

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

    sched: Metric

    @property
    def requested_u(self) -> float:
        return self.u_permille / 1000.0

    @property
    def actual_u(self) -> float:
        return sum(
            c / period
            for c, period in zip(self.c, self.periods)
        )

    @property
    def completion(self) -> float:
        total_expected = sum(self.expected)

        if total_expected == 0:
            return 0.0

        return 100.0 * sum(self.done) / total_expected


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


# =============================================================================
# OpenOCD
# =============================================================================

class OCD:

    def __init__(self, spawn_tries: int = 4):
        self.proc = None
        self.sock = None

        for _ in range(spawn_tries):

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

            for _ in range(30):

                if self.proc.poll() is not None:
                    break

                try:
                    sock = socket.socket()
                    sock.connect(("localhost", 6666))
                    sock.settimeout(45)

                    self.sock = sock
                    self.cmd("cortex_a maskisr on")

                    return

                except ConnectionRefusedError:
                    time.sleep(0.5)

            if self.proc.poll() is None:
                self.proc.kill()

        raise RuntimeError(
            "OpenOCD/JTAG did not come up"
        )


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
            raise RuntimeError(
                f"target did not halt within "
                f"{timeout:.1f}s:\n"
                f"{output}"
            )

        return self.pc()


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
            self.proc is not None
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
        f"{'compl%':>8} "
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
        f"{result.completion:>8.1f} "
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
                "completion_pct",

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
                    round(
                        result.completion,
                        3,
                    ),

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
        "Allocator operation cost"
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
    window: int = GANTT_WINDOW,
) -> None:

    n = min(len(trace), window)
    nt = len(periods)
    colors = ["#1f77b4", "#ff7f0e", "#2ca02c"]

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
        f"EDF schedule  (U={u_permille / 1000:.2f}, "
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

        ocd.add_hw_breakpoint(
            bp_alloc
        )

        ocd.add_hw_breakpoint(
            bp_edf_ready
        )

        ocd.add_hw_breakpoint(
            bp_edf_done
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


        gantt_trace: list[int] | None = None


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
            # Capture the schedule trace for the one representative trial.
            # Target is halted, so the JTAG read is coherent.
            # -----------------------------------------------------------------

            if result.u_permille == GANTT_U:
                trace_len = min(
                    ocd.read_u32(symbols["g_trace_len"]),
                    TRACE_TICKS,
                )

                if trace_len:
                    gantt_trace = ocd.read_bytes(
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
            print(
                f"\nMEASURE HALT: "
                f"pc=0x{ocd.pc():08x}, "
                f"ticks={ocd.read_u32(symbols['gTicks'])}"
            )
            probe_pc = ocd.pc()


            print(
                "\nPOST RELEASE:",
                f"pc=0x{probe_pc:08x}",
                f"release={ocd.read_u32(symbols['g_test_release'])}",
                f"u_index={ocd.read_u32(symbols['g_edf_u_index'])}",
                f"ticks={ocd.read_u32(symbols['gTicks'])}",
            )

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
        alloc_metrics,
    )


    write_edf_csv(
        outdir / "edf_sweep.csv",
        edf_results,
    )


    plot_alloc(
        alloc_metrics,
        outdir / "alloc_timing.png",
    )


    plot_edf(
        edf_results,
        outdir / "edf_sweep.png",
    )


    if gantt_trace is not None:
        plot_gantt(
            gantt_trace,
            periods,
            outdir / "edf_schedule.png",
            GANTT_U,
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
