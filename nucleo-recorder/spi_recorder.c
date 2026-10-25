/*
 * spi_recorder.c
 *
 * Turns one NUCLEO-U083RC into a tiny "SPI device + logic analyzer":
 *
 *   SPI1 (master)  PB3 SCK, PB4 MISO, PB5 MOSI, PB6 CS (plain GPIO)
 *   SPI2 (slave)   PB13 SCK, PB14 MISO, PB15 MOSI, PB12 NSS
 *
 * Four jumper wires connect the two sides (all on morpho connector CN10):
 *   CN10-31 (PB3)  <-> CN10-30 (PB13)   clock
 *   CN10-29 (PB5)  <-> CN10-26 (PB15)   MOSI
 *   CN10-27 (PB4)  <-> CN10-28 (PB14)   MISO
 *   CN10-17 (PB6)  <-> CN10-16 (PB12)   chip select
 *
 * While SPI1 talks to SPI2, the CPU reads the pin levels of port B
 * in a tight loop (one byte per sample) and afterwards prints the
 * recording over the ST-LINK virtual COM port (USART2, 115200 8N1)
 * as a VCD file that sigrok / REA can decode.
 *
 * Usage: call spi_recorder_run() from main() after the HAL init.
 * Then send 'c' over the COM port to take one recording.
 *
 * LISTEN_ONLY = 1 turns the board into a pure recorder for an SPI bus
 * driven by another board: PB3/PB4/PB5/PB6 become inputs, SPI1/SPI2 stay
 * off, and the recording starts right after the end of a CS-low burst,
 * so the next burst is recorded from its idle lead-in. Connect the other
 * board's GND to a Nucleo GND pin.
 */
#ifndef LISTEN_ONLY
#define LISTEN_ONLY 0
#endif

#include <stdint.h>
#include <stddef.h>
#ifndef HOST_TEST
#include "stm32u0xx.h"
#include "spi_recorder.h"
#endif

/* Bit positions of the bus lines inside one sample (= GPIOB->IDR bits 0..7) */
#define BIT_CLK   3u   /* PB3 */
#define BIT_MISO  4u   /* PB4 */
#define BIT_MOSI  5u   /* PB5 */
#define BIT_CS    6u   /* PB6 */

/* Before a sample overwrites it, each buffer byte holds what to do at that moment */
#define EV_CS_HIGH 0x80u   /* chip select level for this sample: 1 = high (idle) */
#define EV_SEND    0x40u   /* start sending the next byte now */

#define BUF_SIZE     30000u
#define SPI_DIV      256u   /* SPI clock = PCLK / 256 (BR = 0b111) */
#define SLAVE_LEAD   2u     /* reply bytes queued in the slave ahead of the master */
#define CAL_SAMPLES  2000u
#define MAX_BYTES    32u

static uint8_t buf[BUF_SIZE];

/* ---- The "unknown device" conversation (the protocol you will reverse engineer) ---- */
typedef struct {
  uint8_t len;
  uint8_t mosi[4];
  uint8_t miso[4];
} xfer_t;

static const xfer_t script[] = {
  { 3, { 0x9F, 0x00, 0x00 },       { 0xFF, 0xC2, 0x17 } },
  { 2, { 0x01, 0x03 },             { 0xFF, 0xFF } },
  { 2, { 0x05, 0x00 },             { 0xFF, 0x01 } },
  { 2, { 0x05, 0x00 },             { 0xFF, 0x00 } },
  { 4, { 0x03, 0x10, 0x00, 0x00 }, { 0xFF, 0xFF, 0x09, 0xC4 } },
};
#define NXFER (sizeof(script) / sizeof(script[0]))

/* Byte streams for the two SPI data registers, padded so the loop may read one past the end */
static uint8_t tx_master[MAX_BYTES + 1];
static uint8_t tx_slave[MAX_BYTES + SLAVE_LEAD + 1];
static uint32_t n_bytes;

/*
 * Fill buf[] with the plan: CS level per sample and the samples where a
 * byte is handed to the SPI. byte_s = how many samples one byte lasts.
 * Returns the number of samples to record, or 0 if the plan does not fit.
 */
