/* GBA DS web player: the app runs here, off the page's thread. It draws the
 * app's window (both screens, stacked as on the device) with WebGL2 into the
 * page's canvas (an OffscreenCanvas), hands its sound to the page's audio
 * worklet as fast as that plays it, and takes the page's buttons and touches.
 * Needs gbads-runtime.js (tools/statesaver/), loaded before this file. */
"use strict";
if (typeof GBADS === "undefined") importScripts("gbads-runtime.js");

let h = null;            // the run (GBADS.start's handle)
// a layout: { cw, ch, rects: [{ src: [x, y, w, h], dst: [x, y, w, h] }], smooth }
let frame = null;        // the last frame: { ptr, w, h, pitch } (pitch in pixels)
let frames = 0, fpsN = 0, fpsT = 0;
let paused = false;
let legacyMask = 0;

const post = (m, transfer) => postMessage(m, transfer || []);
// a promise that failed with nobody waiting on it never reaches the page's onerror: say what it was
self.addEventListener("unhandledrejection", (e) => {
  const r = e.reason;
  post({ t: "exit", code: -1, error: r && (r.stack || r.message) || String(r) });
});
const log = (line) => {
  post({ t: "log", line });
  // a setting the menu just wrote (the app logs it after writing): kept now, not at the next 4 s tick,
  // so leaving or reloading the page right after changing it doesn't lose it
  if (line.startsWith("setting: ")) syncSoon();
};
let syncTimer = 0;
function syncSoon() { if (!syncTimer) syncTimer = setTimeout(() => { syncTimer = 0; if (h) syncFiles(); }, 150); }

/* ------------------------------------------------------------ video */
// Where the app's window is drawn: the page's canvas (VIEWS[0]) and, with the
// second window open (issue #93), that window's canvas (VIEWS[1]). Each has
// its own WebGL2 context and layout; each frame is uploaded to each.
const VIEWS = [null, null];
function glInit(canvas) {
  const gl = canvas.getContext("webgl2", { alpha: true, premultipliedAlpha: true, antialias: false, depth: false, stencil: false,
    preserveDrawingBuffer: false, powerPreference: "high-performance" });
  if (!gl) return null;
  const sh = (type, src) => {
    const s = gl.createShader(type);
    gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
    return s;
  };
  const prog = gl.createProgram();
  gl.attachShader(prog, sh(gl.VERTEX_SHADER, `#version 300 es
    in vec4 a; out vec2 uv;
    void main() { uv = a.zw; gl_Position = vec4(a.xy, 0.0, 1.0); }`));
  // the app's window is XRGB8888 (bytes B, G, R, X): uploaded as RGBA, swapped here
  gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, `#version 300 es
    precision mediump float; in vec2 uv; uniform sampler2D t; out vec4 o;
    void main() { vec4 c = texture(t, uv); o = vec4(c.b, c.g, c.r, 1.0); }`));
  gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
  gl.useProgram(prog);
  const buf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  const loc = gl.getAttribLocation(prog, "a");
  gl.enableVertexAttribArray(loc);
  gl.vertexAttribPointer(loc, 4, gl.FLOAT, false, 0, 0);
  const tex = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.clearColor(0, 0, 0, 0);
  return { gl, tex, texW: 0, texH: 0, verts: 0, quadsFor: null, smooth: null, layout: null };
}

function glQuads(v_) {
  const { gl, layout } = v_;
  if (!layout || !v_.texW) return;
  const { cw, ch, rects } = layout, W = v_.texW, H = v_.texH;
  const v = [];
  for (const r of rects) {
    const [sx, sy, sw, sh] = r.src, [dx, dy, dw, dh] = r.dst;
    const x0 = dx / cw * 2 - 1, x1 = (dx + dw) / cw * 2 - 1;
    const y0 = 1 - dy / ch * 2, y1 = 1 - (dy + dh) / ch * 2;
    const u0 = sx / W, u1 = (sx + sw) / W, v0 = sy / H, v1 = (sy + sh) / H;
    v.push(x0, y0, u0, v0, x1, y0, u1, v0, x0, y1, u0, v1, x0, y1, u0, v1, x1, y0, u1, v0, x1, y1, u1, v1);
  }
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(v), gl.STATIC_DRAW);
  v_.verts = v.length / 4;
  v_.quadsFor = layout;
}

