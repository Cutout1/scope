// Node tests for the protocol, DSP and simulator (no browser needed):
//   node tests/run_tests.js
'use strict';
const assert = require('assert');
const P = require('../js/protocol.js');
const DSP = require('../js/dsp.js');
const { SimDevice, INFO } = require('../js/sim.js');
const { Device, SimTransport } = require('../js/device.js');

let passed = 0;
const tests = [];
const test = (name, fn) => tests.push([name, fn]);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const near = (a, b, tol, msg) => assert.ok(Math.abs(a - b) <= tol, `${msg}: ${a} vs ${b} (±${tol})`);

// ---------------------------------------------------------------------------
test('crc8 matches the firmware (poly 0x07, init 0)', () => {
  // CRC-8/SMBUS check value for "123456789" is 0xF4
  assert.strictEqual(P.crc8(Buffer.from('123456789')), 0xf4);
});

test('parser: round trip, fragmentation, garbage and bad CRC', () => {
  const got = [];
  const parser = new P.Parser((t, p) => got.push(P.decode(t, p)));
  const status = P.device.status({ modeIndex: 0, acqState: 'stopped', menuOpen: true, lowBattery: false,
    logging: true, logPossible: true, overruns: 7, deepState: 'capturing', deepProgress: 1234, logDropped: 9 });
  const frame = P.device.frame({ seq: 42, rate: 500000, c1: [0.5, -1.25], c2: [0, 3.3], triggered: true,
    hasC2: true, roll: false, diff: true });
  const bad = P.device.deepEnd();
  bad[bad.length - 1] ^= 0xff;
  const stream = Buffer.concat([Buffer.from([1, 2, 0xa5, 0x99]), status, bad, frame]);
  for (let i = 0; i < stream.length; i += 3) parser.push(stream.subarray(i, i + 3)); // tiny chunks
  // The garbage 0xA5 0x99 swallows a few bytes until resync; the frame must survive.
  const f = got.find((m) => m.kind === 'frame');
  assert.ok(f, 'frame decoded');
  assert.strictEqual(f.seq, 42);
  assert.strictEqual(f.rate, 500000);
  near(f.c1[1], -1.25, 1e-6, 'c1');
  near(f.c2[1], 3.3, 1e-6, 'c2');
  assert.ok(f.triggered && f.hasC2 && f.diff && !f.roll);
  assert.ok(!got.some((m) => m.kind === 'deepEnd'), 'corrupted packet rejected');

  // Clean stream decodes everything
  const got2 = [];
  const p2 = new P.Parser((t, p) => got2.push(P.decode(t, p)));
  p2.push(Buffer.concat([status, frame]));
  const s = got2[0];
  assert.deepStrictEqual(
    [s.acqState, s.menuOpen, s.logging, s.overruns, s.deepState, s.deepProgress, s.logDropped],
    ['stopped', true, true, 7, 'capturing', 1234, 9]);
});

test('meter packets decode like the firmware layout', () => {
  const dec = (bytes) => {
    let m;
    new P.Parser((t, p) => (m = P.decode(t, p))).push(bytes);
    return m;
  };
  const v = dec(P.device.meter({ mode: 'volt', have: true, over: [false, true], haveMinMax: true,
    value: [1.5, -0.25], min: [1, -1], max: [2, 0.5] }));
  assert.deepStrictEqual([v.mode, v.have, v.over[1], v.value[0], v.min[1], v.max[1]], ['volt', true, true, 1.5, -1, 0.5]);
  const o = dec(P.device.meter({ mode: 'ohm', have: true, open: false, rel: true, range: 2, r: 4700, relOffset: 0.5 }));
  assert.deepStrictEqual([o.range, o.r, o.rel, o.relOffset], [2, 4700, true, 0.5]);
  const c = dec(P.device.meter({ mode: 'cap', state: 'error', haveResult: false, rel: false, haveLive: true,
    c: 0, relOffset: 0, liveV: 1.5, error: 'Discharge to <0.5V first' }));
  assert.deepStrictEqual([c.state, c.error, c.liveV], ['error', 'Discharge to <0.5V first', 1.5]);
});

