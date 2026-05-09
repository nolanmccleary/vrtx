#ifndef __FLAGS_H_
#define __FLAGS_H_


extern char _status_base;

#define VECTOR_FLAG         (*(volatile uint32_t*)&_status_base)
#define TICK_MIRROR         (*((volatile uint32_t*)&_status_base + 1))
#define ALLOC_CHECK         (*((volatile uint32_t*)&_status_base + 2))
#define SDRAM_TEST_RESULT   (*((volatile uint32_t*)&_status_base + 3))
#define SCHED_COUNT_1       (*((volatile uint32_t*)&_status_base + 4))
#define SCHED_COUNT_2       (*((volatile uint32_t*)&_status_base + 5))
#define GENERAL_FLAG        (*((volatile uint32_t*)&_status_base + 6))

#endif