function glDrawView(v_, upload) {
  const { gl, layout } = v_;
  if (upload && frame) {
    const { ptr, w, h: fh, pitch, buf } = frame;
    if (w !== v_.texW || fh !== v_.texH) {
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, fh, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      v_.texW = w; v_.texH = fh; v_.quadsFor = null;
    }
    gl.pixelStorei(gl.UNPACK_ROW_LENGTH, pitch);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, w, fh, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(buf, ptr, pitch * 4 * fh));
  }
  if (!layout || !v_.texW) return;
  if (v_.smooth !== layout.smooth) {
    const f = layout.smooth ? gl.LINEAR : gl.NEAREST;
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, f);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, f);
    v_.smooth = layout.smooth;
  }
  if (v_.quadsFor !== layout) glQuads(v_);
  gl.viewport(0, 0, layout.cw, layout.ch);
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.drawArrays(gl.TRIANGLES, 0, v_.verts);
}
function glDraw(upload) { for (const v of VIEWS) if (v) glDrawView(v, upload); }

function setLayout(l, i) {
  const v = VIEWS[i || 0];
  if (!v) return;
  v.layout = l;
  const canvas = v.gl.canvas;
  if (canvas.width !== l.cw || canvas.height !== l.ch) { canvas.width = l.cw; canvas.height = l.ch; }
  // (no new frame while paused: draw the last one again, uploaded if this view hasn't had it)
  glDrawView(v, !v.texW && !!frame && !!h);
}
// the second window's canvas (or none: it closed)
function setView2(canvas, l) {
  const old = VIEWS[1];
  VIEWS[1] = null;
  if (old) { const x = old.gl.getExtension("WEBGL_lose_context"); if (x) x.loseContext(); }
  if (!canvas) return;
  const v = glInit(canvas);
  if (!v) { post({ t: "log", line: "the second window has no WebGL2" }); return; }
  VIEWS[1] = v;
  if (l) setLayout(l, 1);
}

/* ------------------------------------------------------------ sound */
// The worklet asks for what it has played (`need`, in sample frames); that much
// is pulled from SDL's queue, so the queue drains in real time and the app's
// pacing holds it near audio_latency. With no worklet playing (no sound yet,
// or the page's audio suspended) the queue is drained by the clock instead.
const snd = { port: null, credit: 0, lastNeed: -1e9, wall: 0 };
function attachAudio(port) {
  if (snd.port) snd.port.close();
  snd.port = port; snd.credit = 0;
  if (port) port.onmessage = (e) => {
    if (e.data.need) { snd.credit = Math.min(snd.credit + e.data.need, 8192); snd.lastNeed = performance.now(); }
  };
}
function pumpAudio() {
  const now = performance.now();
  const since = now - snd.wall;
  snd.wall = now;
  if (!h || !h.hasAudio) return;
  const rate = h.audioRate();
  if (!rate) return;
  const live = snd.port && now - snd.lastNeed < 300;
  const want = live ? snd.credit : Math.min(Math.round(since / 1000 * rate), rate >> 2);
  if (want <= 0) return;
  const pcm = h.audioPull(want);
  if (!pcm) return;
  if (live) {
    snd.credit -= pcm.length >> 1;
    snd.port.postMessage({ pcm, rate }, [pcm.buffer]);
  }
}

