#include "wavegen.h"

#include <math.h>

#include "board.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

// PWM: 255 steps, no clock division (753 kHz carrier, filtered on the board).
#define PWM_TOP 254
#define LEVEL_ZERO 128
#define FULL_SCALE_MV 3300

// Table length and DMA pacing per frequency: up to 6000 samples per period,
// paced at f * N <= 60 kS/s by DMA TIMER0 (X/Y * clk_sys, Y = 192 MHz / rate).
// Every supported frequency gives an integer Y below 65536.
#define DMA_TIMER_NUM 0
#define MAX_SAMPLES 6000
#define MAX_PACE_HZ 60000u

// Each sample is a full 32-bit word written to the slice's CC register:
// low half = channel A level (GPIO20), high half = channel B (unused, 0).
static uint32_t table[MAX_SAMPLES];

static int data_chan = -1;  // copies samples to PWM CC, paced by DMA timer
static int ctrl_chan = -1;  // re-arms data_chan's read address, then chains back
static const uint32_t *volatile table_ptr = table;  // read by ctrl_chan
static uint pwm_slice;

void wavegen_init(void) {
    gpio_set_function(PIN_WAVE, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(PIN_WAVE);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_config_set_wrap(&cfg, PWM_TOP);
    pwm_init(pwm_slice, &cfg, false);
    pwm_set_chan_level(pwm_slice, pwm_gpio_to_channel(PIN_WAVE), LEVEL_ZERO);
    pwm_set_enabled(pwm_slice, true);

    dma_timer_claim(DMA_TIMER_NUM);

    data_chan = dma_claim_unused_channel(true);
    ctrl_chan = dma_claim_unused_channel(true);
}

static void stop_chain(void) {
    hw_clear_bits(&dma_hw->ch[data_chan].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[ctrl_chan].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    uint32_t mask = (1u << data_chan) | (1u << ctrl_chan);
    dma_hw->abort = mask;
    while (dma_hw->abort & mask) tight_loop_contents();
}

static uint32_t level_for_mv(float mv) {
    long l = lroundf(LEVEL_ZERO + mv * 127.0f / FULL_SCALE_MV);
    if (l < 0) l = 0;
    if (l > 255) l = 255;
    return (uint32_t)l;
}

void wavegen_configure(wave_t wave, int32_t amplitude_mv, uint32_t freq_mhz) {
    // Samples per period: as many as fit under both limits.
    uint32_t n = (uint32_t)((uint64_t)MAX_PACE_HZ * 1000u / freq_mhz);
    if (n > MAX_SAMPLES) n = MAX_SAMPLES;
    if (n < 2) n = 2;
    uint32_t pace_hz = (uint32_t)((uint64_t)freq_mhz * n / 1000u);

    stop_chain();
    dma_timer_set_fraction(DMA_TIMER_NUM, 1, (uint16_t)(192000000u / pace_hz));
    pwm_set_chan_level(pwm_slice, pwm_gpio_to_channel(PIN_WAVE), LEVEL_ZERO);

    const float a = (float)amplitude_mv;
    for (uint32_t i = 0; i < n; i++) {
        float ph = (float)i / (float)n;  // 0..1
        float v;
        switch (wave) {
        case WAVE_SQUARE:  // high then 0 V, like the original firmware
            v = ph < 0.5f ? a : 0.0f;
            break;
        case WAVE_TRIANGLE:  // starts at 0 V rising, like the sine
            v = ph < 0.25f ? 4.0f * ph : ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f;
            v *= a;
            break;
        case WAVE_SAWTOOTH:  // -A ramping to +A
            v = a * (2.0f * ph - 1.0f);
            break;
        case WAVE_DC:
            v = a;
            break;
        default:
            v = a * sinf(6.2831853f * ph);
            break;
        }
        table[i] = level_for_mv(v);
    }

    // Data channel: n words from the table to the PWM CC register, one per
    // DMA timer tick, then chain to the control channel.
    dma_channel_config dc = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, dma_get_timer_dreq(DMA_TIMER_NUM));
    channel_config_set_chain_to(&dc, ctrl_chan);
    channel_config_set_high_priority(&dc, true);
    dma_channel_configure(data_chan, &dc, &pwm_hw->slice[pwm_slice].cc, table, n, false);

    // Control channel: write the table start address back into the data
    // channel's READ_ADDR, then chain to (restart) the data channel.
    dma_channel_config cc = dma_channel_get_default_config(ctrl_chan);
    channel_config_set_transfer_data_size(&cc, DMA_SIZE_32);
    channel_config_set_read_increment(&cc, false);
    channel_config_set_write_increment(&cc, false);
    channel_config_set_chain_to(&cc, data_chan);
    channel_config_set_high_priority(&cc, true);
    dma_channel_configure(ctrl_chan, &cc, &dma_hw->ch[data_chan].read_addr, &table_ptr, 1, false);

    dma_channel_start(data_chan);
}

const volatile uint32_t *wavegen_phase_counter(void) {
    return &dma_hw->ch[data_chan].transfer_count;
}
