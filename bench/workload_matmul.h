#ifndef WORKLOAD_MATMUL_H
#define WORKLOAD_MATMUL_H

#include <stdint.h>


/* Integer matrix-multiply benchmark (g_metrics slot 6). Owns its heap and its
 * slot; leaves the heap destroyed. */
void matmul_run(void);


/* Checksum of the last multiply -- host-readable correctness check. */
extern volatile uint32_t g_matmul_checksum;


#endif