/* ------------------------------------------------------------ input */
// builds before the player's own button input (web_buttons) get keyboard keys:
// the keys ui.c reads, by ui.h's button order (X and Y have none)
const LEGACY_SCANCODES = [27, 29, 0, 0, 40, 42, 41, 4, 22, 43, 44, 20, 82, 81, 80, 79];
function setButtons(mask) {
  if (!h) return;
  if (h.hasButtons) { h.buttons(mask); return; }
  const diff = mask ^ legacyMask;
  for (let i = 0; i < 16; i++) if ((diff >> i) & 1 && LEGACY_SCANCODES[i]) h.key(LEGACY_SCANCODES[i], (mask >> i) & 1);
  legacyMask = mask;
}

/* ------------------------------------------------------------ files the player keeps */
// battery saves, states and the menu's settings (states/prefs.cfg), and the home folder
// (kept in the browser); screenshots and GBA DS's data folders (cheats, overlays, logos,
// patches, bug reports: in /gba-ds, the app's folder) too, for a folder of the player's
const KEEP = /^\/(saves|states|home|shots)\/|^\/gba-ds\/(cheats|overlays|logos|patches|bug-reports)\//;
const seen = new Map();
function changedFiles(all) {
  const files = [], deleted = [];
  const fs = h ? h.fs : null;
  if (!fs) return { files, deleted };
  const now = new Set();
  for (const [p, n] of fs.nodes) {
    if (n.dir || !KEEP.test(p)) continue;
    now.add(p);
    const sig = n.size + ":" + (n.mtime || 0);
    if (all || seen.get(p) !== sig) { files.push([p, n.data.slice(0, n.size)]); seen.set(p, sig); }
  }
  for (const p of [...seen.keys()]) if (!now.has(p)) { deleted.push(p); seen.delete(p); }
  return { files, deleted };
}
function syncFiles(id) {
  const { files, deleted } = changedFiles(false);
  if (files.length || deleted.length || id !== undefined) post({ t: "files", id, files, deleted }, files.map((f) => f[1].buffer));
}

/* ------------------------------------------------------------ screenshots */
async function screenshot() {
  if (!frame || !h) return null;
  const { ptr, w, h: fh, pitch } = frame;
  const src = new Uint32Array(h.memory.buffer, ptr, pitch * fh); // (the memory now: it may have grown since)
  const img = new ImageData(w, fh), o = new Uint32Array(img.data.buffer);
  for (let y = 0; y < fh; y++) {
    const si = y * pitch, di = y * w;
    for (let x = 0; x < w; x++) { const v = src[si + x]; o[di + x] = 0xff000000 | ((v & 0xff) << 16) | (v & 0xff00) | ((v >>> 16) & 0xff); }
  }
  const c = new OffscreenCanvas(w, fh);
  c.getContext("2d").putImageData(img, 0, 0);
  return c.convertToBlob({ type: "image/png" });
}

