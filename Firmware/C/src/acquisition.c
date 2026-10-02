#include "acquisition.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/divider.h"
#include "hardware/dma.h"
#include "hardware/structs/adc.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "wavegen.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
// clk_adc: 48 MHz from pll_usb (the rated maximum) whenever the rate allows,
// 192 MHz from pll_sys (out of spec, tested OK) for rates above 100 kS/s.
#define ADC_CLOCK_SPEC_HZ 48000000u
#define ADC_CLOCK_FAST_HZ SYS_CLOCK_HZ
#define ADC_SPEC_MAX_RATE 100000u
#define ADC_MIN_PERIOD 96  // cycles per conversion

// Raw ring buffer: 8192 samples (16 KB), aligned to its size for the DMA
// write ring. 4.1 ms of data at 2 MS/s.
#define RING_BITS 14
#define RING_SAMPLES (1u << (RING_BITS - 1))
#define RING_MASK (RING_SAMPLES - 1)
static uint16_t ring[RING_SAMPLES] __attribute__((aligned(1u << RING_BITS)));

// If core 1 falls this far behind the DMA, skip ahead and flag an overrun.
#define OVERRUN_LAG (RING_SAMPLES / 2)

// Each DMA channel runs this many transfers (a multiple of the ring length,
// so it finishes exactly at the ring start) before chaining to the other.
#define DMA_RUN_LEN (0xFFFFFFFFu & ~(RING_SAMPLES - 1))

#define PROGRESSIVE_MAX_RATE 200  // frames this slow are published while filling
#define ROLL_PUBLISH_HZ 50

#define HIST_LEN 256  // >= ACQ_POINTS, power of two
#define HIST_MASK (HIST_LEN - 1)

// ---------------------------------------------------------------------------
// Core 0 <-> core 1 mailbox
// ---------------------------------------------------------------------------
enum {
    CMD_NONE, CMD_CONFIG, CMD_SET_CAL, CMD_RAW_AVG, CMD_REARM, CMD_PAUSE, CMD_RESUME,
    CMD_FORCE, CMD_DEEP, CMD_DEEP_CANCEL, CMD_STREAM,
};

typedef struct {
    int32_t zero_q4[4];  // ADC zero in raw counts x16
    int32_t gain_q7[4];  // microvolts per (raw count x16), x128
} cal_int_t;

static volatile uint32_t mbox_seq, mbox_ack;
static volatile int mbox_cmd;
static volatile uint32_t mbox_arg;
static acq_config_t staged_cfg;
static cal_int_t staged_cal[2];  // [0] = 48 MHz, [1] = 192 MHz
static float raw_avg_result[4];

static volatile bool switch_fire;
static volatile acq_state_t pub_state;
static volatile uint32_t overruns;

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------
#define NFRAMES 3
static acq_frame_t frames[NFRAMES];
static volatile int8_t latest = -1;
static volatile int8_t in_use = -1;
static volatile uint32_t version;

// ---------------------------------------------------------------------------
// Deep capture: one long triggered record (for the companion app). Samples
// are stored as 16 bits: ACQ_DEEP_LSB_UV units on the 4-input paths, raw
// ADC counts x16 on the 1-2 input paths (converted by core 0 when done).
// ---------------------------------------------------------------------------
#define DEEP_PRE_MAX 200
static uint16_t deep_buf[2][ACQ_DEEP_MAX];
static volatile acq_deep_state_t deep_state;
static volatile uint32_t deep_count;
static uint32_t deep_target;
static acq_deep_info_t deep_info;
static bool deep_raw;      // stored as raw counts, needs converting
static int deep_cal_set;   // calibration set in use when captured

// ---------------------------------------------------------------------------
// Logging stream: every output sample, packed C1 | C2 << 16 in
// ACQ_DEEP_LSB_UV units, for continuous transfer to the companion app.
// ---------------------------------------------------------------------------
#define STREAM_LEN 4096
#define STREAM_MASK (STREAM_LEN - 1)
static uint32_t stream_buf[STREAM_LEN];
static volatile uint32_t stream_head, stream_tail, stream_dropped;
static volatile bool stream_req;
static bool stream_on;

// ---------------------------------------------------------------------------
// Core 1 state
// ---------------------------------------------------------------------------
typedef enum { PATH_4CH_AVG, PATH_4CH, PATH_2CH, PATH_1CH } path_t;

