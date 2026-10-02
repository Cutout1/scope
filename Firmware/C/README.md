# Scopocket firmware (C rewrite, hardware v1.3)

This is a new firmware written from scratch. It has four modes: an oscilloscope (100 S/s to 2 MS/s),
a voltmeter, an ohmmeter and a capacitance meter. All four share a configurable waveform generator
and a settings menu.

**Build:** run `./build.sh`, which produces `scopocket.uf2`. The first build downloads the toolchain (ARM GCC,
CMake, pico-sdk with TinyUSB, picotool) into `./toolchain`, which git ignores. To reuse a toolchain you already
have, make `./toolchain` a symlink to it instead.

## Controls
| Control | Scope screen | Settings menu |
|---|---|---|
| MODE | open settings | close and apply |
| LEFT / RIGHT | move the cursor (hold to move 5 px at a time) | change the value (hold to repeat) |
| SELECT | re-arm (Single mode) | next setting (closes after the last one) |
| Trigger switch READY→START | fires the trigger when Trigger = Switch | |
| MODE held at power-up | recalibrate | |

## Settings
| Setting | Options | Notes |
|---|---|---|
| Mode | Scope, Volt, Ohm, Cap | The menu only shows the settings that apply to the current mode, and scrolls when the list is long. |
| Sample rate | 100, 200, 500 S/s, 1k ... 500k, 1M, 2M S/s | Up to 500k: C1 = C1+ − C1−, C2 = C2+ − C2−. 1M: C1+ and C2+ only. 2M: C1+ only. At 1M/2M, readings are relative to GND, marked `*` in the top bar. |
| Volts/div | 100 mV, 200 mV, 500 mV, 1 V, 2 V | 16 px per division, 0 V at the centre line |
| Display | Normal, XY | XY plots C1 (X) against C2 (Y), with the same V/div on both axes. |
| Capture | Repeat, Single, Roll | **Repeat:** keeps capturing. With no trigger it free-runs (auto) every 100 ms or one screen width. Once real triggers are seen, it waits up to 1.2 s for the next one, so signals down to 1 Hz stay triggered at any sample rate. **Single:** one triggered capture, then STOP; press SELECT to re-arm. **Roll:** scrolling and untriggered; newest sample on the right. |
| Trigger | Rising, Falling, Switch, Wave Out | Rising and Falling are edge triggers on C1. Switch fires on the READY→START movement, like the Simple firmware. Wave Out fires at phase 0 of the generator's output, read from its DMA counter, so captures are stable without wiring anything to a trigger input. Switch and Wave never auto-trigger. |
| Trig level | −3.0 V … +3.0 V in 0.1 V steps | Shown as a dotted yellow line. Hysteresis is 0.1 div, minimum 20 mV. |
| Measure | Off, On | One line per channel above the bottom bar: frequency, Vpp and Vrms. Frequency comes from midpoint crossings (10% hysteresis, interpolated between samples). Vrms is taken over whole periods when at least two are captured. |
| Wave out | Sine, Square, Triangle, Sawtooth, DC | Square goes from 0 V to +A, like the original firmware. The others swing ±A. |
| Wave amp | 0.5 – 3.3 V peak | Not shown for DC. |
| DC level | −3.3 … +3.3 V in 0.1 V steps | DC only. Output resolution is about 26 mV (8-bit PWM), and the level isn't calibrated. |
| Wave freq | 1, 2, 5, 10, 20, 50, 100 Hz | Up to 6000 samples per period, paced at up to 60 kS/s. The board's output filter attenuates the higher frequencies somewhat. |

**Screen layout**
- **Top bar:** cursor time (from the trigger, or from the newest sample in Roll), then the C1 and C2 readings.
- **Bottom bar:** sample rate, V/div, time/div (20 samples), and state:
  - `TRIG` / `AUTO`: triggered, or captured by the auto trigger
  - `ARMED`: waiting for a trigger
  - `STOP`: Single capture complete
  - `ROLL`: Roll mode
  - `OVR` (red): core 1 couldn't keep up and skipped data. This should not appear.