/* ------------------------------------------------------------ captures in a worker of their own */
// The live game: its map captures go to the page (which runs them in the
// capture worker, a second copy of the app) and come back to it, between frames.
// The buffers that carry snapshots and pictures go round and come back
// (stateSpare here, outSpare in the capture worker), so updates make no
// garbage for the browser to collect (a collection pauses the game).
const cap = { want: false, info: 0, inbox: [], off: false, stateSpare: null };
// ?debug: slow frames logged with what came before them
const dbg = { on: false, last: 0, resumed: 0, ev: [] };
const mark = (e) => { if (dbg.on) dbg.ev.push(e); };
const cstr = (mem, p) => { const b = new Uint8Array(mem.buffer, p); let n = 0; while (b[n]) n++; return new TextDecoder().decode(b.slice(0, n)); };
function pumpCaptures() {
  // (GBADS_CAPTURE_OFFLOAD=1 had captures handed over from the start)
  if (!h || !cap.want || !h.has("web_capture_job") || !h.idle) return;
  if (cap.off) { h.call("web_capture_offload", 0); cap.want = cap.off = false; cap.inbox.length = 0; return; }
  if (!cap.info) cap.info = h.call("web_alloc", 32);
  while (cap.inbox.length) {
    const r = cap.inbox.shift();
    if (r.state) cap.stateSpare = r.state;
    const t0 = performance.now();
    const ptr = h.call("web_capture_inbox", r.len);
    if (ptr) {
      new Uint8Array(h.memory.buffer, ptr, r.len).set(new Uint8Array(r.buf, 0, r.len));
      h.call("web_capture_deliver", r.job, r.frames, r.step, r.tileValid, r.failed);
    }
    mark("update in " + (performance.now() - t0).toFixed(1) + " ms");
    post({ t: "capreturn", buf: r.buf }, [r.buf]);
  }
  const job = h.call("web_capture_job", cap.info);
  if (!job) return;
  const i = new Uint32Array(h.memory.buffer, cap.info, 6);
  const t0 = performance.now();
  let state = cap.stateSpare;
  cap.stateSpare = null;
  if (!state || state.byteLength !== i[1]) state = new ArrayBuffer(i[1]);
  new Uint8Array(state).set(new Uint8Array(h.memory.buffer, i[0], i[1]));
  mark("request in " + (performance.now() - t0).toFixed(1) + " ms");
  post({ t: "capjob", job, state, still: i[2], rom: cstr(h.memory, i[3]), code: cstr(h.memory, i[4]), tight: !!i[5] }, [state]);
}

// The capture worker: a job's snapshot in, run, its pictures out.
const serve = { h: null, job: null, info: 0, timer: 0, outSpare: null, stateBack: null };
function serveTick() {
  const sh = serve.h;
  if (!sh) return;
  if (!sh.idle) return;
  if (!serve.info) serve.info = sh.call("web_alloc", 128);
  if (serve.job) {
    const ptr = sh.call("web_serve_snapshot"), size = sh.call("web_serve_snapshot_size");
    if (ptr && size === serve.job.state.byteLength) {
      new Uint8Array(sh.memory.buffer, ptr, size).set(new Uint8Array(serve.job.state));
      sh.call("web_serve_job", serve.job.job, serve.job.still);
      serve.stateBack = serve.job.state;
      serve.job = null;
    } else if (ptr && size) {
      const b = new TextEncoder().encode("\0".repeat(1024) + "the snapshot isn't this game's\0");
      post({ t: "capresult", job: serve.job.job, frames: 0, step: 1, tileValid: 0, failed: 1, buf: b.buffer, len: b.length });
      serve.job = null;
    }
  }
  const done = sh.call("web_serve_result", serve.info);
  if (!done) return;
  const mem = sh.memory.buffer, i = new Uint32Array(mem, serve.info, 22);
  const frames = i[0], n = i[4], err = i[5] ? cstr(sh.memory, i[5]) : "";
  const errB = new TextEncoder().encode(err + "\0");
  const len = (n * frames + 256) * 4 + errB.length;
  // (room for the most frames a capture has, so the one buffer always does)
  if (!serve.outSpare || serve.outSpare.byteLength < len) serve.outSpare = new ArrayBuffer(Math.max(len, (n * 16 + 256) * 4 + 256));
  const out = new Uint8Array(serve.outSpare);
  serve.outSpare = null;
  for (let f = 0; f < frames; f++) out.set(new Uint8Array(mem, i[6 + f], n * 4), n * 4 * f);
  out.set(new Uint8Array(mem, i[3], 1024), n * 4 * frames);
  out.set(errB, (n * frames + 256) * 4);
  const state = serve.stateBack;
  serve.stateBack = null;
  post({ t: "capresult", job: done, frames, step: i[1], tileValid: i[2], failed: err ? 1 : 0, buf: out.buffer, len, state }, state ? [out.buffer, state] : [out.buffer]);
}
/* The Scaling filter's upscalers (src/upscale.c, the web build): SDL draws in
 * software here, so the app hands each pass to web_gl_pass, which runs the
 * same shader as a device does in a WebGL2 context of its own (not the one
 * that shows the window) and writes the result back into the app's memory. */
