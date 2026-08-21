#include <stdint.h>

#include "telemetry.h"
#include "tlsf.h"
#include "workload_compute.h"


/*
 * Third benchmark: a nontrivial compute task -- integer matrix multiply
 * C = A * B, N x N int32, on the D-cacheable SDRAM heap.
 *
 * Purpose: measure time-to-complete for the SAME task with the I-cache off
 * (OCRAM code uncached) vs on (code cacheable via the section-0xFFF L2 table).
 * The three matrices are 3 x 4 KB = 12 KB, so they stay resident in the 32 KB
 * L1 D-cache in BOTH builds -- data behaviour is held constant, leaving the
 * timed loop instruction-fetch bound, which is exactly what the I-cache probes.
 *
 * Run COMPUTE_REPS times for a min/mean/max distribution in metric[slot].
 */

#define MAT_N        32
#define COMPUTE_REPS 8


/*
 * Observed result. volatile + a full read-back of C defeats dead-code
 * elimination of the multiply, and lets the host confirm the computation is
 * bit-identical across I-cache on/off builds (a caching bug would change it).
 * Lives in .bss (a Device OCRAM page) so JTAG reads it coherently.
 */
HOST_SHARED volatile uint32_t g_compute_checksum;


void compute_bench(int slot)
{
    heap_init();

    int32_t* A = (int32_t*)kMalloc(MAT_N * MAT_N * sizeof(int32_t));
    int32_t* B = (int32_t*)kMalloc(MAT_N * MAT_N * sizeof(int32_t));
    int32_t* C = (int32_t*)kMalloc(MAT_N * MAT_N * sizeof(int32_t));

    for (int i = 0; i < MAT_N * MAT_N; i++)
    {
        A[i] = (int32_t)(i * 3 + 1);
        B[i] = (int32_t)(i - 7);
    }

    uint32_t checksum = 0u;

    for (int rep = 0; rep < COMPUTE_REPS; rep++)
    {
        MEASURE_BEGIN(slot);

        for (int i = 0; i < MAT_N; i++)
        {
            for (int j = 0; j < MAT_N; j++)
            {
                int32_t acc = 0;

                for (int k = 0; k < MAT_N; k++)
                {
                    acc += A[i * MAT_N + k] * B[k * MAT_N + j];
                }

                C[i * MAT_N + j] = acc;
            }
        }

        MEASURE_END(slot);

        /* Untimed: consume every element of C so the whole multiply stays live. */
        for (int t = 0; t < MAT_N * MAT_N; t++)
        {
            checksum += (uint32_t)C[t];
        }
    }

    g_compute_checksum = checksum;

    kFree(A);
    kFree(B);
    kFree(C);

    heap_destroy();
}
