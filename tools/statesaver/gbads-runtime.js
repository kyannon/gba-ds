/* GBA DS's web build (scripts/build-web.sh) in State Saver's GBA DS tab: a
 * small in-memory WASI (files, clocks, console) and the asyncify driver that
 * lets GBA DS's frame waits (web_sleep) hand control back to the browser.
 * Published beside statesaver.html; also runs under Node for tests. */
"use strict";

const GBADS = (() => {
  const E = { OK: 0, BADF: 8, EXIST: 20, INVAL: 28, ISDIR: 31, NOENT: 44, NOSYS: 52, NOTDIR: 54, NOTEMPTY: 55 };
  const ALL_RIGHTS = 0x1fffffffn;

  /* ---------------------------------------------------------------- files */
  class MemFS {
    constructor() { this.nodes = new Map([["/", { dir: true }]]); }
    static norm(p) {
      const out = [];
      for (const part of p.split("/")) {
        if (!part || part === ".") continue;
        if (part === "..") out.pop(); else out.push(part);
      }
      return "/" + out.join("/");
    }
    parent(p) { return p === "/" ? "/" : p.slice(0, p.lastIndexOf("/")) || "/"; }
    mkdirs(p) {
      p = MemFS.norm(p);
      if (this.nodes.has(p)) return;
      this.mkdirs(this.parent(p));
      this.nodes.set(p, { dir: true });
    }
    write(p, bytes) {
      p = MemFS.norm(p);
      this.mkdirs(this.parent(p));
      this.nodes.set(p, { dir: false, data: bytes, size: bytes.length, mtime: Date.now() });
    }
    read(p) { const n = this.nodes.get(MemFS.norm(p)); return n && !n.dir ? n.data.subarray(0, n.size) : null; }
    list(p) {
      p = MemFS.norm(p);
      const pre = p === "/" ? "/" : p + "/";
      const names = [];
      for (const k of this.nodes.keys()) {
        if (k !== p && k.startsWith(pre) && !k.slice(pre.length).includes("/")) names.push(k.slice(pre.length));
      }
      return names.sort();
    }
  }

  /* -------------------------------------------------------------- the run */
  // opts: { wasm (bytes), fs (MemFS), args, env, onFrame(pixels32, w, h, pitch32),
  //         log(text), onExit(code, error), schedule(fn, ms) }
  async function start(opts) {
    const fs = opts.fs;
    const dec = new TextDecoder(), enc = new TextEncoder();
    let X = null, mem = () => new DataView(X.memory.buffer), u8 = () => new Uint8Array(X.memory.buffer);
    const str = (p, n) => dec.decode(u8().subarray(p, p + n));
    // descriptors: 0-2 console, 3 the root (preopened)
    const fds = new Map([[3, { path: "/", dir: true, pos: 0 }]]);
    let nextFd = 4;
    const lines = { 1: "", 2: "" };
    const resolve = (fd, p, n) => {
      const base = fds.get(fd);
      if (!base) return null;
      const rel = str(p, n);
      return MemFS.norm(rel.startsWith("/") ? rel : base.path + "/" + rel);
    };
    const stat = (buf, node, ino) => {
      const d = mem();
      d.setBigUint64(buf, 0n, true);
      d.setBigUint64(buf + 8, BigInt(ino), true);
      d.setUint8(buf + 16, node.dir ? 3 : 4);
      d.setBigUint64(buf + 24, 1n, true);
      d.setBigUint64(buf + 32, BigInt(node.dir ? 0 : node.size), true);
      const t = BigInt(node.mtime || 0) * 1000000n;
      d.setBigUint64(buf + 40, t, true); d.setBigUint64(buf + 48, t, true); d.setBigUint64(buf + 56, t, true);
    };
    const inoOf = (path) => { let h = 7; for (let i = 0; i < path.length; i++) h = (h * 31 + path.charCodeAt(i)) >>> 0; return h || 1; };
    const ensure = (node, need) => {
      if (node.data.length >= need) return;
      const grown = new Uint8Array(Math.max(need, node.data.length * 2, 4096));
      grown.set(node.data.subarray(0, node.size));
      node.data = grown;
    };
    const iovs = (ptr, cnt) => {
      const d = mem(), out = [];
      for (let i = 0; i < cnt; i++) out.push([d.getUint32(ptr + i * 8, true), d.getUint32(ptr + i * 8 + 4, true)]);
      return out;
    };
    const readAt = (node, pos, list) => {
      let total = 0;
      for (const [p, l] of list) {
        const n = Math.max(0, Math.min(l, node.size - pos));
        u8().set(node.data.subarray(pos, pos + n), p);
        pos += n; total += n;
        if (n < l) break;
      }
      return total;
    };
    class Exit extends Error { constructor(code) { super("exit " + code); this.code = code; } }

    const wasi = {
      args_sizes_get(cp, sp) { const d = mem(); d.setUint32(cp, opts.args.length, true); d.setUint32(sp, opts.args.reduce((a, s) => a + enc.encode(s).length + 1, 0), true); return 0; },
      args_get(argv, buf) {
        const d = mem();
        opts.args.forEach((s, i) => { const b = enc.encode(s + "\0"); d.setUint32(argv + i * 4, buf, true); u8().set(b, buf); buf += b.length; });
        return 0;
      },
      environ_sizes_get(cp, sp) {
        const list = Object.entries(opts.env).map(([k, v]) => k + "=" + v);
        const d = mem(); d.setUint32(cp, list.length, true); d.setUint32(sp, list.reduce((a, s) => a + enc.encode(s).length + 1, 0), true); return 0;
      },
      environ_get(envp, buf) {
        const d = mem();
        Object.entries(opts.env).map(([k, v]) => k + "=" + v).forEach((s, i) => { const b = enc.encode(s + "\0"); d.setUint32(envp + i * 4, buf, true); u8().set(b, buf); buf += b.length; });
        return 0;
      },
      clock_time_get(id, prec, out) {
        const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
        mem().setBigUint64(out, ns, true); return 0;
      },
      clock_res_get(id, out) { mem().setBigUint64(out, 1000n, true); return 0; },
      random_get(buf, len) { crypto.getRandomValues(u8().subarray(buf, buf + len)); return 0; },
      sched_yield() { return 0; },
      poll_oneoff(inp, out, n, nev) {
        // every subscription happens at once (nothing here waits)
        const d = mem();
        for (let i = 0; i < n; i++) {
          d.setBigUint64(out + i * 32, d.getBigUint64(inp + i * 48, true), true);
          d.setUint16(out + i * 32 + 8, 0, true);
          d.setUint8(out + i * 32 + 10, d.getUint8(inp + i * 48 + 8));
        }
        d.setUint32(nev, n, true); return 0;
      },
      proc_exit(code) { throw new Exit(code); },
      fd_prestat_get(fd, buf) {
        if (fd !== 3) return E.BADF;
        mem().setUint8(buf, 0); mem().setUint32(buf + 4, 1, true); return 0;
      },
      fd_prestat_dir_name(fd, p, n) { if (fd !== 3) return E.BADF; u8()[p] = 47; return 0; },
      fd_fdstat_get(fd, buf) {
        const d = mem();
        const f = fds.get(fd);
        if (fd > 2 && !f) return E.BADF;
        d.setUint8(buf, fd <= 2 ? 2 : f.dir ? 3 : 4);
        d.setUint16(buf + 2, 0, true);
        d.setBigUint64(buf + 8, ALL_RIGHTS, true); d.setBigUint64(buf + 16, ALL_RIGHTS, true);
        return 0;
      },
      fd_fdstat_set_flags() { return 0; },
      fd_filestat_get(fd, buf) {
        const f = fds.get(fd);
        if (!f) return fd <= 2 ? (stat(buf, { dir: false, size: 0 }, fd + 1), 0) : E.BADF;
        const node = fs.nodes.get(f.path);
        if (!node) return E.NOENT;
        stat(buf, node, inoOf(f.path)); return 0;
      },
      fd_filestat_set_size(fd, size) {
        const f = fds.get(fd), node = f && fs.nodes.get(f.path);
        if (!node || node.dir) return E.BADF;
        const n = Number(size); ensure(node, n);
        if (n > node.size) node.data.fill(0, node.size, n);
        node.size = n; return 0;
      },
      fd_filestat_set_times() { return 0; },
      fd_sync() { return 0; },
      fd_datasync() { return 0; },
      fd_advise() { return 0; },
      fd_allocate() { return 0; },
      fd_close(fd) { if (!fds.has(fd)) return E.BADF; if (fd !== 3) fds.delete(fd); return 0; },
      fd_seek(fd, off, whence, out) {
        const f = fds.get(fd);
        if (!f) return E.BADF;
        const node = fs.nodes.get(f.path);
        const base = whence === 0 ? 0 : whence === 1 ? f.pos : (node ? node.size : 0);
        const p = base + Number(off);
        if (p < 0) return E.INVAL;
        f.pos = p; mem().setBigUint64(out, BigInt(p), true); return 0;
      },
      fd_tell(fd, out) { const f = fds.get(fd); if (!f) return E.BADF; mem().setBigUint64(out, BigInt(f.pos), true); return 0; },
      fd_read(fd, iov, cnt, nread) {
        if (fd === 0) { mem().setUint32(nread, 0, true); return 0; }
        const f = fds.get(fd), node = f && fs.nodes.get(f.path);
        if (!node) return E.BADF;
        if (node.dir) return E.ISDIR;
        const n = readAt(node, f.pos, iovs(iov, cnt));
        f.pos += n; mem().setUint32(nread, n, true); return 0;
      },
      fd_pread(fd, iov, cnt, off, nread) {
        const f = fds.get(fd), node = f && fs.nodes.get(f.path);
        if (!node || node.dir) return E.BADF;
        mem().setUint32(nread, readAt(node, Number(off), iovs(iov, cnt)), true); return 0;
      },
      fd_write(fd, iov, cnt, nw) {
        const list = iovs(iov, cnt);
        let total = 0;
        if (fd === 1 || fd === 2) {
          for (const [p, l] of list) { lines[fd] += dec.decode(u8().subarray(p, p + l)); total += l; }
          let i;
          while ((i = lines[fd].indexOf("\n")) >= 0) { opts.log(lines[fd].slice(0, i)); lines[fd] = lines[fd].slice(i + 1); }
          mem().setUint32(nw, total, true); return 0;
        }
        const f = fds.get(fd), node = f && fs.nodes.get(f.path);
        if (!node || node.dir) return E.BADF;
        if (f.append) f.pos = node.size;
        for (const [p, l] of list) {
          ensure(node, f.pos + l);
          node.data.set(u8().subarray(p, p + l), f.pos);
          f.pos += l; total += l;
          if (f.pos > node.size) node.size = f.pos;
        }
        node.mtime = Date.now();
        mem().setUint32(nw, total, true); return 0;
      },
      fd_pwrite(fd, iov, cnt, off, nw) {
        const f = fds.get(fd);
        if (!f) return E.BADF;
        const keep = f.pos, app = f.append;
        f.pos = Number(off); f.append = false;
        const r = wasi.fd_write(fd, iov, cnt, nw);
        f.pos = keep; f.append = app; return r;
      },
      fd_readdir(fd, buf, len, cookie, used) {
        const f = fds.get(fd);
        if (!f || !f.dir) return E.BADF;
        const names = [".", "..", ...fs.list(f.path)];
        let off = 0;
        for (let i = Number(cookie); i < names.length && off < len; i++) {
          const nm = enc.encode(names[i]);
          const node = i < 2 ? { dir: true } : fs.nodes.get(MemFS.norm(f.path + "/" + names[i]));
          const ent = new Uint8Array(24 + nm.length);
          const d = new DataView(ent.buffer);
          d.setBigUint64(0, BigInt(i + 1), true);
          d.setBigUint64(8, BigInt(inoOf(f.path + "/" + names[i])), true);
          d.setUint32(16, nm.length, true);
          d.setUint8(20, node && node.dir ? 3 : 4);
          ent.set(nm, 24);
          const n = Math.min(ent.length, len - off);
          u8().set(ent.subarray(0, n), buf + off);
          off += n;
        }
        mem().setUint32(used, off, true); return 0;
      },
      path_open(dirfd, dirflags, p, n, oflags, rb, ri, fdflags, out) {
        const path = resolve(dirfd, p, n);
        if (!path) return E.BADF;
        let node = fs.nodes.get(path);
        if (node && (oflags & 4)) return E.EXIST;
        if (!node) {
          if (!(oflags & 1)) return E.NOENT;
          const par = fs.nodes.get(fs.parent(path));
          if (!par || !par.dir) return E.NOENT;
          node = { dir: false, data: new Uint8Array(0), size: 0, mtime: Date.now() };
          fs.nodes.set(path, node);
        }
        if ((oflags & 2) && !node.dir) return E.NOTDIR;
        if (!node.dir && (oflags & 8)) node.size = 0;
        const fd = nextFd++;
        fds.set(fd, { path, dir: node.dir, pos: 0, append: !!(fdflags & 1) });
        mem().setUint32(out, fd, true); return 0;
      },
      path_filestat_get(dirfd, flags, p, n, buf) {
        const path = resolve(dirfd, p, n);
        const node = path && fs.nodes.get(path);
        if (!node) return E.NOENT;
        stat(buf, node, inoOf(path)); return 0;
      },
      path_filestat_set_times() { return 0; },
      path_create_directory(dirfd, p, n) {
        const path = resolve(dirfd, p, n);
        if (fs.nodes.has(path)) return E.EXIST;
        const par = fs.nodes.get(fs.parent(path));
        if (!par || !par.dir) return E.NOENT;
        fs.nodes.set(path, { dir: true }); return 0;
      },
      path_remove_directory(dirfd, p, n) {
        const path = resolve(dirfd, p, n), node = fs.nodes.get(path);
        if (!node) return E.NOENT;
        if (!node.dir) return E.NOTDIR;
        if (fs.list(path).length) return E.NOTEMPTY;
        fs.nodes.delete(path); return 0;
      },
      path_unlink_file(dirfd, p, n) {
        const path = resolve(dirfd, p, n), node = fs.nodes.get(path);
        if (!node) return E.NOENT;
        if (node.dir) return E.ISDIR;
        fs.nodes.delete(path); return 0;
      },
      path_rename(fd1, p1, n1, fd2, p2, n2) {
        const a = resolve(fd1, p1, n1), b = resolve(fd2, p2, n2), node = fs.nodes.get(a);
        if (!node) return E.NOENT;
        if (node.dir) {
          for (const k of [...fs.nodes.keys()]) if (k === a || k.startsWith(a + "/")) { fs.nodes.set(b + k.slice(a.length), fs.nodes.get(k)); fs.nodes.delete(k); }
        } else { fs.nodes.delete(a); fs.nodes.set(b, node); }
        return 0;
      },
      path_readlink() { return E.INVAL; },
      path_symlink() { return E.NOSYS; },
      path_link() { return E.NOSYS; },
    };
    const wasiImports = new Proxy(wasi, { get: (t, k) => t[k] || (() => E.NOSYS) });

    /* the asyncify dance: web_sleep unwinds the stack to here; later the
     * page calls back in and it rewinds to where it was */
    const ASYNC_BYTES = 1 << 18;
    let dataPtr = 0, state = "normal", delay = 0, stopped = false, paused = false, resumeFn = null;
    const shape = new Int32Array(3);
    const env = {
      web_sleep(ms) {
        if (state === "rewinding") { X.asyncify_stop_rewind(); state = "normal"; return; }
        // a frame's done: show it, then hand control back
        const sp = X.web_alloc ? scratch : 0;
        if (sp) {
          const pix = X.web_screen(sp);
          if (pix) {
            const d = mem();
            opts.onFrame(new Uint32Array(X.memory.buffer, pix, d.getInt32(sp + 4, true) * d.getInt32(sp + 8, true) / 4),
              d.getInt32(sp, true), d.getInt32(sp + 4, true), d.getInt32(sp + 8, true) / 4);
          }
        }
        delay = ms;
        X.asyncify_start_unwind(dataPtr);
        state = "unwinding";
      },
      system() { return -1; },
    };
    let scratch = 0;
    // opts.wasm: the bytes, or a WebAssembly.Module compiled once
    const made = await WebAssembly.instantiate(opts.wasm, { wasi_snapshot_preview1: wasiImports, env: new Proxy(env, { get: (t, k) => t[k] || (() => 0) }) });
    X = (made.instance || made).exports;
    dataPtr = X.web_alloc(8 + ASYNC_BYTES);
    scratch = X.web_alloc(16);
    mem().setInt32(dataPtr, dataPtr + 8, true);
    mem().setInt32(dataPtr + 4, dataPtr + 8 + ASYNC_BYTES, true);

    const step = () => {
      if (stopped) return;
      try {
        X._start();
      } catch (e) {
        if (e instanceof Exit) { stopped = true; opts.onExit(e.code, null); return; }
        stopped = true; opts.onExit(-1, e); return;
      }
      if (state === "unwinding") {
        X.asyncify_stop_unwind();
        state = "sleeping";
        resumeFn = () => {
          resumeFn = null;
          if (stopped) return;
          X.asyncify_start_rewind(dataPtr);
          state = "rewinding";
          step();
        };
        if (!paused) opts.schedule(resumeFn, delay);
      } else {
        stopped = true; opts.onExit(0, null);
      }
    };
    step();
    const cstr = (text) => { const b = enc.encode(text + "\0"); const p = X.web_alloc(b.length); u8().set(b, p); return p; };
    const idle = () => !stopped && state === "sleeping";
    return {
      fs,
      // touch: 1 down, 2 up, 3 move; x, y fractions of the window
      touch(type, x, y) { if (idle()) X.web_touch(type, x, y); },
      // a state of the game playing now (GBA DS's own format), or null
      saveState() {
        if (!idle()) return null;
        const ok = X.web_save_state(cstr("/web-out.ss"));
        const bytes = ok ? fs.read("/web-out.ss") : null;
        fs.nodes.delete("/web-out.ss");
        return bytes ? bytes.slice() : null;
      },
      loadState(bytes) {
        if (!idle()) return false;
        fs.write("/web-in.ss", bytes);
        return !!X.web_load_state(cstr("/web-in.ss"));
      },
      key(scancode, down) { if (!stopped && state !== "unwinding" && state !== "rewinding") X.web_key(scancode, down ? 1 : 0); },
      pause(on) {
        paused = on;
        if (!on && resumeFn && state === "sleeping") opts.schedule(resumeFn, 0);
      },
      stop() { stopped = true; },
      get running() { return !stopped; },
    };
  }

  /* browser KeyboardEvent.code -> SDL scancode */
  const SCANCODE = { Enter: 40, Escape: 41, Backspace: 42, Tab: 43, Space: 44, ArrowRight: 79, ArrowLeft: 80, ArrowDown: 81, ArrowUp: 82,
    ShiftRight: 229, ShiftLeft: 225, Home: 74, F1: 58 };
  for (let i = 0; i < 26; i++) SCANCODE["Key" + String.fromCharCode(65 + i)] = 4 + i;
  for (let i = 1; i <= 9; i++) SCANCODE["Digit" + i] = 29 + i;
  SCANCODE.Digit0 = 39;

  return { MemFS, start, SCANCODE };
})();

if (typeof module !== "undefined") module.exports = GBADS;
