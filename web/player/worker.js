/* GBA DS web player: the app runs here, off the page's thread. It draws the
 * app's window (both screens, stacked as on the device) with WebGL2 into the
 * page's canvas (an OffscreenCanvas), hands its sound to the page's audio
 * worklet as fast as that plays it, and takes the page's buttons and touches.
 * Needs gbads-runtime.js (tools/statesaver/), loaded before this file. */
"use strict";
if (typeof GBADS === "undefined") importScripts("gbads-runtime.js");

let h = null;            // the run (GBADS.start's handle)
let gl = null, glState = null;
let layout = null;       // { cw, ch, rects: [{ src: [x, y, w, h], dst: [x, y, w, h] }], smooth }
let frame = null;        // the last frame: { ptr, w, h, pitch } (pitch in pixels)
let frames = 0, fpsN = 0, fpsT = 0;
let paused = false;
let legacyMask = 0;

const post = (m, transfer) => postMessage(m, transfer || []);
const log = (line) => post({ t: "log", line });

/* ------------------------------------------------------------ video */
function glInit(canvas) {
  gl = canvas.getContext("webgl2", { alpha: true, premultipliedAlpha: true, antialias: false, depth: false, stencil: false,
    preserveDrawingBuffer: false, powerPreference: "high-performance" });
  if (!gl) return false;
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
  glState = { tex, texW: 0, texH: 0, verts: 0, quadsFor: null, smooth: null };
  gl.clearColor(0, 0, 0, 0);
  return true;
}

function glQuads() {
  if (!layout || !glState.texW) return;
  const { cw, ch, rects } = layout, W = glState.texW, H = glState.texH;
  const v = [];
  for (const r of rects) {
    const [sx, sy, sw, sh] = r.src, [dx, dy, dw, dh] = r.dst;
    const x0 = dx / cw * 2 - 1, x1 = (dx + dw) / cw * 2 - 1;
    const y0 = 1 - dy / ch * 2, y1 = 1 - (dy + dh) / ch * 2;
    const u0 = sx / W, u1 = (sx + sw) / W, v0 = sy / H, v1 = (sy + sh) / H;
    v.push(x0, y0, u0, v0, x1, y0, u1, v0, x0, y1, u0, v1, x0, y1, u0, v1, x1, y0, u1, v0, x1, y1, u1, v1);
  }
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(v), gl.STATIC_DRAW);
  glState.verts = v.length / 4;
  glState.quadsFor = layout;
}

function glDraw(upload) {
  if (!gl) return;
  if (upload && frame) {
    const { ptr, w, h: fh, pitch, buf } = frame;
    if (w !== glState.texW || fh !== glState.texH) {
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, fh, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      glState.texW = w; glState.texH = fh; glState.quadsFor = null;
    }
    gl.pixelStorei(gl.UNPACK_ROW_LENGTH, pitch);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, w, fh, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array(buf, ptr, pitch * 4 * fh));
  }
  if (!layout || !glState.texW) return;
  if (glState.smooth !== layout.smooth) {
    const f = layout.smooth ? gl.LINEAR : gl.NEAREST;
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, f);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, f);
    glState.smooth = layout.smooth;
  }
  if (glState.quadsFor !== layout) glQuads();
  gl.viewport(0, 0, layout.cw, layout.ch);
  gl.clear(gl.COLOR_BUFFER_BIT);
  gl.drawArrays(gl.TRIANGLES, 0, glState.verts);
}

function setLayout(l, canvas) {
  layout = l;
  canvas = canvas || (gl && gl.canvas);
  if (canvas && (canvas.width !== l.cw || canvas.height !== l.ch)) { canvas.width = l.cw; canvas.height = l.ch; }
  glDraw(false);
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

/* ------------------------------------------------------------ the run */
// waits shorter than a millisecond go through a message, not setTimeout (which
// browsers stretch to 4 ms once timeouts nest)
const zero = new MessageChannel(), zeroQ = [];
zero.port1.onmessage = () => { const f = zeroQ.shift(); if (f) f(); };
const schedule = (fn, ms) => { if (ms <= 0) { zeroQ.push(fn); zero.port2.postMessage(0); } else setTimeout(fn, ms); };

async function boot(m) {
  if (m.canvas && !glInit(m.canvas)) { post({ t: "exit", code: -1, error: "This browser has no WebGL2, which the player draws with." }); return; }
  if (m.layout) setLayout(m.layout, m.canvas);
  attachAudio(m.audioPort || null);
  const fs = new GBADS.MemFS();
  for (const [p, b, t] of m.files) fs.write(p, b, t);
  for (const [p] of m.files) if (KEEP.test(p)) { const n = fs.nodes.get(p); seen.set(p, n.size + ":" + (n.mtime || 0)); }
  for (const d of ["/states", "/saves", "/shots", "/home", "/roms"]) fs.mkdirs(d);
  log("$ " + m.args.join(" "));
  cap.want = !!m.captureWorker;
  dbg.on = !!m.debug;
  fpsT = performance.now(); snd.wall = fpsT;
  try {
    h = await GBADS.start({
      wasm: m.wasm, fs, args: m.args, env: m.env, log,
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

onmessage = async (e) => {
  const m = e.data;
  switch (m.t) {
    case "boot": await boot(m); break;
    case "bootCapture": await bootCapture(m); break;
    case "capjob": serve.job = m; serveTick(); break;
    case "capresult": cap.inbox.push(m); if (paused) pumpCaptures(); break;
    case "capoff": cap.off = true; pumpCaptures(); break;
    case "capreturn": serve.outSpare = m.buf; break;
    case "layout": setLayout(m.layout); break;
    case "buttons": if (!paused) setButtons(m.mask); break;
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
