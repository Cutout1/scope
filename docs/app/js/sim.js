// Simulated Scopocket for demo mode and tests. It speaks the same binary
// protocol as the firmware (through SPProtocol), so the app can't tell the
// difference. Signals: C1 = the waveform generator output (as if looped
// back), C2 = the same through an RC low-pass, plus a little noise.
(function (root, factory) {
  const m = factory(root);
  if (typeof module === 'object' && module.exports) module.exports = m;
  else root.SPSim = m;
})(typeof self !== 'undefined' ? self : this, function (root) {
  'use strict';
  const P = typeof module === 'object' && module.exports ? require('./protocol.js') : root.SPProtocol;
  const INFO = typeof module === 'object' && module.exports ? require('./device_info.js') : root.SP_DEVICE_INFO;

  const ID = {};
  INFO.settings.forEach((s) => (ID[s.key] = s.id));
  const DEFAULTS = { mode: 0, rate: 0, vdiv: 3, display: 0, capture: 0, trigger: 0, trig_level: 30,
    measure: 0, wave: 0, wave_amp: 6, dc_level: 33, wave_freq: 0 };
  const LSB = INFO.deep_lsb_uv * 1e-6;
  const PRE = INFO.pretrigger, POINTS = INFO.points;

  function gauss() {
    let u = 0, v = 0;
    while (u === 0) u = Math.random();
    while (v === 0) v = Math.random();
    return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * v);
  }

  class SimDevice {
    constructor(opts = {}) {
      this.noise = opts.noise ?? 0.004;
      this.realtime = opts.realtime ?? true; // false: tests drive tick() manually
      this.values = INFO.settings.map((s) => DEFAULTS[s.key] ?? 0);
      this.parser = new P.Parser((t, p) => this.onHost(P.decodeHost(t, p)));
      this.onData = null;
      this.t = 0; // simulated seconds
      this.streams = { frames: true, meters: true };
      this.single = { armed: true };
      this.deep = null;
      this.log = null;
      this.frameDue = 0;
      this.seq = 0;
      this.meter = { vmin: [Infinity, Infinity], vmax: [-Infinity, -Infinity], rel: false, relOffset: 0 };
      this.cap = { state: 'idle', c: 0, rel: false, relOffset: 0, liveV: 0.02, until: 0, haveResult: false };
      this.forceNext = false;
      this.statusDue = 0;
      this.meterDue = 0;
      this.rebuildWave();
    }

    // ---- transport interface -------------------------------------------
    open(onData) {
      this.onData = onData;
      if (this.realtime) {
        this.last = performance.now();
        this.timer = setInterval(() => {
          const now = performance.now();
          this.tick((now - this.last) / 1000);
          this.last = now;
        }, 20);
      }
    }
    close() {
      clearInterval(this.timer);
      this.onData = null;
    }
    write(bytes) {
      this.parser.push(bytes);
    }
    send(bytes) {
      if (this.onData) this.onData(bytes);
    }

    // ---- settings ---------------------------------------------------------
    v(key) { return this.values[ID[key]]; }
    num(key) { return INFO.settings[ID[key]].values[this.v(key)]; }
    get rate() { return this.num('rate'); }
    get mode() { return this.v('mode'); }

    masks() {
      const scope = this.mode === 0, dc = this.v('wave') === 4, roll = this.v('capture') === 2;
      const vis = INFO.settings.map((s) => {
        if (['rate', 'vdiv', 'display', 'capture', 'trigger', 'trig_level', 'measure'].includes(s.key)) return scope;
        if (s.key === 'wave_amp' || s.key === 'wave_freq') return !dc;
        if (s.key === 'dc_level') return dc;
        return true;
      });
      const en = INFO.settings.map((s) => {
        if (s.key === 'trigger') return !roll;
        if (s.key === 'trig_level') return !roll && this.v('trigger') <= 1;
        if (s.key === 'measure') return this.v('display') === 0;
        return true;
      });
      const bits = (a) => a.reduce((m, b, i) => m | (b ? 1 << i : 0), 0);
      return [bits(vis), bits(en)];
    }

    sendSettings() {
      const [vis, en] = this.masks();
      this.send(P.device.settings(this.values, vis, en));
    }

    // ---- signals ----------------------------------------------------------
    rebuildWave() {
      const type = this.v('wave');
      this.amp = this.num('wave_amp') / 1000;
      this.dc = this.num('dc_level') / 1000;
      this.freq = type === 4 ? 0 : this.num('wave_freq') / 1000;
      this.type = type;
      // C2: RC low-pass with its corner at 1.5x the signal frequency.
      // Steady-state response tabulated over one period.
      const N = 2000;
      this.t1 = new Float32Array(N);
      this.t2 = new Float32Array(N);
      for (let i = 0; i < N; i++) this.t1[i] = this.genAt(i / N);
      if (type === 4) {
        this.t2.fill(this.dc);
        return;
      }
      const tau = 1 / (2 * Math.PI * 1.5 * this.freq), dt = 1 / this.freq / N, k = 1 - Math.exp(-dt / tau);
      let y = 0;
      for (let rep = 0; rep < 8; rep++)
        for (let i = 0; i < N; i++) {
          y += (this.t1[i] - y) * k;
          if (rep === 7) this.t2[i] = y;
        }
    }

    genAt(ph) {
      const a = this.amp;
      switch (this.type) {
        case 1: return ph < 0.5 ? a : 0;
        case 2: return a * (ph < 0.25 ? 4 * ph : ph < 0.75 ? 2 - 4 * ph : 4 * ph - 4);
        case 3: return a * (2 * ph - 1);
        case 4: return this.dc;
        default: return a * Math.sin(2 * Math.PI * ph);
      }
    }

    sample(t) {
      const ph = this.freq ? (((t * this.freq) % 1) + 1) % 1 : 0;
      const i = Math.floor(ph * this.t1.length) % this.t1.length;
      return [this.t1[i] + this.noise * gauss(), this.t2[i] + this.noise * gauss()];
    }

    // Next trigger time after t0 (null = never), following the device rules.
    triggerAfter(t0) {
      const trig = this.v('trigger');
      if (trig === 2) return null; // switch
      if (!this.freq) return null;
      const T = 1 / this.freq;
      if (trig === 3) return Math.ceil(t0 * this.freq) * T; // phase 0 of the generator
      const level = this.num('trig_level') / 1e6, rising = trig === 0;
      const N = this.t1.length;
      let lo = Infinity, hi = -Infinity;
      for (const x of this.t1) { lo = Math.min(lo, x); hi = Math.max(hi, x); }
      if (level <= lo || level >= hi) return null;
      for (let i = 0; i < N; i++) {
        const a = this.t1[i], b = this.t1[(i + 1) % N];
        if (rising ? a < level && b >= level : a > level && b <= level) {
          const ph = (i + 1) / N;
          const base = Math.floor(t0 * this.freq) * T;
          let t = base + ph * T;
          if (t < t0) t += T;
          return t;
        }
      }
      return null;
    }

    record(tStart, n) {
      const rate = this.rate, hasC2 = rate <= 1000000;
      const c1 = new Float32Array(n), c2 = new Float32Array(n);
      for (let i = 0; i < n; i++) {
        const [a, b] = this.sample(tStart + i / rate);
        c1[i] = a;
        c2[i] = hasC2 ? b : 0;
      }
      return { c1, c2, hasC2, diff: rate <= 500000 };
    }

    // ---- host commands ----------------------------------------------------
    onHost(m) {
      switch (m.kind) {
        case 'hello':
          this.send(P.device.info(INFO));
          this.sendSettings();
          this.sendStatus();
          break;
        case 'set': {
          const s = INFO.settings[m.id];
          if (!s || m.index >= s.options.length) break;
          this.values[m.id] = m.index;
          if (['wave', 'wave_amp', 'dc_level', 'wave_freq'].includes(s.key)) this.rebuildWave();
          if (['rate', 'capture', 'trigger', 'trig_level', 'mode'].includes(s.key)) {
            this.single.armed = true;
            this.deep = null;
            if (s.key === 'rate' && this.log && this.rate > INFO.log_max_rate) this.log = null;
          }
          if (s.key === 'mode') this.log = null;
          this.sendSettings();
          break;
        }
        case 'action':
          this.onAction(m.action);
          break;
        case 'streams':
          this.streams = m;
          break;
        case 'deep':
          if (this.mode === 0 && !this.deep) {
            const n = Math.max(256, Math.min(INFO.deep_max, m.n));
            this.deep = { n, state: 'armed', t0: null };
          }
          break;
        case 'log':
          if (m.on && this.mode === 0 && this.rate <= INFO.log_max_rate)
            this.log = { index: 0, frac: 0, t: this.t };
          else this.log = null;
          break;
      }
    }

    onAction(a) {
      const A = P.ACT;
      if (a === A.REARM) this.single.armed = true;
      else if (a === A.FORCE) {
        this.forceNext = true;
        this.single.armed = true;
      } else if (a === A.RESET_MINMAX) {
        this.meter.vmin = [Infinity, Infinity];
        this.meter.vmax = [-Infinity, -Infinity];
      } else if (a === A.ZERO) {
        if (this.mode === 2) {
          this.meter.rel = !this.meter.rel;
          this.meter.relOffset = this.meter.rel ? this.ohms() : 0;
        } else if (this.mode === 3 && this.cap.haveResult) {
          this.cap.rel = !this.cap.rel;
          this.cap.relOffset = this.cap.rel ? this.cap.c : 0;
        }
      } else if (a === A.CAP_MEASURE && this.mode === 3) {
        if (this.cap.liveV > 0.5) {
          this.cap.state = 'error';
          this.cap.error = 'Discharge to <0.5V first';
        } else {
          this.cap.state = 'measuring';
          this.cap.until = this.t + 0.3;
        }
      } else if (a === A.DEEP_CANCEL) this.deep = null;
    }

    ohms() { return 4700 * (1 + 0.0005 * gauss()); }

    // ---- periodic output ------------------------------------------------
    tick(dt) {
      this.t += dt;
      if (this.mode === 0) this.tickScope();
      else this.tickMeter();
      if (this.t >= this.statusDue) {
        this.statusDue = this.t + 0.25;
        this.sendStatus();
      }
    }

    acqState() {
      if (this.v('capture') === 2) return 'rolling';
      if (this.deep && this.deep.state === 'capturing') return 'capturing';
      if (this.v('capture') === 1 && !this.single.armed) return 'stopped';
      return 'waiting';
    }

    sendStatus() {
      this.send(P.device.status({
        modeIndex: this.mode, acqState: this.acqState(), menuOpen: false, lowBattery: false,
        logging: !!this.log, logPossible: this.mode === 0 && this.rate <= INFO.log_max_rate,
        overruns: 0, deepState: this.deep ? this.deep.state : 'idle',
        deepProgress: this.deep && this.deep.state === 'capturing'
          ? Math.min(this.deep.n, Math.floor((this.t - this.deep.t0) * this.rate)) : 0,
        logDropped: 0,
      }));
    }

    tickScope() {
      const rate = this.rate;
      // Deep capture runs in (simulated) real time once triggered.
      if (this.deep) {
        const d = this.deep;
        if (d.state === 'armed') {
          const roll = this.v('capture') === 2;
          let tt = roll || this.forceNext ? this.t : this.triggerAfter(this.t);
          if (tt === null && this.v('capture') === 0 && this.v('trigger') <= 1) tt = this.t; // auto
          this.forceNext = false;
          if (tt !== null) {
            d.pre = Math.min(200, Math.floor(d.n / 8));
            d.t0 = tt;
            d.triggered = !roll;
            d.state = 'capturing';
          }
        } else if (d.state === 'capturing' && this.t >= d.t0 + d.n / rate) {
          const rec = this.record(d.t0 - d.pre / rate, d.n);
          this.send(P.device.deepHdr({ count: d.n, rate, pre: d.pre, triggered: d.triggered,
            hasC2: rec.hasC2, diff: rec.diff, lsbUv: INFO.deep_lsb_uv }));
          for (let o = 0; o < d.n; o += 256) {
            const e = Math.min(d.n, o + 256);
            this.send(P.device.deepData(o, rec.c1.subarray(o, e).map((x) => x / LSB),
              rec.c2.subarray(o, e).map((x) => x / LSB)));
          }
          this.send(P.device.deepEnd());
          this.deep = null;
          if (this.v('capture') === 1) this.single.armed = false;
        }
      }
      // Logging stream
      if (this.log) {
        const L = this.log;
        let want = rate * (this.t - L.t) - L.index;
        while (want >= 1) {
          const n = Math.min(256, Math.floor(want));
          const rec = this.record(L.t + L.index / rate, n);
          this.send(P.device.logData(L.index, rec.c1.map((x) => x / LSB), rec.c2.map((x) => x / LSB)));
          L.index += n;
          want -= n;
        }
      }
      // Display frames
      if (!this.streams.frames || this.t < this.frameDue) return;
      if (this.deep && this.deep.state === 'capturing') return;
      const cap = this.v('capture');
      // Like the firmware, frames at 200 S/s and below are sent while they
      // fill, starting with the pre-trigger samples.
      if (rate <= 200 && cap !== 2) {
        this.frameDue = this.t + 0.04;
        if (!this.prog) {
          if (cap === 1 && !this.single.armed) return;
          let tt = this.forceNext ? this.t : this.triggerAfter(this.t);
          const triggered = !this.forceNext && tt !== null;
          if (tt === null) {
            if (cap !== 0 || this.v('trigger') > 1) return; // no auto
            tt = this.t;
          }
          this.forceNext = false;
          this.prog = { tStart: tt - PRE / rate, triggered };
        }
        const pg = this.prog;
        const n = Math.min(POINTS, Math.floor((this.t - pg.tStart) * rate) + 1);
        if (n <= PRE) return; // trigger point not reached yet
        const rec = this.record(pg.tStart, n);
        this.send(P.device.frame({ seq: ++this.seq, rate, c1: rec.c1, c2: rec.c2, triggered: pg.triggered,
          hasC2: rec.hasC2, roll: false, diff: rec.diff }));
        if (n === POINTS) {
          this.prog = null;
          if (cap === 1) this.single.armed = false;
        }
        return;
      }
      this.prog = null;
      const window = POINTS / rate;
      this.frameDue = this.t + Math.max(0.04, window);
      let tStart, triggered = false, roll = false;
      if (cap === 2) {
        roll = true;
        tStart = this.t - (POINTS - 1) / rate;
      } else {
        if (cap === 1 && !this.single.armed) return;
        let tt = this.forceNext ? null : this.triggerAfter(this.t - 1 / Math.max(this.freq, 1));
        if (tt !== null) triggered = true;
        else if (!this.forceNext && (cap !== 0 || this.v('trigger') > 1)) return; // no auto
        this.forceNext = false;
        if (tt === null) tt = this.t;
        tStart = tt - PRE / rate;
        if (cap === 1) this.single.armed = false;
      }
      const rec = this.record(tStart, POINTS);
      this.send(P.device.frame({ seq: ++this.seq, rate, c1: rec.c1, c2: rec.c2, triggered,
        hasC2: rec.hasC2, roll, diff: rec.diff }));
    }

    tickMeter() {
      if (!this.streams.meters || this.t < this.meterDue) return;
      this.meterDue = this.t + 0.1;
      if (this.mode === 1) {
        // Average over whole periods of the generator signal.
        let s1 = 0, s2 = 0;
        for (let i = 0; i < this.t1.length; i++) { s1 += this.t1[i]; s2 += this.t2[i]; }
        const v = [s1 / this.t1.length + 0.0005 * gauss(), s2 / this.t2.length + 0.0005 * gauss()];
        const M = this.meter;
        for (let c = 0; c < 2; c++) { M.vmin[c] = Math.min(M.vmin[c], v[c]); M.vmax[c] = Math.max(M.vmax[c], v[c]); }
        this.send(P.device.meter({ mode: 'volt', have: true, over: [false, false], haveMinMax: true,
          value: v, min: M.vmin, max: M.vmax }));
      } else if (this.mode === 2) {
        this.send(P.device.meter({ mode: 'ohm', have: true, open: false, rel: this.meter.rel, range: 1,
          r: this.ohms(), relOffset: this.meter.relOffset }));
      } else {
        const C = this.cap;
        if (C.state === 'measuring' && this.t >= C.until) {
          C.state = 'result';
          C.c = 10e-9 * (1 + 0.003 * gauss());
          C.haveResult = true;
          C.liveV = 3.2;
        }
        if (C.state !== 'measuring') C.liveV = Math.max(0.01, C.liveV * Math.exp(-0.1 / 0.1));
        this.send(P.device.meter({ mode: 'cap', state: C.state, haveResult: C.haveResult, rel: C.rel,
          haveLive: true, c: C.c, relOffset: C.relOffset, liveV: C.liveV, error: C.state === 'error' ? C.error : '' }));
      }
    }
  }

  return { SimDevice, INFO };
});
