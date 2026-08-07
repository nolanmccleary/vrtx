#include <stdint.h>
#include "pmu.h"
#include "telemetry.h"
#include "flags.h"

/*
 * Phase 0 self-test: measure a busy loop of known length to validate the entire
 * instrumentation pipeline (PMU enable -> measure -> record -> host decode). Runs
 * with interrupts off and no scheduler, so the measurement is isolated from
 * ticks/preemption. Warm-cache: WARMUP unrecorded iterations precede ITERS recorded.
 */

#define BENCH_ID_SELFTEST 0
#define METRIC_BUSY       0

#define WARMUP 64
#define ITERS  256
#define LOOP_N 1000u

/* noinline + volatile accumulator: a stable, non-elidable region whose cost is
   dominated by LOOP_N iterations. Its absolute cycle count is what we sanity-check. */
static uint32_t __attribute__((noinline)) busy(uint32_t n)
{
    volatile uint32_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc += i;
    return acc;
}


void selftest_run(void)
{
    uint32_t read_ovf, probe_ovf;

    pmu_init();
    telemetry_init(BENCH_ID_SELFTEST);
    telemetry_metric_name(METRIC_BUSY, "selftest_busy");

    pmu_calibrate(&read_ovf, &probe_ovf);
    g_telemetry.read_overhead_cyc  = read_ovf;
    g_telemetry.probe_overhead_cyc = probe_ovf;

    /* Record the PMCCNTR:gtimer ratio over a fixed span so ns conversion is ready
       once the absolute clock rate is pinned. */
    uint32_t c0 = pmu_cycles();
    uint64_t g0 = pmu_gtimer();
    for (int i = 0; i < WARMUP; i++) (void)busy(LOOP_N);   /* warm the caches too */
    uint32_t c1 = pmu_cycles();
    uint64_t g1 = pmu_gtimer();
    g_telemetry.cal_cycles = c1 - c0;                      /* host ratio = cal_cycles/cal_gtimer */
    g_telemetry.cal_gtimer = (uint32_t)(g1 - g0);

    __asm__ __volatile__("cpsid if" ::: "memory");         /* isolate the measurement */

    for (int i = 0; i < ITERS; i++) {
        MEASURE_BEGIN(METRIC_BUSY);
        (void)busy(LOOP_N);
        MEASURE_END(METRIC_BUSY);
    }

    telemetry_done();
    FLAG_WRITE(GENERAL_FLAG, 0x0B0);                        /* sentinel: bench complete */

    for (;;) { }
}