static int dma_a = -1, dma_b = -1;
static acq_timing_t timing;
static acq_config_t cfg;
static cal_int_t cal;             // set for the current ADC clock
static cal_int_t cal_sets[2];
static uint32_t adc_clock_now;
static path_t path;
static bool raw_domain;  // fast paths keep raw counts until a frame completes
static int32_t c1ref_weight;  // 1 = subtract C1-, 0 = single-ended C1+
static const volatile uint32_t *wave_phase;
static uint32_t wave_prev;
static bool running;

static uint32_t rp;  // ring read index (samples)
static uint32_t acc[4];
static uint32_t nacc;

static uint64_t raw_sum[4];
static uint64_t raw_sets, raw_target;
static bool raw_active;

typedef enum { ST_FILL_PRE, ST_WAIT, ST_POST, ST_STOPPED, ST_ROLL, ST_DEEP } state_t;
static state_t state;
static state_t deep_return;  // where to go when a deep capture completes
static uint32_t filled;  // samples since pipeline reset (saturating)
static uint32_t wait_n, auto_timeout;
// Auto trigger: free-run quickly (auto_short) when there's no triggerable
// signal, but once real triggers are seen, wait up to auto_long for the next
// one so slow signals (down to 1 Hz) stay triggered.
static uint32_t auto_short, auto_long;
static bool auto_enabled;
static bool armed;
static int32_t lvl, arm_thr;  // trigger thresholds in the current domain
static int32_t hist[2][HIST_LEN];
static uint32_t hpos;
static acq_frame_t *cur;
static uint32_t frame_seq;
static uint32_t roll_interval, roll_countdown;
static bool progressive;

bool acq_timing_for_rate(uint32_t rate_hz, acq_timing_t *t) {
    uint32_t clk = rate_hz <= ADC_SPEC_MAX_RATE ? ADC_CLOCK_SPEC_HZ : ADC_CLOCK_FAST_HZ;
    uint32_t ch = rate_hz > 1000000 ? 1 : rate_hz > 500000 ? 2 : 4;
    if (rate_hz == 0 || (clk / ch) % rate_hz) return false;
    uint32_t cycles = clk / ch / rate_hz;  // = period * oversample
    if (cycles < ADC_MIN_PERIOD) return false;
    // Fastest conversion period that divides evenly -> most oversampling.
    for (uint32_t p = ADC_MIN_PERIOD; p <= cycles && p <= 65536; p++) {
        if (cycles % p == 0) {
            t->rate_hz = rate_hz;
            t->adc_clock_hz = clk;
            t->channels = ch;
            t->adc_period = p;
            t->oversample = cycles / p;
            return true;
        }
    }
    return false;
}

static inline int32_t conv(int32_t q4, int ch) {
    return ((q4 - cal.zero_q4[ch]) * cal.gain_q7[ch]) >> 7;
}

// Inverse of conv(): microvolts on one channel -> raw counts x16.
static int32_t inv_conv(int32_t uv, int ch) {
    if (cal.gain_q7[ch] <= 0) return 0;
    return cal.zero_q4[ch] + (int32_t)(((int64_t)uv * 128) / cal.gain_q7[ch]);
}

// ---------------------------------------------------------------------------
// ADC + DMA control (core 1, not time critical)
// ---------------------------------------------------------------------------
static void stop_dma_chain(void) {
    hw_clear_bits(&dma_hw->ch[dma_a].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[dma_b].al1_ctrl, DMA_CH0_CTRL_TRIG_EN_BITS);
    uint32_t mask = (1u << dma_a) | (1u << dma_b);
    dma_hw->abort = mask;
    while (dma_hw->abort & mask) tight_loop_contents();
}

static void configure_dma(int ch, int other) {
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, RING_BITS);
    channel_config_set_dreq(&c, DREQ_ADC);
    channel_config_set_chain_to(&c, other);
    channel_config_set_high_priority(&c, true);
    dma_channel_configure(ch, &c, ring, &adc_hw->fifo, DMA_RUN_LEN, false);
}

static void reset_pipeline(void);

static void adc_stop(void) {
    running = false;
    hw_clear_bits(&adc_hw->cs, ADC_CS_START_MANY_BITS);
    while (!(adc_hw->cs & ADC_CS_READY_BITS)) tight_loop_contents();
    stop_dma_chain();
    adc_fifo_drain();
}

