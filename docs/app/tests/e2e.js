// End-to-end UI test in demo mode (simulated device), using Playwright:
//   python3 -m http.server 8765 &   (in the app folder)
//   node tests/e2e.js [shots-dir]
// Needs the `playwright` package and its Firefox build (see README).
'use strict';
const { firefox } = require('playwright');
const assert = require('assert');

const URL = process.env.APP_URL || 'http://localhost:8765/?demo';
const SHOTS = process.argv[2] || 'shots';
require('fs').mkdirSync(SHOTS, { recursive: true });

(async () => {
  const browser = await firefox.launch();
  const page = await browser.newPage({ viewport: { width: 1360, height: 900 } });
  const errors = [];
  page.on('pageerror', (e) => errors.push(`pageerror: ${e.message}`));
  page.on('console', (m) => m.type() === 'error' && errors.push(`console: ${m.text()}`));
  const step = async (name, fn) => {
    process.stdout.write(`  ${name} … `);
    await fn();
    console.log('ok');
  };
  const pick = (key, label) =>
    page.selectOption(`#set-${key}`, { label });
  const text = (sel) => page.textContent(sel);

  await page.goto(URL);

  await step('demo connects and shows live frames', async () => {
    await page.waitForSelector('#view-scope:not([hidden])');
    await page.waitForFunction(() => /S\/s/.test(document.querySelector('#scope-badge').textContent));
    assert.match(await text('#chip-conn'), /Demo/);
    await page.screenshot({ path: `${SHOTS}/01_scope_default.png` });
  });

  await step('slow rates: sweep over the previous frame on a fixed axis', async () => {
    // Default is 100 S/s with a 1 Hz sine: each frame takes 1.6 s to fill.
    // Wait for one complete frame, then catch the next one mid-sweep.
    await page.waitForFunction(() => window.SPApp.state.lastFull, null, { timeout: 6000 });
    let swept = null;
    for (let k = 0; k < 40 && !swept; k++) {
      swept = await page.evaluate(() => {
        const r = window.SPApp.scope.rec;
        const gap = Array.from(r.c1).findIndex((x) => Number.isNaN(x));
        return gap > 25 && gap < 140 ? { gap, n: r.c1.length, range: window.SPApp.scope.range(),
          oldAhead: Number.isFinite(r.c1[gap + 10]) } : null;
      });
      if (!swept) await page.waitForTimeout(100);
    }
    assert.ok(swept, 'saw a frame mid-sweep');
    assert.strictEqual(swept.n, 160);
    assert.ok(swept.oldAhead, 'previous frame still drawn ahead of the sweep');
    assert.ok(Math.abs(swept.range[0] + 0.2) < 1e-9 && Math.abs(swept.range[1] - 1.39) < 1e-9, `axis ${swept.range}`);
    await page.screenshot({ path: `${SHOTS}/01b_sweep.png` });
  });

  await step('settings round-trip: 10 kS/s, 100 Hz sine, measured frequency', async () => {
    await pick('wave_freq', '100Hz');
    await pick('rate', '10kS/s');
    await page.waitForFunction(() => /10.0 kS\/s/.test(document.querySelector('#scope-badge').textContent));
    await page.waitForTimeout(600);
    const f = await text('#tbl-meas tbody tr.c1 td:nth-child(2)');
    assert.match(f, /^(99\.\d+|100\.\d+) Hz$/, `C1 frequency cell: ${f}`);
    await page.screenshot({ path: `${SHOTS}/02_scope_10k.png` });
  });

  await step('spectrum and cursors', async () => {
    await page.check('#chk-fft');
    await page.check('#chk-cursors');
    await page.waitForTimeout(500);
    assert.ok(await page.isVisible('#fft-wrap'));
    assert.match(await text('#cursor-read'), /Δt/);
    await page.screenshot({ path: `${SHOTS}/03_fft_cursors.png` });
    await page.uncheck('#chk-cursors');
  });

  await step('drag the trigger level line', async () => {
    const before = await page.$eval('#set-trig_level', (s) => s.value);
    const box = await page.locator('#cv-scope').boundingBox();
    // 0 V is the vertical centre of the plot area (top 12 px, bottom 26 px margins)
    const yZero = box.y + 12 + (box.height - 38) / 2;
    const xMid = box.x + box.width / 2;
    await page.mouse.move(xMid, yZero);
    await page.mouse.down();
    await page.mouse.move(xMid, yZero - (box.height - 38) / 8, { steps: 5 }); // +1 division = +1 V
    await page.mouse.up();
    await page.waitForTimeout(300);
    const after = await page.$eval('#set-trig_level', (s) => s.options[s.selectedIndex].text);
    assert.notStrictEqual(before, await page.$eval('#set-trig_level', (s) => s.value));
    assert.match(after, /^\+1(\.0)?V$/, `trigger level now ${after}`);
  });

  await step('long capture (5000 samples) and zoom', async () => {
    await page.check('#chk-fft');
    await page.selectOption('#sel-deep-len', '5000');
    await page.click('#btn-deep');
    await page.waitForFunction(() => /Captured 5000/.test(document.querySelector('#deep-status').textContent),
      null, { timeout: 10000 });
    assert.ok(await page.isVisible('#btn-live'));
    assert.match(await text('#scope-badge'), /Capture · 5000 pts/);
    await page.screenshot({ path: `${SHOTS}/04_capture.png` });
    const box = await page.locator('#cv-scope').boundingBox();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.wheel(0, -600);
    await page.waitForTimeout(300);
    await page.screenshot({ path: `${SHOTS}/05_capture_zoom.png` });
    await page.click('#btn-live');
    await page.uncheck('#chk-fft');
  });

  await step('data logging at 1 kS/s', async () => {
    await pick('rate', '1kS/s');
    await page.waitForTimeout(400);
    await page.click('#panel-log summary');
    await page.click('#btn-log');
    await page.waitForTimeout(2500);
    await page.click('#btn-log');
    const s = await text('#log-status');
    const n = +s.replace(/,/g, '').match(/^(\d+)/)[1];
    assert.ok(n > 1500 && n < 3500, `logged ${n} samples: ${s}`);
    await page.screenshot({ path: `${SHOTS}/06_logging.png` });
  });

  await step('XY display', async () => {
    await pick('rate', '10kS/s');
    await pick('display', 'XY');
    await page.waitForTimeout(600);
    await page.screenshot({ path: `${SHOTS}/07_xy.png` });
    await pick('display', 'Normal');
  });

  await step('voltmeter', async () => {
    await page.click('#modes button:has-text("Volt")');
    await page.waitForSelector('#view-volt:not([hidden])');
    await page.waitForFunction(() => /V$/.test(document.querySelector('#volt-c1').textContent));
    await page.waitForTimeout(1200);
    await page.screenshot({ path: `${SHOTS}/08_volt.png` });
  });

  await step('ohmmeter with zero', async () => {
    await page.click('#modes button:has-text("Ohm")');
    await page.waitForFunction(() => /kΩ$/.test(document.querySelector('#ohm-value').textContent));
    await page.click('#btn-ohm-zero');
    await page.waitForFunction(() => /zeroed/.test(document.querySelector('#ohm-info').textContent));
    await page.screenshot({ path: `${SHOTS}/09_ohm.png` });
    await page.click('#btn-ohm-zero');
  });

  await step('capacitance measurement', async () => {
    await page.click('#modes button:has-text("Cap")');
    await page.waitForSelector('#view-cap:not([hidden])');
    await page.waitForTimeout(800);
    await page.click('#btn-cap-measure');
    await page.waitForFunction(() => /nF$/.test(document.querySelector('#cap-value').textContent));
    assert.strictEqual(await page.locator('#tbl-cap tbody tr').count(), 1);
    await page.screenshot({ path: `${SHOTS}/10_cap.png` });
  });

  await step('back to scope, disconnect', async () => {
    await page.click('#modes button:has-text("Scope")');
    await page.waitForSelector('#view-scope:not([hidden])');
    await page.click('#btn-disconnect');
    await page.waitForSelector('#view-welcome:not([hidden])');
    await page.screenshot({ path: `${SHOTS}/11_welcome.png` });
  });

  await page.setViewportSize({ width: 390, height: 844 });
  await page.click('#btn-demo');
  await page.waitForSelector('#view-scope:not([hidden])');
  await page.waitForTimeout(800);
  await page.screenshot({ path: `${SHOTS}/12_phone.png`, fullPage: true });

  await browser.close();
  if (errors.length) {
    console.log('Browser errors:\n  ' + errors.join('\n  '));
    process.exitCode = 1;
  } else console.log('No browser errors.');
})().catch((e) => {
  console.error('\nFAILED:', e.message);
  process.exit(1);
});
