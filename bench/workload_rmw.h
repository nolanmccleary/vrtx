#ifndef WORKLOAD_RMW_H
#define WORKLOAD_RMW_H


/* SDRAM read-modify-write sweep at two working-set sizes (g_metrics slots 4-5).
 * Owns its heap and its slots; leaves the heap destroyed. */
void rmw_run(void);


#endif
