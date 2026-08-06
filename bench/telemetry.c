#include "telemetry.h"

/*
 * Lives in a dedicated NOLOAD OCRAM section (see linker/de1-soc.ld). NOLOAD means
 * neither load_image nor the .bss zeroing loop touches it, so telemetry_init() is
 * responsible for zeroing it at runtime.
 */
telemetry_t g_telemetry __attribute__((section(".telemetry"), used));


static void zero(void *dst, uint32_t n)
{
    volatile uint8_t *p = (volatile uint8_t *)dst;
    while (n--) *p++ = 0;
}


void hist_reset(hist_t *h)
{
    zero(h, sizeof(*h));
    h->min = 0xFFFFFFFFu;
    h->max = 0;
}


void telemetry_init(uint32_t bench_id)
{
    zero(&g_telemetry, sizeof(g_telemetry));

    g_telemetry.magic               = TELEM_MAGIC;
    g_telemetry.version             = TELEM_VERSION;
    g_telemetry.cpu_hz              = 0;            /* pinned later; cycles are primary */
    g_telemetry.gtimer_hz           = 0;
    g_telemetry.state               = TELEM_INIT;
    g_telemetry.bench_id            = bench_id;
    g_telemetry.n_metrics           = TELEM_METRICS;
    g_telemetry.hist_subbucket_bits = HIST_SUBBUCKET_BITS;
    g_telemetry.hist_max_pow2       = HIST_MAX_POW2;
    g_telemetry.hist_nbuckets       = HIST_NBUCKETS;

    for (int i = 0; i < TELEM_METRICS; i++) hist_reset(&g_telemetry.metric[i].h);

    g_telemetry.state = TELEM_RUNNING;
}


void telemetry_metric_name(int id, const char *name)
{
    if (id < 0 || id >= TELEM_METRICS) return;
    char *dst = g_telemetry.metric[id].name;
    int i = 0;
    for (; i < 15 && name[i]; i++) dst[i] = name[i];
    dst[i] = '\0';
}


void telemetry_done(void)
{
    g_telemetry.state = TELEM_DONE;
}
