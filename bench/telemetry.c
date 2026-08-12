#include <stddef.h>
#include <stdint.h>

#include "telemetry.h"


/*
 * Dedicated NOLOAD OCRAM region.
 *
 * load_image and BSS initialization do not initialize this memory.
 * telemetry_init() owns initialization.
 */
telemetry_t g_telemetry
    __attribute__((section(".telemetry"), used));


static void zero(void* dst, uint32_t n)
{
    volatile uint8_t* p = (volatile uint8_t*)dst;

    while (n--)
        *p++ = 0;
}


void metric_reset(metric_t* m)
{
    /*
     * Do not clear m->name.
     *
     * Names identify metric slots across repeated EDF trials;
     * only accumulated samples are reset.
     */
    m->count = 0;
    m->sum   = 0;
    m->min   = 0xFFFFFFFFu;
    m->max   = 0;
}


void telemetry_init(void)
{
    zero(&g_telemetry, sizeof(g_telemetry));

    for (int i = 0; i < TELEM_METRICS; i++)
        metric_reset(&g_telemetry.metric[i]);

    g_telemetry.running = 1;
}


void telemetry_name(int id, const char* name)
{
    if (id < 0 || id >= TELEM_METRICS)
        return;

    char* dst = g_telemetry.metric[id].name;

    size_t i;

    for (i = 0; i < 15 && name[i]; i++)
        dst[i] = name[i];

    dst[i] = '\0';
}


void telemetry_done(void)
{
    g_telemetry.running = 0;
}
