// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

// X4 over USB in the browser (Web Serial: Chrome / Edge on Windows, macOS,
// Linux and Android). Speaks the CrossPoint firmware's serial file commands
// (CMD:LS/GET/PUT/RM/MKDIR/MV/TIME, see SerialFileTransfer.h) and answers the
// same HTTP routes as the device's Wi-Fi file server (/api/files, /download,
// /upload, /delete, /mkdir, /rename, /move) by intercepting fetch(), so pages
// written for the Wi-Fi server work unchanged over the cable.
(function () {
  'use strict';

  const ESPRESSIF_VID = 0x303a;
  const CHUNK = 4096;
  const enc = new TextEncoder();
  const dec = new TextDecoder();

  const zh = (navigator.language || '').toLowerCase().startsWith('zh');
  const T = zh ? {
    connect: '连接 X4', disconnect: '断开', connected: '已通过 USB 连接 X4', notConnected: 'X4 未连接',
    unsupported: '此浏览器不支持 USB 连接。请在电脑或安卓手机上使用 Chrome 或 Edge。',
    notConnectedErr: 'X4 未连接：请插上 USB 线，并点击上方「连接 X4」。', noReply: 'X4 没有响应',
    checksum: '传输校验失败，请重试', hint: '插上 USB 线后点「连接 X4」，在列表中选择 X4（USB JTAG/serial）。'
  } : {
    connect: 'Connect X4', disconnect: 'Disconnect', connected: 'X4 connected over USB', notConnected: 'X4 not connected',
    unsupported: 'This browser cannot use USB devices. Use Chrome or Edge on a computer or an Android phone.',
    notConnectedErr: 'X4 not connected: plug in the USB cable and click "Connect X4" above.', noReply: 'No reply from the X4',
    checksum: 'Transfer failed its checksum, please retry', hint: 'Plug in the cable, click "Connect X4" and pick the X4 (USB JTAG/serial).'
  };

  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();
  function crc32(bytes) {
    let c = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  function b64ToBytes(s) {
    const bin = atob(s);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }

  class X4Serial extends EventTarget {
    constructor() {
      super();
      this.supported = 'serial' in navigator;
      this.port = null;
      this.writer = null;
      this.reader = null;
      this.lines = [];       // complete lines received, oldest first
      this.partial = [];     // bytes of the line in progress
      this.waiter = null;    // resolve() of whoever waits for the next line
      this.queue = Promise.resolve();
      this.busy = 0;
      this.connected = false;
      this.ready = Promise.resolve();
      if (this.supported) {
        navigator.serial.addEventListener('disconnect', (e) => { if (e.target === this.port) this.closed(); });
        navigator.serial.addEventListener('connect', () => this.autoConnect());
      }
    }

    // Reopens a port the user already granted on an earlier visit (no prompt).
    autoConnect() {
      if (!this.supported || this.connected) return this.ready;
      this.ready = (async () => {
        const ports = await navigator.serial.getPorts();
        const port = ports.find((p) => p.getInfo().usbVendorId === ESPRESSIF_VID) || ports[0];
        if (port) await this.open(port).catch(() => {});
      })();
      return this.ready;
    }

    async request() {
      const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: ESPRESSIF_VID }] });
      await this.open(port);
    }

    async open(port) {
      if (this.connected) return;
      await port.open({ baudRate: 115200, bufferSize: 64 * 1024 });
      this.port = port;
      this.writer = port.writable.getWriter();
      this.lines = [];
      this.partial = [];
      this.readLoop();
      try {
        await this.exclusive(() => this.command('HELLO', ['HELLO_OK'], 4000));
      } catch (e) {
        await this.disconnect();
        throw e;
      }
      this.connected = true;
      // Let the device show this page's address as its QR code.
      if (/^https?:/.test(location.protocol)) {
        const url = location.href.replace(/[?#].*$/, '').replace(/[^/]*$/, '');
        this.exclusive(() => this.command('WEBURL ' + url, ['WEBURL_OK', 'WEBURL_ERR'])).catch(() => {});
      }
      // Heartbeat: the device shows "browser connected" while it hears from us.
      this.heartbeat = setInterval(() => {
        if (!this.busy) this.exclusive(() => this.command('HELLO', ['HELLO_OK'], 3000)).catch(() => {});
      }, 3000);
      this.dispatchEvent(new Event('change'));
    }

    async readLoop() {
      try {
        this.reader = this.port.readable.getReader();
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          for (const b of value) {
            if (b === 10) {
              if (this.partial.length && this.partial[this.partial.length - 1] === 13) this.partial.pop();
              this.lines.push(Uint8Array.from(this.partial));
              this.partial = [];
            } else {
              this.partial.push(b);
            }
          }
          if (this.lines.length && this.waiter) { const w = this.waiter; this.waiter = null; w(); }
        }
      } catch (e) {
        // Port lost (unplugged or device reset).
      } finally {
        try { this.reader.releaseLock(); } catch (e) {}
        this.closed();
      }
    }

    closed() {
      if (!this.port) return;
      clearInterval(this.heartbeat);
      const port = this.port;
      this.port = null;
      this.connected = false;
      try { this.writer.releaseLock(); } catch (e) {}
      port.close().catch(() => {});
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
      this.dispatchEvent(new Event('change'));
    }

    async disconnect() {
      if (!this.port) return;
      try { await this.reader.cancel(); } catch (e) {}
      this.closed();
    }

    // Runs fn with the port to itself; commands never interleave.
    exclusive(fn) {
      const run = this.queue.then(async () => {
        this.busy++;
        try { return await fn(); } finally { this.busy--; }
      });
      this.queue = run.catch(() => {});
      return run;
    }

    async write(data) {
      if (!this.port) throw new Error(T.notConnectedErr);
      await this.writer.write(typeof data === 'string' ? enc.encode(data) : data);
    }

    async nextLine(timeoutMs) {
      const deadline = Date.now() + timeoutMs;
      while (!this.lines.length) {
        if (!this.port) throw new Error(T.notConnectedErr);
        const left = deadline - Date.now();
        if (left <= 0) throw new Error(T.noReply);
        await new Promise((resolve) => { this.waiter = resolve; setTimeout(resolve, left); });
      }
      return this.lines.shift();
    }

    // First line starting with one of prefixes; log lines are skipped.
    async expect(prefixes, timeoutMs = 10000) {
      const deadline = Date.now() + timeoutMs;
      for (;;) {
        const line = dec.decode(await this.nextLine(Math.max(1, deadline - Date.now())));
        if (prefixes.some((p) => line.startsWith(p))) return line;
      }
    }

    async command(line, prefixes, timeoutMs) {
      this.lines = [];
      await this.write('CMD:' + line + '\n');
      return this.expect(prefixes, timeoutMs);
    }

    // --- file commands (each holds the port for its whole exchange) ---------
    ls(path) {
      return this.exclusive(async () => {
        const head = await this.command('LS ' + path, ['LS_BEGIN', 'LS_ERR']);
        if (head.startsWith('LS_ERR')) throw new Error(head);
        const out = [];
        for (;;) {
          const text = dec.decode(await this.nextLine(10000));
          if (text === 'LS_END') return out;
          if (text.startsWith('E\t')) {
            const [, kind, size, ...rest] = text.split('\t');
            const name = rest.join('\t');
            out.push({ name, size: Number(size), isDirectory: kind === 'd', isEpub: /\.epub$/i.test(name) });
          }
        }
      });
    }

    get(path, onProgress) {
      return this.exclusive(async () => {
        for (let attempt = 0; attempt < 3; attempt++) {
          const head = await this.command('GET ' + path, ['GET_BEGIN', 'GET_ERR']);
          if (head.startsWith('GET_ERR')) throw new Error(head);
          const size = Number(head.split(' ')[1]);
          const data = new Uint8Array(size);
          let got = 0;
          let ok = true;
          for (;;) {
            const line = await this.nextLine(10000);
            if (line[0] === 68 && line[1] === 32) {  // "D "
              const chunk = b64ToBytes(dec.decode(line.subarray(2)));
              if (got + chunk.length > size) { ok = false; continue; }
              data.set(chunk, got);
              got += chunk.length;
              if (onProgress) onProgress(got, size);
            } else {
              const text = dec.decode(line);
              if (text.startsWith('GET_END')) {
                ok = ok && got === size && crc32(data) === parseInt(text.split(' ')[1], 16);
                break;
              }
              if (text.startsWith('GET_ERR')) throw new Error(text);
            }
          }
          if (ok) return data;  // otherwise an interleaved log line corrupted it: retry
        }
        throw new Error(T.checksum);
      });
    }

    put(path, data, onProgress) {
      return this.exclusive(async () => {
        const ready = await this.command(`PUT ${data.length} ${path}`, ['PUT_READY', 'PUT_ERR']);
        if (ready !== 'PUT_READY') throw new Error(ready);
        for (let off = 0; off < data.length; off += CHUNK) {
          await this.write(data.subarray(off, off + CHUNK));
          const ack = await this.expect(['ACK', 'PUT_ERR']);
          if (!ack.startsWith('ACK')) throw new Error(ack);
          if (onProgress) onProgress(Math.min(off + CHUNK, data.length), data.length);
        }
        const result = await this.expect(['PUT_OK', 'PUT_ERR'], 30000);
        if (!result.startsWith('PUT_OK')) throw new Error(result);
        if (parseInt(result.split(' ')[1], 16) !== crc32(data)) throw new Error(T.checksum);
      });
    }

    simple(line, verb) {
      return this.exclusive(async () => {
        const reply = await this.command(line, [verb + '_OK', verb + '_ERR']);
        if (!reply.startsWith(verb + '_OK')) throw new Error(reply);
      });
    }

    syncTime() {
      return this.exclusive(async () => {
        await new Promise((r) => setTimeout(r, 1000 - (Date.now() % 1000)));
        const offset = -new Date().getTimezoneOffset();
        await this.command(`TIME ${Math.round(Date.now() / 1000)} ${offset}`, ['TIME_OK', 'TIME_ERR']);
      });
    }
  }

  const x4 = new X4Serial();
  window.x4 = x4;
  window.X4Serial = X4Serial;  // for tests

  // --- fetch() shim: the Wi-Fi server's routes, served over USB -------------
  const ROUTE = /\/(api\/files|api\/settings|download|upload|delete|mkdir|rename|move|api\/status|api\/x4\/status|api\/x4\/sync-time|api\/weather\/refresh)$/;
  const realFetch = window.fetch.bind(window);
  const join = (dir, name) => (dir.endsWith('/') ? dir : dir + '/') + name;
  const dirname = (p) => p.replace(/\/[^/]*$/, '') || '/';
  const basename = (p) => p.replace(/^.*\//, '');

  window.fetch = async (input, init = {}) => {
    const url = new URL(typeof input === 'string' ? input : input.url, location.href);
    const m = url.pathname.match(ROUTE);
    if (!m || url.origin !== location.origin) return realFetch(input, init);
    const route = m[1];
    const q = url.searchParams;
    const form = init.body instanceof URLSearchParams ? init.body : new URLSearchParams();
    const reply = (body, type = 'text/plain;charset=utf-8') => new Response(body, { status: 200, headers: { 'Content-Type': type } });
    if (route === 'api/x4/status') return reply(JSON.stringify({ connected: x4.connected }), 'application/json');
    if (route === 'api/status') return reply(JSON.stringify({ version: 'USB', ip: 'USB', serial: '', freeHeap: 0 }), 'application/json');
    await x4.ready;
    if (!x4.connected) return new Response(T.notConnectedErr, { status: 503 });
    try {
      switch (route) {
        case 'api/files':
          return reply(JSON.stringify(await x4.ls(q.get('path') || '/')), 'application/json');
        case 'download': {
          const path = q.get('path') || '';
          const data = await x4.get(path, (done, total) => window.dispatchEvent(new CustomEvent('x4-progress', { detail: { done, total } })));
          return new Response(data, { status: 200, headers: { 'Content-Type': 'application/octet-stream' } });
        }
        case 'upload': {
          const dir = q.get('path') || '/';
          const names = [];
          for (const [, value] of init.body.entries()) {
            if (!(value instanceof Blob)) continue;
            const data = new Uint8Array(await value.arrayBuffer());
            await x4.put(join(dir, value.name), data, (done, total) => window.dispatchEvent(new CustomEvent('x4-progress', { detail: { done, total } })));
            names.push(value.name);
          }
          return reply('File uploaded successfully: ' + names.join(', '));
        }
        case 'delete': {
          const paths = form.get('paths') ? JSON.parse(form.get('paths')) : [form.get('path')];
          for (const p of paths) await x4.simple('RM ' + p, 'RM');
          return reply('Deleted');
        }
        case 'mkdir':
          await x4.simple('MKDIR ' + join(form.get('path') || '/', form.get('name')), 'MKDIR');
          return reply('Folder created');
        case 'rename': {
          const src = form.get('path');
          await x4.simple(`MV ${src}\t${join(dirname(src), form.get('name'))}`, 'MV');
          return reply('Renamed');
        }
        case 'move': {
          const src = form.get('path');
          await x4.simple(`MV ${src}\t${join(form.get('dest') || '/', basename(src))}`, 'MV');
          return reply('Moved');
        }
        case 'api/settings': {
          // Only the lock-screen mode is exposed over USB.
          if ((init.method || 'GET').toUpperCase() === 'POST') {
            const body = JSON.parse(typeof init.body === 'string' ? init.body : '{}');
            if (Number.isInteger(body.sleepScreen)) await x4.simple('SLEEPSCREEN ' + body.sleepScreen, 'SLEEPSCREEN');
            return reply('Settings saved');
          }
          const value = await x4.exclusive(async () => Number((await x4.command('SLEEPSCREEN', ['SLEEPSCREEN_OK'])).split(' ')[1]));
          return reply(JSON.stringify([{ key: 'sleepScreen', type: 'enum', value }]), 'application/json');
        }
        case 'api/x4/sync-time':
          await x4.syncTime();
          return reply('{"ok":true}', 'application/json');
        case 'api/weather/refresh':
          await refreshWeather();
          return reply('Weather updated');
      }
    } catch (e) {
      return new Response(String(e.message || e), { status: 503 });
    }
    return realFetch(input, init);
  };

  // --- weather ---------------------------------------------------------------
  // Same reading the device writes when it fetches over Wi-Fi (src/util/Weather.cpp),
  // fetched here by the computer, whose network location stands in for the X4's.
  async function refreshWeather() {
    let place = null;
    try {
      const loc = JSON.parse(new TextDecoder().decode(await x4.get('/weather/location.json')));
      if (loc.mode === 'manual' && typeof loc.lat === 'number') place = { name: loc.name || '', lat: loc.lat, lon: loc.lon };
    } catch (e) { /* no saved location: locate automatically */ }
    // Free IP geolocation services, tried in order (each rate-limits now and then).
    const zh = (navigator.language || '').toLowerCase().startsWith('zh');
    for (const url of ['https://ipwho.is/?fields=city,latitude,longitude' + (zh ? '&lang=zh-CN' : ''), 'https://get.geojs.io/v1/ip/geo.json']) {
      if (place) break;
      try {
        const ip = await (await realFetch(url)).json();
        const lat = Number(ip.latitude), lon = Number(ip.longitude);
        if (Number.isFinite(lat) && Number.isFinite(lon) && (lat || lon)) place = { name: ip.city || '', lat, lon };
      } catch (e) { /* try the next service */ }
    }
    if (!place) throw new Error('location lookup failed');
    const url = 'https://api.open-meteo.com/v1/forecast?latitude=' + place.lat + '&longitude=' + place.lon +
      '&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m,wind_direction_10m' +
      '&daily=sunrise,sunset,temperature_2m_max,temperature_2m_min,uv_index_max&timezone=auto&forecast_days=1';
    const res = await realFetch(url);
    if (!res.ok) throw new Error('weather service: ' + res.status);
    const f = await res.json();
    const c = f.current, d = f.daily;
    const clock = (iso) => String(iso || '').slice(11, 16);
    const now = {
      city: place.name, updated: Math.round(Date.now() / 1000), code: c.weather_code, day: c.is_day,
      temp: c.temperature_2m, feels: c.apparent_temperature, humidity: c.relative_humidity_2m,
      wind: c.wind_speed_10m, windDir: c.wind_direction_10m, uv: d.uv_index_max[0],
      tmax: d.temperature_2m_max[0], tmin: d.temperature_2m_min[0], sunrise: clock(d.sunrise[0]), sunset: clock(d.sunset[0]),
      date: d.time[0],
    };
    await x4.simple('MKDIR /weather', 'MKDIR').catch(() => {});
    await x4.put('/weather/now.json', new TextEncoder().encode(JSON.stringify(now)));
    window.dispatchEvent(new Event('x4-weather'));
  }
  // Every USB connection refreshes the weather, like every Wi-Fi join does on the device.
  // Switching pages reconnects, so a refresh within the last 10 minutes is enough.
  window.addEventListener('x4-connected', () => {
    let last = 0;
    try { last = Number(localStorage.getItem('x4-weather-at')) || 0; } catch (e) { /* storage blocked */ }
    if (Date.now() - last < 10 * 60 * 1000) return;
    refreshWeather().then(() => {
      try { localStorage.setItem('x4-weather-at', String(Date.now())); } catch (e) { /* storage blocked */ }
    }).catch((e) => console.warn('weather:', e));
  });

  // --- connection bar --------------------------------------------------------
  function mountBar() {
    const bar = document.createElement('div');
    bar.id = 'x4-bar';
    bar.innerHTML = `<style>
      #x4-bar { position: sticky; top: 0; z-index: 10; display: flex; gap: 10px; align-items: center; flex-wrap: wrap;
        margin: -16px -16px 12px; padding: 10px 16px; background: var(--card-bg, #fff); border-bottom: 1px solid var(--border-color, #ddd);
        font-size: 14px; box-shadow: 0 2px 4px rgba(0,0,0,.06); }
      #x4-bar .dot { width: 10px; height: 10px; border-radius: 50%; background: #95a5a6; flex: none; }
      #x4-bar.on .dot { background: #27ae60; }
      #x4-bar .msg { flex: 1; min-width: 160px; color: var(--font-color, #333); }
      #x4-bar .hint { width: 100%; color: var(--label-color, #7f8c8d); font-size: 12px; }
      #x4-bar button { padding: 7px 14px; font-size: 14px; border: none; border-radius: 6px; background: var(--accent-color, #6e9a82); color: #fff; cursor: pointer; }
    </style><span class="dot"></span><span class="msg"></span><button type="button"></button><span class="hint"></span>`;
    document.body.prepend(bar);
    const msg = bar.querySelector('.msg');
    const btn = bar.querySelector('button');
    const hint = bar.querySelector('.hint');
    const update = () => {
      bar.classList.toggle('on', x4.connected);
      if (!x4.supported) {
        msg.textContent = T.unsupported;
        btn.hidden = true;
        hint.hidden = true;
        return;
      }
      msg.textContent = x4.connected ? T.connected : T.notConnected;
      btn.textContent = x4.connected ? T.disconnect : T.connect;
      hint.textContent = T.hint;
      hint.hidden = x4.connected;
    };
    btn.onclick = async () => {
      try {
        if (x4.connected) await x4.disconnect();
        else await x4.request();
      } catch (e) {
        if (e.name !== 'NotFoundError') alert(e.message || e);  // NotFoundError = chooser cancelled
      }
    };
    x4.addEventListener('change', () => {
      update();
      if (x4.connected) window.dispatchEvent(new Event('x4-connected'));
    });
    update();
  }

  if (document.body) mountBar();
  else document.addEventListener('DOMContentLoaded', mountBar);
  x4.autoConnect();
})();
