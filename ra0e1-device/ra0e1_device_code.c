/*
 * Code for the FPB-RA0E1 "unknown device".
 *
 * HOW TO USE: open src/hal_entry.c in your e2 studio project. Keep the
 * file as it is (R_BSP_WarmStart, in hal_entry.c or hal_warmstart.c, opens the I/O
 * ports), delete only the existing hal_entry() function, and paste this
 * whole block in its place.
 *
 * Plays a secret SPI protocol on the PMOD1 connector, over and over.
 * The SPI signals are generated in software (bit-banged on GPIO), slow
 * enough for the Nucleo recorder to sample comfortably.
 *
 *   PMOD1 pin 1  P103  CS
 *   PMOD1 pin 2  P101  MOSI
 *   PMOD1 pin 3  P100  CLK (the MISO pin, reused because the device never answers)
 *   PMOD1 pin 5  GND
 *
 * Don't read past this comment if you want the puzzle to stay a puzzle!
 */

#define PIN_CS   BSP_IO_PORT_01_PIN_03
#define PIN_MOSI BSP_IO_PORT_01_PIN_01
#define PIN_CLK  BSP_IO_PORT_01_PIN_00

#define HALF_BIT_US 10u

static void pin_out(bsp_io_port_pin_t pin, uint32_t high)
{
    R_IOPORT_PinWrite(&g_ioport_ctrl, pin, high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

static void wait_us(uint32_t us)
{
    R_BSP_SoftwareDelay(us, BSP_DELAY_UNITS_MICROSECONDS);
}

static void shift_out(uint8_t b)
{
    for (uint32_t i = 0; i < 8u; i++)
    {
        pin_out(PIN_CLK, 0);
        pin_out(PIN_MOSI, (b >> (7u - i)) & 1u);
        wait_us(HALF_BIT_US);
        pin_out(PIN_CLK, 1);
        wait_us(HALF_BIT_US);
    }
}

static void frame(uint8_t k, const uint8_t * p, uint8_t n)
{
    uint8_t x = (uint8_t) (0xA5u ^ k ^ n);

    pin_out(PIN_CS, 0);
    wait_us(4u * HALF_BIT_US);
    shift_out(0xA5u);
    shift_out(k);
    shift_out(n);
    for (uint8_t i = 0; i < n; i++)
    {
        shift_out(p[i]);
        x ^= p[i];
    }
    shift_out(x);
    wait_us(4u * HALF_BIT_US);
    pin_out(PIN_CS, 1);
    wait_us(30u * HALF_BIT_US);
}

static void play_round(uint8_t r)
{
    static const uint8_t c[6][3] = {
        {0xFF, 0x00, 0x00}, {0xFF, 0x80, 0x00}, {0x00, 0xFF, 0x00},
        {0x00, 0x80, 0xFF}, {0x00, 0x00, 0xFF}, {0x80, 0x00, 0xFF},
    };
    uint16_t t = (uint16_t) (215u + (r % 8u));
    uint8_t  a[1] = {r};
    uint8_t  b[2] = {(uint8_t) (t >> 8), (uint8_t) t};

    frame(0x01u, a, 1u);
    frame(0x10u, b, 2u);
    frame(0x20u, c[r % 6u], 3u);
}

void hal_entry(void)
{
    R_IOPORT_PinCfg(&g_ioport_ctrl, PIN_CS, IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_HIGH);
    R_IOPORT_PinCfg(&g_ioport_ctrl, PIN_CLK, IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_HIGH);
    R_IOPORT_PinCfg(&g_ioport_ctrl, PIN_MOSI, IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);

    uint8_t r = 0;
    while (1)
    {
        play_round(r++);
        wait_us(2000u);
    }
}
