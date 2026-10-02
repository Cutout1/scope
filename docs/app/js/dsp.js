// Signal analysis: measurements, FFT, decimation and number formatting.
(function (root, factory) {
  const m = factory();
  if (typeof module === 'object' && module.exports) module.exports = m;
  else root.SPDsp = m;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  // Automatic measurements on a record sampled at `rate`.
  //  - frequency/period from midpoint crossings with 10% hysteresis,
  //    interpolated between samples, averaged over all whole periods
  //  - RMS, mean and duty cycle over whole periods when there are two or
  //    more crossings (so a partial cycle doesn't bias them)
  function measure(v, rate, start = 0, end = v.length) {
    const n = end - start;
    if (n < 2) return null;
    let min = Infinity, max = -Infinity;
    for (let i = start; i < end; i++) {
      if (v[i] < min) min = v[i];
      if (v[i] > max) max = v[i];
    }
    const pp = max - min;
    const mid = (max + min) / 2, hyst = 0.1 * pp;
    const crossings = [];
    if (pp > 0.02) {
      let low = false;
      for (let i = start + 1; i < end; i++) {
        const a = v[i - 1], b = v[i];
        if (b < mid - hyst) low = true;
        else if (low && b >= mid && a < mid) {
          crossings.push(i - 1 + (mid - a) / (b - a));
          low = false;
        }
      }
    }
    let a = start, b = end, period = null, freq = null;
    if (crossings.length >= 2) {
      const first = crossings[0], last = crossings[crossings.length - 1];
      period = (last - first) / (crossings.length - 1) / rate;
      freq = 1 / period;
      a = Math.ceil(first);
      b = Math.ceil(last);
    }
    let sum = 0, sumsq = 0, high = 0;
    for (let i = a; i < b; i++) {
      sum += v[i];
      sumsq += v[i] * v[i];
      if (v[i] >= mid) high++;
    }
    const m = b - a;
    const mean = sum / m;
    const rms = Math.sqrt(sumsq / m);
    const acrms = Math.sqrt(Math.max(0, sumsq / m - mean * mean));
    return { min, max, pp, mean, rms, acrms, freq, period, duty: freq ? high / m : null };
  }

  // In-place iterative radix-2 FFT. re/im lengths must be a power of two.
  function fft(re, im) {
    const n = re.length;
    for (let i = 1, j = 0; i < n; i++) {
      let bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) {
        [re[i], re[j]] = [re[j], re[i]];
        [im[i], im[j]] = [im[j], im[i]];
      }
    }
    for (let len = 2; len <= n; len <<= 1) {
      const ang = (-2 * Math.PI) / len;
      const wr = Math.cos(ang), wi = Math.sin(ang);
      for (let i = 0; i < n; i += len) {
        let cr = 1, ci = 0;
        for (let k = 0; k < len / 2; k++) {
          const a = i + k, b = a + len / 2;
          const tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
          re[b] = re[a] - tr;
          im[b] = im[a] - ti;
          re[a] += tr;
          im[a] += ti;
          const ncr = cr * wr - ci * wi;
          ci = cr * wi + ci * wr;
          cr = ncr;
        }
      }
    }
  }

  const WINDOWS = {
    hann: (k, n) => 0.5 - 0.5 * Math.cos((2 * Math.PI * k) / (n - 1)),
    flattop: (k, n) => {
      const x = (2 * Math.PI * k) / (n - 1);
      return 0.21557895 - 0.41663158 * Math.cos(x) + 0.277263158 * Math.cos(2 * x) -
        0.083578947 * Math.cos(3 * x) + 0.006947368 * Math.cos(4 * x);
    },
    rect: () => 1,
  };

  // Amplitude spectrum in dBV (RMS) of a real record. The DC component is
  // removed first; the record is windowed and zero-padded to a power of two.
  function spectrum(v, rate, opts = {}) {
    const win = WINDOWS[opts.window || 'hann'];
    const n = v.length;
    let mean = 0;
    for (let i = 0; i < n; i++) mean += v[i];
    mean /= n;
    let size = 1;
    while (size < n) size <<= 1;
    size = Math.min(size, 1 << 17);
    const re = new Float64Array(size), im = new Float64Array(size);
    let gain = 0;
    for (let i = 0; i < n && i < size; i++) {
      const w = win(i, n);
      gain += w;
      re[i] = (v[i] - mean) * w;
    }
    fft(re, im);
    const bins = size / 2 + 1;
    const freqs = new Float64Array(bins), dbv = new Float64Array(bins);
    let peak = 1;
    for (let k = 0; k < bins; k++) {
      const mag = Math.hypot(re[k], im[k]);
      // Single-sided amplitude, corrected for the window's coherent gain,
      // expressed as RMS.
      const amp = (k === 0 || k === size / 2 ? mag : 2 * mag) / gain / Math.SQRT2;
      freqs[k] = (k * rate) / size;
      dbv[k] = 20 * Math.log10(Math.max(amp, 1e-9));
      if (k > 0 && dbv[k] > dbv[peak]) peak = k;
    }
    // Parabolic interpolation of the peak frequency.
    let peakFreq = freqs[peak];
    if (peak > 0 && peak < bins - 1) {
      const a = dbv[peak - 1], b = dbv[peak], c = dbv[peak + 1];
      const d = a - 2 * b + c;
      if (d !== 0) peakFreq += (0.5 * (a - c)) / d * (rate / size);
    }
    return { freqs, dbv, peakIndex: peak, peakFreq, peakDbv: dbv[peak], size };
  }

  // Min/max per output column for drawing long records quickly.
  function minmax(v, start, end, buckets) {
    buckets = Math.max(1, Math.floor(buckets) || 1);
    const mins = new Float32Array(buckets), maxs = new Float32Array(buckets);
    const span = (end - start) / buckets;
    for (let b = 0; b < buckets; b++) {
      const a = Math.floor(start + b * span), e = Math.max(a + 1, Math.floor(start + (b + 1) * span));
      let lo = Infinity, hi = -Infinity;
      // Include the previous bucket's last sample so adjacent columns join up.
      for (let i = Math.max(start, a - 1); i < e && i < v.length; i++) {
        if (v[i] < lo) lo = v[i];
        if (v[i] > hi) hi = v[i];
      }
      mins[b] = lo;
      maxs[b] = hi;
    }
    return { mins, maxs };
  }

  // Engineering notation: si(0.0047, 'V') -> "4.70 mV"
  const PREFIXES = [
    [1e9, 'G'], [1e6, 'M'], [1e3, 'k'], [1, ''], [1e-3, 'm'], [1e-6, 'µ'], [1e-9, 'n'], [1e-12, 'p'],
  ];
  function si(value, unit, digits = 3) {
    if (value === null || value === undefined || !isFinite(value)) return '—';
    if (value === 0) return `0 ${unit}`;
    const a = Math.abs(value);
    let [scale, p] = PREFIXES[PREFIXES.length - 1];
    for (const [s, pre] of PREFIXES) {
      if (a >= s * 0.9995) {
        scale = s;
        p = pre;
        break;
      }
    }
    const x = value / scale;
    const ax = Math.abs(x);
    const dec = Math.max(0, digits - (ax >= 100 ? 3 : ax >= 10 ? 2 : 1));
    return `${x.toFixed(dec)} ${p}${unit}`;
  }

  function signedVolts(v, digits = 3) {
    if (v === null || v === undefined || !isFinite(v)) return '—';
    return (v >= 0 ? '+' : '') + v.toFixed(digits) + ' V';
  }

  return { measure, fft, spectrum, minmax, si, signedVolts, WINDOWS };
});