## Voltmeter
- **Readings:** C1 = C1+ − C1− and C2 = C2+ − C2−, to 1 mV. Each value is a 200 ms sliding
  average of two 100 ms windows. 100 ms is a whole number of 50 Hz and 60 Hz cycles, so mains hum
  cancels out.
- **Min/max:** shown for each channel. SELECT resets them.
- **OVER RANGE:** shown when either input saturates the analog front end.

## Ohmmeter
- **Connection:** the resistor goes between C1+ and GND, as in the Simple firmware.
- **How it measures:** one of four P-FET sources drives C1+ from 3.3 V through 1k, 10k, 100k or 1M, and
  R = Rs·V/(3.3 − V). Calibration defines 3.3 V as the board's own rail, so the result is
  ratiometric. Readings use the 48 MHz calibration. The 10 MΩ bias on C1+ (GPIO24) is floated while measuring.
- **Changes from the Simple firmware:**
  - Only the active range is measured, about 9 readings per second instead of four ranges every 500 ms.
  - The range changes when R leaves Rs/4 … 4·Rs, jumping straight to the best range. The overlap
    stops it flipping back and forth between ranges.
  - It starts on the 1M range, which uses the least current. The active range is shown in the top right.
  - Readings are averaged while stable. A real change restarts the average immediately.
  - Above 25 MΩ it shows `OPEN`.
  - Below 10 Ω it shows a green `CONTINUITY` bar.
  - Select toggles Zero, which subtracts the current reading to null out lead resistance.

## Capacitance meter
- **How to measure:** connect the capacitor between C1+ and GND, make sure it's discharged (short its
  leads), then press **SELECT**. The live C1+ voltage is shown, and a measurement only starts once it is below 0.5 V; above that it turns red. The board can
  charge a capacitor but not discharge it, which is why each measurement is started by hand.
