#include <stdint.h>

#include "ktrace.h"
#include "pmu.h"
#include "telemetry.h"
#include "tlsf.h"


#define SZ      64
#define K       64
#define ITERS   512


typedef enum
{
    MALLOC = 0,
    FREE,
    MALLOC_LOADED,
    FREE_LOADED
} malloc_op_e;


static void* load[K];


void allocbench_run(void)
{
    void* p;


    pmu_init();

    telemetry_init();

    telemetry_name(
        MALLOC,
        "malloc"
    );

    telemetry_name(
        FREE,
        "free"
    );

    telemetry_name(
        MALLOC_LOADED,
        "malloc_loaded"
    );

    telemetry_name(
        FREE_LOADED,
        "free_loaded"
    );


    g_telemetry.read_overhead =
        pmu_calibrate();


    /*
     * Remove interrupt noise from allocator measurements.
     */
    __asm__ __volatile__(
        "cpsid if"
        :
        :
        : "memory"
    );


    /* ---------------------------------------------------------------------
     * Empty heap
     * ------------------------------------------------------------------------- */

    heap_init();


    for (int i = 0; i < ITERS; i++)
    {
        MEASURE_BEGIN(MALLOC);

        p = kMalloc(SZ);

        MEASURE_END(MALLOC);


        MEASURE_BEGIN(FREE);

        kFree(p);

        MEASURE_END(FREE);
    }


    heap_destroy();


    /* ---------------------------------------------------------------------
     * Loaded heap
     * ------------------------------------------------------------------------- */

    heap_init();


    for (int i = 0; i < K; i++)
    {
        load[i] = kMalloc(SZ);
    }


    for (int i = 0; i < ITERS; i++)
    {
        MEASURE_BEGIN(MALLOC_LOADED);

        p = kMalloc(SZ);

        MEASURE_END(MALLOC_LOADED);


        MEASURE_BEGIN(FREE_LOADED);

        kFree(p);

        MEASURE_END(FREE_LOADED);
    }


    /*
     * Restore allocator state before EDF starts.
     */
    for (int i = 0; i < K; i++)
    {
        kFree(load[i]);
    }


    heap_destroy();


    telemetry_done();


    /*
     * OpenOCD hardware breakpoint is installed at this function address.
     */
    KTRACE_ALLOC_DONE();


    /*
     * EDF requires timer interrupts.
     */
    __asm__ __volatile__(
        "cpsie if"
        :
        :
        : "memory"
    );
}
