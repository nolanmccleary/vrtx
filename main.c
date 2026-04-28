#include <stdint.h>

#define CANARY (*(volatile uint32_t *)0xFFFF8000)

#define GICD_CTLR       (*(volatile uint32_t *)0xFFFED000)
#define GICD_ISENABLER0 (*(volatile uint32_t *)0xFFFED100)
#define GICC_CTLR       (*(volatile uint32_t *)0xFFFEC100)
#define GICC_PMR        (*(volatile uint32_t *)0xFFFEC104)

#define PTIMER_LOAD     (*(volatile uint32_t *)0xFFFEC600)
#define PTIMER_COUNTER  (*(volatile uint32_t *)0xFFFEC604)
#define PTIMER_CTRL     (*(volatile uint32_t *)0xFFFEC608)
#define PTIMER_STATUS   (*(volatile uint32_t *)0xFFFEC60C)





static void ptimer_init(void)
{
    PTIMER_LOAD = 399999;           // ~1ms at 400MHz PERIPHCLK
    PTIMER_CTRL = (0 << 8)          // prescaler /1
               | (1 << 2)           // IRQ enable
               | (1 << 1)           // auto-reload
               | (1 << 0);          // enable
}


static void gic_init(void)
{
    GICD_CTLR       = 1;
    GICD_ISENABLER0 |= (1 << 29);
    GICC_PMR        = 0xFF;
    GICC_CTLR       = 1;
}


void c_irq_handler(int r0)
{
    PTIMER_STATUS = 1; //clear interrupt flag
    CANARY++; 
}


void c_fiq_handler(int r0)
{
    (void)r0;
}


void main(void)
{
    gic_init();
    ptimer_init();
    CANARY = 67;
    while (1) 
    {
        // CANARY++;
    }
}