- **Display:** only the capacitance. Results below 0.1 nF show as `<0.1nF`.
- **Method:**
  - The capacitor charges from 3.3 V through a source resistor, while captures fit
    V(t) = Vinf − A·e^(−t/τ).
  - τ comes from a golden-section search over the full exponential fit, seeded by the rise ratio
    of the window's two halves. The fit doesn't depend on the starting voltage.
  - Resistive leakage to GND (the 10 MΩ bias, input protection, the capacitor's own leakage)
    lowers Vinf and the effective source resistance together, so the meter computes
    R_eff = Rs·Vinf/3.3 and C = τ/R_eff. This corrects for leakage automatically.
- **Search:**
  - It starts with the 1M source at 50 kS/s.
  - While the curve is too slow it moves to slower sample rates, at most two steps per capture,
    then to the next smaller source (100k, 10k, 1k).
  - The source stays on throughout, so the charge continues from capture to capture.
- **Range:** about **0.1 nF to 1000 µF**. Below roughly 1 nF, stray capacitance dominates.
  - In simulation (0.5–3 mV noise, 3 MΩ – 1 GΩ leakage) the fit was within 1.5% everywhere, using at most 8 captures.
  - Real accuracy depends on the source resistors (1%) and stray capacitance.
- **Zero:** press Right to subtract the last result. Measure with nothing connected first; that reading is the stray capacitance.

## USB link and companion app
Over USB the Scopocket appears as a serial port (CDC, VID `2E8A`). The companion app in
`../../docs/app` (a Web Serial page for Chrome or Edge, published at https://scopocket.com/app/) controls it and receives data:
- **Settings:** live control of every setting, kept in sync with the device's own buttons and menu.
- **Scope:** live display frames at up to 50 per second.
- **Meters:** readings at 10 per second.
- **Long capture:** up to 24,000 samples per channel at any rate (`ACQ_DEEP_MAX`), from a 96 KB buffer
  that core 1 fills after a trigger.
- **Logging:** a continuous stream of every sample at rates up to 50 kS/s.
- **Remote actions:** re-arm, force trigger, meter zero and reset, start a capacitance measurement,
  and reboot into the bootloader for firmware updates.
- **Bootloader shortcut:** opening the port at 1200 baud (the Arduino/Pico convention) also reboots
  into the bootloader. `picotool` can reboot it too.

The protocol is documented at the top of `src/usbproto.h`. The device sends a JSON description of its
settings (`setting_items[]` in `settings.c`), so new settings appear in the app automatically.
`tests/info_json.c` prints that JSON on the host. The USB stack runs on core 0 and doesn't affect
acquisition.

## Architecture
- **Core 1 (`acquisition.c`)**
  - **ADC clock:**
    - Up to 100 kS/s, and in the meters, `clk_adc` runs from `pll_usb` at 48 MHz. That is the
      rated maximum: up to 500k conversions/s across the four inputs.
    - Above 100 kS/s it switches to `pll_sys` at 192 MHz, which is out of spec. That gives
      2M conversions/s, split across 4, 2 or 1 inputs depending on the sample rate.
    - Core 1 switches the clock only while the ADC is stopped.
  - Two chained DMA channels stream the ADC FIFO into a 16 KB ring buffer, wrapping in hardware.
    The buffer never stops.
  - Core 1 sums each run of `oversample` raw sets into one output sample. A boxcar average over
    the whole sample period is a free anti-aliasing filter. At 100 S/s that is 1250 readings
    per sample.
  - The averages are converted to microvolts in fixed point using the calibration.
  - The 1M and 2M paths work differently. Trigger detection compares raw ADC counts against a
    threshold converted once from the calibration. Each frame is converted to volts when it
    completes. This keeps the 2 MS/s loop to roughly 70–80 of its 96 available cycles per sample.
  - If core 1 ever falls behind, it skips ahead and the screen shows `OVR`.
  - It then runs the trigger state machine and fills triple-buffered frames.
  - The hot path is RAM-resident.
- **Core 0 (`main.c`)**
  - Handles buttons, the menu, and rendering into a 160×128 RAM framebuffer (`gfx.c`).
  - DMA sends the framebuffer to the ST7735 at 24 MHz, up to 50 fps.
  - At 200 S/s and below, frames are sent while they fill and drawn as a sweep over the previous
    frame, like an analog scope. Measurements use the last complete frame.
- **ADC timing:** every rate in the menu divides evenly into its ADC clock, so the conversion
  period (96–120 cycles) and the oversample count are exact (`acq_timing_for_rate`).
  - A 96-cycle period uses the ADC's back-to-back mode (DIV = 0), not DIV = 95.
  - The ADC ignores a timed start that lands while a conversion is still finishing
    (datasheet §4.9.2.2), so DIV = 95 silently halves the rate.
  Every other rate runs the ADC at the full 2 MS/s.
- **Calibration:**
  - Each step (GND, then 3.3 V) is measured at both ADC clocks. The 48 MHz and 192 MHz sets
    are stored separately (flash record v2), and each clock uses its own.
  - Records from earlier builds, including the first C port of the Simple firmware, are v1. Booting this firmware
    with one stored asks for calibration once.

| File | Purpose |
|---|---|
| `src/acquisition.*` | ADC/DMA streaming, decimation, trigger, frames (core 1) |
| `src/display.*`, `src/gfx.*` | ST7735 framebuffer + DMA flush, drawing primitives |
| `src/scope_view.*` | Scope screen |
| `src/meters.*` | Voltmeter and ohmmeter, mode dispatch |
| `src/capmeter.*` | Capacitance meter |
| `src/menu.*`, `src/settings.*` | Settings menu and setting definitions |
| `src/buttons.*` | Debounce, auto-repeat, trigger switch |
| `src/calibrate.*`, `src/calibration.*` | Calibration UI and flash storage |
| `src/usbproto.*` | USB protocol: packet framing, settings sync, frame/meter/deep/log streaming |
| `src/wavegen.*` | Waveform tables (3 kS/s; length sets the frequency), PWM DAC on GPIO20 fed by DMA |
