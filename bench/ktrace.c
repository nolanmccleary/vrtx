#include "ktrace.h"


#ifdef MODE_TEST


volatile uint32_t g_test_release = 0u;


#define KTRACE_BP_ATTR \
    __attribute__((noinline, noclone, used, aligned(4)))


KTRACE_BP_ATTR
void ktrace_bp_alloc_done(void)
{
    __asm__ __volatile__(
        "nop"
        :
        :
        : "memory"
    );
}


KTRACE_BP_ATTR
void ktrace_bp_edf_ready(void)
{
    __asm__ __volatile__(
        "nop"
        :
        :
        : "memory"
    );
}


KTRACE_BP_ATTR
void ktrace_bp_edf_done(void)
{
    __asm__ __volatile__(
        "nop"
        :
        :
        : "memory"
    );
}


void ktrace_wait_release(void)
{
    g_test_release = 0u;

    __asm__ __volatile__(
        "dmb sy"
        :
        :
        : "memory"
    );


    while (g_test_release == 0u)
    {
        __asm__ __volatile__(
            ""
            :
            :
            : "memory"
        );
    }


    __asm__ __volatile__(
        "dmb sy"
        :
        :
        : "memory"
    );


    g_test_release = 0u;
}


#endif
