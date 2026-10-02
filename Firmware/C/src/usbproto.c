#include "usbproto.h"

#include <stdio.h>
#include <string.h>

#include "acquisition.h"
#include "meters.h"
#include "pico/bootrom.h"
#include "pico/stdio/driver.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "settings.h"
#include "tusb.h"

#define PROTO_VERSION 1
#define FW_VERSION "1.0"

#define SYNC 0xA5
#define HOST_TIMEOUT_MS 3000
#define FRAME_MIN_US 20000   // scope frames: up to 50/s
#define METER_PERIOD_MS 100
#define STATUS_PERIOD_MS 250
#define DEEP_CHUNK 256       // samples per DEEP_DAT packet
#define LOG_CHUNK 256        // samples per LOG_DAT packet

enum {
    H_HELLO = 0x01, H_PING, H_SET, H_ACTION, H_STREAMS, H_DEEP, H_LOG,
    D_INFO = 0x81, D_SETTINGS, D_STATUS, D_FRAME, D_METER, D_DEEP_HDR, D_DEEP_DAT, D_DEEP_END,
    D_LOG_DAT,
};

// ---------------------------------------------------------------------------
// Transmit queue -> CDC
// ---------------------------------------------------------------------------
#define TXQ_SIZE 8192
static uint8_t txq[TXQ_SIZE];
static uint32_t txq_head, txq_tail;  // both owned by core 0

static uint32_t txq_used(void) { return (txq_head - txq_tail) & (TXQ_SIZE - 1); }
static uint32_t txq_free(void) { return TXQ_SIZE - 1 - txq_used(); }

static void txq_put(const void *data, uint32_t len) {
    const uint8_t *p = data;
    while (len--) {
        txq[txq_head] = *p++;
        txq_head = (txq_head + 1) & (TXQ_SIZE - 1);
    }
}

static uint8_t crc8_update(uint8_t crc, const uint8_t *p, uint32_t len) {
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) crc = (uint8_t)(crc & 0x80 ? (crc << 1) ^ 0x07 : crc << 1);
    }
    return crc;
}

// Queues one packet from up to two payload pieces; false if it doesn't fit.
static bool send2(uint8_t type, const void *a, uint32_t alen, const void *b, uint32_t blen) {
    uint32_t len = alen + blen;
    if (len > 0xFFFF || txq_free() < len + 5) return false;
    uint8_t hdr[4] = {SYNC, type, (uint8_t)len, (uint8_t)(len >> 8)};
    uint8_t crc = crc8_update(0, hdr + 1, 3);
    crc = crc8_update(crc, a, alen);
    crc = crc8_update(crc, b, blen);
    txq_put(hdr, 4);
    txq_put(a, alen);
    txq_put(b, blen);
    txq_put(&crc, 1);
    return true;
}

static bool send(uint8_t type, const void *payload, uint32_t len) { return send2(type, payload, len, NULL, 0); }

static void pump_tx(void) {
    while (txq_used() && tud_ready()) {
        uint32_t avail = tud_cdc_write_available();
        if (!avail) break;
        uint32_t contiguous = txq_head >= txq_tail ? txq_head - txq_tail : TXQ_SIZE - txq_tail;
        uint32_t n = contiguous < avail ? contiguous : avail;
        stdio_usb.out_chars((const char *)&txq[txq_tail], (int)n);  // never blocks: n <= avail
        txq_tail = (txq_tail + n) & (TXQ_SIZE - 1);
    }
}

// ---------------------------------------------------------------------------
// Little-endian helpers
// ---------------------------------------------------------------------------
static uint8_t *put_u16(uint8_t *p, uint32_t v) {
    *p++ = (uint8_t)v;
    *p++ = (uint8_t)(v >> 8);
    return p;
}
static uint8_t *put_u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) *p++ = (uint8_t)(v >> (8 * i));
    return p;
}
static uint32_t get_u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool hello;
static absolute_time_t last_rx;
static bool want_frames = true, want_meters = true;
static bool send_info, send_settings;
static settings_t last_settings;
static uint16_t last_vis, last_en;
static uint32_t last_frame_version;
static absolute_time_t next_frame, next_meter, next_status;
static bool log_on;
static uint32_t log_index;
static bool deep_requested;  // host asked; send when done
static uint32_t deep_sent;   // samples sent so far
static bool deep_hdr_sent;

