#include <stdint.h>

#define UART_BASE  0xFFC02000UL
#define UREG(off)  (*(volatile uint32_t *)(UART_BASE + (off)))
#define UART_RBR   UREG(0x00)
#define UART_DLL   UREG(0x00)
#define UART_DLH   UREG(0x04)
#define UART_IER   UREG(0x04)
#define UART_FCR   UREG(0x08)
#define UART_LCR   UREG(0x0C)
#define UART_LSR   UREG(0x14)

static void uart_init(void) {
    UART_LCR = 0x80;        /* DLAB=1 */
    UART_DLL = 54;
    UART_DLH = 0;
    UART_LCR = 0x03;        /* 8N1 */
    UART_FCR = 0x07;
    UART_IER = 0x00;
}

static void uart_putc(char c) {
    while (!(UART_LSR & (1u << 5)));
    UART_RBR = (uint32_t)c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void main(void) {
    uart_init();
    while (1) {
        uart_puts("[A] tick\n");
        uart_puts("[B] tick\n");
        for (volatile int i = 0; i < 500000; i++);
    }
}
