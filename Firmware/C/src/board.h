// Scopocket v1.3 pin map (from the v1.3 netlist, confirmed against the
// MicroPython firmware and hardware testing).
#pragma once

// Buttons (active low, external 10k pull-ups)
#define PIN_BTN_LEFT   0
#define PIN_BTN_RIGHT  1
#define PIN_BTN_MODE   2
#define PIN_BTN_SELECT 3

// Thevenin sources into C1+ (P-FET, active low): 1k, 10k, 100k, 1M
#define PIN_THEV_1K   4
#define PIN_THEV_10K  5
#define PIN_THEV_100K 6
#define PIN_THEV_1M   7

// Trigger switch (active low). Firmware assignment, verified on hardware;
// the netlist labels these the other way round.
#define PIN_SWITCH_READY 8
#define PIN_SWITCH_START 9

// ST7735 display on SPI1
#define PIN_TFT_DC   10
#define PIN_TFT_RST  11
#define PIN_TFT_CS   13
#define PIN_TFT_SCK  14
#define PIN_TFT_MOSI 15

// Waveform generator PWM output (slice 2, channel A)
#define PIN_WAVE 20

// Low battery comparator (low = battery low)
#define PIN_LOW_BATT 21

// 10 MOhm input bias returns, normally driven low
#define PIN_BIAS_22 22
#define PIN_BIAS_23 23
#define PIN_BIAS_24 24
#define PIN_BIAS_25 25

// ADC channels (GPIO26..29)
#define ADC_CH_C2    0
#define ADC_CH_C2REF 1
#define ADC_CH_C1    2
#define ADC_CH_C1REF 3

#define SYS_CLOCK_HZ 192000000u