function glUpscaleImports(exports) {
  let gl = null, tried = false, vbo = null, fbo = null, texIn = null, texOut = null;
  const progs = new Map();
  const mem = () => exports().memory.buffer;
  const cstr = (p) => { if (!p) return ""; const u = new Uint8Array(mem()); let e = p; while (u[e]) e++; return new TextDecoder().decode(u.subarray(p, e)); };
  function init() {
    tried = true;
    try {
      gl = new OffscreenCanvas(1, 1).getContext("webgl2", { alpha: true, antialias: false, depth: false, stencil: false, premultipliedAlpha: false, preserveDrawingBuffer: false });
    } catch (e) { gl = null; }
    if (!gl) { log("upscale: no WebGL2 for the upscalers"); return; }
    vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
    fbo = gl.createFramebuffer();
    const mk = () => { const t = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, t);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE); return t; };
    texIn = mk(); texOut = mk();
  }
  function compile(type, src) {
    const sh = gl.createShader(type); gl.shaderSource(sh, src); gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) { const e = gl.getShaderInfoLog(sh); gl.deleteShader(sh); throw new Error(e); }
    return sh;
  }
  function build(vs, fs, uniforms) {
    const p = gl.createProgram();
    gl.attachShader(p, compile(gl.VERTEX_SHADER, vs)); gl.attachShader(p, compile(gl.FRAGMENT_SHADER, fs));
    gl.bindAttribLocation(p, 0, "position"); gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error("link: " + gl.getProgramInfoLog(p));
    gl.useProgram(p);
    gl.uniform1i(gl.getUniformLocation(p, "tex"), 0);
    for (const line of uniforms.split("\n")) {
      const [k, v] = line.split("=");
      if (k) { const l = gl.getUniformLocation(p, k); if (l) gl.uniform1f(l, parseFloat(v)); }
    }
    return { p, texSize: gl.getUniformLocation(p, "texSize"), outputSize: gl.getUniformLocation(p, "outputSize") };
  }
  return {
    web_gl_ok() { if (!tried) init(); return gl ? 1 : 0; },
    web_gl_pass(key, vs0, fs0, vs1, fs1, uni, inP, inW, inH, outP, outW, outH) {
      if (!tried) init();
      if (!gl) return 0;
      let pr = progs.get(key);
      if (pr === undefined) {
        const u = cstr(uni), errs = [];
        for (const [vs, fs] of [[vs0, fs0], [vs1, fs1]]) {
          try { pr = build(cstr(vs), cstr(fs), u); break; } catch (e) { errs.push(String(e.message || e)); pr = null; }
        }
        if (!pr) log("upscale: a filter's shader didn't build in WebGL2: " + errs.join(" / "));
        progs.set(key, pr);
      }
      if (!pr) return 0;
      gl.useProgram(pr.p);
      gl.disable(gl.BLEND); gl.disable(gl.SCISSOR_TEST);
      gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, texOut);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, outW, outH, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texOut, 0);
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) return 0;
      gl.bindTexture(gl.TEXTURE_2D, texIn);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, inW, inH, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(mem(), inP, inW * inH * 4));
      gl.viewport(0, 0, outW, outH);
      if (pr.texSize) gl.uniform2f(pr.texSize, inW, inH);
      if (pr.outputSize) gl.uniform2f(pr.outputSize, outW, outH);
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
      gl.readPixels(0, 0, outW, outH, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(mem(), outP, outW * outH * 4));
      return 1;
    },
  };
}
async function bootCapture(m) {
  const fs = new GBADS.MemFS();
  for (const [p, b, t] of m.files) fs.write(p, b, t);
  log("$ " + Object.entries(m.env).filter(([k]) => /CAPTURE/.test(k)).map(([k, v]) => k + "=" + v).join(" ") + " " + m.args.join(" "));
  try {
    serve.h = await GBADS.start({
      wasm: m.wasm, fs, args: m.args, env: m.env, log, onFrame: () => {}, schedule,
      onExit: (code, err) => { post({ t: "exit", code, error: err ? String(err.stack || err) : null }); serve.h = null; },
    });
    serve.timer = setInterval(serveTick, 2);
  } catch (e) {
    post({ t: "exit", code: -1, error: "The capture worker didn't start: " + (e.message || e) });
  }
}

