#include <stdint.h>

#define CANARY (*(volatile uint32_t *)0xFFFF8000)

void main(void) {
    CANARY = 67;
    while (1) 
    {
        // CANARY++;
        CANARY = 600;
    }
}