bool usbp_host_active(void) {
    return hello && absolute_time_diff_us(last_rx, get_absolute_time()) < HOST_TIMEOUT_MS * 1000;
}

void usbp_init(void) {
    stdio_usb_init();
    last_rx = get_absolute_time();
}

// ---------------------------------------------------------------------------
// INFO (JSON)
// ---------------------------------------------------------------------------
static char info_buf[4096];  // INFO is ~3.4 KB

static int json_settings(char *p, int max) {
    int n = 0;
#define OUT(...) n += snprintf(p + n, n < max ? (size_t)(max - n) : 0, __VA_ARGS__)
    OUT("[");
    for (int i = 0; i < setting_count; i++) {
        const setting_item_t *it = &setting_items[i];
        OUT("%s{\"id\":%d,\"key\":\"%s\",\"label\":\"%s\",\"options\":[", i ? "," : "", i, it->key, it->label);
        char buf[24];
        for (int k = 0; k < it->count; k++) {
            it->format(k, buf, sizeof buf);
            OUT("%s\"%s\"", k ? "," : "", buf);
        }
        OUT("]");
        if (it->numeric) {
            OUT(",\"unit\":\"%s\",\"values\":[", it->unit);
            for (int k = 0; k < it->count; k++) OUT("%s%ld", k ? "," : "", (long)it->numeric(k));
            OUT("]");
        }
        OUT("}");
    }
    OUT("]");
#undef OUT
    return n;
}

static void queue_info(void) {
    int n = snprintf(info_buf, sizeof info_buf,
                     "{\"proto\":%d,\"fw\":\"%s\",\"name\":\"Scopocket\",\"board\":\"v1.3\","
                     "\"points\":%d,\"pretrigger\":%d,\"deep_max\":%d,\"deep_lsb_uv\":%d,"
                     "\"log_max_rate\":%d,\"settings\":",
                     PROTO_VERSION, FW_VERSION, ACQ_POINTS, ACQ_PRETRIGGER, ACQ_DEEP_MAX,
                     ACQ_DEEP_LSB_UV, ACQ_STREAM_MAX_RATE);
    n += json_settings(info_buf + n, (int)sizeof info_buf - n - 2);
    if (n > (int)sizeof info_buf - 2) n = (int)sizeof info_buf - 2;  // (never expected)
    info_buf[n++] = '}';
    send_info = !send(D_INFO, info_buf, (uint32_t)n);
}

// ---------------------------------------------------------------------------
// Periodic messages
// ---------------------------------------------------------------------------
static void masks(uint16_t *vis, uint16_t *en) {
    *vis = *en = 0;
    for (int i = 0; i < setting_count && i < 16; i++) {
        if (setting_visible(i)) *vis |= 1u << i;
        if (setting_enabled(i)) *en |= 1u << i;
    }
}

static void queue_settings(void) {
    uint8_t buf[64], *p = buf;
    *p++ = (uint8_t)setting_count;
    for (int i = 0; i < setting_count; i++) *p++ = (uint8_t)*setting_items[i].value;
    uint16_t vis, en;
    masks(&vis, &en);
    p = put_u16(p, vis);
    p = put_u16(p, en);
    if (send(D_SETTINGS, buf, (uint32_t)(p - buf))) {
        last_settings = settings;
        last_vis = vis;
        last_en = en;
        send_settings = false;
    }
}

static void queue_status(void) {
    uint8_t buf[32], *p = buf;
    uint32_t progress;
    acq_deep_state_t ds = acq_deep_state(&progress);
    *p++ = (uint8_t)settings.mode;
    *p++ = (uint8_t)acq_state();
    bool log_possible = settings.mode == MODE_SCOPE && settings_rate_hz() <= ACQ_STREAM_MAX_RATE;
    *p++ = app_menu_open() | app_low_battery() << 1 | log_on << 2 | log_possible << 3;
    p = put_u32(p, acq_overruns());
    *p++ = (uint8_t)ds;
    p = put_u32(p, progress);
    p = put_u32(p, acq_stream_dropped());
    send(D_STATUS, buf, (uint32_t)(p - buf));
}