static void adc_start(void) {
    // Ring layout per set: 4ch [C2+, C2-, C1+, C1-], 2ch [C2+, C1+], 1ch [C1+]
    uint32_t rrobin, ainsel;
    switch (timing.channels) {
    case 4: rrobin = 0xF; ainsel = 0; break;
    case 2: rrobin = (1u << ADC_CH_C2) | (1u << ADC_CH_C1); ainsel = ADC_CH_C2; break;
    default: rrobin = 0; ainsel = ADC_CH_C1; break;
    }
    adc_hw->cs = ADC_CS_EN_BITS;
    while (!(adc_hw->cs & ADC_CS_READY_BITS)) tight_loop_contents();
    adc_hw->fcs = ADC_FCS_EN_BITS | ADC_FCS_DREQ_EN_BITS | (1u << ADC_FCS_THRESH_LSB) |
                  ADC_FCS_OVER_BITS | ADC_FCS_UNDER_BITS;
    adc_fifo_drain();
    // DIV = n starts a conversion every n + 1 cycles, but a start that lands
    // while a conversion is still finishing is ignored (RP2040 datasheet
    // 4.9.2.2). n = 95 would therefore drop every other start and halve the
    // rate, so the 96-cycle period uses back-to-back mode (DIV = 0) instead.
    adc_hw->div = timing.adc_period == ADC_MIN_PERIOD ? 0
                                                      : (timing.adc_period - 1) << ADC_DIV_INT_LSB;

    configure_dma(dma_a, dma_b);
    configure_dma(dma_b, dma_a);
    dma_channel_start(dma_a);

    rp = 0;
    reset_pipeline();
    adc_hw->cs = ADC_CS_EN_BITS | (rrobin << ADC_CS_RROBIN_LSB) | (ainsel << ADC_CS_AINSEL_LSB) |
                 ADC_CS_START_MANY_BITS;
    running = true;
}

static void compute_thresholds(void) {
    int32_t level = cfg.trig_level_uv;
    int32_t arm = cfg.trig == TRIG_FALLING ? level + cfg.trig_hyst_uv : level - cfg.trig_hyst_uv;
    if (raw_domain) {
        lvl = inv_conv(level, ADC_CH_C1);
        arm_thr = inv_conv(arm, ADC_CH_C1);
    } else {
        lvl = level;
        arm_thr = arm;
    }
}

static void set_adc_clock(uint32_t hz) {
    if (hz == adc_clock_now) return;
    if (hz == ADC_CLOCK_SPEC_HZ)
        clock_configure(clk_adc, 0, CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 48000000, hz);
    else
        clock_configure(clk_adc, 0, CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, SYS_CLOCK_HZ, hz);
    adc_clock_now = hz;
}

static void select_calibration(void) {
    cal = cal_sets[adc_clock_now == ADC_CLOCK_SPEC_HZ ? 0 : 1];
}

static void apply_config(const acq_config_t *c) {
    acq_timing_t t;
    if (!acq_timing_for_rate(c->rate_hz, &t)) return;
    adc_stop();
    set_adc_clock(t.adc_clock_hz);  // only while the ADC is stopped
    select_calibration();
    timing = t;
    cfg = *c;
    if (t.channels == 4) path = t.oversample > 1 ? PATH_4CH_AVG : PATH_4CH;
    else path = t.channels == 2 ? PATH_2CH : PATH_1CH;
    raw_domain = t.channels < 4;
    progressive = !raw_domain && c->rate_hz <= PROGRESSIVE_MAX_RATE;
    c1ref_weight = c->c1_single_ended ? 0 : 1;
    wave_phase = wavegen_phase_counter();
    wave_prev = *wave_phase;
    // Switch and Wave triggers always arrive (or are user driven): no auto.
    auto_enabled = c->cap == CAP_REPEAT && (c->trig == TRIG_RISING || c->trig == TRIG_FALLING);
    uint32_t tmo = c->rate_hz / 10;  // 100 ms...
    auto_short = tmo > ACQ_POINTS ? tmo : ACQ_POINTS;  // ...or one screen
    tmo = c->rate_hz * 6 / 5;  // 1.2 s: one period of 1 Hz plus margin
    auto_long = tmo > auto_short ? tmo : auto_short;
    auto_timeout = auto_short;
    roll_interval = c->rate_hz / ROLL_PUBLISH_HZ;
    if (roll_interval == 0) roll_interval = 1;
    compute_thresholds();
    switch_fire = false;
    stream_on = stream_req && t.channels == 4 && c->rate_hz <= ACQ_STREAM_MAX_RATE;
    if (deep_state == ACQ_DEEP_CAPTURING) deep_state = ACQ_DEEP_IDLE;  // config changed: abandon
    adc_start();
}