test('host commands encode/decode', () => {
  const got = [];
  const p = new P.Parser((t, b) => got.push(P.decodeHost(t, b)));
  p.push(Buffer.concat([P.host.set(6, 41), P.host.deep(24000), P.host.action(P.ACT.FORCE), P.host.log(true),
    P.host.streams(true, false)]));
  assert.deepStrictEqual(got.map((m) => m.kind), ['set', 'deep', 'action', 'log', 'streams']);
  assert.strictEqual(got[0].index, 41);
  assert.strictEqual(got[1].n, 24000);
  assert.strictEqual(got[4].meters, false);
});

// ---------------------------------------------------------------------------
test('measurements: sine frequency, Vpp, RMS', () => {
  const rate = 10000, f = 123.4, n = 4000;
  const v = new Float32Array(n);
  for (let i = 0; i < n; i++) v[i] = 1.5 * Math.sin((2 * Math.PI * f * i) / rate) + 0.2;
  const m = DSP.measure(v, rate);
  near(m.freq, f, 0.05, 'freq');
  near(m.pp, 3.0, 0.01, 'pp');
  near(m.mean, 0.2, 0.005, 'mean');
  near(m.acrms, 1.5 / Math.SQRT2, 0.005, 'ac rms');
  near(m.duty, 0.5, 0.02, 'duty');
});

test('measurements: square duty cycle', () => {
  const rate = 100000, f = 1000, n = 20000;
  const v = new Float32Array(n);
  for (let i = 0; i < n; i++) v[i] = ((i * f) / rate) % 1 < 0.25 ? 3.3 : 0;
  const m = DSP.measure(v, rate);
  near(m.freq, f, 1, 'freq');
  near(m.duty, 0.25, 0.01, 'duty');
});

test('spectrum: peak frequency and level of a sine', () => {
  const rate = 50000, f = 1234, n = 8192, a = 1.0;
  const v = new Float32Array(n);
  for (let i = 0; i < n; i++) v[i] = a * Math.sin((2 * Math.PI * f * i) / rate);
  const s = DSP.spectrum(v, rate, { window: 'flattop' });
  near(s.peakFreq, f, rate / s.size, 'peak freq');
  near(s.peakDbv, 20 * Math.log10(a / Math.SQRT2), 0.1, 'peak dBV (flat-top)');
});

test('si formatting', () => {
  assert.strictEqual(DSP.si(0.0047, 'V'), '4.70 mV');
  assert.strictEqual(DSP.si(1234.5, 'Hz'), '1.23 kHz');
  assert.strictEqual(DSP.si(10e-9, 'F'), '10.0 nF');
});

// ---------------------------------------------------------------------------
async function connectSim(opts = {}) {
  const sim = new SimDevice({ realtime: false, noise: opts.noise ?? 0.002 });
  const dev = new Device(new SimTransport(sim));
  const events = { frame: [], meter: [], log: [], deep: null, status: null };
  dev.on('frame', (f) => events.frame.push(f));
  dev.on('meter', (m) => events.meter.push(m));
  dev.on('log', (l) => events.log.push(l));
  dev.on('deep', (d) => (events.deep = d));
  dev.on('status', (s) => (events.status = s));
  await dev.connect();
  await sleep(20);
  const run = async (seconds, step = 0.02) => {
    for (let t = 0; t < seconds; t += step) {
      sim.tick(step);
      await sleep(0);
    }
    await sleep(10);
  };
  return { sim, dev, events, run };
}

const idx = (key, label) => {
  const s = INFO.settings.find((x) => x.key === key);
  const i = s.options.indexOf(label);
  assert.ok(i >= 0, `${key} has option ${label}: ${s.options}`);
  return i;
};