/* ------------------------------------------------------------ DK's level above, in a worker of its own */
// The game hands each frame's state to the page (which runs the copy in the climb worker, a second copy of the
// app) and takes the picture back between frames, a frame behind. The buffers go round as the captures' do.
const clb = { want: false, info: 0, spare: null, inbox: [], off: false };
const PIC_BYTES = 240 * 160 * 4;
function pumpClimb() {
  if (!h || !clb.want || !h.has("web_climb_job") || !h.idle) return;
  if (clb.off) { h.call("web_climb_offload", 0); clb.want = clb.off = false; clb.inbox.length = 0; return; }
  if (!clb.info) clb.info = h.call("web_alloc", 64);
  while (clb.inbox.length) {
    const r = clb.inbox.shift();
    if (r.state) clb.spare = r.state;
    const ptr = h.call("web_climb_inbox");
    if (ptr && r.buf.byteLength === PIC_BYTES) {
      new Uint8Array(h.memory.buffer, ptr, PIC_BYTES).set(new Uint8Array(r.buf));
      h.call("web_climb_deliver", r.seq, r.epoch, r.ok, r.regs[0], r.regs[1], r.regs[2]);
    }
    post({ t: "climbreturn", buf: r.buf }, [r.buf]);
  }
  const job = h.call("web_climb_job", clb.info);
  if (!job) return;
  const i = new Uint32Array(h.memory.buffer, clb.info, 10);
  let state = clb.spare;
  clb.spare = null;
  if (!state || state.byteLength !== i[1]) state = new ArrayBuffer(i[1]);
  new Uint8Array(state).set(new Uint8Array(h.memory.buffer, i[0], i[1]));
  const oam = new Uint16Array(512);
  oam.set(new Uint16Array(h.memory.buffer, i[7], 512));
  post({ t: "climbjob", seq: job - 1, epoch: i[3], keysPrev: i[4], keysNow: i[5], cam: i[6] | 0, state, oam: oam.buffer, rom: cstr(h.memory, i[8]), code: cstr(h.memory, i[9]) }, [state, oam.buffer]);
}