// ---------------------------------------------------------------------------
// Hot path (RAM resident)
// ---------------------------------------------------------------------------
static void __not_in_flash_func(reset_pipeline)(void) {
    acc[0] = acc[1] = acc[2] = acc[3] = 0;
    nacc = 0;
    filled = 0;
    cur = NULL;
    armed = false;
    wait_n = 0;
    roll_countdown = roll_interval;
    if (deep_state == ACQ_DEEP_CAPTURING) deep_state = ACQ_DEEP_IDLE;
    state = cfg.cap == CAP_ROLL ? ST_ROLL : ST_FILL_PRE;
    pub_state = cfg.cap == CAP_ROLL ? ACQ_ROLLING : ACQ_WAITING;
    version++;
}

static inline uint32_t __not_in_flash_func(write_index)(void) {
    uintptr_t wa = (dma_hw->ch[dma_a].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS)
                       ? dma_hw->ch[dma_a].write_addr
                       : dma_hw->ch[dma_b].write_addr;
    return ((uint32_t)(wa - (uintptr_t)ring) >> 1) & RING_MASK & ~3u;
}

static acq_frame_t *__not_in_flash_func(pick_free_frame)(void) {
    for (int i = 0; i < NFRAMES; i++)
        if (i != latest && i != in_use) return &frames[i];
    return NULL;
}

static void __not_in_flash_func(publish)(acq_frame_t *f) {
    __dmb();
    latest = (int8_t)(f - frames);
    version++;
}

// Raw counts -> microvolts for frames captured on the 1/2-channel paths.
static void __not_in_flash_func(finish_frame)(acq_frame_t *f) {
    if (!raw_domain) return;
    int n = f->count;
    for (int i = 0; i < n; i++) {
        f->uv[0][i] = conv(f->uv[0][i], ADC_CH_C1);
        if (f->has_c2) f->uv[1][i] = conv(f->uv[1][i], ADC_CH_C2);
        else f->uv[1][i] = 0;
    }
}

static void __not_in_flash_func(init_frame)(acq_frame_t *f, bool triggered, bool roll) {
    f->triggered = triggered;
    f->roll = roll;
    f->has_c2 = timing.channels != 1;
    f->diff = timing.channels == 4 && c1ref_weight;
    f->rate_hz = timing.rate_hz;
    f->seq = ++frame_seq;
}

static void __not_in_flash_func(publish_roll)(void) {
    acq_frame_t *f = pick_free_frame();
    if (!f) return;
    uint32_t n = filled < ACQ_POINTS ? filled : ACQ_POINTS;
    uint32_t start = (hpos - n) & HIST_MASK;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t h = (start + i) & HIST_MASK;
        f->uv[0][i] = hist[0][h];
        f->uv[1][i] = hist[1][h];
    }
    f->count = (uint16_t)n;
    init_frame(f, false, true);
    finish_frame(f);
    publish(f);
}

static void __not_in_flash_func(start_frame)(bool triggered) {
    acq_frame_t *f = pick_free_frame();
    if (!f) return;
    // Copy the pre-trigger history plus the trigger sample.
    uint32_t start = (hpos - (ACQ_PRETRIGGER + 1)) & HIST_MASK;
    for (int i = 0; i <= ACQ_PRETRIGGER; i++) {
        uint32_t h = (start + i) & HIST_MASK;
        f->uv[0][i] = hist[0][h];
        f->uv[1][i] = hist[1][h];
    }
    f->count = ACQ_PRETRIGGER + 1;
    init_frame(f, triggered, false);
    cur = f;
    state = ST_POST;
    pub_state = ACQ_CAPTURING;
    if (progressive) publish(f);
}

static inline uint16_t __attribute__((always_inline)) deep_pack(int32_t v) {
    return raw_domain ? (uint16_t)v : (uint16_t)(int16_t)(v >> 8);  // 256 uV LSB
}

