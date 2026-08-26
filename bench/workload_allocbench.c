#include <stdint.h>

#include "telemetry.h"
#include "tlsf.h"


#define ALLOC_SIZE_BYTES    64
#define LOADED_ALLOC_COUNT  64
#define ALLOC_ITERS         512


/* g_metrics slots -> "malloc" / "free" / "malloc_loaded" / "free_loaded" (test.py) */
typedef enum
{
    MALLOC = 0,
    FREE,
    MALLOC_LOADED,
    FREE_LOADED
} malloc_op_e;


static void* loaded_blocks[LOADED_ALLOC_COUNT];


void allocbench_run(void)
{
    void* block;

    metric_reset(MALLOC);
    metric_reset(FREE);
    metric_reset(MALLOC_LOADED);
    metric_reset(FREE_LOADED);


    /* ---------------------------------------------------------------------
     * Empty heap
     * ------------------------------------------------------------------------- */

    heap_init();


    for (int i = 0; i < ALLOC_ITERS; i++)
    {
        MEASURE_BEGIN(MALLOC);

        block = kMalloc(ALLOC_SIZE_BYTES);

        MEASURE_END(MALLOC);


        MEASURE_BEGIN(FREE);

        kFree(block);

        MEASURE_END(FREE);
    }


    heap_destroy();


    /* ---------------------------------------------------------------------
     * Loaded heap
     * ------------------------------------------------------------------------- */

    heap_init();


    for (int i = 0; i < LOADED_ALLOC_COUNT; i++)
    {
        loaded_blocks[i] = kMalloc(ALLOC_SIZE_BYTES);
    }


    for (int i = 0; i < ALLOC_ITERS; i++)
    {
        MEASURE_BEGIN(MALLOC_LOADED);

        block = kMalloc(ALLOC_SIZE_BYTES);

        MEASURE_END(MALLOC_LOADED);


        MEASURE_BEGIN(FREE_LOADED);

        kFree(block);

        MEASURE_END(FREE_LOADED);
    }


    /*
     * Restore allocator state before EDF starts.
     */
    for (int i = 0; i < LOADED_ALLOC_COUNT; i++)
    {
        kFree(loaded_blocks[i]);
    }


    heap_destroy();
}
