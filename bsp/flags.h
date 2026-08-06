#ifndef __FLAGS_H_
#define __FLAGS_H_

#ifdef FLAGS_ENABLED
#define FLAG_WRITE(reg, val) ((reg) = (val))
#else
#define FLAG_WRITE(reg, val) ((void)0)
#endif

extern char _status_base;

#define VECTOR_FLAG         (*(volatile uint32_t*)&_status_base)
#define TICK_MIRROR         (*((volatile uint32_t*)&_status_base + 1))
#define ALLOC_CHECK         (*((volatile uint32_t*)&_status_base + 2))
#define SDRAM_TEST_RESULT   (*((volatile uint32_t*)&_status_base + 3))
#define SCHED_COUNT_1       (*((volatile uint32_t*)&_status_base + 4))
#define SCHED_COUNT_2       (*((volatile uint32_t*)&_status_base + 5))
#define GENERAL_FLAG        (*((volatile uint32_t*)&_status_base + 6))
#define NUM_THREADS         (*((volatile uint32_t*)&_status_base + 7))
#define NUM_RUNNING         (*((volatile uint32_t*)&_status_base + 8))
#define THREAD_COUNT_1      (*((volatile uint32_t*)&_status_base + 9))
#define THREAD_COUNT_2      (*((volatile uint32_t*)&_status_base + 10))
#define THREAD_COUNT_3      (*((volatile uint32_t*)&_status_base + 11))

#endif