static void __not_in_flash_func(start_deep)(bool triggered) {
    uint32_t pre = deep_target / 8;
    if (pre > DEEP_PRE_MAX) pre = DEEP_PRE_MAX;
    if (pre + 1 > filled) pre = filled ? filled - 1 : 0;
    uint32_t start = (hpos - (pre + 1)) & HIST_MASK;
    for (uint32_t i = 0; i <= pre; i++) {
        uint32_t h = (start + i) & HIST_MASK;
        deep_buf[0][i] = deep_pack(hist[0][h]);
        deep_buf[1][i] = deep_pack(hist[1][h]);
    }
    deep_count = pre + 1;
    deep_info.rate_hz = timing.rate_hz;
    deep_info.pre = pre;
    deep_info.has_c2 = timing.channels != 1;
    deep_info.diff = timing.channels == 4 && c1ref_weight;
    deep_info.triggered = triggered;
    deep_raw = raw_domain;
    deep_cal_set = adc_clock_now == ADC_CLOCK_SPEC_HZ ? 0 : 1;
    deep_return = state == ST_ROLL ? ST_ROLL : cfg.cap == CAP_SINGLE ? ST_STOPPED : ST_WAIT;
    state = ST_DEEP;
    deep_state = ACQ_DEEP_CAPTURING;
    pub_state = ACQ_CAPTURING;
    version++;
}

// A trigger (or auto/force): either the armed deep capture or a frame.
static void __not_in_flash_func(begin_capture)(bool triggered) {
    if (deep_state == ACQ_DEEP_ARMED) start_deep(triggered);
    else start_frame(triggered);
}

static void __not_in_flash_func(back_to_waiting)(void) {
    state = ST_WAIT;
    wave_prev = *wave_phase;
    armed = false;
    wait_n = 0;
    pub_state = ACQ_WAITING;
}

static inline void __attribute__((always_inline)) on_sample(int32_t v1, int32_t v2, const bool can_stream) {
    if (can_stream && stream_on) {
        uint32_t h = stream_head, next = (h + 1) & STREAM_MASK;
        if (next != stream_tail) {
            stream_buf[h] = (uint16_t)(int16_t)(v1 >> 8) | ((uint32_t)(uint16_t)(int16_t)(v2 >> 8) << 16);
            stream_head = next;
        } else {
            stream_dropped++;
        }
    }

    uint32_t h = hpos;
    hist[0][h] = v1;
    hist[1][h] = v2;
    hpos = (h + 1) & HIST_MASK;
    if (filled < 0x7FFFFFFF) filled++;

    switch (state) {
    case ST_WAIT: {
        bool fire = false;
        if (cfg.trig == TRIG_RISING) {
            if (v1 < arm_thr) armed = true;
            else if (armed && v1 >= lvl) fire = true;
        } else if (cfg.trig == TRIG_FALLING) {
            if (v1 > arm_thr) armed = true;
            else if (armed && v1 <= lvl) fire = true;
        } else if (cfg.trig == TRIG_WAVE) {
            // The generator's DMA count jumps back up at the start of a period.
            uint32_t c = *wave_phase;
            if (c > wave_prev) fire = true;
            wave_prev = c;
        } else if (switch_fire) {
            switch_fire = false;
            fire = true;
        }
        if (fire) {
            auto_timeout = auto_long;
            begin_capture(true);
        } else if (auto_enabled && ++wait_n >= auto_timeout) {
            auto_timeout = auto_short;  // signal gone: back to fast free-running
            begin_capture(false);
        }
        break;
    }

    case ST_POST: {
        acq_frame_t *f = cur;
        uint32_t n = f->count;
        f->uv[0][n] = v1;
        f->uv[1][n] = v2;
        f->count = (uint16_t)(n + 1);
        if (progressive) version++;
        if (n + 1 < ACQ_POINTS) break;
        finish_frame(f);
        if (!progressive) publish(f);
        cur = NULL;
        armed = false;
        wait_n = 0;
        if (cfg.cap == CAP_SINGLE) {
            state = ST_STOPPED;
            pub_state = ACQ_STOPPED;
        } else {
            state = ST_WAIT;
            wave_prev = *wave_phase;
            pub_state = ACQ_WAITING;
        }
        version++;
        break;
    }

    case ST_ROLL:
        if (deep_state == ACQ_DEEP_ARMED) {
            start_deep(false);  // roll has no trigger: start right away
            break;
        }
        if (--roll_countdown == 0) {
            roll_countdown = roll_interval;
            publish_roll();
        }
        break;

    case ST_FILL_PRE:
        if (filled >= ACQ_PRETRIGGER) {
            state = ST_WAIT;
            wave_prev = *wave_phase;
        }
        break;

    case ST_STOPPED:
        break;

    case ST_DEEP: {
        uint32_t n = deep_count;
        deep_buf[0][n] = deep_pack(v1);
        deep_buf[1][n] = deep_pack(v2);
        deep_count = ++n;
        if (n < deep_target) break;
        deep_info.count = n;
        __dmb();
        deep_state = ACQ_DEEP_DONE;
        if (deep_return == ST_WAIT) back_to_waiting();
        else if (deep_return == ST_STOPPED) {
            state = ST_STOPPED;
            pub_state = ACQ_STOPPED;
        } else {
            state = ST_ROLL;
            pub_state = ACQ_ROLLING;
        }
        version++;
        break;
    }
    }
}

