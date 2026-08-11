/*
 * Core (workload-independent) qmeta table: the memory regions every image has.
 * Workloads add their own result-struct regions + field descriptors in their own
 * TUs (see bench/workload_edf.c). All addresses/sizes below are link-time
 * constants, so the whole table is resolved at link and read statically by the host.
 */

#include <stdint.h>
#include "qmeta.h"

/* Region bounds + sizes come from the linker (linker/de1-soc.ld). Absolute size
   symbols (_*_size) let us fill .size without a symbol-difference in a C initializer. */
extern char _heap_start,      _heap_size;
extern char _telemetry_start, _telemetry_size;
extern char _bss_start,       _bss_size;
extern char _data_start,      _data_size;

extern char _sys_stack_top, _svc_stack_top, _irq_stack_top, _fiq_stack_top,
            _abt_stack_top, _und_stack_top;
extern char _stack_size, _abt_stack_size, _und_stack_size;

/* Scheduler globals the host may want to sample directly. */
extern uint32_t gTicks;
extern uint32_t gMissedDeadlines;


const qmeta_hdr_t g_qmeta_hdr QMETA_HDR = {
    .magic         = QMETA_MAGIC,
    .version       = QMETA_VERSION,
    .region_stride = sizeof(qmeta_region_t),
    .field_stride  = sizeof(qmeta_field_t),
};

const qmeta_region_t g_qmeta_core[] QMETA_REGIONS = {
    QMETA_REGION("heap",      &_heap_start,      &_heap_size,      QMETA_KIND_HEAP),
    QMETA_REGION("telemetry", &_telemetry_start, &_telemetry_size, QMETA_KIND_TELEMETRY | QMETA_F_COHERENT),
    QMETA_REGION("bss",       &_bss_start,       &_bss_size,       QMETA_KIND_BSS),
    QMETA_REGION("data",      &_data_start,      &_data_size,      QMETA_KIND_DATA),

    QMETA_REGION("gTicks",    &gTicks,           4u,               QMETA_KIND_GLOBAL | QMETA_F_COHERENT),
    QMETA_REGION("gMisses",   &gMissedDeadlines, 4u,               QMETA_KIND_GLOBAL | QMETA_F_COHERENT),

    QMETA_REGION("sys_stack", &_sys_stack_top,   &_stack_size,     QMETA_KIND_STACK | QMETA_F_STACK_DESC | QMETA_F_COHERENT),
    QMETA_REGION("svc_stack", &_svc_stack_top,   &_stack_size,     QMETA_KIND_STACK | QMETA_F_STACK_DESC | QMETA_F_COHERENT),
    QMETA_REGION("irq_stack", &_irq_stack_top,   &_stack_size,     QMETA_KIND_STACK | QMETA_F_STACK_DESC | QMETA_F_COHERENT),
    QMETA_REGION("fiq_stack", &_fiq_stack_top,   &_stack_size,     QMETA_KIND_STACK | QMETA_F_STACK_DESC | QMETA_F_COHERENT),
    QMETA_REGION("abt_stack", &_abt_stack_top,   &_abt_stack_size, QMETA_KIND_STACK | QMETA_F_STACK_DESC | QMETA_F_COHERENT),
    QMETA_REGION("und_stack", &_und_stack_top,   &_und_stack_size, QMETA_KIND_STACK | QMETA_F_STACK_DESC | QMETA_F_COHERENT),
};
