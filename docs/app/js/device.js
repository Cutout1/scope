// Device connection: transports (Web Serial, simulator) and the protocol
// client that keeps the device's info/settings/status and emits data events.
(function (root, factory) {
  const m = factory(root);
  if (typeof module === 'object' && module.exports) module.exports = m;
  else root.SPDevice = m;
})(typeof self !== 'undefined' ? self : this, function (root) {
  'use strict';
  const P = typeof module === 'object' && module.exports ? require('./protocol.js') : root.SPProtocol;

  const USB_FILTERS = [{ usbVendorId: 0x2e8a }]; // Raspberry Pi (RP2040)

  // ---- Web Serial transport ------------------------------------------------
  class SerialTransport {
    constructor(port) {
      this.port = port;
      this.name = 'USB';
    }
    static supported() {
      return typeof navigator !== 'undefined' && 'serial' in navigator;
    }
    static async request() {
      return new SerialTransport(await navigator.serial.requestPort({ filters: USB_FILTERS }));
    }
    static async previouslyGranted() {
      if (!SerialTransport.supported()) return [];
      const ports = await navigator.serial.getPorts();
      return ports.map((p) => new SerialTransport(p));
    }
    async open(onData, onClose) {
      // The baud rate is irrelevant for USB CDC, but must not be 1200, which
      // tells the RP2040 to reboot into its bootloader.
      await this.port.open({ baudRate: 115200, bufferSize: 65536 });
      try {
        await this.port.setSignals({ dataTerminalReady: true, requestToSend: true });
      } catch (e) {
        /* not all platforms support it; the firmware doesn't need DTR */
      }
      this.writer = this.port.writable.getWriter();
      this.closing = false;
      (async () => {
        while (this.port.readable && !this.closing) {
          this.reader = this.port.readable.getReader();
          try {
            for (;;) {
              const { value, done } = await this.reader.read();
              if (done) break;
              if (value) onData(value);
            }
          } catch (e) {
            break; // device unplugged or port closed
          } finally {
            this.reader.releaseLock();
          }
        }
        if (!this.closing && onClose) onClose();
      })();
    }
    write(bytes) {
      return this.writer.write(bytes).catch(() => {});
    }
    async close() {
      this.closing = true;
      try { await this.reader?.cancel(); } catch (e) { /* ignore */ }
      try { this.writer?.releaseLock(); } catch (e) { /* ignore */ }
      try { await this.port.close(); } catch (e) { /* ignore */ }
    }
  }

  // ---- Simulator transport -------------------------------------------------
  class SimTransport {
    constructor(sim) {
      this.sim = sim;
      this.name = 'Demo';
    }
    async open(onData) {
      // Deliver asynchronously, like a real port.
      this.sim.open((bytes) => setTimeout(() => onData(bytes.slice()), 0));
    }
    write(bytes) {
      setTimeout(() => this.sim.write(bytes), 0);
    }
    async close() {
      this.sim.close();
    }
  }

  // ---- Protocol client --------------------------------------------------------
  class Device {
    constructor(transport) {
      this.transport = transport;
      this.handlers = {};
      this.info = null;
      this.settings = null;
      this.status = null;
      this.parser = new P.Parser((type, payload) => this.onPacket(type, payload));
      this.deep = null;
    }
    on(ev, fn) {
      (this.handlers[ev] ||= []).push(fn);
      return this;
    }
    emit(ev, data) {
      (this.handlers[ev] || []).forEach((fn) => fn(data));
    }

    async connect() {
      await this.transport.open(
        (bytes) => this.parser.push(bytes),
        () => this.emit('disconnect'),
      );
      this.send(P.host.hello());
      this.send(P.host.streams(true, true));
      this.pinger = setInterval(() => this.send(P.host.ping()), 1000);
      // Retry HELLO until INFO arrives (the device may still be booting).
      this.helloRetry = setInterval(() => {
        if (!this.info) this.send(P.host.hello());
      }, 1000);
    }
    async disconnect() {
      clearInterval(this.pinger);
      clearInterval(this.helloRetry);
      await this.transport.close();
    }
    send(bytes) {
      this.transport.write(bytes);
    }

    // ---- commands ----
    setting(key) {
      return this.info && this.info.settings.find((s) => s.key === key);
    }
    value(key) {
      const s = this.setting(key);
      return s && this.settings ? this.settings.values[s.id] : undefined;
    }
    numeric(key) {
      const s = this.setting(key);
      const v = this.value(key);
      return s && s.values && v !== undefined ? s.values[v] : undefined;
    }
    visible(key) {
      const s = this.setting(key);
      return !!(s && this.settings && this.settings.visible & (1 << s.id));
    }
    enabled(key) {
      const s = this.setting(key);
      return !!(s && this.settings && this.settings.enabled & (1 << s.id));
    }
    set(key, index) {
      const s = this.setting(key);
      if (!s) return;
      index = Math.max(0, Math.min(s.options.length - 1, index));
      this.send(P.host.set(s.id, index));
      // Optimistic local update; the device confirms with SETTINGS.
      if (this.settings) {
        this.settings.values[s.id] = index;
        this.emit('settings', this.settings);
      }
    }
    action(name) {
      this.send(P.host.action(P.ACT[name]));
    }
    startDeep(n) {
      this.deep = null;
      this.send(P.host.deep(n));
    }
    setLogging(on) {
      this.send(P.host.log(on));
    }

    // ---- incoming ----
    onPacket(type, payload) {
      let m;
      try {
        m = P.decode(type, payload);
      } catch (e) {
        console.warn('bad packet', type, e);
        return;
      }
      switch (m.kind) {
        case 'info':
          this.info = m.info;
          this.emit('info', m.info);
          break;
        case 'settings':
          this.settings = m;
          this.emit('settings', m);
          break;
        case 'status':
          this.status = m;
          this.emit('status', m);
          break;
        case 'frame':
          this.emit('frame', m);
          break;
        case 'meter':
          this.emit('meter', m);
          break;
        case 'deepHdr':
          this.deep = { ...m, c1: new Float32Array(m.count), c2: new Float32Array(m.count), received: 0 };
          this.emit('deepProgress', this.deep);
          break;
        case 'deepData': {
          const d = this.deep;
          if (!d) break;
          const k = d.lsbUv * 1e-6;
          for (let i = 0; i < m.n && m.offset + i < d.count; i++) {
            d.c1[m.offset + i] = m.c1[i] * k;
            d.c2[m.offset + i] = m.c2[i] * k;
          }
          d.received = Math.max(d.received, m.offset + m.n);
          this.emit('deepProgress', d);
          break;
        }
        case 'deepEnd':
          if (this.deep) this.emit('deep', this.deep);
          break;
        case 'log': {
          const k = (this.info ? this.info.deep_lsb_uv : 256) * 1e-6;
          const c1 = new Float32Array(m.n), c2 = new Float32Array(m.n);
          for (let i = 0; i < m.n; i++) {
            c1[i] = m.c1[i] * k;
            c2[i] = m.c2[i] * k;
          }
          this.emit('log', { index: m.index, c1, c2 });
          break;
        }
      }
    }
  }

  return { Device, SerialTransport, SimTransport, USB_FILTERS };
});
