#ifndef __KTRACE_H__
#define __KTRACE_H__

/*
 * Kernel trace hooks. Default to no-ops so the production kernel is unaffected. An
 * instrumented bench build overrides any subset via a forced include
 * (-include bench/ktrace_<workload>.h) that defines these before this header's
 * guards, letting Phase 2+ measure the kernel without editing kernel source.
 *
 * Call sites are wired into the kernel in the phase that consumes them (scheduler
 * in Phase 2, allocator in Phase 3), alongside their definitions, so each addition
 * is proven zero-cost-when-off at the point it lands.
 */

#ifndef KTRACE_TICK_ENTER
#define KTRACE_TICK_ENTER()     ((void)0)
#endif
#ifndef KTRACE_TICK_EXIT
#define KTRACE_TICK_EXIT()      ((void)0)
#endif
#ifndef KTRACE_SWITCH_IN
#define KTRACE_SWITCH_IN(t)     ((void)0)
#endif
#ifndef KTRACE_MALLOC_ENTER
#define KTRACE_MALLOC_ENTER(sz) ((void)0)
#endif
#ifndef KTRACE_MALLOC_EXIT
#define KTRACE_MALLOC_EXIT(p)   ((void)0)
#endif
#ifndef KTRACE_FREE_ENTER
#define KTRACE_FREE_ENTER(p)    ((void)0)
#endif
#ifndef KTRACE_FREE_EXIT
#define KTRACE_FREE_EXIT()      ((void)0)
#endif

#endif