static uint32_t build_schedule(uint32_t byte_s)
{
  uint32_t i, j, t, cs_start;

  for (i = 0; i < BUF_SIZE; i++) {
    buf[i] = EV_CS_HIGH;
  }
  for (i = 0; i < sizeof(tx_slave); i++) {
    tx_slave[i] = 0xFF;
  }
  for (i = 0; i < sizeof(tx_master); i++) {
    tx_master[i] = 0x00;
  }

  n_bytes = 0;
  t = 2u * byte_s;                         /* idle time before the first transfer */
  for (i = 0; i < NXFER; i++) {
    cs_start = t;
    t += byte_s / 2u;                      /* CS low, then a short setup time */
    for (j = 0; j < script[i].len; j++) {
      if (t >= BUF_SIZE || n_bytes >= MAX_BYTES) {
        return 0;
      }
      buf[t] |= EV_SEND;
      tx_master[n_bytes] = script[i].mosi[j];
      tx_slave[n_bytes] = script[i].miso[j];
      n_bytes++;
      t += (3u * byte_s) / 2u;             /* 1.5 byte times between bytes */
    }
    t += byte_s;                           /* let the last byte finish */
    if (t >= BUF_SIZE) {
      return 0;
    }
    for (j = cs_start; j < t; j++) {
      buf[j] &= (uint8_t)~EV_CS_HIGH;      /* CS low for the whole transfer */
    }
    t += 3u * byte_s;                      /* gap between transfers */
  }
  t += byte_s;                             /* idle tail */
  return (t <= BUF_SIZE) ? t : 0;
}

/* ---- Output helpers ---- */
typedef void (*putc_fn)(char c);

static void put_str(putc_fn out, const char *s)
{
  while (*s) {
    out(*s++);
  }
}

static void put_u64(putc_fn out, uint64_t v)
{
  char tmp[21];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  } while (v != 0u);
  while (n > 0) {
    out(tmp[--n]);
  }
}

static void put_line_u64(putc_fn out, const char *label, uint64_t v)
{
  put_str(out, label);
  put_u64(out, v);
  put_str(out, "\r\n");
}

/*
 * Print n recorded samples as VCD. total_cycles = CPU cycles the recording took,
 * hclk = CPU clock in Hz. Only changes are printed, which keeps the file small.
 */
static void vcd_write(putc_fn out, uint32_t n, uint64_t total_cycles, uint32_t hclk)
{
  static const struct { uint64_t ps; const char *name; } units[] = {
    { 1000000u, "1 us" }, { 100000u, "100 ns" }, { 10000u, "10 ns" }, { 1000u, "1 ns" },
  };
  static const uint8_t bits[4] = { BIT_CLK, BIT_MOSI, BIT_MISO, BIT_CS };
  static const char ids[4] = { '!', '"', '#', '$' };
  static const char *const names[4] = { "clk", "mosi", "miso", "cs" };

  uint64_t period_ps = (total_cycles * (1000000000000ull / hclk)) / n;
  uint32_t u = 3, i, k;
  uint8_t prev = 0;

  for (k = 0; k < 4; k++) {
    if (units[k].ps <= period_ps) {
      u = k;
      break;
    }
  }

  put_str(out, "$comment NUCLEO-U083RC SPI recorder. Sample period ");
  put_u64(out, period_ps);
  put_str(out, " ps, ");
  put_u64(out, n);
  put_str(out, " samples $end\r\n");
  put_str(out, "$timescale ");
  put_str(out, units[u].name);
  put_str(out, " $end\r\n$scope module spi $end\r\n");
  for (k = 0; k < 4; k++) {
    put_str(out, "$var wire 1 ");
    out(ids[k]);
    out(' ');
    put_str(out, names[k]);
    put_str(out, " $end\r\n");
  }
  put_str(out, "$upscope $end\r\n$enddefinitions $end\r\n");

  for (i = 0; i < n; i++) {
    uint8_t s = buf[i];
    if (i == 0 || s != prev) {
      out('#');
      put_u64(out, ((uint64_t)i * period_ps) / units[u].ps);
      put_str(out, "\r\n");
      for (k = 0; k < 4; k++) {
        uint8_t m = (uint8_t)(1u << bits[k]);
        if (i == 0 || ((s ^ prev) & m)) {
          out((s & m) ? '1' : '0');
          out(ids[k]);
          put_str(out, "\r\n");
        }
      }
      prev = s;
    }
  }
  out('#');
  put_u64(out, ((uint64_t)n * period_ps) / units[u].ps);
  put_str(out, "\r\n");
}

