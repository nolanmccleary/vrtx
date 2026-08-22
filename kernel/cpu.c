#include "cpu.h"
#include "telemetry.h"


/* Engage mailbox: CPU0 posts the go signal AFTER its caches are on, while CPU1
 * reads it in the pen with caches off (not yet in the coherency domain). Must be
 * uncached (host_shared) or CPU0's write sits in its L1 and CPU1 never sees it. */
HOST_SHARED volatile uint32_t g_cpu_mailbox;
