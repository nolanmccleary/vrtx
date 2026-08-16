#ifndef WORKLOAD_COMPUTE_H
#define WORKLOAD_COMPUTE_H

#include <stdint.h>


/* Run the matrix-multiply compute benchmark, recording per-repetition cycle
 * counts into g_telemetry.metric[slot]. Does its own heap_init/heap_destroy and
 * leaves the heap destroyed (matching the allocbench teardown contract). */
void compute_bench(int slot);


/* Checksum of the last multiply -- host-readable correctness/consistency check. */
extern volatile uint32_t g_compute_checksum;


#endif
