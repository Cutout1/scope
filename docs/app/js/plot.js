// Canvas plots: scope (YT / XY), spectrum and strip chart.
(function (root) {
  'use strict';
  const DSP = root.SPDsp;

  const C = {
    bg: '#0b0f14',
    grid: '#1f2833',
    gridMajor: '#2c3846',
    axis: '#8593a3',
    text: '#c9d4e0',
    c1: '#ff5a5a',
    c2: '#4ea8ff',
    trig: '#ffd23f',
    cursor: '#3ddc84',
    xy: '#ffd23f',
  };

  // Base: HiDPI canvas that redraws on resize.
  class CanvasView {
    constructor(canvas) {
      this.canvas = canvas;
      this.ctx = canvas.getContext('2d');
      this.margin = { l: 56, r: 30, t: 12, b: 26 };
      this.w = this.h = 0;
      // The observer fires once right away (after subclass construction).
      new ResizeObserver(() => this.resize()).observe(canvas);
    }
    resize() {
      const dpr = window.devicePixelRatio || 1;
      const r = this.canvas.getBoundingClientRect();
      if (r.width < 100 || r.height < 60) {
        this.w = this.h = 0; // hidden or too small: nothing to draw
        return;
      }
      this.w = r.width;
      this.h = r.height;
      this.canvas.width = Math.round(this.w * dpr);
      this.canvas.height = Math.round(this.h * dpr);
      this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      this.draw();
    }
    get pw() { return this.w - this.margin.l - this.margin.r; }
    get ph() { return this.h - this.margin.t - this.margin.b; }
    clear() {
      const g = this.ctx;
      g.fillStyle = C.bg;
      g.fillRect(0, 0, this.w, this.h);
    }
    text(s, x, y, align = 'left', color = C.axis, base = 'middle') {
      const g = this.ctx;
      g.fillStyle = color;
      g.font = '11px ui-monospace, SFMono-Regular, Menlo, Consolas, monospace';
      g.textAlign = align;
      g.textBaseline = base;
      g.fillText(s, x, y);
    }
    draw() {}
    local(ev) {
      const r = this.canvas.getBoundingClientRect();
      return { x: ev.clientX - r.left, y: ev.clientY - r.top };
    }
  }

  const fmtT = (s) => DSP.si(s, 's', 3);

  // -------------------------------------------------------------------------
  // Scope plot. Record: { c1, c2, rate, t0 (time of sample 0, s), hasC2 }.
  // -------------------------------------------------------------------------
  class ScopePlot extends CanvasView {
    constructor(canvas) {
      super(canvas);
      this.rec = null;
      this.vdiv = 1;
      this.view = null; // [tmin, tmax] or null = whole record
      this.cursors = { on: false, a: null, b: null };
      this.trigLevel = null;
      this.showTrigTime = true;
      this.xy = false;
      this.onTrigLevel = null; // callback(volts) when the level is dragged
      this.onCursors = null;
      this.drag = null;
      this.bindEvents();
    }

    setRecord(rec, keepView = false) {
      this.rec = rec;
      if (!keepView) this.view = null;
      this.draw();
    }

    span() {
      if (!this.rec) return [0, 1];
      // Live frames carry their full time span, so a frame that is still
      // filling (slow rates) draws across a fixed axis instead of stretching.
      if (this.rec.span) return this.rec.span;
      const n = this.rec.c1.length;
      return [this.rec.t0, this.rec.t0 + (n - 1) / this.rec.rate];
    }
    range() { return this.view || this.span(); }

    tx(t) { const [a, b] = this.range(); return this.margin.l + ((t - a) / (b - a)) * this.pw; }
    xt(x) { const [a, b] = this.range(); return a + ((x - this.margin.l) / this.pw) * (b - a); }
    vy(v) { return this.margin.t + this.ph / 2 - (v / (this.vdiv * 4)) * (this.ph / 2); }
    yv(y) { return -((y - this.margin.t - this.ph / 2) / (this.ph / 2)) * this.vdiv * 4; }

    draw() {
      if (!this.w) return;
      this.clear();
      if (this.xy) return this.drawXY();
      const g = this.ctx, m = this.margin;
      const [t0, t1] = this.range();
      // Grid: 10 x 8 divisions
      for (let i = 0; i <= 10; i++) {
        const x = m.l + (i / 10) * this.pw;
        g.strokeStyle = i === 0 || i === 10 ? C.gridMajor : C.grid;
        g.beginPath(); g.moveTo(x + 0.5, m.t); g.lineTo(x + 0.5, m.t + this.ph); g.stroke();
        if (i % 2 === 0) this.text(fmtT(t0 + ((t1 - t0) * i) / 10), x, m.t + this.ph + 13, 'center');
      }
      for (let i = 0; i <= 8; i++) {
        const y = m.t + (i / 8) * this.ph;
        g.strokeStyle = i === 4 ? C.gridMajor : C.grid;
        g.beginPath(); g.moveTo(m.l, y + 0.5); g.lineTo(m.l + this.pw, y + 0.5); g.stroke();
        this.text(DSP.si((4 - i) * this.vdiv, 'V', 2), m.l - 6, y, 'right');
      }
      if (!this.rec) {
        this.text('No data', m.l + this.pw / 2, m.t + this.ph / 2, 'center', C.axis);
        return;
      }
      // Trigger time and level markers
      if (this.showTrigTime && t0 <= 0 && t1 >= 0) {
        const x = this.tx(0);
        g.fillStyle = C.trig;
        g.beginPath(); g.moveTo(x - 5, m.t); g.lineTo(x + 5, m.t); g.lineTo(x, m.t + 7); g.fill();
      }
      if (this.trigLevel !== null) {
        const y = this.vy(this.trigLevel);
        if (y >= m.t && y <= m.t + this.ph) {
          g.strokeStyle = C.trig;
          g.setLineDash([4, 4]);
          g.beginPath(); g.moveTo(m.l, y); g.lineTo(m.l + this.pw, y); g.stroke();
          g.setLineDash([]);
          g.fillStyle = C.trig;
          g.beginPath(); g.moveTo(m.l - 8, y - 5); g.lineTo(m.l, y); g.lineTo(m.l - 8, y + 5); g.fill();
        }
      }
      g.save();
      g.beginPath();
      g.rect(m.l, m.t, this.pw, this.ph);
      g.clip();
      if (this.rec.hasC2) this.trace(this.rec.c2, C.c2);
      this.trace(this.rec.c1, C.c1);
      g.restore();
      if (this.cursors.on) this.drawCursors();
    }

    trace(v, color) {
      const g = this.ctx, rec = this.rec;
      const [t0, t1] = this.range();
      const i0 = Math.max(0, Math.floor((t0 - rec.t0) * rec.rate) - 1);
      const i1 = Math.min(v.length, Math.ceil((t1 - rec.t0) * rec.rate) + 2);
      if (i1 - i0 < 2) return;
      g.strokeStyle = color;
      g.lineWidth = 1.3;
      g.beginPath();
      const px = this.pw;
      if (i1 - i0 > px * 2) {
        // Min/max envelope per pixel column
        const cols = Math.floor(px);
        const { mins, maxs } = DSP.minmax(v, i0, i1, cols);
        for (let c = 0; c < cols; c++) {
          if (!isFinite(mins[c])) continue;
          const x = this.margin.l + c + 0.5;
          g.moveTo(x, this.vy(maxs[c]));
          g.lineTo(x, this.vy(mins[c]) + 0.5);
        }
      } else {
        let pen = false; // NaN samples (the sweep gap) break the line
        for (let i = i0; i < i1; i++) {
          if (v[i] !== v[i]) {
            pen = false;
            continue;
          }
          const x = this.tx(rec.t0 + i / rec.rate), y = this.vy(v[i]);
          if (pen) g.lineTo(x, y);
          else g.moveTo(x, y);
          pen = true;
        }
      }
      g.stroke();
      g.lineWidth = 1;
    }

    drawXY() {
      const g = this.ctx, m = this.margin;
      // Square plot area centred, same V/div on both axes
      const side = Math.min(this.pw, this.ph);
      const ox = m.l + (this.pw - side) / 2, oy = m.t + (this.ph - side) / 2;
      const scale = side / (8 * this.vdiv);
      for (let i = 0; i <= 8; i++) {
        g.strokeStyle = i === 4 ? C.gridMajor : C.grid;
        const p = (i / 8) * side;
        g.beginPath(); g.moveTo(ox + p + 0.5, oy); g.lineTo(ox + p + 0.5, oy + side); g.stroke();
        g.beginPath(); g.moveTo(ox, oy + p + 0.5); g.lineTo(ox + side, oy + p + 0.5); g.stroke();
        if (i % 2 === 0) {
          this.text(DSP.si((i - 4) * this.vdiv, 'V', 2), ox + p, oy + side + 13, 'center');
          this.text(DSP.si((4 - i) * this.vdiv, 'V', 2), ox - 6, oy + p, 'right');
        }
      }
      this.text('X = C1', ox + side - 4, oy + 10, 'right', C.c1);
      this.text('Y = C2', ox + 4, oy + 10, 'left', C.c2);
      if (!this.rec || !this.rec.hasC2) {
        this.text(this.rec ? 'XY needs C2 (1 MS/s or less)' : 'No data', ox + side / 2, oy + side / 2, 'center');
        return;
      }
      g.save();
      g.beginPath(); g.rect(ox, oy, side, side); g.clip();
      g.strokeStyle = C.xy;
      g.lineWidth = 1.3;
      g.beginPath();
      const { c1, c2 } = this.rec;
      for (let i = 0; i < c1.length; i++) {
        const x = ox + side / 2 + c1[i] * scale, y = oy + side / 2 - c2[i] * scale;
        if (i === 0) g.moveTo(x, y);
        else g.lineTo(x, y);
      }
      g.stroke();
      g.restore();
      g.lineWidth = 1;
    }

    drawCursors() {
      const g = this.ctx, m = this.margin;
      const [t0, t1] = this.range();
      if (this.cursors.a === null) {
        this.cursors.a = t0 + (t1 - t0) * 0.3;
        this.cursors.b = t0 + (t1 - t0) * 0.7;
        this.onCursors && this.onCursors(this.cursors);
      }
      for (const [k, label] of [['a', 'A'], ['b', 'B']]) {
        const x = this.tx(this.cursors[k]);
        if (x < m.l || x > m.l + this.pw) continue;
        g.strokeStyle = C.cursor;
        g.setLineDash([6, 3]);
        g.beginPath(); g.moveTo(x + 0.5, m.t); g.lineTo(x + 0.5, m.t + this.ph); g.stroke();
        g.setLineDash([]);
        g.fillStyle = C.cursor;
        g.fillRect(x - 7, m.t + this.ph - 14, 14, 14);
        this.text(label, x, m.t + this.ph - 7, 'center', C.bg);
      }
    }

    // Value of a channel at time t (linear interpolation)
    valueAt(ch, t) {
      const rec = this.rec;
      if (!rec) return null;
      const v = ch === 1 ? rec.c1 : rec.c2;
      if (ch === 2 && !rec.hasC2) return null;
      const f = (t - rec.t0) * rec.rate, i = Math.floor(f);
      if (i < 0 || i >= v.length - 1) return null;
      return v[i] + (v[i + 1] - v[i]) * (f - i);
    }

    bindEvents() {
      const cv = this.canvas;
      cv.addEventListener('wheel', (ev) => {
        if (!this.rec || this.xy) return;
        ev.preventDefault();
        const p = this.local(ev);
        const [a, b] = this.range(), [sa, sb] = this.span();
        const t = this.xt(p.x);
        const k = Math.exp(ev.deltaY * 0.0015);
        let na = t - (t - a) * k, nb = t + (b - t) * k;
        const minSpan = 8 / this.rec.rate;
        if (nb - na < minSpan) return;
        if (nb - na >= sb - sa) { this.view = null; this.draw(); return; }
        if (na < sa) { nb += sa - na; na = sa; }
        if (nb > sb) { na -= nb - sb; nb = sb; }
        this.view = [na, nb];
        this.draw();
      }, { passive: false });
      cv.addEventListener('pointerdown', (ev) => {
        if (!this.rec || this.xy) return;
        const p = this.local(ev);
        const near = (x) => Math.abs(p.x - x) < 7;
        if (this.cursors.on && near(this.tx(this.cursors.a))) this.drag = { kind: 'a' };
        else if (this.cursors.on && near(this.tx(this.cursors.b))) this.drag = { kind: 'b' };
        else if (this.trigLevel !== null && this.onTrigLevel && Math.abs(p.y - this.vy(this.trigLevel)) < 6)
          this.drag = { kind: 'trig' };
        else this.drag = { kind: 'pan', x: p.x, view: this.range() };
        cv.setPointerCapture(ev.pointerId);
      });
      cv.addEventListener('pointermove', (ev) => {
        const p = this.local(ev);
        if (!this.drag) {
          // Cursor feedback
          let cur = 'default';
          if (this.rec && !this.xy) {
            if (this.cursors.on && (Math.abs(p.x - this.tx(this.cursors.a)) < 7 || Math.abs(p.x - this.tx(this.cursors.b)) < 7))
              cur = 'ew-resize';
            else if (this.trigLevel !== null && this.onTrigLevel && Math.abs(p.y - this.vy(this.trigLevel)) < 6)
              cur = 'ns-resize';
            else cur = 'grab';
          }
          cv.style.cursor = cur;
          return;
        }
        const d = this.drag;
        if (d.kind === 'a' || d.kind === 'b') {
          const [a, b] = this.range();
          this.cursors[d.kind] = Math.max(a, Math.min(b, this.xt(p.x)));
          this.onCursors && this.onCursors(this.cursors);
        } else if (d.kind === 'trig') {
          this.trigLevel = this.yv(p.y);
        } else if (d.kind === 'pan' && this.view) {
          const [sa, sb] = this.span();
          const dt = ((d.x - p.x) / this.pw) * (d.view[1] - d.view[0]);
          let na = d.view[0] + dt, nb = d.view[1] + dt;
          if (na < sa) { nb += sa - na; na = sa; }
          if (nb > sb) { na -= nb - sb; nb = sb; }
          this.view = [na, nb];
        }
        this.draw();
      });
      cv.addEventListener('pointerup', () => {
        if (this.drag && this.drag.kind === 'trig') this.onTrigLevel(this.trigLevel);
        this.drag = null;
      });
      cv.addEventListener('dblclick', () => {
        this.view = null;
        this.draw();
      });
    }
  }

  // -------------------------------------------------------------------------
  // Spectrum plot
  // -------------------------------------------------------------------------
  class SpectrumPlot extends CanvasView {
    constructor(canvas) {
      super(canvas);
      this.spec = null;
      this.logX = false;
      this.hover = null;
      canvas.addEventListener('pointermove', (ev) => {
        this.hover = this.local(ev).x;
        this.draw();
      });
      canvas.addEventListener('pointerleave', () => {
        this.hover = null;
        this.draw();
      });
    }
    setSpectrum(spec) {
      this.spec = spec;
      this.draw();
    }
    fx(f) {
      const s = this.spec, fmax = s.freqs[s.freqs.length - 1];
      if (this.logX) {
        const fmin = s.freqs[1];
        return this.margin.l + (Math.log(Math.max(f, fmin) / fmin) / Math.log(fmax / fmin)) * this.pw;
      }
      return this.margin.l + (f / fmax) * this.pw;
    }
    xf(x) {
      const s = this.spec, fmax = s.freqs[s.freqs.length - 1], u = (x - this.margin.l) / this.pw;
      if (this.logX) {
        const fmin = s.freqs[1];
        return fmin * Math.pow(fmax / fmin, u);
      }
      return u * fmax;
    }
    draw() {
      if (!this.w) return;
      this.clear();
      const g = this.ctx, m = this.margin;
      const top = 20, bottom = -100;
      const dy = (db) => m.t + ((top - db) / (top - bottom)) * this.ph;
      for (let db = top; db >= bottom; db -= 20) {
        const y = dy(db);
        g.strokeStyle = C.grid;
        g.beginPath(); g.moveTo(m.l, y + 0.5); g.lineTo(m.l + this.pw, y + 0.5); g.stroke();
        this.text(`${db} dBV`, m.l - 6, y, 'right');
      }
      if (!this.spec) {
        this.text('No data', m.l + this.pw / 2, m.t + this.ph / 2, 'center');
        return;
      }
      const s = this.spec, fmax = s.freqs[s.freqs.length - 1];
      for (let i = 0; i <= 10; i++) {
        const f = this.logX ? this.xf(m.l + (i / 10) * this.pw) : (fmax * i) / 10;
        const x = this.fx(f);
        g.strokeStyle = C.grid;
        g.beginPath(); g.moveTo(x + 0.5, m.t); g.lineTo(x + 0.5, m.t + this.ph); g.stroke();
        if (i % 2 === 0) this.text(DSP.si(f, 'Hz', 3), x, m.t + this.ph + 13, 'center');
      }
      g.save();
      g.beginPath(); g.rect(m.l, m.t, this.pw, this.ph); g.clip();
      g.strokeStyle = C.c1;
      g.beginPath();
      // Max per pixel column so narrow peaks stay visible
      let col = -1, colMax = -Infinity;
      const flush = () => { if (col >= 0) g.lineTo(m.l + col + 0.5, dy(colMax)); };
      g.moveTo(this.fx(s.freqs[1]), dy(s.dbv[1]));
      for (let k = 1; k < s.freqs.length; k++) {
        const c = Math.floor(this.fx(s.freqs[k]) - m.l);
        if (c !== col) {
          flush();
          col = c;
          colMax = s.dbv[k];
        } else colMax = Math.max(colMax, s.dbv[k]);
      }
      flush();
      g.stroke();
      g.restore();
      // Peak marker
      const px = this.fx(s.peakFreq), py = dy(s.peakDbv);
      g.fillStyle = C.trig;
      g.beginPath(); g.moveTo(px, py - 3); g.lineTo(px - 5, py - 11); g.lineTo(px + 5, py - 11); g.fill();
      this.text(`peak ${DSP.si(s.peakFreq, 'Hz', 4)}  ${s.peakDbv.toFixed(1)} dBV`, m.l + this.pw - 6, m.t + 10, 'right', C.trig);
      if (this.hover !== null && this.hover > m.l && this.hover < m.l + this.pw) {
        const f = this.xf(this.hover);
        const k = Math.min(s.freqs.length - 1, Math.max(0, Math.round((f / fmax) * (s.freqs.length - 1))));
        g.strokeStyle = C.cursor;
        g.beginPath(); g.moveTo(this.hover + 0.5, m.t); g.lineTo(this.hover + 0.5, m.t + this.ph); g.stroke();
        this.text(`${DSP.si(s.freqs[k], 'Hz', 4)}  ${s.dbv[k].toFixed(1)} dBV`, m.l + 6, m.t + 10, 'left', C.cursor);
      }
    }
  }

  // -------------------------------------------------------------------------
  // Strip chart: growing series against time (logging, meter trends).
  // series: [{ name, color, data: GrowArray }], time from sample index / rate
  // or from an explicit time array.
  // -------------------------------------------------------------------------
  class GrowArray {
    constructor(cap = 4096) {
      this.a = new Float32Array(cap);
      this.n = 0;
    }
    push(v) {
      if (this.n === this.a.length) {
        const b = new Float32Array(this.a.length * 2);
        b.set(this.a);
        this.a = b;
      }
      this.a[this.n++] = v;
    }
    pushAll(arr) {
      for (let i = 0; i < arr.length; i++) this.push(arr[i]);
    }
    view() { return this.a.subarray(0, this.n); }
    clear() { this.n = 0; }
  }

  class StripChart extends CanvasView {
    constructor(canvas, opts = {}) {
      super(canvas);
      this.series = [];
      this.time = null; // GrowArray of seconds, or null => index / rate
      this.rate = 1;
      this.unit = opts.unit || 'V';
      this.minSpan = opts.minSpan || 0; // smallest y range (absolute)
      this.minRel = opts.minRel || 0;   // ...or relative to the value
      this.follow = true;
      this.windowSec = opts.windowSec || 10;
      this.view = null;
      this.drag = null;
      canvas.addEventListener('wheel', (ev) => {
        ev.preventDefault();
        const [a, b] = this.range();
        const t = a + ((this.local(ev).x - this.margin.l) / this.pw) * (b - a);
        const k = Math.exp(ev.deltaY * 0.0015);
        this.windowSec = Math.max(1e-3, (b - a) * k);
        if (this.follow) return this.draw();
        this.view = [t - (t - a) * k, t + (b - t) * k];
        this.draw();
      }, { passive: false });
      canvas.addEventListener('pointerdown', (ev) => {
        this.drag = { x: this.local(ev).x, view: this.range() };
        canvas.setPointerCapture(ev.pointerId);
      });
      canvas.addEventListener('pointermove', (ev) => {
        if (!this.drag) return;
        const dt = ((this.drag.x - this.local(ev).x) / this.pw) * (this.drag.view[1] - this.drag.view[0]);
        if (Math.abs(dt) > 0) this.follow = false;
        this.view = [this.drag.view[0] + dt, this.drag.view[1] + dt];
        this.draw();
      });
      canvas.addEventListener('pointerup', () => (this.drag = null));
      canvas.addEventListener('dblclick', () => {
        this.follow = true;
        this.view = null;
        this.draw();
      });
    }
    count() { return this.series.length ? this.series[0].data.n : 0; }
    tAt(i) { return this.time ? this.time.a[i] : i / this.rate; }
    duration() { const n = this.count(); return n ? this.tAt(n - 1) : 0; }
    range() {
      if (this.follow || !this.view) {
        const end = Math.max(this.duration(), this.windowSec);
        return [end - this.windowSec, end];
      }
      return this.view;
    }
    indexAt(t) {
      // first index with time >= t
      const n = this.count();
      if (!this.time) return Math.max(0, Math.min(n, Math.ceil(t * this.rate)));
      let lo = 0, hi = n;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (this.time.a[mid] < t) lo = mid + 1;
        else hi = mid;
      }
      return lo;
    }
    draw() {
      if (!this.w) return;
      this.clear();
      const g = this.ctx, m = this.margin;
      const [t0, t1] = this.range();
      const i0 = Math.max(0, this.indexAt(t0) - 1), i1 = Math.min(this.count(), this.indexAt(t1) + 1);
      // Y range from visible data
      let lo = Infinity, hi = -Infinity;
      for (const s of this.series)
        for (let i = i0; i < i1; i++) {
          const v = s.data.a[i];
          if (v < lo) lo = v;
          if (v > hi) hi = v;
        }
      if (!isFinite(lo)) { lo = -1; hi = 1; }
      // Don't zoom in on noise: enforce a minimum span around the data.
      const want = Math.max(this.minSpan, this.minRel * Math.max(Math.abs(lo), Math.abs(hi)));
      if (hi - lo < want) {
        const c = (hi + lo) / 2;
        lo = c - want / 2;
        hi = c + want / 2;
      }
      const pad = Math.max((hi - lo) * 0.1, 1e-9);
      lo -= pad;
      hi += pad;
      const vy = (v) => m.t + ((hi - v) / (hi - lo)) * this.ph;
      const tx = (t) => m.l + ((t - t0) / (t1 - t0)) * this.pw;
      for (let i = 0; i <= 10; i++) {
        const x = m.l + (i / 10) * this.pw;
        g.strokeStyle = C.grid;
        g.beginPath(); g.moveTo(x + 0.5, m.t); g.lineTo(x + 0.5, m.t + this.ph); g.stroke();
        if (i % 2 === 0) this.text(fmtT(t0 + ((t1 - t0) * i) / 10), x, m.t + this.ph + 13, 'center');
      }
      for (let i = 0; i <= 4; i++) {
        const y = m.t + (i / 4) * this.ph;
        g.strokeStyle = C.grid;
        g.beginPath(); g.moveTo(m.l, y + 0.5); g.lineTo(m.l + this.pw, y + 0.5); g.stroke();
        this.text(DSP.si(hi - ((hi - lo) * i) / 4, this.unit, 3), m.l - 6, y, 'right');
      }
      if (i1 - i0 < 2) {
        this.text('Waiting for data…', m.l + this.pw / 2, m.t + this.ph / 2, 'center');
        return;
      }
      g.save();
      g.beginPath(); g.rect(m.l, m.t, this.pw, this.ph); g.clip();
      for (const s of this.series) {
        g.strokeStyle = s.color;
        g.lineWidth = 1.3;
        g.beginPath();
        const v = s.data.a;
        if (!this.time && i1 - i0 > this.pw * 2) {
          const cols = Math.floor(this.pw);
          const { mins, maxs } = DSP.minmax(v, i0, i1, cols);
          const ta = this.tAt(i0), tb = this.tAt(i1 - 1);
          const xa = tx(ta), xb = tx(tb);
          for (let c = 0; c < cols; c++) {
            const x = xa + ((c + 0.5) / cols) * (xb - xa);
            g.moveTo(x, vy(maxs[c]));
            g.lineTo(x, vy(mins[c]) + 0.5);
          }
        } else {
          for (let i = i0; i < i1; i++) {
            const x = tx(this.tAt(i)), y = vy(v[i]);
            if (i === i0) g.moveTo(x, y);
            else g.lineTo(x, y);
          }
        }
        g.stroke();
      }
      g.restore();
      g.lineWidth = 1;
      let lx = m.l + 8;
      for (const s of this.series) {
        this.text(s.name, lx, m.t + 10, 'left', s.color);
        lx += 14 + 7 * s.name.length;
      }
    }
  }

  root.SPPlot = { ScopePlot, SpectrumPlot, StripChart, GrowArray, COLORS: C };
})(typeof self !== 'undefined' ? self : this);
