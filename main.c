#include <stdint.h>

#define CANARY (*(volatile uint32_t *)0xFFFF8000)


void c_irq_handler(int r0)
{
    (void)r0;
}

void c_fiq_handler(int r0)
{
    (void)r0;
}


void main(void) {
    CANARY = 67;
    while (1) 
    {
        // CANARY++;
        CANARY = 600;
    }
}