static inline void __attribute__((always_inline)) raw_accumulate(const uint32_t a[4], uint32_t sets) {
    for (int i = 0; i < 4; i++) raw_sum[i] += a[i];
    raw_sets += sets;
    if (raw_sets >= raw_target) {
        for (int i = 0; i < 4; i++)
            raw_avg_result[i] = (float)raw_sum[i] / (float)raw_sets * (16.0f + 1.0f / 256.0f);
        raw_active = false;
        __dmb();
        mbox_ack = mbox_seq;
    }
}

static void __not_in_flash_func(process)(void) {
    uint32_t wp = write_index();
    uint32_t r = rp;

    if (((wp - r) & RING_MASK) >= OVERRUN_LAG) {
        // Fell behind: drop the backlog and restart the current capture.
        r = wp;
        overruns++;
        if (state == ST_POST) state = ST_FILL_PRE;
        else if (state == ST_WAIT) state = ST_FILL_PRE;
        if (state == ST_FILL_PRE) {
            filled = 0;
            cur = NULL;
            pub_state = ACQ_WAITING;
        }
        acc[0] = acc[1] = acc[2] = acc[3] = 0;
        nacc = 0;
    }

    switch (path) {
    case PATH_1CH:
        while (r != wp) {
            int32_t q = (int32_t)ring[r] << 4;
            r = (r + 1) & RING_MASK;
            on_sample(q, 0, false);
        }
        break;

    case PATH_2CH:
        while (r != wp) {
            const uint16_t *s = &ring[r];
            int32_t c2 = (int32_t)s[0] << 4;
            int32_t c1 = (int32_t)s[1] << 4;
            r = (r + 2) & RING_MASK;
            on_sample(c1, c2, false);
        }
        break;

    case PATH_4CH:
        while (r != wp) {
            const uint16_t *s = &ring[r];
            int32_t c2 = conv((int32_t)s[0] << 4, ADC_CH_C2) - conv((int32_t)s[1] << 4, ADC_CH_C2REF);
            int32_t c1 = conv((int32_t)s[2] << 4, ADC_CH_C1) -
                         c1ref_weight * conv((int32_t)s[3] << 4, ADC_CH_C1REF);
            if (raw_active) {
                const uint32_t a[4] = {s[0], s[1], s[2], s[3]};
                raw_accumulate(a, 1);
            }
            r = (r + 4) & RING_MASK;
            on_sample(c1, c2, true);
        }
        break;

    case PATH_4CH_AVG: {
        uint32_t k = timing.oversample;
        while (r != wp) {
            const uint16_t *s = &ring[r];
            acc[0] += s[0];
            acc[1] += s[1];
            acc[2] += s[2];
            acc[3] += s[3];
            r = (r + 4) & RING_MASK;
            if (++nacc < k) continue;

            int32_t a[4];
            for (int i = 0; i < 4; i++) a[i] = (int32_t)hw_divider_u32_quotient_inlined(acc[i] << 4, k);
            int32_t c1 = conv(a[ADC_CH_C1], ADC_CH_C1) - c1ref_weight * conv(a[ADC_CH_C1REF], ADC_CH_C1REF);
            int32_t c2 = conv(a[ADC_CH_C2], ADC_CH_C2) - conv(a[ADC_CH_C2REF], ADC_CH_C2REF);
            if (raw_active) raw_accumulate(acc, k);
            acc[0] = acc[1] = acc[2] = acc[3] = 0;
            nacc = 0;
            on_sample(c1, c2, true);
        }
        break;
    }
    }
    rp = r;
}

