#include "cpu.h"
#include "system.h"
#include "telemetry.h"


HOST_SHARED volatile uint32_t g_cpu_mailbox;


cpu_t g_cpus[NUM_CPUS];





