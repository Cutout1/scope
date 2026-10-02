---
id: companion-app
title: Companion App
---

The Scopocket Companion app runs in your web browser and connects to the Scopocket over its USB-C cable. It mirrors and controls every setting on the device, and adds a larger live display along with tools that don't fit on the Scopocket's small screen.

**[Open the Scopocket Companion](app/)**

## What it does

- **Live oscilloscope view.** Zoom and pan, drag the trigger level, use two time cursors, and pause the display.
- **Automatic measurements.** Frequency, period, peak-to-peak voltage, minimum, maximum, mean, RMS and duty cycle for both channels.
- **Spectrum analyzer (FFT)** of the signal on screen.
- **Long captures.** Up to 24,000 samples per channel, much more than fits on the device screen, ready to zoom into, measure and analyze.
- **Data logging.** Record every sample continuously for as long as you need, at sample rates up to 50 kS/s.
- **Meters.** The voltmeter, ohmmeter and capacitance meter, with trend charts and a history of readings.
- **Export.** Save any capture or log as a CSV file for a spreadsheet or Python, or save a picture of the display.
- **Waveform generator controls** and every other device setting. Changes made with the device's buttons show up in the app, and the other way around.
- **Firmware updates.** One button restarts the Scopocket into update mode.

## Requirements

- **Browser:** Google Chrome or Microsoft Edge on Windows, macOS, Linux or ChromeOS. The app uses Web Serial, which Firefox and Safari don't support.
- **Firmware:** the Scopocket must run the current firmware, [`advanced_scopocket_firmware.uf2`](https://github.com/Cutout1/scope/raw/main/Firmware/advanced_scopocket_firmware.uf2), from the Firmware folder of the GitHub repository. To install it:
  1. Put the Scopocket in update mode. Either hold the BOOTSEL connection while switching it on, or use the app's *Firmware update mode…* button if it already runs this firmware.
  2. The Scopocket appears as a USB drive called RPI-RP2. Copy the file onto it.
- **Demo mode:** you can try the app without a Scopocket. **Demo** in the top bar runs a simulated one.

## Connecting

1. Plug the Scopocket into the computer with a USB-C data cable and switch it on.
2. Open the app and click **Connect**.
3. Choose the Scopocket from the list.

On Linux, if connecting fails with "Failed to open serial port", your account needs permission to use serial ports. Add it to the `dialout` group (Debian, Ubuntu, Fedora) or the `uucp` group (Arch), then log out and back in.
