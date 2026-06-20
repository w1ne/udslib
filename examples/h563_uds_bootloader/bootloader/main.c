#include "fdcan.h"

int main(void)
{
    uart_init();
    uart_puts("BL-START\n");
    for (;;) {
    }
}