static void __not_in_flash_func(handle_command)(uint32_t seq) {
    switch (mbox_cmd) {
    case CMD_CONFIG:
        apply_config(&staged_cfg);
        break;
    case CMD_SET_CAL:
        cal_sets[0] = staged_cal[0];
        cal_sets[1] = staged_cal[1];
        select_calibration();
        compute_thresholds();
        break;
    case CMD_REARM:
        if (state == ST_STOPPED) {
            back_to_waiting();
            version++;
        }
        break;
    case CMD_FORCE:
        if (state == ST_STOPPED) back_to_waiting();
        if (state == ST_WAIT) begin_capture(false);
        break;
    case CMD_DEEP:
        deep_target = mbox_arg;
        deep_count = 0;
        deep_state = ACQ_DEEP_ARMED;
        if (state == ST_STOPPED) back_to_waiting();  // single mode: arm for it
        version++;
        break;
    case CMD_DEEP_CANCEL:
        if (state == ST_DEEP) {
            if (deep_return == ST_WAIT) back_to_waiting();
            else {
                state = deep_return;
                pub_state = state == ST_ROLL ? ACQ_ROLLING : ACQ_STOPPED;
            }
        }
        deep_state = ACQ_DEEP_IDLE;
        version++;
        break;
    case CMD_STREAM:
        stream_req = mbox_arg != 0;
        stream_tail = stream_head;
        stream_dropped = 0;
        stream_on = stream_req && timing.channels == 4 && timing.rate_hz <= ACQ_STREAM_MAX_RATE;
        break;
    case CMD_RAW_AVG:
        if (timing.channels != 4) break;  // needs all four inputs
        raw_sum[0] = raw_sum[1] = raw_sum[2] = raw_sum[3] = 0;
        raw_sets = 0;
        raw_target = (uint64_t)timing.rate_hz * timing.oversample * mbox_arg / 1000u;
        if (raw_target == 0) raw_target = 1;
        raw_active = true;
        return;  // acked when done
    case CMD_PAUSE:
        mbox_ack = seq;
        while (mbox_seq == seq) tight_loop_contents();  // RAM-only spin
        // Resume: discard whatever arrived while parked.
        rp = write_index();
        reset_pipeline();
        mbox_ack = mbox_seq;
        return;
    default:
        break;
    }
    __dmb();
    mbox_ack = seq;
}

static void __not_in_flash_func(core1_main)(void) {
    uint32_t handled = mbox_seq;
    for (;;) {
        uint32_t seq = mbox_seq;
        if (seq != handled) {
            handle_command(seq);
            handled = mbox_seq;
        }
        if (running) process();
    }
}

// ---------------------------------------------------------------------------
// Core 0 API
// ---------------------------------------------------------------------------
static void wait_idle(void) {
    while (mbox_ack != mbox_seq) tight_loop_contents();
}

static void post(int cmd, uint32_t arg) {
    wait_idle();
    mbox_cmd = cmd;
    mbox_arg = arg;
    __dmb();
    uint32_t seq = mbox_seq + 1;
    mbox_seq = seq;
    while (mbox_ack != seq) tight_loop_contents();
}

void acq_init(const acq_config_t *c) {
    // Start at the in-spec 48 MHz clock; apply_config switches as needed.
    clock_configure(clk_adc, 0, CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 48000000,
                    ADC_CLOCK_SPEC_HZ);
    adc_clock_now = ADC_CLOCK_SPEC_HZ;
    adc_init();
    for (int pin = 26; pin <= 29; pin++) adc_gpio_init(pin);

    dma_a = dma_claim_unused_channel(true);
    dma_b = dma_claim_unused_channel(true);

    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 4; i++) {
            cal_sets[k].zero_q4[i] = 2048 * 16;
            cal_sets[k].gain_q7[i] = 0;
        }
    cal = cal_sets[0];

    multicore_launch_core1(core1_main);
    acq_configure(c);
}

void acq_configure(const acq_config_t *c) {
    wait_idle();
    staged_cfg = *c;
    post(CMD_CONFIG, 0);
}

static void cal_to_int(const calibration_t *c, cal_int_t *out) {
    // Calibration is stored in read_u16() units (raw * 16.0039); convert to
    // raw counts x16 and a microvolt gain per channel.
    const float zero[4] = {c->c2_0, c->c2ref_0, c->c1_0, c->c1ref_0};
    const float full[4] = {c->c2_3v3, c->c2ref_3v3, c->c1_3v3, c->c1ref_3v3};
    const float u16_to_q4 = 16.0f / (16.0f + 1.0f / 256.0f);
    for (int i = 0; i < 4; i++) {
        float zq4 = zero[i] * u16_to_q4;
        float span = (full[i] - zero[i]) * u16_to_q4;
        out->zero_q4[i] = (int32_t)lroundf(zq4);
        out->gain_q7[i] = span > 1.0f ? (int32_t)lroundf(3.3e6f / span * 128.0f) : 0;
    }
}

void acq_set_calibration(const calibration_set_t *c) {
    wait_idle();
    cal_to_int(&c->adc48, &staged_cal[0]);
    cal_to_int(&c->adc192, &staged_cal[1]);
    post(CMD_SET_CAL, 0);
}