// The climb worker: a job's state in, the copy's two frames run, its picture out.
const cserve = { h: null, job: null, info: 0, timer: 0, outSpare: null, stateBack: null };
function climbServeTick() {
  const sh = cserve.h;
  if (!sh || !sh.idle) return;
  if (!cserve.info) cserve.info = sh.call("web_alloc", 64);
  const j = cserve.job;
  if (j) {
    const ptr = sh.call("web_serve_climb_snapshot"), size = sh.call("web_serve_climb_snapshot_size");
    if (ptr && size === j.state.byteLength) {
      new Uint8Array(sh.memory.buffer, ptr, size).set(new Uint8Array(j.state));
      const ip = sh.call("web_serve_climb_info"), dv = new DataView(sh.memory.buffer, ip, 24);
      dv.setUint32(0, j.seq, true); dv.setUint32(4, j.epoch, true); dv.setInt32(8, 0, true);
      dv.setUint32(12, j.keysPrev, true); dv.setUint32(16, j.keysNow, true); dv.setInt32(20, j.cam, true);
      new Uint8Array(sh.memory.buffer, ip + 24, 1024).set(new Uint8Array(j.oam));
      sh.call("web_serve_climb_go");
      cserve.stateBack = j.state;
      cserve.job = null;
    } else if (ptr && size) {
      // (not this game's state: no picture for it)
      post({ t: "climbresult", seq: j.seq, epoch: j.epoch, ok: 0, regs: [0, 0, 0], buf: new ArrayBuffer(PIC_BYTES), state: j.state }, [j.state]);
      cserve.job = null;
    }
  }
  if (!sh.call("web_serve_climb_result", cserve.info)) return;
  const mem = sh.memory.buffer, i = new Uint32Array(mem, cserve.info, 8);
  let out = cserve.outSpare;
  cserve.outSpare = null;
  if (!out || out.byteLength !== PIC_BYTES) out = new ArrayBuffer(PIC_BYTES);
  new Uint8Array(out).set(new Uint8Array(mem, i[0], PIC_BYTES));
  const state = cserve.stateBack;
  cserve.stateBack = null;
  post({ t: "climbresult", seq: i[1], epoch: i[2], ok: i[3], regs: [i[4], i[5], i[6]], buf: out, state }, state ? [out, state] : [out]);
}
async function bootClimb(m) {
  const fs = new GBADS.MemFS();
  for (const [p, b, t] of m.files) fs.write(p, b, t);
  log("$ GBADS_CLIMB_SERVE=" + m.env.GBADS_CLIMB_SERVE + " " + m.args.join(" "));
  try {
    cserve.h = await GBADS.start({
      wasm: m.wasm, fs, args: m.args, env: m.env, log, onFrame: () => {}, schedule,
      onExit: (code, err) => { post({ t: "exit", code, error: err ? String(err.stack || err) : null }); cserve.h = null; },
    });
    cserve.timer = setInterval(climbServeTick, 2);
  } catch (e) {
    post({ t: "exit", code: -1, error: "The climb worker didn't start: " + (e.message || e) });
  }
}

/* ------------------------------------------------------------ the run */
// waits shorter than a millisecond go through a message, not setTimeout (which
// browsers stretch to 4 ms once timeouts nest)
const zero = new MessageChannel(), zeroQ = [];
zero.port1.onmessage = () => { const f = zeroQ.shift(); if (f) f(); };
const schedule = (fn, ms) => { if (ms <= 0) { zeroQ.push(fn); zero.port2.postMessage(0); } else setTimeout(fn, ms); };

async function boot(m) {
  if (m.canvas && !(VIEWS[0] = glInit(m.canvas))) { post({ t: "exit", code: -1, error: "This browser has no WebGL2, which the player draws with." }); return; }
  if (m.layout) setLayout(m.layout, 0);
  if (m.canvas2) setView2(m.canvas2, m.layout2);
  attachAudio(m.audioPort || null);
  const fs = new GBADS.MemFS();
  for (const [p, b, t] of m.files) fs.write(p, b, t);
  for (const [p] of m.files) if (KEEP.test(p)) { const n = fs.nodes.get(p); seen.set(p, n.size + ":" + (n.mtime || 0)); }
  for (const d of ["/states", "/saves", "/shots", "/home", "/roms"]) fs.mkdirs(d);
  log("$ " + m.args.join(" "));
  cap.want = !!m.captureWorker;
  clb.want = !!m.climbWorker;
  dbg.on = !!m.debug;
  fpsT = performance.now(); snd.wall = fpsT;
  try {
    h = await GBADS.start({
      wasm: m.wasm, fs, args: m.args, env: m.env, log, imports: glUpscaleImports,
      onFrame: (px, w, fh, pitch) => {
        if (dbg.on) {
          const n = performance.now(), gap = n - dbg.last;
          if (dbg.last && gap > 40) log("slow frame: " + gap.toFixed(0) + " ms since the last (the game's own work " + (n - dbg.resumed).toFixed(0) + " ms)" + (dbg.ev.length ? "; before it: " + dbg.ev.join(", ") : ""));
          dbg.ev.length = 0; dbg.last = n;
        }
        frame = { ptr: px.byteOffset, buf: px.buffer, w, h: fh, pitch };
        frames++; fpsN++;
        glDraw(true);
        // (the sound is pulled once the app has handed control back: not from inside its frame)
        queueMicrotask(pumpAudio);
        if (cap.want) queueMicrotask(pumpCaptures);
        if (clb.want) queueMicrotask(pumpClimb);
        const now = performance.now();
        if (now - fpsT >= 1000) { post({ t: "fps", fps: fpsN * 1000 / (now - fpsT), frames }); fpsN = 0; fpsT = now; }
      },
      onExit: (code, err) => {
        syncFiles();
        post({ t: "exit", code, error: err ? String(err.stack || err) : null });
        h = null;
      },
      schedule: (fn, ms) => schedule(() => { dbg.resumed = performance.now(); fn(); }, ms),
    });
    if (h) {
      post({ t: "started", hasAudio: h.hasAudio, hasButtons: h.hasButtons });
    }
  } catch (e) {
    post({ t: "exit", code: -1, error: "GBA DS didn't start: " + (e.message || e) });
  }
  setInterval(() => { if (h) syncFiles(); }, 4000);
}

