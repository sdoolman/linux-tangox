#include <linux/init.h>
#include <asm/io.h>

#include "memmap.h"
#include "setup.h"
#include "uart.h"

static const unsigned long uart_addr[] __initconst = {
	UART0_BASE,
	UART1_BASE,
	UART2_BASE,
};

#define DRAM_LOG_BASE    0xABF00100UL
#define DRAM_LOG_MAGIC   0x5043484CUL  /* 'PCHL' */

struct pch_early_log {
	u32 magic;
	u32 head;
	u32 count;
	u32 reserved;
	char buf[65536 - 16];
};

static void __iomem *uart_base;

static inline unsigned serial_in(int reg)
{
	return readl(uart_base + reg);
}

static inline void serial_out(int reg, unsigned value)
{
	writel(value, uart_base + reg);
}

void __init prom_console_init(void)
{
	int port = CONFIG_TANGOX_EARLY_CONSOLE_PORT;
	int baud = CONFIG_TANGOX_EARLY_CONSOLE_RATE;
	unsigned int div;

	uart_base = ioremap(uart_addr[port], 0x30);
	if (!uart_base)
		return;

	div = DIV_ROUND_CLOSEST(UART_CLOCK, 16 * baud);

	serial_out(UART_CLKSEL, 1);
	serial_out(UART_CLKDIV, div);

	serial_out(UART_IER, 0x0);
	serial_out(UART_FCR, 0);
	serial_out(UART_LCR, 0x3);
}

void prom_putchar(char c)
{
	volatile struct pch_early_log *pl = (volatile struct pch_early_log *)DRAM_LOG_BASE;
	unsigned int timeout;
	int status, bits;


	if (pl->magic != DRAM_LOG_MAGIC) {
		pl->head = 0;
		pl->count = 0;
		pl->magic = DRAM_LOG_MAGIC;
	}

	if (pl->head < sizeof(pl->buf) - 1) {
		pl->buf[pl->head++] = c;
		pl->buf[pl->head] = '\0';
		pl->count++;
	}

	if (!uart_base)
		return;

	timeout = 1000;
	bits = 0x60;

	do {
		status = serial_in(UART_LSR);

		if (--timeout == 0)
			break;
	} while ((status & bits) != bits);

	if (timeout)
		serial_out(UART_THR, c);
}
