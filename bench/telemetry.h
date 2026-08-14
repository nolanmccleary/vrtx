#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "pmu.h"


#ifndef TELEM_METRICS
#define TELEM_METRICS 8
#endif


typedef struct
{
    char name[16];
    uint64_t count;
    uint64_t sum;
    uint32_t min;
    uint32_t max;
}   metric_t;


typedef struct
{
    uint32_t running;
    uint32_t read_overhead;
    metric_t metric[TELEM_METRICS];
}   telemetry_t;


_Static_assert(sizeof(metric_t) == 40u, "metric_t layout changed");
_Static_assert(offsetof(telemetry_t, metric) == 8u, "telemetry_t layout changed");


extern telemetry_t g_telemetry;


void telemetry_init(void);
void telemetry_name(int id, const char* name);
void telemetry_done(void);

void metric_reset(metric_t* m);


static inline void metric_update(metric_t* m, uint32_t cycles)
{
    m->count++;
    m->sum += cycles;

    if (cycles < m->min)
        m->min = cycles;

    if (cycles > m->max)
        m->max = cycles;
}


static inline uint32_t telem_correct(uint32_t delta)
{
    uint32_t overhead = g_telemetry.read_overhead;

    return (delta > overhead)
        ? (delta - overhead)
        : 0;
}


#define MEASURE_BEGIN(id) \
    uint32_t _mt_##id = pmu_cycles()


#define MEASURE_END(id)                                            \
    metric_update(                                                 \
        &g_telemetry.metric[id],                                   \
        telem_correct(pmu_cycles() - _mt_##id)                     \
    )


#endif
