#include <stdint.h>
#include "bsp.h"
#include "preempt_sched.h"
#include "tlsf.h"
#include "ktrace.h"








void cpu1_main(void)
{
    for (;;){};
}



void allocbench_run(void);
void edf_run(void);

void main(void)
{

#if defined(MODE_TEST)
    allocbench_run();
    edf_run();
#endif

    for (;;) { }
}
