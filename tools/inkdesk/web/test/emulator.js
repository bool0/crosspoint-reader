// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

// Emulated X4 for testing the browser app without hardware: speaks the
// firmware's serial protocol (HELLO/WEBURL/LS/GET/PUT/RM/MKDIR/MV/TIME) over
// mock Web Serial streams, keeps files in memory and sprinkles in log lines
// the way the real device does. Usage: x4.open(window.X4Emulator.port()).
(function () {
  const enc = new TextEncoder(), dec = new TextDecoder();
  const T = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const crc = (b) => { let c = 0xffffffff; for (const x of b) c = T[(c ^ x) & 255] ^ (c >>> 8); return ((c ^ 0xffffffff) >>> 0).toString(16).padStart(8, '0'); };
  const fs = {};
  const dirs = new Set(['/']);
  const state = { fs, dirs, savedUrl: null, log: [], sleepScreen: 0 };
  function port() {
    let ctrl, pending = new Uint8Array(0), put = null;
    const out = (s) => ctrl.enqueue(typeof s === 'string' ? enc.encode(s) : s);
    const handle = (line) => {
      state.log.push(line.slice(0, 80));
      if (Math.random() < 0.3) out('[123] [DBG] [GFX] noise line\n');
      const [verb, ...r] = line.slice(4).split(' ');
      const arg = r.join(' ');
      if (verb === 'HELLO') return out('HELLO_OK emu\n');
      if (verb === 'WEBURL') { state.savedUrl = arg; return out('WEBURL_OK\n'); }
      if (verb === 'LS') {
        out('LS_BEGIN\n');
        const pre = arg === '/' ? '/' : arg + '/';
        for (const d of dirs) if (d !== '/' && d.startsWith(pre) && !d.slice(pre.length).includes('/')) out(`E\td\t0\t${d.slice(pre.length)}\n`);
        for (const f in fs) if (f.startsWith(pre) && !f.slice(pre.length).includes('/')) out(`E\tf\t${fs[f].length}\t${f.slice(pre.length)}\n`);
        return out('LS_END\n');
      }
      if (verb === 'GET') {
        const d = fs[arg];
        if (!d) return out('GET_ERR cannot open\n');
        out(`GET_BEGIN ${d.length}\n`);
        for (let i = 0; i < d.length; i += 768) out('D ' + btoa(String.fromCharCode(...d.subarray(i, i + 768))) + '\n');
        return out(`GET_END ${crc(d)}\n`);
      }
      if (verb === 'PUT') {
        const size = Number(r[0]);
        const path = r.slice(1).join(' ');
        put = { size, path, data: new Uint8Array(size), got: 0 };
        out('PUT_READY\n');
        if (size === 0) { fs[path] = put.data; put = null; out(`PUT_OK ${crc(new Uint8Array(0))}\n`); }
        return;
      }
      if (verb === 'RM') {
        if (fs[arg]) delete fs[arg];
        else if (dirs.has(arg)) { dirs.delete(arg); for (const f in fs) if (f.startsWith(arg + '/')) delete fs[f]; }
        else return out('RM_ERR not found\n');
        return out('RM_OK\n');
      }
      if (verb === 'MKDIR') { dirs.add(arg); return out('MKDIR_OK\n'); }
      if (verb === 'MV') { const [a, b] = arg.split('\t'); if (fs[a]) { fs[b] = fs[a]; delete fs[a]; return out('MV_OK\n'); } return out('MV_ERR failed\n'); }
      if (verb === 'TIME') return out('TIME_OK ' + r[0] + '\n');
      if (verb === 'SLEEPSCREEN') { if (r.length && r[0] !== '') state.sleepScreen = Number(r[0]); return out('SLEEPSCREEN_OK ' + state.sleepScreen + '\n'); }
    };
    const feed = (bytes) => {
      if (put) {
        const take = Math.min(bytes.length, put.size - put.got);
        put.data.set(bytes.subarray(0, take), put.got);
        put.got += take;
        if (put.got % 4096 === 0 || put.got === put.size) out(`ACK ${put.got}\n`);
        if (put.got === put.size) { fs[put.path] = put.data; out(`PUT_OK ${crc(put.data)}\n`); put = null; }
        return;
      }
      const m = new Uint8Array(pending.length + bytes.length);
      m.set(pending); m.set(bytes, pending.length); pending = m;
      let i;
      while ((i = pending.indexOf(10)) >= 0) {
        const line = dec.decode(pending.subarray(0, i));
        pending = pending.subarray(i + 1);
        if (line.startsWith('CMD:')) handle(line);
      }
    };
    return {
      getInfo: () => ({ usbVendorId: 0x303a }), open: async () => {}, close: async () => {},
      readable: new ReadableStream({ start(c) { ctrl = c; } }),
      writable: new WritableStream({ write(chunk) { feed(chunk); } }),
    };
  }
  window.X4Emulator = { port, state, put: (path, text) => { fs[path] = enc.encode(text); let d = path.replace(/\/[^/]*$/, ''); while (d) { dirs.add(d); d = d.replace(/\/[^/]*$/, ''); } } };
})();
