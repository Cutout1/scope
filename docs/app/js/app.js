// Scopocket Companion: UI wiring.
(function () {
  'use strict';
  const DSP = window.SPDsp;
  const { Device, SerialTransport, SimTransport } = window.SPDevice;
  const { SimDevice } = window.SPSim;
  const { ScopePlot, SpectrumPlot, StripChart, GrowArray, COLORS } = window.SPPlot;
  const $ = (id) => document.getElementById(id);

  const SCOPE_KEYS = ['rate', 'vdiv', 'display', 'capture', 'trigger', 'trig_level', 'measure'];
  const GEN_KEYS = ['wave', 'wave_amp', 'dc_level', 'wave_freq'];
  const LOG_LIMIT = 10_000_000; // samples per channel kept in memory while logging

  let dev = null;
  const selects = {};
  const S = {
    live: null,          // latest live record
    capture: null,       // deep capture record, when viewing it
    viewing: 'live',
    paused: false,
    lastRate: null,
    analysisDue: 0,
    logging: false,
    logRate: 0,
    logStart: null,
    trendStart: performance.now(),
    capCount: 0,
    capLastState: null,
  };

  // ---- plots ----------------------------------------------------------------
  const scope = new ScopePlot($('cv-scope'));
  const spec = new SpectrumPlot($('cv-fft'));
  const logChart = new StripChart($('cv-log'), { windowSec: 10 });
  const voltChart = new StripChart($('cv-volt'), { windowSec: 60, minSpan: 0.02 });
  const ohmChart = new StripChart($('cv-ohm'), { windowSec: 60, unit: 'Ω', minRel: 0.01 });

  const logData = { c1: new GrowArray(1 << 16), c2: new GrowArray(1 << 16) };
  logChart.series = [
    { name: 'C1', color: COLORS.c1, data: logData.c1 },
    { name: 'C2', color: COLORS.c2, data: logData.c2 },
  ];
  const voltTrend = { t: new GrowArray(), c1: new GrowArray(), c2: new GrowArray() };
  voltChart.time = voltTrend.t;
  voltChart.series = [
    { name: 'C1', color: COLORS.c1, data: voltTrend.c1 },
    { name: 'C2', color: COLORS.c2, data: voltTrend.c2 },
  ];
  const ohmTrend = { t: new GrowArray(), r: new GrowArray() };
  ohmChart.time = ohmTrend.t;
  ohmChart.series = [{ name: 'R', color: COLORS.trig, data: ohmTrend.r }];

  // ---- messages ---------------------------------------------------------------
  function say(msg, error = false) {
    const f = $('footer');
    f.textContent = msg;
    f.classList.toggle('error', error);
  }

  function download(name, blobOrText, type = 'text/csv') {
    const blob = blobOrText instanceof Blob ? blobOrText : new Blob([blobOrText], { type });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    document.body.appendChild(a);
    a.click();
    setTimeout(() => {
      URL.revokeObjectURL(a.href);
      a.remove();
    }, 1000);
  }
  const stamp = () => new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);

  // ---- connection -----------------------------------------------------------
  function initSupport() {
    if (!SerialTransport.supported()) {
      $('btn-connect').disabled = true;
      const fileHint = location.protocol === 'file:'
        ? ' If you are using Chrome or Edge, serve this folder over http://localhost (see README).' : '';
      $('banner').textContent =
        'Web Serial is not available here, so only Demo mode works. Use Chrome or Edge on a computer or Chromebook.' + fileHint;
      $('banner').hidden = false;
      return;
    }
    SerialTransport.previouslyGranted().then((ports) => {
      if (ports.length) $('btn-reconnect').hidden = false;
    });
    navigator.serial.addEventListener('connect', () => {
      if (!dev) $('btn-reconnect').hidden = false;
    });
  }

  async function connect(transport) {
    if (dev) await disconnect();
    say(`Connecting (${transport.name})…`);
    const d = new Device(transport);
    d.on('info', onInfo)
      .on('settings', onSettings)
      .on('status', onStatus)
      .on('frame', onFrame)
      .on('meter', onMeter)
      .on('deepProgress', onDeepProgress)
      .on('deep', onDeep)
      .on('log', onLog)
      .on('disconnect', () => {
        say('Device disconnected.', true);
        disconnect();
      });
    dev = d;
    try {
      await d.connect();
    } catch (e) {
      dev = null;
      let hint = '';
      if (/Linux/.test(navigator.userAgent) && !/CrOS/.test(navigator.userAgent))
        hint = ' On Linux this usually means your account may not use serial ports: add it to the ' +
          '"dialout" group (Debian/Ubuntu/Fedora) or "uucp" group (Arch), then log out and back in. ' +
          'See the README for a udev rule that avoids logging out.';
      else hint = ' Close any other program using the port (serial monitors, Thonny, another tab) and try again.';
      say(`Could not open the port: ${e.message}.${hint}`, true);
      return;
    }
    setConnected(transport.name);
    // If no INFO arrives, the port probably isn't a Scopocket.
    setTimeout(() => {
      if (dev === d && !d.info) say('No answer from the device. Is it running the Scopocket firmware?', true);
    }, 4000);
  }

  async function disconnect() {
    const d = dev;
    dev = null;
    if (d) await d.disconnect().catch(() => {});
    setConnected(null);
  }

  function setConnected(name) {
    const on = !!name;
    $('btn-connect').hidden = on;
    $('btn-demo').hidden = on;
    $('btn-reconnect').hidden = on || $('btn-reconnect').hidden;
    $('btn-disconnect').hidden = !on;
    $('chip-conn').textContent = on ? `Connected · ${name}` : 'Not connected';
    $('chip-conn').className = `chip ${on ? 'on' : 'off'}`;
    if (!on) {
      for (const id of ['chip-acq', 'chip-menu', 'chip-batt', 'chip-ovr']) $(id).hidden = true;
      $('modes').hidden = true;
      $('sidebar').hidden = true;
      showView('welcome');
      S.logging = false;
    }
  }

  $('btn-connect').onclick = async () => {
    try {
      await connect(await SerialTransport.request());
    } catch (e) {
      if (e.name !== 'NotFoundError') say(e.message, true); // NotFoundError = chooser cancelled
    }
  };
  $('btn-reconnect').onclick = async () => {
    const ports = await SerialTransport.previouslyGranted();
    if (ports.length) connect(ports[0]);
  };
  $('btn-demo').onclick = () => connect(new SimTransport(new SimDevice()));
  $('btn-disconnect').onclick = () => {
    disconnect();
    say('Disconnected.');
  };

  // ---- settings -------------------------------------------------------------
  function onInfo(info) {
    say(`${info.name} ${info.board}, firmware ${info.fw} (protocol ${info.proto}).`);
    $('device-info').textContent =
      `${info.name} ${info.board} · firmware ${info.fw}\nLong capture up to ${info.deep_max} samples\n` +
      `Logging up to ${DSP.si(info.log_max_rate, 'S/s', 2)}`;
    // Mode tabs
    const mode = info.settings.find((s) => s.key === 'mode');
    const nav = $('modes');
    nav.innerHTML = '';
    mode.options.forEach((label, i) => {
      const b = document.createElement('button');
      b.textContent = label;
      b.dataset.index = i;
      b.onclick = () => dev && dev.set('mode', i);
      nav.appendChild(b);
    });
    nav.hidden = false;
    // Setting selectors
    const build = (keys, container) => {
      container.innerHTML = '';
      for (const key of keys) {
        const s = info.settings.find((x) => x.key === key);
        if (!s) continue;
        const label = document.createElement('label');
        label.textContent = s.label;
        label.htmlFor = `set-${key}`;
        const sel = document.createElement('select');
        sel.id = `set-${key}`;
        s.options.forEach((o, i) => sel.add(new Option(o, i)));
        sel.onchange = () => dev && dev.set(key, +sel.value);
        container.append(label, sel);
        selects[key] = { sel, label };
      }
    };
    build(SCOPE_KEYS, $('settings-scope'));
    build(GEN_KEYS, $('settings-gen'));
    $('sidebar').hidden = false;
    if (dev && dev.settings) onSettings(dev.settings);
  }

  function onSettings() {
    if (!dev || !dev.info) return;
    for (const [key, { sel, label }] of Object.entries(selects)) {
      const v = dev.value(key);
      if (v !== undefined && document.activeElement !== sel) sel.value = v;
      const vis = dev.visible(key);
      sel.classList.toggle('hidden', !vis);
      label.classList.toggle('hidden', !vis);
      sel.disabled = !dev.enabled(key);
    }
    $('settings-scope').parentElement.hidden = dev.value('mode') !== 0;
    const modeIdx = dev.value('mode');
    if (modeIdx !== S.modeIdx) {
      S.modeIdx = modeIdx;
      Object.values(voltTrend).forEach((g) => g.clear());
      Object.values(ohmTrend).forEach((g) => g.clear());
      S.trendStart = performance.now();
    }
    for (const b of $('modes').children) b.classList.toggle('active', +b.dataset.index === modeIdx);
    showView(['scope', 'volt', 'ohm', 'cap'][modeIdx] || 'scope');

    // Scope display parameters
    scope.vdiv = dev.numeric('vdiv') / 1e6;
    scope.xy = dev.value('display') === 1;
    const edge = dev.enabled('trig_level');
    scope.trigLevel = edge ? dev.numeric('trig_level') / 1e6 : null;
    scope.onTrigLevel = edge ? setTrigLevel : null;
    $('btn-rearm').hidden = dev.value('capture') !== 1;
    $('chk-cursors').disabled = scope.xy;
    scope.draw();
  }

  function setTrigLevel(volts) {
    const s = dev.setting('trig_level');
    let best = 0;
    s.values.forEach((uv, i) => {
      if (Math.abs(uv / 1e6 - volts) < Math.abs(s.values[best] / 1e6 - volts)) best = i;
    });
    dev.set('trig_level', best);
  }

  function showView(name) {
    if (S.view === name) return;
    S.view = name;
    for (const v of ['welcome', 'scope', 'volt', 'ohm', 'cap']) $(`view-${v}`).hidden = v !== name;
    // Canvases inside a hidden view have no size: redraw when shown.
    requestAnimationFrame(() => {
      if (name === 'scope') { scope.resize(); spec.resize(); logChart.resize(); }
      if (name === 'volt') voltChart.resize();
      if (name === 'ohm') ohmChart.resize();
    });
  }

  // ---- status -----------------------------------------------------------------
  const ACQ_LABEL = { waiting: 'Waiting', capturing: 'Capturing', stopped: 'Stopped', rolling: 'Rolling' };
  function onStatus(st) {
    const chip = $('chip-acq');
    chip.hidden = st.mode !== 'scope';
    let label = ACQ_LABEL[st.acqState];
    const cap = dev.value('capture'), trig = dev.value('trigger');
    if (st.acqState === 'waiting') {
      // Repeat mode with edge triggers runs continuously (auto); Single and
      // Switch triggering really are waiting.
      if (cap === 1 || trig === 2) label = 'Armed';
      else label = 'Running';
    }
    chip.textContent = label;
    chip.className = `chip ${st.acqState === 'stopped' ? 'warn' : ''}`;
    $('chip-menu').hidden = !st.menuOpen;
    $('chip-batt').hidden = !st.lowBattery;
    $('chip-ovr').hidden = !(S.lastOverruns !== undefined && st.overruns !== S.lastOverruns);
    S.lastOverruns = st.overruns;

    // Long capture
    const busy = st.deepState === 'armed' || st.deepState === 'capturing';
    $('btn-deep').disabled = busy;
    $('btn-deep-cancel').hidden = !busy;
    $('deep-progress').hidden = !busy && !S.deepReceiving;
    if (busy) {
      const n = +$('sel-deep-len').value;
      $('deep-progress').value = st.deepState === 'capturing' ? st.deepProgress / n : 0;
      $('deep-status').textContent = st.deepState === 'armed' ? 'Waiting for trigger…' : 'Capturing…';
    }

    // Logging
    S.logPossible = st.logPossible;
    if (S.logging && !st.logging && S.logStart !== null && performance.now() - S.logStart > 1500) {
      S.logging = false;
      say('Logging stopped by the device (mode or sample rate changed).');
    }
    $('btn-log').disabled = !st.logPossible && !S.logging;
    $('btn-log').textContent = S.logging ? 'Stop logging' : 'Start logging';
    updateLogStatus(st.logDropped);
  }

  // ---- scope data ------------------------------------------------------------
  function onFrame(f) {
    const pre = dev.info ? dev.info.pretrigger : 20;
    const full = dev.info ? dev.info.points : 160;
    const width = (full - 1) / f.rate;
    // Triggered frames start at the pre-trigger point and fill left to right;
    // roll frames end at "now" (0) and grow leftwards. Either way the axis
    // covers the full frame, however many samples have arrived so far.
    const rec = {
      c1: f.c1, c2: f.c2, rate: f.rate, hasC2: f.hasC2, diff: f.diff, roll: f.roll, triggered: f.triggered,
      t0: f.roll ? -(f.n - 1) / f.rate : -pre / f.rate,
      span: f.roll ? [-width, 0] : [-pre / f.rate, -pre / f.rate + width],
    };
    // Sweep: a frame that is still filling (slow rates) is drawn over the last
    // complete frame, which stays visible ahead of the write position.
    if (!f.roll && f.n === full) S.lastFull = rec;
    const L = S.lastFull;
    if (!f.roll && f.n < full && L && L.rate === f.rate && L.hasC2 === f.hasC2) {
      const GAP = 4;
      const mix = (fresh, old) => {
        const out = new Float32Array(full);
        for (let i = 0; i < full; i++) out[i] = i < f.n ? fresh[i] : i < f.n + GAP ? NaN : old[i];
        return out;
      };
      rec.c1 = mix(f.c1, L.c1);
      rec.c2 = mix(f.c2, L.c2);
      rec.measureFrom = L; // measurements use the last complete frame
    }
    S.live = rec;
    if (S.paused || S.viewing !== 'live') return;
    const rateChanged = S.lastRate !== f.rate;
    S.lastRate = f.rate;
    scope.showTrigTime = !f.roll;
    scope.setRecord(rec, !rateChanged);
    updateBadge();
    analyse();
  }

  function currentRecord() {
    return S.viewing === 'capture' ? S.capture : S.live;
  }

  function updateBadge() {
    const r = currentRecord();
    if (!r) return ($('scope-badge').textContent = '');
    const parts = [DSP.si(r.rate, 'S/s', 3)];
    if (S.viewing === 'capture') parts.unshift(`Capture · ${r.c1.length} pts · ${DSP.si(r.c1.length / r.rate, 's', 3)}`);
    else parts.push(r.roll ? 'ROLL' : r.triggered ? 'TRIG' : 'AUTO');
    if (!r.diff) parts.push('vs GND');
    if (S.paused) parts.push('PAUSED');
    $('scope-badge').textContent = parts.join(' · ');
  }

  // Measurements, spectrum and cursor readouts, at most ~8 times per second.
  function analyse(force = false) {
    const now = performance.now();
    if (!force && now < S.analysisDue) return;
    S.analysisDue = now + 120;
    const shown = currentRecord();
    const r = shown && shown.measureFrom ? shown.measureFrom : shown;
    const rows = document.querySelectorAll('#tbl-meas tbody tr');
    if (!r) return;
    const [ta, tb] = scope.range();
    const i0 = Math.max(0, Math.floor((ta - r.t0) * r.rate));
    const i1 = Math.min(r.c1.length, Math.ceil((tb - r.t0) * r.rate) + 1);
    [r.c1, r.hasC2 ? r.c2 : null].forEach((v, ch) => {
      const cells = rows[ch].querySelectorAll('td');
      const m = v ? DSP.measure(v, r.rate, i0, i1) : null;
      const vals = m
        ? [DSP.si(m.freq, 'Hz', 4), DSP.si(m.period, 's', 4), DSP.si(m.pp, 'V'), DSP.signedVolts(m.min),
          DSP.signedVolts(m.max), DSP.signedVolts(m.mean), DSP.si(m.rms, 'V'), DSP.si(m.acrms, 'V'),
          m.duty === null ? '—' : `${(m.duty * 100).toFixed(1)} %`]
        : Array(9).fill('—');
      vals.forEach((t, k) => (cells[k].textContent = t));
    });
    if ($('chk-fft').checked && i1 - i0 >= 16) {
      const v = r.c1.subarray(i0, i1);
      spec.logX = $('chk-logx').checked;
      spec.setSpectrum(DSP.spectrum(v, r.rate, { window: $('sel-window').value }));
    }
    updateCursorReadout();
  }

  function updateCursorReadout() {
    const box = $('cursor-read');
    box.hidden = !scope.cursors.on || scope.xy;
    if (box.hidden || scope.cursors.a === null) return;
    const { a, b } = scope.cursors;
    const dt = b - a;
    const line = (ch) => {
      const va = scope.valueAt(ch, a), vb = scope.valueAt(ch, b);
      if (va === null || vb === null) return `C${ch}  —`;
      return `C${ch}  A ${DSP.signedVolts(va)}  B ${DSP.signedVolts(vb)}  ΔV ${DSP.signedVolts(vb - va)}`;
    };
    box.textContent =
      `A ${DSP.si(a, 's', 4)}   B ${DSP.si(b, 's', 4)}   Δt ${DSP.si(dt, 's', 4)}   1/Δt ${DSP.si(dt ? 1 / Math.abs(dt) : null, 'Hz', 4)}\n` +
      `${line(1)}\n${line(2)}`;
  }
  scope.onCursors = () => updateCursorReadout();

  // Redraw analysis when the user zooms/pans.
  $('cv-scope').addEventListener('wheel', () => setTimeout(() => analyse(true), 0));
  $('cv-scope').addEventListener('pointerup', () => analyse(true));
  $('cv-scope').addEventListener('dblclick', () => analyse(true));

  $('btn-pause').onclick = () => {
    S.paused = !S.paused;
    $('btn-pause').textContent = S.paused ? 'Resume' : 'Pause';
    $('btn-pause').classList.toggle('active-toggle', S.paused);
    if (!S.paused && S.viewing === 'live' && S.live) scope.setRecord(S.live, true);
    updateBadge();
  };
  $('btn-rearm').onclick = () => dev && dev.action('REARM');
  $('btn-force').onclick = () => dev && dev.action('FORCE');
  $('chk-cursors').onchange = () => {
    scope.cursors.on = $('chk-cursors').checked;
    scope.cursors.a = scope.cursors.b = null; // place inside the current view
    scope.draw();
    updateCursorReadout();
  };
  $('chk-fft').onchange = () => {
    const on = $('chk-fft').checked;
    $('fft-wrap').hidden = !on;
    $('sel-window').hidden = !on;
    $('lbl-logx').hidden = !on;
    requestAnimationFrame(() => {
      spec.resize();
      analyse(true);
    });
  };
  $('sel-window').onchange = () => analyse(true);
  $('chk-logx').onchange = () => analyse(true);

  $('btn-csv').onclick = () => {
    const r = currentRecord();
    if (!r) return;
    const lines = [`time_s,c1_V${r.hasC2 ? ',c2_V' : ''}`];
    for (let i = 0; i < r.c1.length; i++) {
      const t = (r.t0 + i / r.rate).toPrecision(9);
      lines.push(r.hasC2 ? `${t},${r.c1[i].toFixed(5)},${r.c2[i].toFixed(5)}` : `${t},${r.c1[i].toFixed(5)}`);
    }
    download(`scopocket-${S.viewing}-${stamp()}.csv`, lines.join('\n') + '\n');
  };
  $('btn-png').onclick = () => {
    // Scope plot (and spectrum if shown) stacked into one image
    const a = $('cv-scope'), b = $('cv-fft'), withFft = !$('fft-wrap').hidden;
    const out = document.createElement('canvas');
    out.width = a.width;
    out.height = a.height + (withFft ? b.height : 0);
    const g = out.getContext('2d');
    g.drawImage(a, 0, 0);
    if (withFft) g.drawImage(b, 0, a.height);
    out.toBlob((blob) => download(`scopocket-${stamp()}.png`, blob));
  };

  // ---- long capture -------------------------------------------------------
  $('btn-deep').onclick = () => {
    if (!dev) return;
    S.deepReceiving = false;
    dev.startDeep(+$('sel-deep-len').value);
    $('deep-status').textContent = 'Arming…';
  };
  $('btn-deep-cancel').onclick = () => {
    dev && dev.action('DEEP_CANCEL');
    $('deep-status').textContent = 'Cancelled.';
  };
  $('btn-live').onclick = () => {
    S.viewing = 'live';
    $('btn-live').hidden = true;
    if (S.live) {
      scope.showTrigTime = !S.live.roll;
      scope.setRecord(S.live);
    }
    updateBadge();
    analyse(true);
  };

  function onDeepProgress(d) {
    S.deepReceiving = d.received < d.count;
    $('deep-progress').hidden = false;
    $('deep-progress').value = d.received / d.count;
    $('deep-status').textContent = `Transferring ${d.received} / ${d.count}…`;
  }

  function onDeep(d) {
    S.deepReceiving = false;
    $('deep-progress').hidden = true;
    S.capture = {
      c1: d.c1, c2: d.c2, rate: d.rate, hasC2: d.hasC2, diff: d.diff, triggered: d.triggered,
      t0: -d.pre / d.rate,
    };
    S.viewing = 'capture';
    $('btn-live').hidden = false;
    scope.showTrigTime = true;
    scope.setRecord(S.capture);
    $('deep-status').textContent =
      `Captured ${d.count} samples (${DSP.si(d.count / d.rate, 's', 3)}) at ${DSP.si(d.rate, 'S/s', 3)}` +
      (d.triggered ? '' : ', untriggered') + '.';
    updateBadge();
    analyse(true);
  }

  // ---- logging ----------------------------------------------------------------
  $('btn-log').onclick = () => {
    if (!dev) return;
    if (S.logging) {
      dev.setLogging(false);
      S.logging = false;
      $('btn-log').textContent = 'Start logging';
      say(`Logging stopped: ${logData.c1.n} samples.`);
      return;
    }
    if (!S.logPossible) return say('Logging works at sample rates up to 50 kS/s in Scope mode.', true);
    logData.c1.clear();
    logData.c2.clear();
    S.logRate = dev.numeric('rate');
    logChart.rate = S.logRate;
    logChart.follow = true;
    S.logging = true;
    S.logStart = performance.now();
    S.logExpect = 0;
    S.logGaps = 0;
    dev.setLogging(true);
    $('btn-log').textContent = 'Stop logging';
    $('panel-log').open = true;
    requestAnimationFrame(() => logChart.resize());
  };
  $('btn-log-clear').onclick = () => {
    logData.c1.clear();
    logData.c2.clear();
    logChart.draw();
    updateLogStatus();
  };
  $('btn-log-csv').onclick = () => {
    const n = logData.c1.n;
    if (!n) return;
    // Build in chunks: logs can be millions of rows.
    const parts = ['time_s,c1_V,c2_V\n'];
    const c1 = logData.c1.a, c2 = logData.c2.a;
    for (let start = 0; start < n; start += 50000) {
      let s = '';
      for (let i = start; i < Math.min(n, start + 50000); i++)
        s += `${(i / S.logRate).toPrecision(9)},${c1[i].toFixed(4)},${c2[i].toFixed(4)}\n`;
      parts.push(s);
    }
    download(`scopocket-log-${stamp()}.csv`, new Blob(parts, { type: 'text/csv' }));
  };

  let logDrawDue = 0;
  function onLog(l) {
    if (!S.logging) return;
    if (l.index !== S.logExpect) S.logGaps++;
    S.logExpect = l.index + l.c1.length;
    if (logData.c1.n + l.c1.length > LOG_LIMIT) {
      dev.setLogging(false);
      S.logging = false;
      say(`Logging stopped at the ${LOG_LIMIT.toLocaleString()}-sample memory limit. Save it as CSV.`);
      return;
    }
    logData.c1.pushAll(l.c1);
    logData.c2.pushAll(l.c2);
    const now = performance.now();
    if (now > logDrawDue) {
      logDrawDue = now + 100;
      logChart.draw();
      updateLogStatus();
    }
  }

  function updateLogStatus(dropped) {
    const n = logData.c1.n;
    if (!n && !S.logging) return ($('log-status').textContent = '');
    let s = `${n.toLocaleString()} samples · ${DSP.si(n / (S.logRate || 1), 's', 3)} at ${DSP.si(S.logRate, 'S/s', 3)}`;
    if (dropped) s += ` · ${dropped.toLocaleString()} dropped by the device`;
    if (S.logGaps) s += ` · ${S.logGaps} gaps`;
    $('log-status').textContent = s;
  }

  // ---- meters -----------------------------------------------------------------
  function trendTime() {
    return (performance.now() - S.trendStart) / 1000;
  }

  function onMeter(m) {
    if (m.mode === 'volt') {
      for (const ch of [0, 1]) {
        const el = $(`volt-c${ch + 1}`);
        el.textContent = m.have ? DSP.signedVolts(m.value[ch]) : '…';
        el.classList.toggle('bad', m.over[ch]);
        $(`volt-mm${ch + 1}`).textContent = m.over[ch]
          ? 'OVER RANGE'
          : m.haveMinMax ? `min ${DSP.signedVolts(m.min[ch])}   max ${DSP.signedVolts(m.max[ch])}` : '';
      }
      if (m.have) {
        voltTrend.t.push(trendTime());
        voltTrend.c1.push(m.value[0]);
        voltTrend.c2.push(m.value[1]);
        voltChart.draw();
      }
    } else if (m.mode === 'ohm') {
      const r = m.r - (m.rel ? m.relOffset : 0);
      const el = $('ohm-value');
      el.textContent = !m.have ? '…' : m.open ? 'OPEN' : DSP.si(r, 'Ω', 4);
      el.classList.toggle('bad', m.have && m.open);
      el.classList.toggle('good', m.have && !m.open && r < 10);
      const ranges = ['1k', '10k', '100k', '1M'];
      $('ohm-info').textContent =
        `range ${ranges[m.range] || '?'}` + (m.rel ? `   zeroed −${DSP.si(m.relOffset, 'Ω', 3)}` : '') +
        (m.have && !m.open && r < 10 ? '   continuity' : '');
      $('btn-ohm-zero').textContent = m.rel ? 'Zero off' : 'Zero leads';
      if (m.have && !m.open) {
        ohmTrend.t.push(trendTime());
        ohmTrend.r.push(r);
        ohmChart.draw();
      }
    } else if (m.mode === 'cap') {
      const el = $('cap-value');
      const c = m.c - (m.rel ? m.relOffset : 0);
      if (m.state === 'measuring') el.textContent = 'Measuring…';
      else if (m.state === 'error') el.textContent = '—';
      else if (m.haveResult) el.textContent = c < 0.1e-9 ? '<0.1 nF' : DSP.si(c, 'F', 3);
      el.classList.toggle('bad', m.state === 'error');
      $('cap-info').textContent = m.state === 'error' ? m.error : m.rel ? `zeroed −${DSP.si(m.relOffset, 'F', 3)}` : '';
      $('cap-live').textContent = m.haveLive ? `C1+ ${m.liveV.toFixed(2)} V` + (m.liveV > 0.5 ? ' — discharge first' : '') : '';
      $('cap-live').style.color = m.haveLive && m.liveV > 0.5 ? 'var(--bad)' : '';
      $('btn-cap-measure').disabled = m.state === 'measuring';
      $('btn-cap-zero').textContent = m.rel ? 'Zero off' : 'Zero';
      if (m.state === 'result' && S.capLastState === 'measuring') {
        const row = $('tbl-cap').tBodies[0].insertRow(0);
        row.insertCell().textContent = ++S.capCount;
        row.insertCell().textContent = new Date().toLocaleTimeString();
        row.insertCell().textContent = el.textContent;
      }
      S.capLastState = m.state;
    }
  }

  $('btn-volt-reset').onclick = () => dev && dev.action('RESET_MINMAX');
  $('btn-volt-clear').onclick = () => {
    Object.values(voltTrend).forEach((g) => g.clear());
    S.trendStart = performance.now();
    voltChart.draw();
  };
  $('btn-volt-csv').onclick = () => {
    const lines = ['time_s,c1_V,c2_V'];
    for (let i = 0; i < voltTrend.t.n; i++)
      lines.push(`${voltTrend.t.a[i].toFixed(3)},${voltTrend.c1.a[i].toFixed(4)},${voltTrend.c2.a[i].toFixed(4)}`);
    download(`scopocket-volts-${stamp()}.csv`, lines.join('\n') + '\n');
  };
  $('btn-ohm-zero').onclick = () => dev && dev.action('ZERO');
  $('btn-ohm-clear').onclick = () => {
    Object.values(ohmTrend).forEach((g) => g.clear());
    S.trendStart = performance.now();
    ohmChart.draw();
  };
  $('btn-cap-measure').onclick = () => dev && dev.action('CAP_MEASURE');
  $('btn-cap-zero').onclick = () => dev && dev.action('ZERO');

  // ---- firmware update -----------------------------------------------------
  $('btn-bootsel').onclick = async () => {
    if (!dev) return;
    const ok = confirm(
      'Restart the Scopocket in firmware update mode?\n\n' +
        'It will disconnect and appear as a USB drive called RPI-RP2. Copy the new scopocket.uf2 onto that ' +
        'drive; it restarts by itself when the copy finishes.');
    if (!ok) return;
    dev.action('BOOTSEL');
    setTimeout(() => {
      disconnect();
      say('The Scopocket is in firmware update mode: copy scopocket.uf2 to the RPI-RP2 drive.');
    }, 300);
  };

  // For debugging from the console and for the UI tests.
  window.SPApp = { scope, spec, state: S, get device() { return dev; } };

  // ---- start ----------------------------------------------------------------
  initSupport();
  setConnected(null);
  if (new URLSearchParams(location.search).has('demo')) $('btn-demo').click();
})();