static void queue_frame(void) {
    uint32_t v = acq_version();
    if (v == last_frame_version || txq_free() < 16 + 8 * ACQ_POINTS) return;
    const acq_frame_t *f = acq_acquire_frame();
    if (f) {
        uint16_t n = f->count;
        uint8_t hdr[16], *p = hdr;
        p = put_u32(p, f->seq);
        p = put_u32(p, f->rate_hz);
        *p++ = f->triggered | f->has_c2 << 1 | f->roll << 2 | f->diff << 3;
        p = put_u16(p, n);
        // C1 and C2 arrays are contiguous in the frame only per channel, so
        // copy both into one payload (int32 little-endian, as in RAM).
        static int32_t both[2 * ACQ_POINTS];
        memcpy(both, f->uv[0], n * sizeof(int32_t));
        memcpy(both + n, f->uv[1], n * sizeof(int32_t));
        if (send2(D_FRAME, hdr, (uint32_t)(p - hdr), both, n * 2u * sizeof(int32_t))) {
            last_frame_version = v;
        }
    }
    acq_release_frame();
}

static void queue_meter(void) {
    uint8_t buf[96];
    int n = meters_report(settings.mode, buf, sizeof buf);
    if (n > 0) send(D_METER, buf, (uint32_t)n);
}

static void pump_deep(void) {
    if (!deep_requested || acq_deep_state(NULL) != ACQ_DEEP_DONE) return;
    acq_deep_info_t info;
    const int16_t *c1 = acq_deep_data(0, &info);
    const int16_t *c2 = acq_deep_data(1, NULL);
    if (!deep_hdr_sent) {
        uint8_t buf[24], *p = buf;
        p = put_u32(p, info.count);
        p = put_u32(p, info.rate_hz);
        p = put_u32(p, info.pre);
        *p++ = info.triggered | info.has_c2 << 1 | info.diff << 3;
        p = put_u32(p, ACQ_DEEP_LSB_UV);
        if (!send(D_DEEP_HDR, buf, (uint32_t)(p - buf))) return;
        deep_hdr_sent = true;
        deep_sent = 0;
    }
    while (deep_sent < info.count) {
        uint32_t n = info.count - deep_sent;
        if (n > DEEP_CHUNK) n = DEEP_CHUNK;
        static uint8_t buf[6 + 4 * DEEP_CHUNK];
        uint8_t *p = buf;
        p = put_u32(p, deep_sent);
        p = put_u16(p, n);
        memcpy(p, c1 + deep_sent, n * 2);
        memcpy(p + n * 2, c2 + deep_sent, n * 2);
        if (!send(D_DEEP_DAT, buf, 6 + n * 4)) return;  // queue full: continue later
        deep_sent += n;
    }
    if (send(D_DEEP_END, NULL, 0)) {
        deep_requested = false;
        acq_deep_release();
    }
}

static void pump_log(void) {
    if (!log_on) return;
    static uint32_t samples[LOG_CHUNK];
    static uint8_t buf[6 + 4 * LOG_CHUNK];
    while (txq_free() >= sizeof buf + 5) {
        uint32_t n = acq_stream_read(samples, LOG_CHUNK);
        if (!n) break;
        uint8_t *p = buf;
        p = put_u32(p, log_index);
        p = put_u16(p, n);
        memcpy(p, samples, n * 4);
        send(D_LOG_DAT, buf, 6 + n * 4);
        log_index += n;
    }
}

static void set_log(bool on) {
    log_on = on;
    log_index = 0;
    acq_stream_enable(on);
}

