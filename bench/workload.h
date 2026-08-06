#ifndef __WORKLOAD_H__
#define __WORKLOAD_H__

#include <stdint.h>

/*
 * A workload is one benchmark/demo per image. main() dispatches to g_workload.run(),
 * which owns its own subsystem bringup (scheduler + tick, or bare instrumentation).
 * Exactly one workload TU is linked per build (selected by WORKLOAD= in the Makefile).
 */
typedef struct {
    const char *name;
    uint32_t    id;
    void      (*run)(void);   /* owns its lifetime; typically never returns */
} workload_t;

extern const workload_t g_workload;

#endif
