#include "display.h"

#include "board.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#define TFT_SPI    spi1
#define TFT_SPI_HZ 24000000  // clk_peri (192 MHz) / 8

uint16_t display_fb[DISP_H][DISP_W];

static int dma_chan;
static bool flushing;

static void write_cmd(uint8_t cmd, const uint8_t *data, size_t len) {
    gpio_put(PIN_TFT_DC, 0);
    gpio_put(PIN_TFT_CS, 0);
    spi_write_blocking(TFT_SPI, &cmd, 1);
    if (len) {
        gpio_put(PIN_TFT_DC, 1);
        spi_write_blocking(TFT_SPI, data, len);
    }
    gpio_put(PIN_TFT_CS, 1);
}

#define CMD(c, ...)                                               \
    do {                                                          \
        static const uint8_t d_[] = {__VA_ARGS__};                \
        write_cmd((c), d_, sizeof d_);                            \
    } while (0)

void display_init(void) {
    spi_init(TFT_SPI, TFT_SPI_HZ);
    spi_set_format(TFT_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_TFT_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TFT_MOSI, GPIO_FUNC_SPI);

    const uint pins[] = {PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST};
    for (int i = 0; i < 3; i++) {
        gpio_init(pins[i]);
        gpio_put(pins[i], 1);
        gpio_set_dir(pins[i], GPIO_OUT);
    }

    gpio_put(PIN_TFT_RST, 0);
    sleep_ms(1);
    gpio_put(PIN_TFT_RST, 1);
    sleep_ms(120);

    // ST7735 red-tab initialisation
    write_cmd(0x01, NULL, 0);  // SWRESET
    sleep_ms(150);
    write_cmd(0x11, NULL, 0);  // SLPOUT
    sleep_ms(255);
    CMD(0xB1, 0x01, 0x2C, 0x2D);                    // FRMCTR1
    CMD(0xB2, 0x01, 0x2C, 0x2D);                    // FRMCTR2
    CMD(0xB3, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D);  // FRMCTR3
    CMD(0xB4, 0x07);                                // INVCTR
    CMD(0xC0, 0xA2, 0x02, 0x84);                    // PWCTR1
    CMD(0xC1, 0xC5);                                // PWCTR2
    CMD(0xC2, 0x0A, 0x00);                          // PWCTR3
    CMD(0xC3, 0x8A, 0x2A);                          // PWCTR4
    CMD(0xC4, 0x8A, 0xEE);                          // PWCTR5
    CMD(0xC5, 0x0E);                                // VMCTR1
    write_cmd(0x20, NULL, 0);                       // INVOFF
    CMD(0x36, 0xA0);                                // MADCTL: landscape, RGB
    CMD(0x3A, 0x05);                                // COLMOD: 16 bpp
    CMD(0xE0, 0x0f, 0x1a, 0x0f, 0x18, 0x2f, 0x28, 0x20, 0x22, 0x1f, 0x1b, 0x23, 0x37,
        0x00, 0x07, 0x02, 0x10);  // GMCTRP1
    CMD(0xE1, 0x0f, 0x1b, 0x0f, 0x17, 0x33, 0x2c, 0x29, 0x2e, 0x30, 0x30, 0x39, 0x3f,
        0x00, 0x07, 0x03, 0x10);  // GMCTRN1
    write_cmd(0x13, NULL, 0);     // NORON
    sleep_ms(10);
    CMD(0x2A, 0, 0, 0, DISP_W - 1);  // CASET
    CMD(0x2B, 0, 0, 0, DISP_H - 1);  // RASET
    write_cmd(0x29, NULL, 0);        // DISPON
    sleep_ms(100);

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, spi_get_dreq(TFT_SPI, true));
    dma_channel_configure(dma_chan, &c, &spi_get_hw(TFT_SPI)->dr, display_fb,
                          sizeof display_fb, false);
}

bool display_busy(void) {
    if (!flushing) return false;
    if (dma_channel_is_busy(dma_chan) || spi_is_busy(TFT_SPI)) return true;
    // Transfer complete: release the bus.
    while (spi_is_readable(TFT_SPI)) (void)spi_get_hw(TFT_SPI)->dr;
    spi_get_hw(TFT_SPI)->icr = SPI_SSPICR_RORIC_BITS;
    gpio_put(PIN_TFT_CS, 1);
    flushing = false;
    return false;
}

void display_wait(void) {
    while (display_busy()) tight_loop_contents();
}

void display_flush(void) {
    display_wait();
    // RAMWR into the full-screen window set during init.
    uint8_t cmd = 0x2C;
    gpio_put(PIN_TFT_DC, 0);
    gpio_put(PIN_TFT_CS, 0);
    spi_write_blocking(TFT_SPI, &cmd, 1);
    gpio_put(PIN_TFT_DC, 1);
    flushing = true;
    dma_channel_set_read_addr(dma_chan, display_fb, false);
    dma_channel_set_trans_count(dma_chan, sizeof display_fb, true);
}
