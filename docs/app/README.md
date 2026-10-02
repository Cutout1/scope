# Scopocket Companion

This is a browser app for the Scopocket (hardware v1.3, running the C firmware in `Firmware/C/`, version 1.0 or later). It talks to the
device over USB with **Web Serial**, so nothing needs installing. It works in Chrome or Edge on
Windows, macOS, Linux and ChromeOS. Firefox and Safari don't support Web Serial, but **Demo** mode
(a simulated Scopocket) works in any browser.

## Running it
Web Serial only works on secure pages: `https://…` or `http://localhost`. Pick one:

- **Locally:** run `python3 -m http.server 8000` in this folder, then open http://localhost:8000.
- **Hosted:** copy the folder to any static host. For example, put it in `docs/app/` of the Scopocket
  repository so GitHub Pages serves it at `https://scopocket.com/app/`.

Opening `index.html` straight from disk works for Demo mode. Whether Web Serial is allowed on `file://`
varies by browser version.

Plug in the Scopocket, switch it on, click **Connect** and pick it from the list. Its USB ID is the Raspberry Pi
vendor ID `2E8A`.

## Troubleshooting
**"Failed to open serial port" on Linux.** The Scopocket appears in the list, but the browser isn't
allowed to open it. Serial ports belong to the `dialout` group (Debian, Ubuntu, Fedora) or the `uucp`
group (Arch and derivatives). Use either fix:
- Add yourself to the group, then log out and back in:
  `sudo usermod -aG dialout $USER` (or `uucp`).
- Or give the logged-in user access to the Scopocket only, with no re-login needed:
  ```sh
  echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", TAG+="uaccess"' |
    sudo tee /etc/udev/rules.d/60-scopocket.rules
  sudo udevadm control --reload && sudo udevadm trigger
  ```
  Then replug the Scopocket.

**"Failed to open serial port" on other systems.** Another program has the port open: a serial
monitor, Thonny, the Arduino IDE, or the app in another tab. Close it and try again.

## Features
- **Live scope**
  - **Display:** large live display of every frame, with the trigger point and level marked. Drag the
    level line to change the trigger level.
  - **Navigation:** scroll to zoom, drag to pan, double-click to reset.
  - **Measurements:** frequency, period, Vpp, min, max, mean, RMS, AC RMS and duty cycle for both
    channels, over the visible part of the record.
  - **Cursors:** two time cursors with Δt, 1/Δt and the voltage of each channel at each cursor.
  - **Spectrum (FFT):** Hann, flat-top or rectangular window, linear or log frequency axis, with
    peak frequency and level.
  - **XY display**, **Pause**, **Force trigger**, and **Re-arm** for Single mode.
  - **Export:** CSV or a PNG image.
- **Long capture:** one triggered record of up to 24,000 samples per channel at any sample rate,
  instead of the 160-point display frames. That's 12 ms at 2 MS/s, or four minutes at 100 S/s.
  Zoom, measure, run an FFT and export, exactly as with live data.
- **Data logging:** a continuous stream of every sample at rates up to 50 kS/s, for as long as you
  like (up to 10 million samples kept in memory). Scrolling strip chart, CSV export.
- **Meters:** voltmeter (with min/max and a trend chart), ohmmeter (zero leads, continuity, trend
  chart), and capacitance meter (measure, zero, a history of readings).
- **Settings:** every device setting appears in the sidebar and stays in sync both ways. Changes made
  with the device's own buttons show up in the app.
- **Firmware updates:** *Firmware update mode…* reboots the Scopocket into its USB bootloader, so a new
  `scopocket.uf2` can be copied onto it without pressing BOOTSEL.

## Code layout
| File | Purpose |
|---|---|
| `js/protocol.js` | Packet framing, CRC-8, encoders and decoders, shared with the firmware's `usbproto.h` |
| `js/device.js` | Web Serial and simulator transports; `Device` keeps info, settings and status, and emits data events |
| `js/sim.js` | Simulated Scopocket speaking the real protocol (generator looped into C1, RC filter on C2) |
| `js/device_info.js` | The firmware's settings description, used by the simulator (`tools/update_device_info.sh`) |
| `js/dsp.js` | Measurements, FFT and spectrum, min/max decimation, number formatting |
| `js/plot.js` | Canvas plots: scope (YT and XY), spectrum, strip chart |
| `js/app.js` | UI wiring |

The device describes its own settings in its INFO packet (names, option labels, numeric values), and the
sidebar is built from that. Adding a setting to the firmware's `setting_items[]` table makes it appear in
the app with no app changes.

## Tests
- `node tests/run_tests.js` runs unit and integration tests: protocol framing and resync after line
  noise, measurements, FFT, and full Device ↔ simulator sessions covering settings, triggering, long
  capture, logging and meters.
- `tests/e2e.js` is an end-to-end UI test that drives the app in Demo mode with Playwright and
  Firefox and takes screenshots:
  ```sh
  npm install playwright && npx playwright install firefox
  python3 -m http.server 8765 &
  node tests/e2e.js shots/
  ```