// the wide view's ratio ("16:9", or "off": the GBA's 3:2) for the game running now and the next
// ones (game.c web_wide); between the game's frames, so tried again shortly if it's mid-frame
function wideSet(ratio, tries) {
  if (!h) return;
  if (!h.has("web_wide")) { post({ t: "wide", ok: false }); return; }
  const [n, d] = ratio === "off" ? [0, 0] : String(ratio).split(":").map(Number);
  if (!h.idle) { if (tries < 60) setTimeout(() => wideSet(ratio, tries + 1), 16); return; }
  h.call("web_wide", n || 0, d || 0);
  post({ t: "wide", ok: true, ratio });
}

onmessage = async (e) => {
  const m = e.data;
  switch (m.t) {
    case "boot": await boot(m); break;
    case "bootCapture": await bootCapture(m); break;
    case "bootClimb": await bootClimb(m); break;
    case "climbjob": cserve.job = m; climbServeTick(); break;
    // (a picture is put in as soon as the game is between frames: it is used the frame after)
    case "climbresult": clb.inbox.push(m); pumpClimb(); break;
    case "climboff": clb.off = true; pumpClimb(); break;
    case "climbreturn": cserve.outSpare = m.buf; break;
    case "capjob": serve.job = m; serveTick(); break;
    case "capresult": cap.inbox.push(m); if (paused) pumpCaptures(); break;
    case "capoff": cap.off = true; pumpCaptures(); break;
    case "capreturn": serve.outSpare = m.buf; break;
    case "layout": setLayout(m.layout, 0); if (m.layout2) setLayout(m.layout2, 1); break;
    case "view2": setView2(m.canvas || null, m.layout || null); break;
    case "buttons": if (!paused) setButtons(m.mask); break;
    case "wide": wideSet(m.ratio, 0); break;
    case "touch": if (h && !paused) h.touch(m.type, m.x, m.y); break;
    case "pause":
      paused = m.on;
      if (h) { if (m.on) setButtons(0); h.pause(m.on); }
      if (m.on) syncFiles();
      break;
    case "audio": attachAudio(m.port || null); break;
    case "saveState": { const b = h ? h.saveState() : null; post({ t: "reply", id: m.id, bytes: b }, b ? [b.buffer] : []); break; }
    case "loadState": post({ t: "reply", id: m.id, ok: h ? h.loadState(new Uint8Array(m.bytes)) : false }); break;
    case "screenshot": post({ t: "reply", id: m.id, blob: await screenshot() }); break;
    case "sync": syncFiles(m.id); break;
    case "stop": if (h) { syncFiles(); h.stop(); h = null; } post({ t: "reply", id: m.id }); break;
  }
};
