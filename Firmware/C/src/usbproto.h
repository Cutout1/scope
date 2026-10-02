// USB link to the companion app (CDC serial, binary packets).
//
// Packet: 0xA5 | type u8 | length u16 LE | payload | crc8 (poly 0x07 over
// type, length and payload). All multi-byte values are little-endian.
//
// Host -> device
//   0x01 HELLO                       -> INFO, SETTINGS, STATUS
//   0x02 PING                        keep-alive (streams stop after 3 s silence)
//   0x03 SET      u8 id, u8 index    change a setting (ids/options from INFO)
//   0x04 ACTION   u8 action          see USBP_ACT_*
//   0x05 STREAMS  u8 flags           bit0 scope frames, bit1 meter readings
//   0x06 DEEP     u32 samples        start a deep capture (scope mode)
//   0x07 LOG      u8 on              continuous logging stream (<= 50 kS/s)
//
// Device -> host
//   0x81 INFO     JSON text: firmware/protocol versions, limits, settings
//   0x82 SETTINGS u8 n, u8 index[n], u16 visible mask, u16 enabled mask
//   0x83 STATUS   u8 mode, u8 acq_state, u8 flags, u32 overruns,
//                 u8 deep_state, u32 deep_progress, u32 log_dropped
//                 flags: bit0 menu open, bit1 low battery, bit2 logging,
//                        bit3 logging possible at this rate
//   0x84 FRAME    u32 seq, u32 rate_hz, u8 flags, u16 n, i32 c1[n], i32 c2[n]
//                 (microvolts; flags: bit0 triggered, bit1 has C2, bit2 roll,
//                 bit3 differential)
//   0x85 METER    u8 mode, then mode specific (see meters_report())
//   0x86 DEEP_HDR u32 n, u32 rate_hz, u32 pre, u8 flags, u32 lsb_uv
//   0x87 DEEP_DAT u32 offset, u16 n, i16 c1[n], i16 c2[n]
//   0x88 DEEP_END
//   0x89 LOG_DAT  u32 index, u16 n, i16 (c1, c2)[n]   (lsb_uv units)
#pragma once

#include <stdbool.h>

enum {
    USBP_ACT_REARM = 1,
    USBP_ACT_FORCE = 2,
    USBP_ACT_RESET_MINMAX = 3,
    USBP_ACT_ZERO = 4,
    USBP_ACT_CAP_MEASURE = 5,
    USBP_ACT_DEEP_CANCEL = 6,
    USBP_ACT_BOOTSEL = 0x7E,
};

void usbp_init(void);
// Call frequently from the main loop: handles input and queues/sends data.
void usbp_service(void);
bool usbp_host_active(void);

// Provided by main.c
void app_set_setting(int id, int index);
bool app_menu_open(void);
bool app_low_battery(void);