/* Count rising edges of one line, as a quick wiring check */
static uint32_t count_rising(uint32_t n, uint32_t bit)
{
  uint32_t i, c = 0;
  for (i = 1; i < n; i++) {
    if (!(buf[i - 1] & (1u << bit)) && (buf[i] & (1u << bit))) {
      c++;
    }
  }
  return c;
}

#ifndef HOST_TEST

/* ---- Hardware part ---- */

static void uart_putc(char c)
{
  while (!(USART2->ISR & USART_ISR_TXE_TXFNF)) {
  }
  USART2->TDR = (uint8_t)c;
}

static int uart_getc(void)
{
  if (USART2->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
    USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
  }
  if (USART2->ISR & USART_ISR_RXNE_RXFNE) {
    return (int)(USART2->RDR & 0xFFu);
  }
  return -1;
}

static uint32_t pclk_hz(void)
{
  uint32_t ppre = (RCC->CFGR & RCC_CFGR_PPRE) >> RCC_CFGR_PPRE_Pos;
  return (ppre < 4u) ? SystemCoreClock : (SystemCoreClock >> (ppre - 3u));
}

/* mode: 1 = output, 2 = alternate function; pull: 0 = none, 1 = up */
static void pin_setup(GPIO_TypeDef *port, uint32_t pin, uint32_t mode, uint32_t af, uint32_t pull)
{
  port->MODER = (port->MODER & ~(3u << (2u * pin))) | (mode << (2u * pin));
  port->PUPDR = (port->PUPDR & ~(3u << (2u * pin))) | (pull << (2u * pin));
  port->OSPEEDR = (port->OSPEEDR & ~(3u << (2u * pin))) | (1u << (2u * pin));
  port->AFR[pin >> 3] = (port->AFR[pin >> 3] & ~(0xFu << (4u * (pin & 7u)))) | (af << (4u * (pin & 7u)));
}