test('device <-> simulator: info, settings, triggered frames', async () => {
  const { sim, dev, events, run } = await connectSim();
  assert.ok(dev.info && dev.info.settings.length === INFO.settings.length, 'info received');
  assert.ok(dev.settings, 'settings received');
  dev.set('wave_freq', idx('wave_freq', '100Hz'));
  dev.set('rate', idx('rate', '10kS/s'));
  dev.set('trig_level', idx('trig_level', '+1V'));
  await sleep(10);
  assert.strictEqual(sim.rate, 10000);
  await run(0.5);
  assert.ok(events.frame.length > 3, `frames: ${events.frame.length}`);
  const f = events.frame[events.frame.length - 1];
  assert.ok(f.triggered, 'triggered');
  assert.strictEqual(f.rate, 10000);
  // Rising through +1 V at the trigger point (sample 20)
  assert.ok(f.c1[19] < 1.05 && f.c1[21] > 0.95, `crossing at trigger: ${f.c1[19]} ${f.c1[21]}`);
  await dev.disconnect();
});

test('deep capture: 8000 samples, correct frequency and spectrum', async () => {
  const { dev, events, run } = await connectSim();
  dev.set('wave_freq', idx('wave_freq', '50Hz'));
  dev.set('rate', idx('rate', '20kS/s'));
  await sleep(10);
  dev.startDeep(8000);
  await sleep(10);
  await run(0.8);
  const d = events.deep;
  assert.ok(d, 'deep capture received');
  assert.strictEqual(d.count, 8000);
  assert.strictEqual(d.received, 8000);
  const m = DSP.measure(d.c1, d.rate);
  near(m.freq, 50, 0.2, 'freq');
  near(m.pp, 6.6, 0.15, 'Vpp (3.3 V sine)');
  const s = DSP.spectrum(d.c1, d.rate);
  near(s.peakFreq, 50, 1, 'spectrum peak');
  await dev.disconnect();
});

test('logging stream: continuous indices at 1 kS/s', async () => {
  const { dev, events, run } = await connectSim();
  dev.set('rate', idx('rate', '1kS/s'));
  await sleep(10);
  dev.setLogging(true);
  await sleep(10);
  await run(1.0);
  dev.setLogging(false);
  let expect = 0, total = 0;
  for (const l of events.log) {
    assert.strictEqual(l.index, expect, 'contiguous');
    expect += l.c1.length;
    total += l.c1.length;
  }
  near(total, 1000, 30, 'samples in 1 s');
  assert.ok(events.status.logPossible);
  await dev.disconnect();
});

test('logging refused above the stream limit', async () => {
  const { dev, events, run } = await connectSim();
  dev.set('rate', idx('rate', '500kS/s'));
  await sleep(10);
  dev.setLogging(true);
  await sleep(10);
  await run(0.1);
  assert.strictEqual(events.log.length, 0);
  assert.ok(!events.status.logPossible);
  await dev.disconnect();
});

test('meters: volt, ohm (zero), cap (measure)', async () => {
  const { dev, events, run } = await connectSim();
  dev.set('mode', idx('mode', 'Volt'));
  await sleep(10);
  await run(0.3);
  assert.ok(events.meter.some((m) => m.mode === 'volt' && m.have));
  dev.set('mode', idx('mode', 'Ohm'));
  await sleep(10);
  await run(0.3);
  const o = events.meter.filter((m) => m.mode === 'ohm').pop();
  near(o.r, 4700, 20, 'ohms');
  dev.action('ZERO');
  await sleep(10);
  await run(0.2);
  assert.ok(events.meter.filter((m) => m.mode === 'ohm').pop().rel, 'zero on');
  dev.set('mode', idx('mode', 'Cap'));
  await sleep(10);
  await run(0.5); // let the live voltage settle
  dev.action('CAP_MEASURE');
  await sleep(10);
  await run(0.6);
  const c = events.meter.filter((m) => m.mode === 'cap').pop();
  assert.strictEqual(c.state, 'result');
  near(c.c, 10e-9, 0.2e-9, 'capacitance');
  await dev.disconnect();
});

// ---------------------------------------------------------------------------
(async () => {
  for (const [name, fn] of tests) {
    try {
      await fn();
      passed++;
      console.log(`  ok   ${name}`);
    } catch (e) {
      console.log(`  FAIL ${name}\n       ${e.message}`);
      process.exitCode = 1;
    }
  }
  console.log(`${passed}/${tests.length} passed`);
})();