void acq_raw_average(float out_u16[4], uint32_t ms) {
    for (int i = 0; i < 4; i++) raw_avg_result[i] = 0;
    post(CMD_RAW_AVG, ms);
    for (int i = 0; i < 4; i++) out_u16[i] = raw_avg_result[i];
}

static bool raw_pending;
static uint32_t raw_seq;

void acq_raw_average_start(uint32_t ms) {
    wait_idle();
    mbox_cmd = CMD_RAW_AVG;
    mbox_arg = ms;
    __dmb();
    raw_seq = mbox_seq + 1;
    mbox_seq = raw_seq;
    raw_pending = true;
}

bool acq_raw_average_poll(float out_u16[4]) {
    if (!raw_pending || mbox_ack != raw_seq) return false;
    __dmb();
    for (int i = 0; i < 4; i++) out_u16[i] = raw_avg_result[i];
    raw_pending = false;
    return true;
}

void acq_rearm(void) { post(CMD_REARM, 0); }
void acq_switch_trigger(void) { switch_fire = true; }

acq_state_t acq_state(void) { return pub_state; }
uint32_t acq_overruns(void) { return overruns; }

void acq_pause(void) { post(CMD_PAUSE, 0); }

void acq_resume(void) {
    // Bumping the sequence releases the spin in CMD_PAUSE.
    uint32_t seq = mbox_seq + 1;
    mbox_cmd = CMD_RESUME;
    __dmb();
    mbox_seq = seq;
    while (mbox_ack != seq) tight_loop_contents();
}

const acq_frame_t *acq_acquire_frame(void) {
    for (;;) {
        int8_t i = latest;
        if (i < 0) return NULL;
        in_use = i;
        __dmb();
        if (latest == i) return &frames[i];
    }
}

void acq_release_frame(void) { in_use = -1; }

uint32_t acq_version(void) { return version; }

// ---------------------------------------------------------------------------
// Deep capture / force / stream (core 0 API)
// ---------------------------------------------------------------------------
bool acq_deep_start(uint32_t n) {
    if (n < 256) n = 256;
    if (n > ACQ_DEEP_MAX) n = ACQ_DEEP_MAX;
    if (deep_state == ACQ_DEEP_ARMED || deep_state == ACQ_DEEP_CAPTURING) return false;
    post(CMD_DEEP, n);
    return true;
}

void acq_deep_cancel(void) { post(CMD_DEEP_CANCEL, 0); }

acq_deep_state_t acq_deep_state(uint32_t *progress) {
    if (progress) *progress = deep_count;
    return deep_state;
}

const int16_t *acq_deep_data(int ch, acq_deep_info_t *info) {
    if (deep_state != ACQ_DEEP_DONE) return NULL;
    if (deep_raw) {
        // 1-2 input paths stored raw counts: convert in place, once.
        const cal_int_t *c = &cal_sets[deep_cal_set];
        for (uint32_t i = 0; i < deep_info.count; i++) {
            int32_t q = deep_buf[0][i];
            deep_buf[0][i] = (uint16_t)(int16_t)((((q - c->zero_q4[ADC_CH_C1]) * c->gain_q7[ADC_CH_C1]) >> 7) >> 8);
            if (deep_info.has_c2) {
                q = deep_buf[1][i];
                deep_buf[1][i] = (uint16_t)(int16_t)((((q - c->zero_q4[ADC_CH_C2]) * c->gain_q7[ADC_CH_C2]) >> 7) >> 8);
            } else {
                deep_buf[1][i] = 0;
            }
        }
        deep_raw = false;
    }
    if (info) *info = deep_info;
    return (const int16_t *)deep_buf[ch ? 1 : 0];
}

void acq_deep_release(void) {
    if (deep_state == ACQ_DEEP_DONE) deep_state = ACQ_DEEP_IDLE;
}

void acq_force_trigger(void) { post(CMD_FORCE, 0); }

void acq_stream_enable(bool on) { post(CMD_STREAM, on); }

bool acq_stream_active(void) { return stream_on; }

uint32_t acq_stream_read(uint32_t *dst, uint32_t max) {
    uint32_t t = stream_tail, h = stream_head, n = 0;
    while (t != h && n < max) {
        dst[n++] = stream_buf[t];
        t = (t + 1) & STREAM_MASK;
    }
    __dmb();
    stream_tail = t;
    return n;
}

uint32_t acq_stream_dropped(void) { return stream_dropped; }
