// Scopocket USB protocol (must match Firmware/C/src/usbproto.h).
//
// Packet: 0xA5 | type u8 | length u16 LE | payload | crc8 (poly 0x07 over
// type, length and payload). Little-endian throughout.
(function (root, factory) {
  const m = factory();
  if (typeof module === 'object' && module.exports) module.exports = m;
  else root.SPProtocol = m;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const SYNC = 0xa5;
  const H = { HELLO: 0x01, PING: 0x02, SET: 0x03, ACTION: 0x04, STREAMS: 0x05, DEEP: 0x06, LOG: 0x07 };
  const D = {
    INFO: 0x81, SETTINGS: 0x82, STATUS: 0x83, FRAME: 0x84, METER: 0x85,
    DEEP_HDR: 0x86, DEEP_DAT: 0x87, DEEP_END: 0x88, LOG_DAT: 0x89,
  };
  const ACT = { REARM: 1, FORCE: 2, RESET_MINMAX: 3, ZERO: 4, CAP_MEASURE: 5, DEEP_CANCEL: 6, BOOTSEL: 0x7e };
  const ACQ_STATES = ['waiting', 'capturing', 'stopped', 'rolling'];
  const DEEP_STATES = ['idle', 'armed', 'capturing', 'done'];
  const MODES = ['scope', 'volt', 'ohm', 'cap'];
  const CAP_STATES = ['idle', 'measuring', 'result', 'error'];

  function crc8(bytes, crc = 0) {
    for (let i = 0; i < bytes.length; i++) {
      crc ^= bytes[i];
      for (let b = 0; b < 8; b++) crc = crc & 0x80 ? ((crc << 1) ^ 0x07) & 0xff : (crc << 1) & 0xff;
    }
    return crc;
  }

  function encode(type, payload) {
    payload = payload || new Uint8Array(0);
    const out = new Uint8Array(payload.length + 5);
    out[0] = SYNC;
    out[1] = type;
    out[2] = payload.length & 0xff;
    out[3] = payload.length >> 8;
    out.set(payload, 4);
    out[out.length - 1] = crc8(out.subarray(1, out.length - 1));
    return out;
  }

  const MAX_PAYLOAD = 8192;
  const KNOWN_TYPES = new Set([...Object.values(H), ...Object.values(D)]);

  // Incremental packet parser: push() bytes as they arrive. Unknown types and
  // oversized lengths are treated as a false sync, so line noise can't make
  // it swallow the packets that follow.
  class Parser {
    constructor(onPacket) {
      this.onPacket = onPacket;
      this.state = 0;
      this.errors = 0;
    }
    push(bytes) {
      for (let i = 0; i < bytes.length; i++) {
        const b = bytes[i];
        switch (this.state) {
          case 0:
            if (b === SYNC) this.state = 1;
            break;
          case 1:
            if (KNOWN_TYPES.has(b)) {
              this.type = b;
              this.state = 2;
            } else {
              this.state = b === SYNC ? 1 : 0;
            }
            break;
          case 2:
            this.len = b;
            this.state = 3;
            break;
          case 3:
            this.len |= b << 8;
            if (this.len > MAX_PAYLOAD) {
              this.errors++;
              this.state = 0;
              break;
            }
            this.buf = new Uint8Array(this.len);
            this.got = 0;
            this.state = this.len ? 4 : 5;
            break;
          case 4: {
            // copy as much as possible in one go
            const n = Math.min(this.len - this.got, bytes.length - i);
            this.buf.set(bytes.subarray(i, i + n), this.got);
            this.got += n;
            i += n - 1;
            if (this.got === this.len) this.state = 5;
            break;
          }
          case 5: {
            const hdr = new Uint8Array([this.type, this.len & 0xff, this.len >> 8]);
            const crc = crc8(this.buf, crc8(hdr));
            if (crc === b) this.onPacket(this.type, this.buf);
            else this.errors++;
            this.state = 0;
            break;
          }
        }
      }
    }
  }

  // ---- host -> device -------------------------------------------------------
  const u32 = (v) => new Uint8Array([v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >>> 24) & 0xff]);
  const host = {
    hello: () => encode(H.HELLO),
    ping: () => encode(H.PING),
    set: (id, index) => encode(H.SET, new Uint8Array([id, index])),
    action: (a) => encode(H.ACTION, new Uint8Array([a])),
    streams: (frames, meters) => encode(H.STREAMS, new Uint8Array([(frames ? 1 : 0) | (meters ? 2 : 0)])),
    deep: (n) => encode(H.DEEP, u32(n)),
    log: (on) => encode(H.LOG, new Uint8Array([on ? 1 : 0])),
  };

  // ---- device -> host decoding ------------------------------------------------
  const utf8 = (bytes) =>
    typeof TextDecoder !== 'undefined' ? new TextDecoder().decode(bytes) : Buffer.from(bytes).toString('utf8');

  function decode(type, p) {
    const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
    switch (type) {
      case D.INFO:
        return { kind: 'info', info: JSON.parse(utf8(p)) };
      case D.SETTINGS: {
        const n = p[0];
        const values = Array.from(p.subarray(1, 1 + n));
        return { kind: 'settings', values, visible: dv.getUint16(1 + n, true), enabled: dv.getUint16(3 + n, true) };
      }
      case D.STATUS: {
        const flags = p[2];
        return {
          kind: 'status',
          mode: MODES[p[0]] || 'scope',
          modeIndex: p[0],
          acqState: ACQ_STATES[p[1]] || 'waiting',
          menuOpen: !!(flags & 1),
          lowBattery: !!(flags & 2),
          logging: !!(flags & 4),
          logPossible: !!(flags & 8),
          overruns: dv.getUint32(3, true),
          deepState: DEEP_STATES[p[7]] || 'idle',
          deepProgress: dv.getUint32(8, true),
          logDropped: dv.getUint32(12, true),
        };
      }
      case D.FRAME: {
        const seq = dv.getUint32(0, true), rate = dv.getUint32(4, true), flags = p[8], n = dv.getUint16(9, true);
        const c1 = new Float32Array(n), c2 = new Float32Array(n);
        for (let i = 0; i < n; i++) {
          c1[i] = dv.getInt32(11 + 4 * i, true) * 1e-6;
          c2[i] = dv.getInt32(11 + 4 * (n + i), true) * 1e-6;
        }
        return {
          kind: 'frame', seq, rate, n, c1, c2,
          triggered: !!(flags & 1), hasC2: !!(flags & 2), roll: !!(flags & 4), diff: !!(flags & 8),
        };
      }
      case D.METER: {
        const mode = MODES[p[0]] || 'volt';
        if (mode === 'volt') {
          const f = p[1];
          return {
            kind: 'meter', mode, have: !!(f & 1), over: [!!(f & 2), !!(f & 4)], haveMinMax: !!(f & 8),
            value: [dv.getFloat32(2, true), dv.getFloat32(6, true)],
            min: [dv.getFloat32(10, true), dv.getFloat32(18, true)],
            max: [dv.getFloat32(14, true), dv.getFloat32(22, true)],
          };
        }
        if (mode === 'ohm') {
          const f = p[1];
          return {
            kind: 'meter', mode, have: !!(f & 1), open: !!(f & 2), rel: !!(f & 4),
            range: p[2], r: dv.getFloat32(3, true), relOffset: dv.getFloat32(7, true),
          };
        }
        const f = p[2];
        let end = 15;
        while (end < p.length && p[end] !== 0) end++;
        return {
          kind: 'meter', mode: 'cap', state: CAP_STATES[p[1]] || 'idle',
          haveResult: !!(f & 1), rel: !!(f & 2), haveLive: !!(f & 4),
          c: dv.getFloat32(3, true), relOffset: dv.getFloat32(7, true), liveV: dv.getFloat32(11, true),
          error: utf8(p.subarray(15, end)),
        };
      }
      case D.DEEP_HDR: {
        const flags = p[12];
        return {
          kind: 'deepHdr', count: dv.getUint32(0, true), rate: dv.getUint32(4, true), pre: dv.getUint32(8, true),
          triggered: !!(flags & 1), hasC2: !!(flags & 2), diff: !!(flags & 8), lsbUv: dv.getUint32(13, true),
        };
      }
      case D.DEEP_DAT: {
        const offset = dv.getUint32(0, true), n = dv.getUint16(4, true);
        const c1 = new Int16Array(n), c2 = new Int16Array(n);
        for (let i = 0; i < n; i++) {
          c1[i] = dv.getInt16(6 + 2 * i, true);
          c2[i] = dv.getInt16(6 + 2 * (n + i), true);
        }
        return { kind: 'deepData', offset, n, c1, c2 };
      }
      case D.DEEP_END:
        return { kind: 'deepEnd' };
      case D.LOG_DAT: {
        const index = dv.getUint32(0, true), n = dv.getUint16(4, true);
        const c1 = new Int16Array(n), c2 = new Int16Array(n);
        for (let i = 0; i < n; i++) {
          c1[i] = dv.getInt16(6 + 4 * i, true);
          c2[i] = dv.getInt16(8 + 4 * i, true);
        }
        return { kind: 'log', index, n, c1, c2 };
      }
      default:
        return { kind: 'unknown', type };
    }
  }

  // ---- device -> host encoding (used by the simulator and tests) -------------
  class Writer {
    constructor(size) {
      this.buf = new Uint8Array(size);
      this.dv = new DataView(this.buf.buffer);
      this.o = 0;
    }
    u8(v) { this.dv.setUint8(this.o, v); this.o += 1; return this; }
    u16(v) { this.dv.setUint16(this.o, v, true); this.o += 2; return this; }
    u32(v) { this.dv.setUint32(this.o, v >>> 0, true); this.o += 4; return this; }
    i16(v) { this.dv.setInt16(this.o, v, true); this.o += 2; return this; }
    i32(v) { this.dv.setInt32(this.o, v, true); this.o += 4; return this; }
    f32(v) { this.dv.setFloat32(this.o, v, true); this.o += 4; return this; }
    bytes() { return this.buf.subarray(0, this.o); }
  }
  const clamp16 = (v) => Math.max(-32768, Math.min(32767, Math.round(v)));

  const device = {
    info: (obj) => encode(D.INFO, new TextEncoder().encode(JSON.stringify(obj))),
    settings: (values, visible, enabled) => {
      const w = new Writer(values.length + 5).u8(values.length);
      values.forEach((v) => w.u8(v));
      return encode(D.SETTINGS, w.u16(visible).u16(enabled).bytes());
    },
    status: (s) => {
      const flags = (s.menuOpen ? 1 : 0) | (s.lowBattery ? 2 : 0) | (s.logging ? 4 : 0) | (s.logPossible ? 8 : 0);
      return encode(
        D.STATUS,
        new Writer(16).u8(s.modeIndex).u8(ACQ_STATES.indexOf(s.acqState)).u8(flags).u32(s.overruns || 0)
          .u8(DEEP_STATES.indexOf(s.deepState)).u32(s.deepProgress || 0).u32(s.logDropped || 0).bytes(),
      );
    },
    frame: (f) => {
      const n = f.c1.length;
      const w = new Writer(11 + 8 * n).u32(f.seq).u32(f.rate)
        .u8((f.triggered ? 1 : 0) | (f.hasC2 ? 2 : 0) | (f.roll ? 4 : 0) | (f.diff ? 8 : 0)).u16(n);
      for (let i = 0; i < n; i++) w.i32(Math.round(f.c1[i] * 1e6));
      for (let i = 0; i < n; i++) w.i32(Math.round(f.c2[i] * 1e6));
      return encode(D.FRAME, w.bytes());
    },
    meter: (m) => {
      const w = new Writer(96);
      if (m.mode === 'volt') {
        w.u8(1).u8((m.have ? 1 : 0) | (m.over[0] ? 2 : 0) | (m.over[1] ? 4 : 0) | (m.haveMinMax ? 8 : 0));
        w.f32(m.value[0]).f32(m.value[1]).f32(m.min[0]).f32(m.max[0]).f32(m.min[1]).f32(m.max[1]);
      } else if (m.mode === 'ohm') {
        w.u8(2).u8((m.have ? 1 : 0) | (m.open ? 2 : 0) | (m.rel ? 4 : 0)).u8(m.range).f32(m.r).f32(m.relOffset);
      } else {
        w.u8(3).u8(CAP_STATES.indexOf(m.state)).u8((m.haveResult ? 1 : 0) | (m.rel ? 2 : 0) | (m.haveLive ? 4 : 0));
        w.f32(m.c).f32(m.relOffset).f32(m.liveV);
        for (const ch of m.error || '') w.u8(ch.charCodeAt(0));
        w.u8(0);
      }
      return encode(D.METER, w.bytes());
    },
    deepHdr: (h) =>
      encode(D.DEEP_HDR, new Writer(17).u32(h.count).u32(h.rate).u32(h.pre)
        .u8((h.triggered ? 1 : 0) | (h.hasC2 ? 2 : 0) | (h.diff ? 8 : 0)).u32(h.lsbUv).bytes()),
    deepData: (offset, c1, c2) => {
      const n = c1.length, w = new Writer(6 + 4 * n).u32(offset).u16(n);
      for (let i = 0; i < n; i++) w.i16(clamp16(c1[i]));
      for (let i = 0; i < n; i++) w.i16(clamp16(c2[i]));
      return encode(D.DEEP_DAT, w.bytes());
    },
    deepEnd: () => encode(D.DEEP_END),
    logData: (index, c1, c2) => {
      const n = c1.length, w = new Writer(6 + 4 * n).u32(index).u16(n);
      for (let i = 0; i < n; i++) w.i16(clamp16(c1[i])).i16(clamp16(c2[i]));
      return encode(D.LOG_DAT, w.bytes());
    },
  };

  // Host-side decode of host packets (used by the simulator).
  function decodeHost(type, p) {
    const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
    switch (type) {
      case H.HELLO: return { kind: 'hello' };
      case H.PING: return { kind: 'ping' };
      case H.SET: return { kind: 'set', id: p[0], index: p[1] };
      case H.ACTION: return { kind: 'action', action: p[0] };
      case H.STREAMS: return { kind: 'streams', frames: !!(p[0] & 1), meters: !!(p[0] & 2) };
      case H.DEEP: return { kind: 'deep', n: dv.getUint32(0, true) };
      case H.LOG: return { kind: 'log', on: !!p[0] };
      default: return { kind: 'unknown', type };
    }
  }

  return {
    SYNC, H, D, ACT, ACQ_STATES, DEEP_STATES, MODES, CAP_STATES,
    crc8, encode, Parser, host, decode, device, decodeHost, Writer,
  };
});
