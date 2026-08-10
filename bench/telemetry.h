#ifndef __TELEMETRY_H__
#define __TELEMETRY_H__

#include <stdint.h>

/*
 * Versioned telemetry channel. A single g_telemetry instance lives in a dedicated
 * .telemetry OCRAM section; the host resolves its address by symbol (nm) and decodes
 * it over JTAG. All timing is in CPU cycles (see pmu.h).
 *
 * Histogram: HdrHistogram-style sub-octave buckets. Each power-of-two octave is split
 * into S = 2^HIST_SUBBUCKET_BITS linear bins, giving a bounded relative error of
 * 2^-HIST_SUBBUCKET_BITS everywhere above the exact low region (v < S is stored
 * one-value-per-bucket). Exact min/max/sum/count anchor the tails and the mean;
 * buckets give the distribution/percentiles.
 */

#ifndef HIST_SUBBUCKET_BITS
#define HIST_SUBBUCKET_BITS 4          /* S=16 sub-buckets/octave -> <=6.25% rel. error */
#endif
#ifndef HIST_MAX_POW2
#define HIST_MAX_POW2 20               /* track up to 2^20-1 cycles; beyond -> overflow */
#endif
#ifndef TELEM_METRICS
#define TELEM_METRICS 8
#endif

#define HIST_S        (1u << HIST_SUBBUCKET_BITS)
#define HIST_NBUCKETS (HIST_S * (HIST_MAX_POW2 - HIST_SUBBUCKET_BITS + 1))

#define TELEM_MAGIC   0x51544C30u      /* "QTL0" */
#define TELEM_VERSION 1



enum 
{
    TELEM_INIT = 0, 
    TELEM_RUNNING = 1, 
    TELEM_DONE = 2, 
    TELEM_ERROR = 3 
};



typedef struct 
{
    uint64_t count;
    uint64_t sum;                      /* exact mean = sum / count */
    uint32_t min;                      /* exact low tail  */
    uint32_t max;                      /* exact high tail */
    uint32_t overflow;                 /* samples >= 2^HIST_MAX_POW2 */
    uint32_t buckets[HIST_NBUCKETS];
} hist_t;



typedef struct 
{
    char   name[16];
    hist_t h;
} metric_t;



typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t cpu_hz;                   /* 0 until pinned; cycles remain primary */
    uint32_t gtimer_hz;                /* 0 until pinned */
    uint32_t cal_cycles;               /* cycle/gtimer ratio calibration: matched deltas */
    uint32_t cal_gtimer;               /* ratio = cal_cycles / cal_gtimer (cycles/tick) */
    uint32_t read_overhead_cyc;        /* subtracted from every sample */
    uint32_t state;
    uint32_t bench_id;
    uint32_t n_metrics;
    uint32_t hist_subbucket_bits;      /* self-describing: host reconstructs from these */
    uint32_t hist_max_pow2;
    uint32_t hist_nbuckets;
    metric_t metric[TELEM_METRICS];
} telemetry_t;


extern telemetry_t g_telemetry;


void telemetry_init(uint32_t bench_id);           /* zero + stamp header, state=RUNNING */
void telemetry_metric_name(int id, const char* name);
void telemetry_done(void);                         /* state=DONE */
void hist_reset(hist_t* h);



/* Map a value to its sub-octave bucket index. O(1) via count-leading-zeros. */
static inline uint32_t hist_index(uint32_t v)
{
    if (v < HIST_S) return v;                      /* exact region: index == value */

    uint32_t e   = 31u - (uint32_t)__builtin_clz(v);       /* MSB position, e >= k */
    uint32_t sub = (v >> (e - HIST_SUBBUCKET_BITS)) - HIST_S; /* [0,S): drop leading 1 */
    
    return HIST_S + (e - HIST_SUBBUCKET_BITS) * HIST_S + sub;
}



/* Record one sample (already overhead-corrected). Inlined into the hot path. */
static inline void hist_record(hist_t* h, uint32_t v)
{
    h->count++;
    h->sum += v;

    if (v < h->min)
        h->min = v;

    if (v > h->max) 
        h->max = v;

    if (v >= (1u << HIST_MAX_POW2))
    { 
        h->overflow++; 
        return;
    }

    h->buckets[hist_index(v)]++;
}



/* Subtract the calibrated read overhead, clamped at zero. */
static inline uint32_t telem_correct(uint32_t delta)
{
    uint32_t o = g_telemetry.read_overhead_cyc;
    return (delta > o) ? (delta - o) : 0;
}



/* Convenience region macros. id is a compile-time metric slot index. */
#define MEASURE_BEGIN(id) uint32_t _mt_##id = pmu_cycles()
#define MEASURE_END(id) \
    hist_record(&g_telemetry.metric[id].h, telem_correct(pmu_cycles() - _mt_##id))

#endif
