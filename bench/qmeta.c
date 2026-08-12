#include <stdint.h>
#include "qmeta.h"


/*
 * Linker-provided regions.
 */
extern char _heap_start;
extern char _heap_size;

extern char _status_base;
extern char _capture_region;

extern char _telemetry_start;
extern char _telemetry_size;

extern char _bss_start;
extern char _bss_size;

extern char _data_start;
extern char _data_size;


/*
 * Linker-provided stack descriptors.
 */
extern char _sys_stack_top;
extern char _svc_stack_top;
extern char _irq_stack_top;
extern char _fiq_stack_top;
extern char _abt_stack_top;
extern char _und_stack_top;

extern char _stack_size;
extern char _abt_stack_size;
extern char _und_stack_size;


/*
 * Scheduler globals useful to the host.
 */
extern uint32_t gTicks;
extern uint32_t gMissedDeadlines;



const qmeta_hdr_t g_qmeta_hdr QMETA_HDR =
{
    .magic          = QMETA_MAGIC,
    .version        = QMETA_VERSION,
    .region_stride  = sizeof(qmeta_region_t)
};



const qmeta_region_t g_qmeta_core[] QMETA_REGIONS =
{
    QMETA_REGION(
        "heap",
        &_heap_start,
        &_heap_size,
        QMETA_KIND_HEAP
    ),

    QMETA_REGION(
        "status",
        &_status_base,
        &_capture_region,
        QMETA_KIND_STATUS | QMETA_F_COHERENT
    ),

    QMETA_REGION(
        "telemetry",
        &_telemetry_start,
        &_telemetry_size,
        QMETA_KIND_TELEMETRY | QMETA_F_COHERENT
    ),

    QMETA_REGION(
        "bss",
        &_bss_start,
        &_bss_size,
        QMETA_KIND_BSS
    ),

    QMETA_REGION(
        "data",
        &_data_start,
        &_data_size,
        QMETA_KIND_DATA
    ),


    QMETA_REGION(
        "gTicks",
        &gTicks,
        4u,
        QMETA_KIND_GLOBAL | QMETA_F_COHERENT
    ),

    QMETA_REGION(
        "gMisses",
        &gMissedDeadlines,
        4u,
        QMETA_KIND_GLOBAL | QMETA_F_COHERENT
    ),


    QMETA_REGION(
        "sys_stack",
        &_sys_stack_top,
        &_stack_size,
        QMETA_KIND_STACK |
        QMETA_F_STACK_DESC |
        QMETA_F_COHERENT
    ),

    QMETA_REGION(
        "svc_stack",
        &_svc_stack_top,
        &_stack_size,
        QMETA_KIND_STACK |
        QMETA_F_STACK_DESC |
        QMETA_F_COHERENT
    ),

    QMETA_REGION(
        "irq_stack",
        &_irq_stack_top,
        &_stack_size,
        QMETA_KIND_STACK |
        QMETA_F_STACK_DESC |
        QMETA_F_COHERENT
    ),

    QMETA_REGION(
        "fiq_stack",
        &_fiq_stack_top,
        &_stack_size,
        QMETA_KIND_STACK |
        QMETA_F_STACK_DESC |
        QMETA_F_COHERENT
    ),

    QMETA_REGION(
        "abt_stack",
        &_abt_stack_top,
        &_abt_stack_size,
        QMETA_KIND_STACK |
        QMETA_F_STACK_DESC |
        QMETA_F_COHERENT
    ),

    QMETA_REGION(
        "und_stack",
        &_und_stack_top,
        &_und_stack_size,
        QMETA_KIND_STACK |
        QMETA_F_STACK_DESC |
        QMETA_F_COHERENT
    )
};
