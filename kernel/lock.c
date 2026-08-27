#include "lock.h"


volatile uint32_t g_spin_exit;


extern void _lock_mutex_persistent(void* mutex);
extern mutex_lock_e _lock_mutex_best_effort(void* mutex);
extern void _unlock_mutex(void* mutex);


void lock_mutex_persistent(mutex_t *mutex)
{
    _lock_mutex_persistent(mutex);
    (void)(0);
}


mutex_lock_e lock_mutex_best_effort(mutex_t *mutex)
{
    return _lock_mutex_best_effort(mutex);
}


void unlock_mutex(mutex_t* mutex)
{
    _unlock_mutex(mutex);
    (void)(0);
}
