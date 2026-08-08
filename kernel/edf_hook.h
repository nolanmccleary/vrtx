#ifndef __EDF_HOOK_H__
#define __EDF_HOOK_H__

/*
 * Per-tick hook for the EDF verification build only. The run's stop condition
 * cannot live in the workload's main_thread loop (it starves under load), so the
 * EDF build injects a snapshot into the tick path via this guard. No-op in every
 * other build; the core scheduler is unchanged. Mirrors the KTRACE_* injection.
 */
#ifdef MODE_EDF
void edf_tick_hook(void);      /* implemented in bench/workload_edf.c */
#define EDF_TICK_HOOK() edf_tick_hook()
#else
#define EDF_TICK_HOOK() ((void)0)
#endif

#endif