static void uart_init(void)
{
  RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
  RCC->APBENR1 |= RCC_APBENR1_USART2EN;
  RCC->CCIPR &= ~RCC_CCIPR_USART2SEL;          /* USART2 clocked from PCLK */
  pin_setup(GPIOA, 2, 2, 7, 0);                /* PA2 = USART2_TX (AF7) */
  pin_setup(GPIOA, 3, 2, 7, 1);                /* PA3 = USART2_RX (AF7) */
  USART2->CR1 = 0;
  USART2->PRESC = 0;
  USART2->BRR = (pclk_hz() + 115200u / 2u) / 115200u;
  USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

/* Reset both SPIs (empties their FIFOs) and configure them for mode 0, 8 bit, MSB first */
static void spi_init(void)
{
  RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
  RCC->APBENR2 |= RCC_APBENR2_SPI1EN;
  RCC->APBENR1 |= RCC_APBENR1_SPI2EN;
  RCC->APBRSTR2 |= RCC_APBRSTR2_SPI1RST;
  RCC->APBRSTR2 &= ~RCC_APBRSTR2_SPI1RST;
  RCC->APBRSTR1 |= RCC_APBRSTR1_SPI2RST;
  RCC->APBRSTR1 &= ~RCC_APBRSTR1_SPI2RST;

  GPIOB->BSRR = 1u << BIT_CS;                  /* CS idle high */
  pin_setup(GPIOB, 6, 1, 0, 0);                /* PB6 = CS output */
  pin_setup(GPIOB, 3, 2, 5, 0);                /* PB3 = SPI1_SCK */
  pin_setup(GPIOB, 4, 2, 5, 1);                /* PB4 = SPI1_MISO, pull-up */
  pin_setup(GPIOB, 5, 2, 5, 0);                /* PB5 = SPI1_MOSI */
  pin_setup(GPIOB, 12, 2, 5, 0);               /* PB12 = SPI2_NSS */
  pin_setup(GPIOB, 13, 2, 5, 0);               /* PB13 = SPI2_SCK */
  pin_setup(GPIOB, 14, 2, 5, 0);               /* PB14 = SPI2_MISO */
  pin_setup(GPIOB, 15, 2, 5, 0);               /* PB15 = SPI2_MOSI */

  /* SPI2: slave, hardware NSS, 8-bit frames */
  SPI2->CR1 = 0;
  SPI2->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
  SPI2->CR1 = SPI_CR1_SPE;

  /* SPI1: master, PCLK/256, software NSS (CS is driven by hand on PB6) */
  SPI1->CR1 = SPI_CR1_MSTR | (7u << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI;
  SPI1->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
  SPI1->CR1 |= SPI_CR1_SPE;
}

/*
 * The recording loop. Every pass takes the same number of CPU cycles:
 * set CS from the plan, hand a byte to both SPIs or to a dummy (chosen
 * by index, not by an if), then read the pins into the same buffer byte.
 */
/* Always optimised, even in a Debug build: a faster loop means more samples per SPI bit */
static void __attribute__((noinline, optimize("O2"))) capture(uint32_t n, int live)
{
  volatile uint8_t sink[2];
  volatile uint8_t *const mdst[2] = { &sink[0], live ? (volatile uint8_t *)&SPI1->DR : &sink[0] };
  volatile uint8_t *const sdst[2] = { &sink[1], live ? (volatile uint8_t *)&SPI2->DR : &sink[1] };
  const uint8_t *mt = tx_master;
  const uint8_t *st = tx_slave + SLAVE_LEAD;
  uint32_t i;

  for (i = 0; i < n; i++) {
    uint32_t v = buf[i];
    uint32_t f = (v >> 6) & 1u;
    GPIOB->BSRR = (1u << (BIT_CS + 16u)) >> ((v >> 7) << 4);
    *sdst[f] = *st;
    *mdst[f] = *mt;
    st += f;
    mt += f;
    buf[i] = (uint8_t)GPIOB->IDR;
  }
}

/* Run capture() with SysTick counting CPU cycles; returns the cycles used */
static uint32_t timed_capture(uint32_t n, int live)
{
  uint32_t ctrl = SysTick->CTRL, load = SysTick->LOAD, start, end;

  __disable_irq();
  SysTick->CTRL = 0;
  SysTick->LOAD = 0x00FFFFFFu;
  SysTick->VAL = 0;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
  start = SysTick->VAL;
  capture(n, live);
  end = SysTick->VAL;
  SysTick->CTRL = 0;
  SysTick->LOAD = load;
  SysTick->VAL = 0;
  SysTick->CTRL = ctrl;
  __enable_irq();

  return (start - end) & 0x00FFFFFFu;
}

static void record_once(void)
{
  uint32_t i, cal_cycles, cycles, n, byte_s, cps_x100;
  uint32_t spi_bit_cycles = SPI_DIV * (SystemCoreClock / pclk_hz());

  spi_init();

  /* 1. Calibrate: how many CPU cycles does one loop pass take? */
  for (i = 0; i < CAL_SAMPLES; i++) {
    buf[i] = EV_CS_HIGH;
  }
  cal_cycles = timed_capture(CAL_SAMPLES, 0);
  cps_x100 = (cal_cycles * 100u) / CAL_SAMPLES;
  byte_s = (8u * spi_bit_cycles * 100u) / cps_x100 + 1u;

  /* 2. Plan the conversation in sample units */
  n = build_schedule(byte_s);
  if (n == 0) {
    put_str(uart_putc, "ERROR: recording does not fit in the buffer\r\n");
    return;
  }

  /* 3. Queue the first reply bytes in the slave, then record */
  for (i = 0; i < SLAVE_LEAD; i++) {
    *(volatile uint8_t *)&SPI2->DR = tx_slave[i];
  }
  cycles = timed_capture(n, 1);
  GPIOB->BSRR = 1u << BIT_CS;

  /* 4. Report and dump */
  put_line_u64(uart_putc, "CPU clock (Hz): ", SystemCoreClock);
  put_line_u64(uart_putc, "Cycles per sample (x100): ", (cycles * 100ull) / n);
  put_line_u64(uart_putc, "Samples: ", n);
  put_line_u64(uart_putc, "Bytes sent: ", n_bytes);
  put_line_u64(uart_putc, "Clock pulses seen (expected 8 x bytes): ", count_rising(n, BIT_CLK));
  put_line_u64(uart_putc, "CS pulses seen (expected 5): ", count_rising(n, BIT_CS));
  put_line_u64(uart_putc, "MISO rising edges seen (0 = check MISO wire): ", count_rising(n, BIT_MISO));
  put_str(uart_putc, "-----BEGIN VCD-----\r\n");
  vcd_write(uart_putc, n, cycles, SystemCoreClock);
  put_str(uart_putc, "-----END VCD-----\r\n");
}

/* Listen-only: tight loop that only stores the pin levels */
static void __attribute__((noinline, optimize("O2"))) sniff(uint32_t n)
{
  uint32_t i;
  for (i = 0; i < n; i++) {
    buf[i] = (uint8_t)GPIOB->IDR;
  }
}

/* Waits until a CS-low burst has ended (CS low, then high). Returns 0 on timeout. */
static int wait_burst_end(void)
{
  uint32_t t;
  for (t = 0; t < 20000000u && (GPIOB->IDR & (1u << BIT_CS)); t++) {
  }
  if (GPIOB->IDR & (1u << BIT_CS)) {
    return 0;
  }
  for (t = 0; t < 20000000u && !(GPIOB->IDR & (1u << BIT_CS)); t++) {
  }
  return (GPIOB->IDR & (1u << BIT_CS)) ? 1 : 0;
}

static void listen_once(void)
{
  uint32_t ctrl = SysTick->CTRL, load = SysTick->LOAD, start, end, cycles;

  RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
  pin_setup(GPIOB, 3, 0, 0, 0);                /* all four lines: plain inputs */
  pin_setup(GPIOB, 4, 0, 0, 1);                /* MISO with pull-up in case nobody drives it */
  pin_setup(GPIOB, 5, 0, 0, 0);
  pin_setup(GPIOB, 6, 0, 0, 1);                /* CS with pull-up: idle high when unconnected */

  put_str(uart_putc, "Waiting for SPI traffic (CS low)...\r\n");
  if (!wait_burst_end()) {
    put_str(uart_putc, "ERROR: no CS activity seen. Check the CS and GND wires.\r\n");
    return;
  }

  __disable_irq();
  SysTick->CTRL = 0;
  SysTick->LOAD = 0x00FFFFFFu;
  SysTick->VAL = 0;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
  start = SysTick->VAL;
  sniff(BUF_SIZE);
  end = SysTick->VAL;
  SysTick->CTRL = 0;
  SysTick->LOAD = load;
  SysTick->VAL = 0;
  SysTick->CTRL = ctrl;
  __enable_irq();
  cycles = (start - end) & 0x00FFFFFFu;

  put_line_u64(uart_putc, "CPU clock (Hz): ", SystemCoreClock);
  put_line_u64(uart_putc, "Cycles per sample (x100): ", (cycles * 100ull) / BUF_SIZE);
  put_line_u64(uart_putc, "Samples: ", BUF_SIZE);
  put_line_u64(uart_putc, "Clock pulses seen: ", count_rising(BUF_SIZE, BIT_CLK));
  put_line_u64(uart_putc, "CS pulses seen: ", count_rising(BUF_SIZE, BIT_CS));
  put_line_u64(uart_putc, "MISO rising edges seen: ", count_rising(BUF_SIZE, BIT_MISO));
  put_str(uart_putc, "-----BEGIN VCD-----\r\n");
  vcd_write(uart_putc, BUF_SIZE, cycles, SystemCoreClock);
  put_str(uart_putc, "-----END VCD-----\r\n");
}

void spi_recorder_run(void)
{
  uart_init();
  if (LISTEN_ONLY) {
    put_str(uart_putc, "\r\nNUCLEO-U083RC SPI recorder (LISTEN ONLY) ready. Send 'c' to record.\r\n");
    for (;;) {
      int c = uart_getc();
      if (c == 'c' || c == 'C') {
        listen_once();
        put_str(uart_putc, "Done. Send 'c' to record again.\r\n");
      }
    }
  }
  put_str(uart_putc, "\r\nNUCLEO-U083RC SPI recorder ready. Send 'c' to record.\r\n");
  for (;;) {
    int c = uart_getc();
    if (c == 'c' || c == 'C') {
      record_once();
      put_str(uart_putc, "Done. Send 'c' to record again.\r\n");
    }
  }
}

#endif /* HOST_TEST */