// ---------------------------------------------------------------------------
// Receive
// ---------------------------------------------------------------------------
static void handle_packet(uint8_t type, const uint8_t *p, uint32_t len) {
    last_rx = get_absolute_time();
    switch (type) {
    case H_HELLO:
        hello = true;
        send_info = true;
        send_settings = true;
        last_frame_version = 0;
        break;
    case H_PING:
        break;
    case H_SET:
        if (len >= 2) app_set_setting(p[0], p[1]);
        send_settings = true;
        break;
    case H_ACTION:
        if (len < 1) break;
        switch (p[0]) {
        case USBP_ACT_REARM: acq_rearm(); break;
        case USBP_ACT_FORCE: acq_force_trigger(); break;
        case USBP_ACT_RESET_MINMAX: meters_remote(settings.mode, METER_ACT_RESET_MINMAX); break;
        case USBP_ACT_ZERO: meters_remote(settings.mode, METER_ACT_ZERO); break;
        case USBP_ACT_CAP_MEASURE: meters_remote(settings.mode, METER_ACT_MEASURE); break;
        case USBP_ACT_DEEP_CANCEL:
            deep_requested = false;
            acq_deep_cancel();
            acq_deep_release();
            break;
        case USBP_ACT_BOOTSEL: reset_usb_boot(0, 0); break;
        }
        break;
    case H_STREAMS:
        if (len >= 1) {
            want_frames = p[0] & 1;
            want_meters = (p[0] >> 1) & 1;
        }
        break;
    case H_DEEP:
        if (len >= 4 && settings.mode == MODE_SCOPE && !deep_requested) {
            acq_deep_release();  // drop any old, unsent result
            if (acq_deep_start(get_u32(p))) {
                deep_requested = true;
                deep_hdr_sent = false;
            }
        }
        break;
    case H_LOG:
        if (len >= 1) set_log(p[0] != 0);
        break;
    }
}

static void receive(void) {
    static enum { R_SYNC, R_TYPE, R_LEN0, R_LEN1, R_DATA, R_CRC } st = R_SYNC;
    static uint8_t type, crc;
    static uint16_t len, got;
    static uint8_t data[256];
    char buf[64];
    int n;
    while ((n = stdio_usb.in_chars(buf, sizeof buf)) > 0) {
        for (int i = 0; i < n; i++) {
            uint8_t b = (uint8_t)buf[i];
            switch (st) {
            case R_SYNC:
                if (b == SYNC) st = R_TYPE;
                break;
            case R_TYPE:
                // Unknown type: a false sync, look for the next one.
                if (b < H_HELLO || b > H_LOG) {
                    st = b == SYNC ? R_TYPE : R_SYNC;
                    break;
                }
                type = b;
                crc = crc8_update(0, &b, 1);
                st = R_LEN0;
                break;
            case R_LEN0:
                len = b;
                crc = crc8_update(crc, &b, 1);
                st = R_LEN1;
                break;
            case R_LEN1:
                len |= (uint16_t)(b << 8);
                crc = crc8_update(crc, &b, 1);
                got = 0;
                st = len > sizeof data ? R_SYNC : len ? R_DATA : R_CRC;
                break;
            case R_DATA:
                data[got++] = b;
                crc = crc8_update(crc, &b, 1);
                if (got == len) st = R_CRC;
                break;
            case R_CRC:
                if (b == crc) handle_packet(type, data, len);
                st = R_SYNC;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------

void usbp_service(void) {
    receive();
    if (!usbp_host_active()) {
        if (log_on) set_log(false);
        txq_tail = txq_head;  // nobody listening: drop anything queued
        return;
    }

    if (send_info) queue_info();
    if (!send_info) {
        uint16_t vis, en;
        masks(&vis, &en);
        if (send_settings || memcmp(&last_settings, &settings, sizeof settings) || vis != last_vis ||
            en != last_en)
            queue_settings();

        if (time_reached(next_status)) {
            next_status = make_timeout_time_ms(STATUS_PERIOD_MS);
            queue_status();
        }
        // Logging only exists in scope mode at loggable rates.
        if (log_on && (settings.mode != MODE_SCOPE || !acq_stream_active())) set_log(false);
        pump_deep();
        pump_log();
        if (settings.mode == MODE_SCOPE) {
            if (want_frames && time_reached(next_frame)) {
                next_frame = make_timeout_time_us(FRAME_MIN_US);
                queue_frame();
            }
        } else if (want_meters && time_reached(next_meter)) {
            next_meter = make_timeout_time_ms(METER_PERIOD_MS);
            queue_meter();
        }
    }
    pump_tx();
}
