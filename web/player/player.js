/* GBA DS web player: the page. Takes a GBA DS build (its web/gba-ds.wasm: the
 * one that comes with the player, or a zip dropped on it) and a
 * game, runs GBA DS in a worker (worker.js) for the screens of a chosen
 * device, and maps the keyboard and controllers to the device's buttons.
 * Packed by scripts/build-web-player.sh into one .html file or a folder. */
"use strict";
(() => {
const $ = (id) => document.getElementById(id);
const VERSION = document.documentElement.dataset.version || "dev";

/* ================================================================ the device's buttons */
// ui.h's order: bit i is B_A, B_B, B_X, B_Y, B_START, B_SELECT, B_HOME, B_L, B_R, B_L2, B_R2, B_L3, B_UP, B_DOWN, B_LEFT, B_RIGHT
const BUTTONS = [
  { id: "up", bit: 12, name: "Up", note: "D-pad" },
  { id: "down", bit: 13, name: "Down", note: "D-pad" },
  { id: "left", bit: 14, name: "Left", note: "D-pad" },
  { id: "right", bit: 15, name: "Right", note: "D-pad" },
  { id: "a", bit: 0, name: "A", note: "right face button" },
  { id: "b", bit: 1, name: "B", note: "bottom" },
  { id: "x", bit: 2, name: "X", note: "top" },
  { id: "y", bit: 3, name: "Y", note: "left" },
  { id: "l", bit: 7, name: "L", note: "" },
  { id: "r", bit: 8, name: "R", note: "" },
  { id: "l2", bit: 9, name: "L2", note: "hold to rewind" },
  { id: "r2", bit: 10, name: "R2", note: "hold to fast-forward" },
  { id: "l3", bit: 11, name: "L3", note: "swap screens" },
  { id: "start", bit: 4, name: "Start", note: "" },
  { id: "select", bit: 5, name: "Select", note: "" },
  { id: "home", bit: 6, name: "Home", note: "GBA DS's menu" },
];
const KEY_DEFAULTS = {
  up: ["ArrowUp"], down: ["ArrowDown"], left: ["ArrowLeft"], right: ["ArrowRight"],
  a: ["KeyX"], b: ["KeyZ"], x: ["KeyS"], y: ["KeyA"], l: ["KeyQ"], r: ["KeyW"],
  l2: ["KeyE"], r2: ["Space"], l3: ["Tab"], start: ["Enter"], select: ["ShiftRight", "Backspace"], home: ["Escape", "KeyH"],
};
// the browsers' standard layout, by position (as GBA DS reads face buttons on the device)
const PAD_STANDARD = {
  up: [{ b: 12 }, { a: 1, d: -1 }], down: [{ b: 13 }, { a: 1, d: 1 }], left: [{ b: 14 }, { a: 0, d: -1 }], right: [{ b: 15 }, { a: 0, d: 1 }],
  a: [{ b: 1 }], b: [{ b: 0 }], x: [{ b: 3 }], y: [{ b: 2 }], l: [{ b: 4 }], r: [{ b: 5 }], l2: [{ b: 6 }], r2: [{ b: 7 }],
  l3: [{ b: 10 }], start: [{ b: 9 }], select: [{ b: 8 }], home: [{ b: 16 }],
};
// a controller the browser doesn't recognise: a guess, to be mapped
const PAD_GUESS = {
  up: [{ a: 1, d: -1 }], down: [{ a: 1, d: 1 }], left: [{ a: 0, d: -1 }], right: [{ a: 0, d: 1 }],
  a: [{ b: 1 }], b: [{ b: 0 }], x: [{ b: 3 }], y: [{ b: 2 }], l: [{ b: 4 }], r: [{ b: 5 }], l2: [{ b: 6 }], r2: [{ b: 7 }],
  l3: [{ b: 10 }], start: [{ b: 9 }], select: [{ b: 8 }], home: [{ b: 12 }],
};

/* ================================================================ the devices (src/ui.c knownDevices) */
const DEVICES = [
  { id: "rgdsplus", name: "Anbernic RG DS Plus", top: [1024, 768, 4.3], bottom: [1024, 768, 4.3] },
  { id: "rgds", name: "Anbernic RG DS", top: [640, 480, 4.0], bottom: [640, 480, 4.0] },
  { id: "thor", name: "AYN Thor", top: [1920, 1080, 6.0], bottom: [1240, 1080, 3.92] },
  { id: "duo", name: "Retroid Pocket Duo", top: [1920, 1080, 5.5], bottom: [1280, 960, 4.5] },
  { id: "duolite", name: "Retroid Pocket Duo Lite", top: [1920, 1080, 5.5], bottom: [1280, 960, 4.2] },
  { id: "pocketds", name: "AYANEO Pocket DS", top: [1920, 1080, 7.0], bottom: [1024, 768, 5.0] },
  { id: "rgdds", name: "Anbernic RG DDS", top: [1200, 720, 4.7], bottom: [960, 720, 4.0] },
  { id: "gba", name: "Two GBA screens (240×160, 3×)", top: [720, 480, 2.9], bottom: [720, 480, 2.9] },
];

/* ================================================================ settings */
const SET_KEY = "gbadsPlayer.settings", CTL_KEY = "gbadsPlayer.controls";
const DEFAULTS = { device: "rgdsplus", custom: { top: [1024, 768, 4.3], bottom: [1024, 768, 4.3] }, arrange: "auto", sizes: "device",
  gap: 4, smooth: true, scale: 1, volume: 80, muted: false, pauseHidden: true,
  touchShow: "auto", touchPlace: "below", touchSize: 100, touchVibrate: true };
const readJSON = (k) => { try { return JSON.parse(localStorage.getItem(k) || "null"); } catch (e) { return null; } };
const writeJSON = (k, v) => { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) {} };
const S = Object.assign({}, DEFAULTS, readJSON(SET_KEY) || {});
const saveSettings = () => writeJSON(SET_KEY, S);
const ctl = Object.assign({ keys: null, pads: {} }, readJSON(CTL_KEY) || {});
const keyMap = () => ctl.keys || KEY_DEFAULTS;
const saveControls = () => writeJSON(CTL_KEY, ctl);

/* ================================================================ storage (IndexedDB) */
let dbp = null;
function db() {
  if (dbp) return dbp;
  dbp = new Promise((res, rej) => {
    let r;
    try { r = indexedDB.open("gba-ds-player", 1); } catch (e) { rej(e); return; }
    r.onupgradeneeded = () => { for (const s of ["kv", "roms", "files"]) if (!r.result.objectStoreNames.contains(s)) r.result.createObjectStore(s); };
    r.onsuccess = () => res(r.result);
    r.onerror = () => rej(r.error);
  });
  dbp.catch(() => {});
  return dbp;
}
async function idb(store, mode, fn) {
  const d = await db();
  return new Promise((res, rej) => {
    const tx = d.transaction(store, mode), st = tx.objectStore(store);
    let out;
    const q = fn(st);
    if (q) q.onsuccess = () => { out = q.result; };
    tx.oncomplete = () => res(out);
    tx.onerror = () => rej(tx.error);
    tx.onabort = () => rej(tx.error);
  });
}
const kvGet = (k) => idb("kv", "readonly", (s) => s.get(k)).catch(() => undefined);
const kvSet = (k, v) => idb("kv", "readwrite", (s) => s.put(v, k)).catch(() => {});
async function allFiles() {
  try {
    const d = await db();
    return await new Promise((res, rej) => {
      const out = [], tx = d.transaction("files", "readonly"), c = tx.objectStore("files").openCursor();
      c.onsuccess = () => { const cur = c.result; if (cur) { out.push([cur.key, cur.value]); cur.continue(); } };
      tx.oncomplete = () => res(out); tx.onerror = () => rej(tx.error);
    });
  } catch (e) { return []; }
}
function keepFiles(files, deleted) {
  if (!files.length && !deleted.length) return;
  idb("files", "readwrite", (s) => { for (const [p, b] of files) s.put(b, p); for (const p of deleted) s.delete(p); }).catch(() => {});
}

/* ================================================================ little things */
function toast(text, err) {
  const t = document.createElement("div");
  t.className = "toast" + (err ? " err" : "");
  t.textContent = text;
  $("toasts").appendChild(t);
  setTimeout(() => t.remove(), err ? 7000 : 3500);
}
const extOf = (n) => (n.match(/\.([^.\/]+)$/) || ["", ""])[1].toLowerCase();
const ascii = (b, from, to) => b && b.length >= to ? String.fromCharCode(...b.subarray(from, to)).replace(/[^\x20-\x7e]/g, "").trim() : "";
const romCode = (b) => ascii(b, 0xac, 0xb0);
const romTitle = (b) => ascii(b, 0xa0, 0xac);
// a GBA ROM: the fixed byte 0x96 at 0xB2 and a branch at the start
const looksLikeRom = (b) => b.length >= 0xc0 && b[0xb2] === 0x96 && b[3] === 0xea;
function download(name, data, type) {
  const url = URL.createObjectURL(data instanceof Blob ? data : new Blob([data], { type: type || "application/octet-stream" }));
  const a = document.createElement("a");
  a.href = url; a.download = name;
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}
const stamp = () => { const d = new Date(), p = (n) => String(n).padStart(2, "0"); return d.getFullYear() + p(d.getMonth() + 1) + p(d.getDate()) + "-" + p(d.getHours()) + p(d.getMinutes()) + p(d.getSeconds()); };
const baseName = (n) => n.replace(/[\/\\]/g, "_").replace(/\.[^.]+$/, "");
// a script's source: inline in the one-file player (script#src-NAME), else NAME.js beside the page
function sourceURL(name, extra) {
  const el = document.getElementById("src-" + name);
  if (!el) return name + ".js";
  const parts = (extra || []).map((n) => document.getElementById("src-" + n).textContent);
  parts.push(el.textContent);
  return URL.createObjectURL(new Blob(parts, { type: "text/javascript" }));
}

/* ================================================================ what's loaded */
const P = {
  build: null,       // { name, version, module, files: [[rel, bytes]], bundled }
  rom: null,         // { name, bytes, code, title }
  save: null,        // { name, bytes } battery save to start with
  state: null,       // { name, bytes } state to start from
  worker: null, running: false, paused: false, autoPaused: false, booting: false,
  caps: null, log: [], reqs: new Map(), reqN: 0,
  win: null,         // the app's window: { w, h, screens: [{ x, y, w, h, inch }] }
  rects: null,       // the screens on the page (CSS px): [{ x, y, w, h }]
};

// what a build's web version can do, from what it exports
function buildCaps(module) {
  const names = new Set(WebAssembly.Module.exports(module).map((e) => e.name));
  return { hasAudio: names.has("web_audio_pull"), hasButtons: names.has("web_buttons"), hasCaptureWorker: names.has("web_capture_job") };
}
const NO_SOUND = "No sound: this build's web version was made without it.";
const NO_BUTTONS = "Keyboard only: X and Y can't be pressed with this build.";
function warnCaps(b) {
  if (!b.caps.hasAudio) toast(NO_SOUND, true);
  else if (!b.caps.hasButtons) toast(NO_BUTTONS, true);
}

async function takeBuild(name, bytes, opts) {
  let entries;
  try { entries = GBAZip.open(bytes); } catch (e) { toast(name + ": " + e.message, true); return false; }
  return takeBuildEntries(name, entries, bytes, opts || {});
}
async function takeBuildEntries(name, entries, bytes, opts) {
  const w = entries.find((e) => /(^|\/)web\/gba-ds\.wasm$/.test(e.name));
  if (!w) return false;
  const root = w.name.slice(0, -"web/gba-ds.wasm".length);
  const files = [];
  let version = "";
  for (const e of entries) {
    if (e.dir || !e.name.startsWith(root)) continue;
    const rel = e.name.slice(root.length);
    if (rel === "web/VERSION") { version = new TextDecoder().decode(await e.read()).trim(); continue; }
    if (rel === "gba-ds" || rel.startsWith("web/") || rel.startsWith("licenses/") || rel.startsWith("images/") || /\.(sh|md|zip)$/.test(rel)) continue;
    files.push([rel, await e.read()]);
  }
  if (!version) version = name.replace(/^gba-ds-v?/, "").replace(/-linux.*$|\.zip$/g, "") || "unknown";
  let module;
  try { module = await WebAssembly.compile(await w.read()); }
  catch (e) { toast("That build's web/gba-ds.wasm didn't compile: " + e.message, true); return false; }
  P.build = { name, version, module, files, bundled: !!opts.bundled, caps: buildCaps(module) };
  if (opts.remember) kvSet("build", { name, bytes });
  showSlots();
  return true;
}
async function takeBareWasm(name, bytes) {
  let module;
  try { module = await WebAssembly.compile(bytes); } catch (e) { toast(name + " didn't compile: " + e.message, true); return false; }
  P.build = { name, version: "local", module, files: [], bundled: false, caps: buildCaps(module) };
  showSlots();
  return true;
}
function takeRom(name, bytes, remember) {
  P.rom = { name, bytes, code: romCode(bytes), title: romTitle(bytes) || baseName(name) };
  if (remember !== false) rememberRom(P.rom);
  showSlots();
}
async function rememberRom(r) {
  try {
    await idb("roms", "readwrite", (s) => s.put(r.bytes, r.name));
    const list = ((await kvGet("recent")) || []).filter((x) => x.name !== r.name);
    list.unshift({ name: r.name, code: r.code, title: r.title, t: Date.now() });
    for (const old of list.splice(10)) idb("roms", "readwrite", (s) => s.delete(old.name)).catch(() => {});
    await kvSet("recent", list);
    showRecent();
  } catch (e) {}
}

// what dropped files are, in an order that lets each make sense: builds, games, saves, states
async function takeFiles(list) {
  const items = [];
  for (const f of list) items.push({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()) });
  let newGame = false, newBuild = false, newState = null;
  for (const it of items) {
    const ext = extOf(it.name);
    try {
      if (GBAZip.isZip(it.bytes)) {
        const entries = GBAZip.open(it.bytes);
        if (await takeBuildEntries(it.name, entries, it.bytes, { remember: true })) { newBuild = true; toast("Build " + P.build.version + " loaded."); warnCaps(P.build); continue; }
        const st = entries.find((e) => /\.state$/i.test(e.name) && !/-rewound\.state$/i.test(e.name));
        if (st && entries.some((e) => /-info\.txt$/i.test(e.name))) {
          newState = { name: st.name.split("/").pop(), bytes: await st.read() };
          toast("Bug report: its moment loads with the game (" + newState.name + ").");
          continue;
        }
        const romE = entries.find((e) => /\.(gba|agb|bin)$/i.test(e.name) && !e.dir);
        if (romE) { const b = await romE.read(); if (looksLikeRom(b)) { takeRom(romE.name.split("/").pop(), b); newGame = true; continue; } }
        toast(it.name + ": no GBA DS build, game or bug report inside.", true);
      } else if (ext === "wasm") {
        if (await takeBareWasm(it.name, it.bytes)) { newBuild = true; toast("Web build loaded (no settings file with it: GBA DS's defaults)."); warnCaps(P.build); }
      } else if (["gba", "agb", "bin"].includes(ext) || looksLikeRom(it.bytes)) {
        takeRom(it.name, it.bytes); newGame = true;
      } else if (["sav", "srm"].includes(ext)) {
        P.save = { name: it.name, bytes: it.bytes }; showSlots();
      } else if (/^(ss\d?|state)$/.test(ext)) {
        newState = { name: it.name, bytes: it.bytes };
      } else {
        toast(it.name + ": drop a GBA DS build (.zip), a game (.gba), a save (.sav) or a state (.ss).", true);
      }
    } catch (e) { toast(it.name + ": " + e.message, true); }
  }
  // a state on its own, into the game that's running
  if (newState && P.running && !newGame && !newBuild) {
    const ok = await request({ t: "loadState", bytes: newState.bytes });
    toast(ok.ok ? "Loaded " + newState.name + "." : "That state didn't load. Is it for this game?", !ok.ok);
    return;
  }
  if (newState) { P.state = newState; showSlots(); }
  if (newBuild && P.running && !newGame && !P.save) { await restart(true, "Running the new build from the same moment."); return; }
  if ((newGame || newBuild || newState || P.save) && P.build && P.rom) await boot();
  else showSlots();
}

/* ================================================================ the screens */
function device() {
  if (S.device === "custom") return { id: "custom", name: "Custom", top: S.custom.top, bottom: S.custom.bottom };
  return DEVICES.find((d) => d.id === S.device) || DEVICES[0];
}
// the window GBA DS gets: both screens stacked, the top one first, each centred across it
function appWindow() {
  const d = device(), k = Number(S.scale) || 1;
  const [tw, th] = d.top.map((v, i) => i < 2 ? Math.max(i ? 160 : 240, Math.round(v * k / 2) * 2) : v);
  const [bw, bh] = d.bottom.map((v, i) => i < 2 ? Math.max(i ? 160 : 240, Math.round(v * k / 2) * 2) : v);
  const W = Math.max(tw, bw), H = th + bh;
  const same = tw === bw && th === bh;
  const ppi = (w, h, inch) => (Math.hypot(w, h) / inch).toFixed(1);
  return {
    w: W, h: H,
    screens: [{ x: (W - tw) >> 1, y: 0, w: tw, h: th, inch: d.top[2] }, { x: (W - bw) >> 1, y: th, w: bw, h: bh, inch: d.bottom[2] }],
    env: same ? {} : { GBADS_SCREENS: tw + "x" + th + "," + bw + "x" + bh },
    args: same ? [] : ["--screen_ppi=" + ppi(tw, th, d.top[2]) + "," + ppi(bw, bh, d.bottom[2])],
  };
}
// where the two screens go on the page (CSS px), for the arrangement and sizes chosen
function placeScreens(win, W, H) {
  const [a, b] = win.screens;
  const dims = (s) => S.sizes === "device" ? (() => { const ppi = Math.hypot(s.w, s.h) / s.inch; return [s.w / ppi, s.h / ppi]; })() : [s.w, s.h];
  const [aw, ah] = dims(a), [bw, bh] = dims(b);
  const pad = Math.min(W, H) < 500 ? 8 : 24;
  const try_ = (side) => {
    const g = (Number(S.gap) / 100) * (side ? (aw + bw) / 2 : (ah + bh) / 2);
    const tw = side ? aw + g + bw : Math.max(aw, bw), th = side ? Math.max(ah, bh) : ah + g + bh;
    const k = Math.min((W - pad * 2) / tw, (H - pad * 2) / th);
    const ox = (W - tw * k) / 2, oy = (H - th * k) / 2;
    const r = side
      ? [{ x: ox, y: oy + (th - ah) * k / 2, w: aw * k, h: ah * k }, { x: ox + (aw + g) * k, y: oy + (th - bh) * k / 2, w: bw * k, h: bh * k }]
      : [{ x: ox + (tw - aw) * k / 2, y: oy, w: aw * k, h: ah * k }, { x: ox + (tw - bw) * k / 2, y: oy + (ah + g) * k, w: bw * k, h: bh * k }];
    return { k, r };
  };
  const pick = S.arrange === "side" ? try_(true) : S.arrange === "stack" ? try_(false) : (() => { const s = try_(true), t = try_(false); return s.k > t.k * 1.02 ? s : t; })();
  return pick.r;
}
// the on-screen controls: on a touchscreen without a controller (or always, or never), while playing
const coarse = matchMedia("(pointer: coarse)");
function touchShown() {
  if (!(P.running || P.booting) || S.touchShow === "never") return false;
  return S.touchShow === "always" || (coarse.matches && !pads().length);
}
// the screen's safe area (notch, home bar), from CSS (?safe=top,right,bottom,left stands in for one, to model a phone)
// (#app is padded by the top and sides already; the controls keep clear of the bottom's)
let safeProbe = null;
function safeArea() {
  if (!safeProbe) {
    safeProbe = document.createElement("div");
    safeProbe.style.cssText = "position:fixed;left:0;top:0;width:0;height:0;visibility:hidden;pointer-events:none;padding-bottom:var(--sab)";
    document.body.appendChild(safeProbe);
  }
  return { top: 0, left: 0, right: 0, bottom: parseFloat(getComputedStyle(safeProbe).paddingBottom) || 0 };
}
function relayout() {
  const stage = $("stage"), W = stage.clientWidth, H = stage.clientHeight;
  if (!W || !H) return;
  const win = P.win || appWindow();
  const box = GBATouch.place(W, H, { show: touchShown(), placement: S.touchPlace, size: S.touchSize / 100, vibrate: !!S.touchVibrate, safe: safeArea() });
  const rects = placeScreens(win, box.w, box.h).map((r) => ({ x: r.x + box.x, y: r.y + box.y, w: r.w, h: r.h }));
  P.rects = rects;
  const dpr = window.devicePixelRatio || 1;
  const bz = $("bezels");
  bz.textContent = "";
  if (P.running || P.booting) for (const r of rects) {
    const d = document.createElement("div");
    d.className = "bezel";
    Object.assign(d.style, { left: r.x + "px", top: r.y + "px", width: r.w + "px", height: r.h + "px" });
    bz.appendChild(d);
  }
  if (!P.worker) return;
  const R = (v) => Math.round(v * dpr);
  P.worker.postMessage({ t: "layout", layout: {
    cw: R(W), ch: R(H), smooth: !!S.smooth,
    rects: win.screens.map((s, i) => ({ src: [s.x, s.y, s.w, s.h], dst: [R(rects[i].x), R(rects[i].y), R(rects[i].x + rects[i].w) - R(rects[i].x), R(rects[i].y + rects[i].h) - R(rects[i].y)] })),
  } });
}
new ResizeObserver(() => relayout()).observe($("stage"));
matchMedia("(resolution: 1dppx)").addEventListener?.("change", relayout);

/* ================================================================ sound */
const A = { ctx: null, node: null, gain: null, ready: null };
function soundGain() { return S.muted ? 0 : Math.pow(S.volume / 100, 2); }
function ensureSound() {
  if (A.ready) { if (A.ctx && A.ctx.state === "suspended") A.ctx.resume().catch(() => {}); return A.ready; }
  A.ready = (async () => {
    try {
      const C = window.AudioContext || window.webkitAudioContext;
      try { A.ctx = new C({ sampleRate: 48000, latencyHint: "interactive" }); } catch (e) { A.ctx = new C(); }
      A.gain = A.ctx.createGain();
      try {
        await A.ctx.audioWorklet.addModule(sourceURL("worklet"));
        A.node = new AudioWorkletNode(A.ctx, "gbads-out", { numberOfInputs: 0, outputChannelCount: [2] });
        A.link = (port) => A.node.port.postMessage({ port }, [port]);
      } catch (e) {
        // (Chrome won't load a worklet for a page opened from disk: the same ring, played here)
        console.info("sound without a worklet:", e.message);
        const src = document.getElementById("src-worklet") ? document.getElementById("src-worklet").textContent : await (await fetch("worklet.js")).text();
        const Ring = new Function(src + "\nreturn GbadsRing;")();
        const ring = new Ring(A.ctx.sampleRate);
        A.node = A.ctx.createScriptProcessor(1024, 0, 2);
        A.node.onaudioprocess = (ev) => ring.fill(ev.outputBuffer.getChannelData(0), ev.outputBuffer.getChannelData(1));
        A.link = (port) => ring.link(port);
        A.ring = ring;
      }
      A.gain.gain.value = soundGain();
      A.node.connect(A.gain).connect(A.ctx.destination);
      if (A.ctx.state === "suspended") await A.ctx.resume().catch(() => {});
      linkSound();
      showSound();
    } catch (e) {
      console.warn("no sound:", e);
      toast("No sound in this browser: " + (e.message || e), true);
    }
  })();
  return A.ready;
}
// a fresh channel from this run's worker to the worklet
function linkSound() {
  if (!A.node || !P.worker) return;
  const ch = new MessageChannel();
  A.link(ch.port1);
  P.worker.postMessage({ t: "audio", port: ch.port2 }, [ch.port2]);
}
function showSound() {
  const off = S.muted || !A.ctx || A.ctx.state !== "running" || !!(P.build && !P.build.caps.hasAudio);
  $("soundBtn").classList.toggle("on", off);
  $("soundBtn").title = P.build && !P.build.caps.hasAudio ? "This build has no sound" : S.muted ? "Sound is off" : !A.ctx || A.ctx.state !== "running" ? "Click anywhere for sound" : "Sound on";
  if (A.gain) A.gain.gain.setTargetAtTime(soundGain(), A.ctx.currentTime, 0.02);
}
for (const ev of ["pointerdown", "keydown", "drop"]) window.addEventListener(ev, () => { ensureSound().then(showSound); }, { capture: true });

/* ================================================================ the run */
function request(m, transfer) {
  return new Promise((res) => {
    if (!P.worker) { res({}); return; }
    const id = ++P.reqN;
    P.reqs.set(id, res);
    P.worker.postMessage(Object.assign(m, { id }), transfer || []);
    setTimeout(() => { if (P.reqs.has(id)) { P.reqs.delete(id); res({}); } }, 5000);
  });
}
function addLog(line) {
  P.log.push(line);
  if (P.log.length > 2000) P.log.splice(0, P.log.length - 2000);
  if ($("logDlg").open) { const el = $("logText"), end = el.scrollTop + el.clientHeight >= el.scrollHeight - 4; el.textContent = P.log.join("\n"); if (end) el.scrollTop = el.scrollHeight; }
}
async function stopRun() {
  stopCapture();
  if (!P.worker) return;
  await request({ t: "stop" });
  P.worker.terminate();
  P.worker = null;
  P.running = false;
}
// a new canvas each run (a canvas hands its drawing to one worker only)
function freshCanvas() {
  const old = $("screen"), c = document.createElement("canvas");
  c.id = "screen";
  old.replaceWith(c);
  bindTouch(c);
  return c;
}
async function boot(opts) {
  opts = opts || {};
  if (!P.build) { toast("Now drop a GBA DS build (.zip) with a web build inside.", true); showSlots(); return; }
  if (!P.rom && !opts.library) { toast("Now drop a GBA game (.gba).", true); showSlots(); return; }
  if (P.booting) return;
  P.booting = true;
  const state = opts.state || (P.state && P.state.bytes) || null;
  P.state = null;
  await stopRun();
  if (typeof OffscreenCanvas === "undefined" || !HTMLCanvasElement.prototype.transferControlToOffscreen) {
    P.booting = false;
    toast("This browser can't draw from a worker (OffscreenCanvas). Firefox 105, Chrome 69 or Safari 17 and later can.", true);
    return;
  }
  const win = appWindow();
  P.win = win;
  const files = [];
  for (const [rel, b] of P.build.files) files.push(["/gba-ds/" + rel, b]);
  for (const [p, b] of await allFiles()) files.push([p, b]);
  const args = ["/gba-ds/gba-ds", "--asset-dir=/gba-ds", "--state-dir=/states", "--save-dir=/saves", "--screenshot-dir=/shots", "--rom-dir=/roms", ...win.args];
  if (opts.library) {
    const recent = (await kvGet("recent")) || [];
    for (const r of recent) { const b = await idb("roms", "readonly", (s) => s.get(r.name)).catch(() => null); if (b) files.push(["/roms/" + r.name.replace(/[\/\\]/g, "_"), b]); }
  } else {
    const romName = P.rom.name.replace(/[\/\\]/g, "_");
    files.push(["/roms/" + romName, P.rom.bytes]);
    if (P.save) { files.push(["/saves/" + baseName(romName) + ".sav", P.save.bytes]); P.save = null; }
    if (state) { files.push(["/load.ss", state]); args.push("--load-state=/load.ss"); }
    args.push("/roms/" + romName);
  }
  const debug = /[?&]debug\b/.test(location.search);
  const env = { GBADS_CONFIG: "/gba-ds/gba-ds.cfg", GBADS_WINDOW: win.w + "x" + win.h, HOME: "/home", ...win.env };
  if (debug) env.GBADS_DEBUG = "1";
  // map captures in a worker of their own, from the start (?nocapw: in the game, to compare)
  const capWorker = !!P.build.caps.hasCaptureWorker && !/[?&]nocapw\b/.test(location.search);
  if (capWorker) env.GBADS_CAPTURE_OFFLOAD = "1";
  P.capFiles = files.filter(([p]) => p.startsWith("/gba-ds/") || p.startsWith("/roms/"));
  P.capEnv = { GBADS_CONFIG: env.GBADS_CONFIG, HOME: "/home", ...(debug ? { GBADS_DEBUG: "1" } : {}) };
  P.log.length = 0;
  let worker;
  try { worker = new Worker(sourceURL("worker", ["runtime"])); }
  catch (e) { P.booting = false; toast("The player's worker didn't start: " + e.message, true); return; }
  P.worker = worker;
  P.caps = null;
  worker.onmessage = (e) => onWorker(worker, e.data);
  worker.onerror = (e) => { if (P.worker === worker) { addLog("worker error: " + e.message); toast("GBA DS stopped: " + e.message, true); endRun(worker); } };
  const canvas = freshCanvas().transferControlToOffscreen();
  document.body.classList.add("playing");
  $("start").hidden = true;
  P.running = true; P.paused = false; P.autoPaused = false; P.library = !!opts.library;
  showPaused();
  showNow(opts.library);
  relayout();
  const stage = $("stage"), dpr = window.devicePixelRatio || 1;
  worker.postMessage({ t: "boot", wasm: P.build.module, files, args, env, canvas, captureWorker: capWorker, debug,
    layout: { cw: Math.round(stage.clientWidth * dpr), ch: Math.round(stage.clientHeight * dpr), smooth: !!S.smooth, rects: [] } }, [canvas]);
  relayout();
  if (A.node) linkSound();
  P.booting = false;
  lastMask = -1;
}
/* ---- map captures in a worker of their own (a second copy of the app) */
const CAP = { w: null, key: "", busy: false, next: null, dead: false };
function stopCapture() {
  if (CAP.w) CAP.w.terminate();
  Object.assign(CAP, { w: null, key: "", busy: false, next: null, dead: false });
}
function captureJob(m) {
  if (CAP.dead) return;
  const key = m.rom + "|" + m.code + "|" + m.tight;
  if (!CAP.w || CAP.key !== key) {
    if (CAP.w) CAP.w.terminate();
    let w;
    try { w = new Worker(sourceURL("worker", ["runtime"])); }
    catch (e) { captureFailed("the capture worker didn't start: " + e.message); return; }
    Object.assign(CAP, { w, key, busy: false, next: null });
    w.onmessage = (e) => onCapture(w, e.data);
    w.onerror = (e) => { if (CAP.w === w) captureFailed(e.message); };
    const env = Object.assign({}, P.capEnv, { GBADS_CAPTURE_SERVE: m.code, GBADS_CAPTURE_TIGHT: m.tight ? "1" : "0" });
    w.postMessage({ t: "bootCapture", wasm: P.build.module, files: P.capFiles, args: ["/gba-ds/gba-ds", "--asset-dir=/gba-ds", m.rom], env });
  }
  // (one at a time: a newer job waiting replaces an older one)
  if (CAP.busy) { CAP.next = m; return; }
  CAP.busy = true;
  CAP.w.postMessage({ t: "capjob", job: m.job, state: m.state, still: m.still }, [m.state]);
}
function onCapture(w, m) {
  if (w !== CAP.w) return;
  switch (m.t) {
    case "log": addLog("[capture] " + m.line); break;
    case "capresult":
      CAP.results = (CAP.results || 0) + 1;
      if (P.worker) P.worker.postMessage(m, m.state ? [m.buf, m.state] : [m.buf]);
      CAP.busy = false;
      if (CAP.next) { const n = CAP.next; CAP.next = null; captureJob(n); }
      break;
    case "exit": captureFailed(m.error || "it closed (" + m.code + ")"); break;
  }
}
// captures go back to running in the game (slower, but the second screen keeps working)
function captureFailed(why) {
  addLog("capture worker stopped: " + why + "; captures run in the game from now on");
  if (CAP.w) CAP.w.terminate();
  Object.assign(CAP, { w: null, busy: false, next: null, dead: true });
  if (P.worker) P.worker.postMessage({ t: "capoff" });
}

function onWorker(worker, m) {
  if (worker !== P.worker) return;
  switch (m.t) {
    case "capjob": CAP.jobs = (CAP.jobs || 0) + 1; captureJob(m); break;
    // (the pictures' buffer, back to the capture worker to be used again)
    case "capreturn": if (CAP.w) CAP.w.postMessage(m, [m.buf]); break;
    case "log": addLog(m.line); break;
    case "fps": $("fps").textContent = Math.round(m.fps) + " fps"; break;
    case "started":
      P.caps = m;
      break;
    case "files": keepFiles(m.files, m.deleted); if (m.id && P.reqs.has(m.id)) { P.reqs.get(m.id)(m); P.reqs.delete(m.id); } break;
    case "reply": if (P.reqs.has(m.id)) { P.reqs.get(m.id)(m); P.reqs.delete(m.id); } break;
    case "exit":
      if (m.error) { addLog("stopped: " + m.error); toast("GBA DS stopped: " + m.error.split("\n")[0] + " (the log in Settings has more).", true); }
      else { addLog("exited (" + m.code + ")"); toast("GBA DS closed."); }
      endRun(worker);
      break;
  }
}
function endRun(worker) {
  if (worker !== P.worker) return;
  stopCapture();
  setTimeout(() => worker.terminate(), 200);
  P.worker = null;
  showStopped();
}
function showStopped() {
  P.running = false; P.paused = false;
  GBATouch.release();
  document.body.classList.remove("playing");
  $("start").hidden = false;
  $("fps").textContent = "";
  showPaused(); showNow(); showSlots(); relayout();
}
// again, from this moment (device or resolution changed, a new build)
async function restart(keepMoment, note) {
  if (!P.running) return;
  let state = null;
  if (keepMoment) { const r = await request({ t: "saveState" }); state = r.bytes || null; }
  await request({ t: "sync" });
  if (note) toast(note);
  await boot({ state, library: P.library });
}
function setPaused(on, auto) {
  if (!P.worker) return;
  P.paused = on; P.autoPaused = on && !!auto;
  P.worker.postMessage({ t: "pause", on });
  showPaused();
}
function showPaused() {
  $("pausedVeil").hidden = !(P.running && P.paused);
  $("pauseBtn").classList.toggle("on", P.paused);
  $("pauseBtn").title = P.paused ? "Resume" : "Pause";
}
function showNow(library) {
  $("nowPlaying").hidden = !P.running;
  if (!P.running) return;
  $("nowTitle").textContent = library || !P.rom ? "Game list" : P.rom.title;
  $("nowSub").textContent = (P.rom && !library ? P.rom.code + "  ·  " : "") + "GBA DS " + P.build.version + "  ·  " + device().name;
}
document.addEventListener("visibilitychange", () => {
  if (!P.running) return;
  if (document.hidden && S.pauseHidden && !P.paused) setPaused(true, true);
  else if (!document.hidden && P.autoPaused) setPaused(false);
});
window.addEventListener("pagehide", () => { if (P.worker) P.worker.postMessage({ t: "sync" }); });

/* ================================================================ the start screen */
function showSlots() {
  const b = P.build, r = P.rom;
  $("slotBuild").classList.toggle("have", !!b);
  $("buildName").textContent = b ? "GBA DS " + b.version : "No build yet";
  $("buildHint").textContent = b ? (!b.caps.hasAudio ? "No sound in this build's web version" : !b.caps.hasButtons ? "Keyboard only: no X or Y" : b.bundled ? "Comes with this player · drop another build (.zip) to try it" : b.name) : "A GBA DS build (.zip) with a web build inside";
  $("buildHint").classList.toggle("warn", !!b && !(b.caps.hasAudio && b.caps.hasButtons));
  $("slotGame").classList.toggle("have", !!r);
  $("gameName").textContent = r ? r.title : "No game yet";
  $("gameHint").textContent = r ? r.code + " · " + r.name : "A GBA game (.gba, or zipped)";
  const extra = P.state || P.save;
  $("slotSave").classList.toggle("have", !!extra);
  $("saveName").textContent = P.state ? "State: " + P.state.name : P.save ? "Save: " + P.save.name : "The player keeps your saves";
  $("playBtn").disabled = !(b && r);
  $("playBtn").textContent = r ? "Play " + r.title : "Play";
}
async function showRecent() {
  const list = (await kvGet("recent")) || [];
  $("recent").hidden = !list.length;
  $("listBtn").hidden = !list.length || !P.build;
  const ul = $("recentList");
  ul.textContent = "";
  for (const it of list) {
    const li = document.createElement("li");
    const pick = document.createElement("button");
    pick.className = "pick"; pick.type = "button";
    pick.innerHTML = '<span class="t"></span><span class="c"></span>';
    pick.querySelector(".t").textContent = it.title || it.name;
    pick.querySelector(".c").textContent = it.code + " · " + it.name;
    pick.onclick = async () => {
      const bytes = await idb("roms", "readonly", (s) => s.get(it.name)).catch(() => null);
      if (!bytes) { toast("That game isn't kept any more: drop it again.", true); return; }
      takeRom(it.name, bytes, true);
      if (P.build) boot();
    };
    const rm = document.createElement("button");
    rm.className = "rm"; rm.type = "button"; rm.textContent = "×"; rm.title = "Forget this game"; rm.setAttribute("aria-label", "Forget " + (it.title || it.name));
    rm.onclick = async () => {
      await idb("roms", "readwrite", (s) => s.delete(it.name)).catch(() => {});
      await kvSet("recent", list.filter((x) => x.name !== it.name));
      showRecent();
    };
    li.append(pick, rm);
    ul.appendChild(li);
  }
}
for (const id of ["slotBuild", "slotGame", "slotSave", "openBtn"]) $(id).onclick = () => $("fileInput").click();
$("fileInput").onchange = (e) => { const f = [...e.target.files]; e.target.value = ""; if (f.length) takeFiles(f); };
$("playBtn").onclick = () => boot();
$("listBtn").onclick = () => boot({ library: true });

// dropping, anywhere on the page
let dragDepth = 0;
window.addEventListener("dragenter", (e) => { if ([...e.dataTransfer.types].includes("Files")) { dragDepth++; $("dropVeil").hidden = false; } });
window.addEventListener("dragleave", () => { if (--dragDepth <= 0) { dragDepth = 0; $("dropVeil").hidden = true; } });
window.addEventListener("dragover", (e) => { e.preventDefault(); e.dataTransfer.dropEffect = "copy"; });
window.addEventListener("drop", (e) => {
  e.preventDefault(); dragDepth = 0; $("dropVeil").hidden = true;
  const f = [...(e.dataTransfer.files || [])];
  if (f.length) takeFiles(f);
});

/* ================================================================ toolbar */
$("pauseBtn").onclick = () => setPaused(!P.paused);
$("resetBtn").onclick = async () => { await request({ t: "sync" }); boot({ library: P.library }); };
$("closeBtn").onclick = async () => { await stopRun(); showStopped(); };
$("stateBtn").onclick = async () => {
  const r = await request({ t: "saveState" });
  if (!r.bytes) { toast("No state right now: try again in a moment.", true); return; }
  download(baseName(P.rom ? P.rom.name : "gba-ds") + "-" + stamp() + ".ss", r.bytes);
  toast("State saved. Drop it on the player to come back to this moment.");
};
$("shotBtn").onclick = async () => {
  const r = await request({ t: "screenshot" });
  if (!r.blob) { toast("No screenshot right now.", true); return; }
  download(baseName(P.rom ? P.rom.name : "gba-ds") + "-" + stamp() + ".png", r.blob);
};
$("soundBtn").onclick = async () => {
  await ensureSound();
  if (A.ctx && A.ctx.state !== "running") { await A.ctx.resume().catch(() => {}); if (S.muted) S.muted = false; }
  else S.muted = !S.muted;
  saveSettings(); showSound();
};
$("fullBtn").onclick = () => {
  const el = $("stage");
  if (document.fullscreenElement) document.exitFullscreen();
  else (el.requestFullscreen || el.webkitRequestFullscreen).call(el).catch?.(() => {});
};

/* ================================================================ touch: the bottom screen, as on the device */
let touching = false;
function touchAt(e) {
  if (!P.rects || !P.win) return null;
  const c = $("screen").getBoundingClientRect();
  const px = e.clientX - c.left, py = e.clientY - c.top, r = P.rects[1], s = P.win.screens[1];
  const fx = (px - r.x) / r.w, fy = (py - r.y) / r.h;
  const inside = fx >= 0 && fx <= 1 && fy >= 0 && fy <= 1;
  const cx = Math.min(1, Math.max(0, fx)), cy = Math.min(1, Math.max(0, fy));
  return { inside, x: (s.x + cx * s.w) / P.win.w, y: (s.y + cy * s.h) / P.win.h };
}
function bindTouch(c) {
  c.addEventListener("pointerdown", (e) => {
    if (!P.worker || P.paused) return;
    const t = touchAt(e);
    if (!t || !t.inside) return;
    touching = true;
    P.worker.postMessage({ t: "touch", type: 1, x: t.x, y: t.y });
    try { c.setPointerCapture(e.pointerId); } catch (err) {}
    e.preventDefault();
  });
  c.addEventListener("pointermove", (e) => {
    const t = touchAt(e);
    c.classList.toggle("touch", !!(t && t.inside));
    if (touching && P.worker && t) P.worker.postMessage({ t: "touch", type: 3, x: t.x, y: t.y });
  });
  for (const ev of ["pointerup", "pointercancel"]) c.addEventListener(ev, (e) => {
    if (!touching) return;
    touching = false;
    const t = touchAt(e);
    if (P.worker && t) P.worker.postMessage({ t: "touch", type: 2, x: t.x, y: t.y });
  });
  c.addEventListener("contextmenu", (e) => e.preventDefault());
}
bindTouch($("screen"));

/* ================================================================ input: keyboard and controllers */
const keysDown = new Set();
const typing = (e) => { const t = e.target; return t && (t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA"); };
const anyDialog = () => [...document.querySelectorAll("dialog")].some((d) => d.open);
let capture = null; // asking for one key or button: { kind: "key" | "pad", done(binding|null) }
window.addEventListener("keydown", (e) => {
  if (capture && capture.kind === "key") {
    e.preventDefault(); e.stopPropagation();
    if (e.code === "Escape") capture.done(null); else capture.done(e.code, e.key);
    return;
  }
  if (typing(e) || e.metaKey || e.ctrlKey || e.altKey) return;
  const km = keyMap();
  if (BUTTONS.some((b) => (km[b.id] || []).includes(e.code))) {
    keysDown.add(e.code);
    // (in a panel the keys only light up their rows; Escape still closes it)
    if (P.running && !anyDialog()) e.preventDefault();
  }
}, true);
window.addEventListener("keyup", (e) => { keysDown.delete(e.code); });
window.addEventListener("blur", () => keysDown.clear());

// a pad's bindings: what's saved for it, else the standard layout (or a guess)
function padMap(p) { return ctl.pads[p.id] || (p.mapping === "standard" ? PAD_STANDARD : PAD_GUESS); }
// hats as one axis (Firefox and Chrome on macOS, for many Bluetooth pads): 8 positions from -1 to 1, rest beyond
const hatPos = (v) => Math.abs(v) > 1.05 ? -1 : (Math.round((v + 1) * 3.5) + 8) % 8;
function held(p, bd) {
  if ("b" in bd) { const b = p.buttons[bd.b]; return !!b && (b.pressed || b.value > 0.5); }
  if ("h" in bd) { const pos = hatPos(p.axes[bd.a]); if (pos < 0) return false; const d = Math.abs(pos - bd.h); return Math.min(d, 8 - d) <= 1; }
  if ("a" in bd) { const v = p.axes[bd.a]; return v !== undefined && v * bd.d > 0.5 && Math.abs(v) <= 1.05; }
  return false;
}
const pads = () => [...(navigator.getGamepads ? navigator.getGamepads() : [])].filter(Boolean);
function currentMask() {
  let mask = 0;
  const km = keyMap();
  for (const b of BUTTONS) {
    if ((km[b.id] || []).some((c) => keysDown.has(c))) mask |= 1 << b.bit;
  }
  mask |= GBATouch.mask;
  for (const p of pads()) {
    const pm = padMap(p);
    for (const b of BUTTONS) if ((pm[b.id] || []).some((bd) => held(p, bd))) mask |= 1 << b.bit;
  }
  return mask;
}
let lastMask = -1;
function tick() {
  const mask = currentMask();
  if (P.worker && P.running) {
    const send = anyDialog() || P.paused ? 0 : mask;
    if (send !== lastMask) { P.worker.postMessage({ t: "buttons", mask: send }); lastMask = send; }
  }
  if ($("controls").open) showHeld(mask);
  if (capture && capture.kind === "pad") capture.poll();
  requestAnimationFrame(tick);
}
requestAnimationFrame(tick);
window.addEventListener("gamepadconnected", (e) => {
  const p = e.gamepad;
  const known = ctl.pads[p.id] || p.mapping === "standard";
  toast(known ? "Controller connected: " + padName(p) + "." : "Controller connected: " + padName(p) + ". The browser doesn't know its layout: map it in Controls.", !known);
  if ($("controls").open) showControls();
});
window.addEventListener("gamepaddisconnected", () => { if ($("controls").open) showControls(); relayout(); });
window.addEventListener("gamepadconnected", () => relayout());
coarse.addEventListener?.("change", relayout);
const padName = (p) => p.id.replace(/\s*\(.*?Vendor:.*?\)\s*/i, "").replace(/^[0-9a-f]{4}-[0-9a-f]{4}-/i, "").trim() || "Controller";

/* ================================================================ controls panel */
let padSel = null; // the pad the panel shows (its id)
const KEY_LABELS = { ArrowUp: "↑", ArrowDown: "↓", ArrowLeft: "←", ArrowRight: "→", Space: "Space", Enter: "Return", Escape: "Esc", Backspace: "Delete", Tab: "Tab",
  ShiftLeft: "Left shift", ShiftRight: "Right shift", ControlLeft: "Left ctrl", ControlRight: "Right ctrl", AltLeft: "Left option", AltRight: "Right option", Backquote: "`" };
const keyLabel = (c) => KEY_LABELS[c] || c.replace(/^Key/, "").replace(/^Digit/, "").replace(/^Numpad/, "Num ");
const padLabel = (bd) => "b" in bd ? "Button " + bd.b + (STD_NAMES[bd.b] ? " (" + STD_NAMES[bd.b] + ")" : "")
  : "h" in bd ? "Hat " + ["↑", "↗", "→", "↘", "↓", "↙", "←", "↖"][bd.h] : "Axis " + bd.a + (bd.d < 0 ? " −" : " +");
let STD_NAMES = {};
const STD = ["bottom", "right", "left", "top", "L1", "R1", "L2", "R2", "back", "start", "L3", "R3", "↑", "↓", "←", "→", "home"];
function showControls() {
  const list = pads();
  if (!padSel || !list.some((p) => p.id === padSel)) padSel = list.length ? list[0].id : null;
  const pad = list.find((p) => p.id === padSel) || null;
  STD_NAMES = pad && pad.mapping === "standard" ? Object.fromEntries(STD.map((n, i) => [i, n])) : {};
  const line = $("padLine");
  line.textContent = "";
  if (!list.length) {
    line.append("No controller yet: press a button on one (Bluetooth or USB) to use it here.");
  } else {
    const sel = document.createElement("select");
    sel.setAttribute("aria-label", "Controller");
    for (const p of list) { const o = document.createElement("option"); o.value = p.id; o.textContent = padName(p); sel.appendChild(o); }
    sel.value = padSel;
    sel.onchange = () => { padSel = sel.value; showControls(); };
    const st = document.createElement("span");
    const saved = !!ctl.pads[pad.id];
    st.className = "pad-state " + (saved || pad.mapping === "standard" ? "ok" : "warn");
    st.textContent = saved ? "Your mapping" : pad.mapping === "standard" ? "Standard layout" : "Layout not known: map it";
    line.append(sel, st);
  }
  $("padHead").textContent = pad ? padName(pad) : "Controller";
  $("wizardBtn").disabled = !pad; $("resetPad").disabled = !pad || !ctl.pads[pad.id];
  const rows = $("mapRows");
  rows.textContent = "";
  const km = keyMap(), pm = pad ? padMap(pad) : null;
  for (const b of BUTTONS) {
    const tr = document.createElement("div");
    tr.className = "tr"; tr.dataset.bit = b.bit; tr.setAttribute("role", "row");
    const nm = document.createElement("span");
    nm.className = "bname"; nm.innerHTML = "<b></b><small></small>";
    nm.querySelector("b").textContent = b.name; nm.querySelector("small").textContent = b.note;
    const kc = document.createElement("span"); kc.className = "chips";
    for (const c of km[b.id] || []) kc.appendChild(chip(keyLabel(c), () => { setKeys(b.id, (keyMap()[b.id] || []).filter((x) => x !== c)); }));
    kc.appendChild(addBtn("Add a key for " + b.name, () => askKey(b)));
    const pc = document.createElement("span"); pc.className = "chips";
    if (pad) {
      for (const bd of pm[b.id] || []) pc.appendChild(chip(padLabel(bd), () => setPad(pad, b.id, (padMap(pad)[b.id] || []).filter((x) => JSON.stringify(x) !== JSON.stringify(bd)))));
      pc.appendChild(addBtn("Add a controller button for " + b.name, () => askPad(pad, [b])));
    } else { const n = document.createElement("span"); n.className = "none"; n.textContent = "—"; pc.appendChild(n); }
    tr.append(nm, kc, pc);
    rows.appendChild(tr);
  }
}
function chip(text, remove) {
  const c = document.createElement("span");
  c.className = "chip"; c.textContent = text;
  const x = document.createElement("button");
  x.type = "button"; x.textContent = "×"; x.title = "Remove"; x.setAttribute("aria-label", "Remove " + text);
  x.onclick = remove;
  c.appendChild(x);
  return c;
}
function addBtn(label, fn) { const a = document.createElement("button"); a.type = "button"; a.className = "add"; a.textContent = "+"; a.title = label; a.setAttribute("aria-label", label); a.onclick = fn; return a; }
function showHeld(mask) { for (const tr of $("mapRows").children) tr.classList.toggle("held", !!(mask >> Number(tr.dataset.bit) & 1)); }
function setKeys(id, codes) {
  const km = JSON.parse(JSON.stringify(keyMap()));
  km[id] = codes;
  ctl.keys = km; saveControls(); showControls();
}
function setPad(pad, id, binds) {
  const pm = JSON.parse(JSON.stringify(padMap(pad)));
  pm[id] = binds;
  ctl.pads[pad.id] = pm; saveControls(); showControls();
}

// asking for a key or a button
function askOpen(step, q, hint, onSkip) {
  $("askStep").textContent = step; $("askQ").textContent = q; $("askH").textContent = hint;
  $("askSkip").hidden = !onSkip;
  $("askSkip").onclick = onSkip || null;
  if (!$("ask").open) $("ask").showModal();
}
function askClose() { capture = null; if ($("ask").open) $("ask").close(); }
$("askCancel").onclick = () => { if (capture) capture.done(null, null, true); askClose(); };
$("ask").addEventListener("cancel", (e) => { e.preventDefault(); }); // (Escape is for the question)
function askKey(b) {
  askOpen("Keyboard", "Press a key for " + b.name, "Esc cancels");
  capture = { kind: "key", done: (code) => {
    askClose();
    if (!code) return;
    const km = JSON.parse(JSON.stringify(keyMap()));
    // a key moves from whatever had it
    for (const k of Object.keys(km)) km[k] = (km[k] || []).filter((c) => c !== code);
    km[b.id] = [...(km[b.id] || []), code];
    ctl.keys = km; saveControls(); showControls();
  } };
}
// what moved on a pad since `rest` (a fresh press only: everything must be let go first)
function snapshot(p) { return { b: p.buttons.map((x) => x.pressed || x.value > 0.5), a: [...p.axes] }; }
function moved(p, rest) {
  for (let i = 0; i < p.buttons.length; i++) if ((p.buttons[i].pressed || p.buttons[i].value > 0.5) && !rest.b[i]) return { b: i };
  for (let i = 0; i < p.axes.length; i++) {
    const v = p.axes[i], r = rest.a[i];
    if (Math.abs(r) > 1.05 && Math.abs(v) <= 1.05) { const pos = hatPos(v); if (pos >= 0 && pos % 2 === 0) return { a: i, h: pos }; continue; }
    if (Math.abs(r) <= 1.05 && Math.abs(v - r) > 0.6 && Math.abs(v) > 0.5 && Math.abs(v) <= 1.05) return { a: i, d: v > 0 ? 1 : -1 };
  }
  return null;
}
const atRest = (p, rest) => p.buttons.every((x, i) => !(x.pressed || x.value > 0.5) || rest.b[i]) && p.axes.every((v, i) => Math.abs(v - rest.a[i]) < 0.3);
// ask for each button in `list` in turn (the wizard: all of them; else one)
function askPad(pad, list, whole) {
  const id = pad.id;
  const get = () => pads().find((p) => p.id === id);
  let i = 0, rest = null, waitRelease = false;
  const result = JSON.parse(JSON.stringify(padMap(pad)));
  if (whole) for (const b of BUTTONS) result[b.id] = [];
  const used = [];
  const show = () => askOpen(whole ? (i + 1) + " of " + list.length : padName(pad), "Press " + list[i].name + (list[i].note ? " (" + list[i].note + ")" : ""),
    whole ? "Skip a button this controller doesn't have." : "", whole ? () => next(null) : null);
  const finish = (cancelled) => {
    askClose();
    if (cancelled) return;
    ctl.pads[id] = result; saveControls(); showControls();
    if (whole) toast("Controller mapped.");
  };
  const next = (bd) => {
    if (bd) {
      if (whole) result[list[i].id] = [bd];
      else result[list[i].id] = [...(result[list[i].id] || []).filter((x) => JSON.stringify(x) !== JSON.stringify(bd)), bd];
      used.push(JSON.stringify(bd));
    }
    if (++i >= list.length) { finish(false); return; }
    waitRelease = true; show();
  };
  capture = {
    kind: "pad",
    done: (a, b, cancelled) => finish(true),
    poll: () => {
      const p = get();
      if (!p) return;
      if (!rest) { rest = snapshot(p); return; }
      if (waitRelease) { if (atRest(p, rest)) waitRelease = false; return; }
      const bd = moved(p, rest);
      if (!bd) return;
      if (whole && used.includes(JSON.stringify(bd))) { $("askH").textContent = "That one's taken: press another, or skip."; waitRelease = true; return; }
      next(bd);
    },
  };
  show();
}
$("controlsBtn").onclick = () => { showControls(); $("controls").showModal(); };
$("wizardBtn").onclick = () => { const p = pads().find((x) => x.id === padSel); if (p) askPad(p, BUTTONS, true); };
$("resetKeys").onclick = () => { ctl.keys = null; saveControls(); showControls(); };
$("resetPad").onclick = () => { if (padSel) { delete ctl.pads[padSel]; saveControls(); showControls(); } };

/* ================================================================ settings panel */
for (const d of [...DEVICES, { id: "custom", name: "Custom screens…" }]) {
  const o = document.createElement("option");
  o.value = d.id;
  if (d.top) { const [tw, th] = d.top, [bw, bh] = d.bottom; o.textContent = d.name + " · " + (tw === bw && th === bh ? tw + "×" + th + " ×2" : tw + "×" + th + " + " + bw + "×" + bh); }
  else o.textContent = d.name;
  $("device").appendChild(o);
}
const CUSTOM = [["cTopW", "top", 0], ["cTopH", "top", 1], ["cTopIn", "top", 2], ["cBotW", "bottom", 0], ["cBotH", "bottom", 1], ["cBotIn", "bottom", 2]];
function showSettings() {
  $("device").value = S.device;
  $("customDevice").hidden = S.device !== "custom";
  for (const [id, k, i] of CUSTOM) $(id).value = S.custom[k][i];
  $("arrange").value = S.arrange; $("sizes").value = S.sizes; $("gap").value = S.gap;
  $("smooth").value = S.smooth ? "1" : "0"; $("scale").value = String(S.scale);
  $("volume").value = S.volume; $("pauseHidden").checked = !!S.pauseHidden;
  $("touchShow").value = S.touchShow; $("touchPlace").value = S.touchPlace; $("touchSize").value = S.touchSize; $("touchVibrate").checked = !!S.touchVibrate;
  $("exportSave").disabled = !P.rom;
  $("forgetBuild").hidden = !P.build || P.build.bundled;
  $("aboutNote").textContent = "Player " + VERSION + (P.build ? " · GBA DS " + P.build.version : "") +
    (P.caps ? " · sound " + (P.caps.hasAudio ? "yes" : "no") + ", every button " + (P.caps.hasButtons ? "yes" : "no") : "") + " · " + navigator.userAgent.replace(/^Mozilla\/5\.0 /, "");
}
let restartTimer = 0;
const restartSoon = (note) => { clearTimeout(restartTimer); restartTimer = setTimeout(() => { if (P.running) restart(true, note); else { P.win = null; relayout(); } }, 500); };
$("device").onchange = () => { S.device = $("device").value; saveSettings(); showSettings(); P.win = null; restartSoon("Restarted for the " + device().name + "'s screens."); };
for (const [id, k, i] of CUSTOM) $(id).onchange = () => {
  const v = Number($(id).value);
  if (!(v > 0)) return;
  S.custom = JSON.parse(JSON.stringify(S.custom)); S.custom[k][i] = v; saveSettings();
  if (S.device === "custom") restartSoon("Restarted for the custom screens.");
};
$("arrange").onchange = () => { S.arrange = $("arrange").value; saveSettings(); relayout(); };
$("sizes").onchange = () => { S.sizes = $("sizes").value; saveSettings(); relayout(); };
$("gap").oninput = () => { S.gap = Number($("gap").value); saveSettings(); relayout(); };
$("smooth").onchange = () => { S.smooth = $("smooth").value === "1"; saveSettings(); relayout(); };
$("scale").onchange = () => { S.scale = Number($("scale").value); saveSettings(); restartSoon("Restarted at the new resolution."); };
$("volume").oninput = () => { S.volume = Number($("volume").value); S.muted = false; saveSettings(); showSound(); };
$("touchShow").onchange = () => { S.touchShow = $("touchShow").value; saveSettings(); relayout(); };
$("touchPlace").onchange = () => { S.touchPlace = $("touchPlace").value; saveSettings(); relayout(); };
$("touchSize").oninput = () => { S.touchSize = Number($("touchSize").value); saveSettings(); relayout(); };
$("touchVibrate").onchange = () => { S.touchVibrate = $("touchVibrate").checked; saveSettings(); relayout(); };
$("pauseHidden").onchange = () => { S.pauseHidden = $("pauseHidden").checked; saveSettings(); };
$("settingsBtn").onclick = () => { showSettings(); $("settings").showModal(); };
$("exportSave").onclick = async () => {
  if (!P.rom) return;
  if (P.worker) await request({ t: "sync" });
  const path = "/saves/" + baseName(P.rom.name.replace(/[\/\\]/g, "_")) + ".sav";
  const b = await idb("files", "readonly", (s) => s.get(path)).catch(() => null);
  if (!b) { toast("No battery save for " + P.rom.title + " yet.", true); return; }
  download(baseName(P.rom.name) + ".sav", b);
};
$("forgetBuild").onclick = async () => {
  await idb("kv", "readwrite", (s) => s.delete("build")).catch(() => {});
  P.build = null;
  await loadBundled();
  showSlots(); showSettings();
  toast(P.build ? "Using the build that comes with this player." : "Forgotten: drop a build to play.");
};
$("clearAll").onclick = async () => {
  if (!confirm("Delete every save, state, game and build this player keeps in this browser? This can't be undone.")) return;
  if (P.running) { await stopRun(); showStopped(); }
  try { (await db()).close(); } catch (e) {}
  dbp = null;
  await new Promise((r) => { const q = indexedDB.deleteDatabase("gba-ds-player"); q.onsuccess = q.onerror = q.onblocked = () => r(); });
  for (const k of [SET_KEY, CTL_KEY]) try { localStorage.removeItem(k); } catch (e) {}
  location.reload();
};
$("logBtn").onclick = () => { $("logText").textContent = P.log.join("\n") || "(nothing yet: start a game)"; $("logDlg").showModal(); $("logText").scrollTop = 1e9; };
$("logCopy").onclick = () => navigator.clipboard.writeText(P.log.join("\n")).then(() => toast("Log copied."), () => toast("Couldn't copy.", true));

// for tests and the console: what's loaded and running (read it, don't change it)
window.gbadsPlayer = { P, S, sound: A, capture: CAP, currentMask: () => currentMask() };

/* ================================================================ phones */
// on their side, the bar hides while playing; the pill at the top brings it back
$("barPill").onclick = () => { document.body.classList.toggle("showbar"); };
document.addEventListener("pointerdown", (e) => { if (document.body.classList.contains("showbar") && !e.target.closest(".bar, #barPill, dialog")) document.body.classList.remove("showbar"); });
// iPhone and iPad: Safari clears a page's storage when it isn't opened for a while, unless it's on the Home Screen
(() => {
  const ios = /iPhone|iPad|iPod/.test(navigator.userAgent) || (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);
  let dismissed = false;
  try { dismissed = localStorage.getItem("gbadsPlayer.iosNotice") === "1"; } catch (e) {}
  if (ios && !navigator.standalone && !dismissed) $("iosNotice").hidden = false;
  $("iosNoticeX").onclick = () => { $("iosNotice").hidden = true; try { localStorage.setItem("gbadsPlayer.iosNotice", "1"); } catch (e) {} };
})();
// ?safe=top,right,bottom,left: a phone's safe area, to model one in a desktop browser
(() => {
  const m = location.search.match(/[?&]safe=([\d.]+),([\d.]+),([\d.]+),([\d.]+)/);
  if (!m) return;
  const r = document.documentElement.style;
  r.setProperty("--sat", m[1] + "px"); r.setProperty("--sar", m[2] + "px"); r.setProperty("--sab", m[3] + "px"); r.setProperty("--sal", m[4] + "px");
})();
// offline, and on the Home Screen: the folder version, served over http(s), keeps its files (sw.js)
if ("serviceWorker" in navigator && !document.getElementById("src-worker") && /^https?:$/.test(location.protocol) && !/[?&]nosw\b/.test(location.search)) {
  navigator.serviceWorker.register("sw.js").catch((e) => console.info("no offline copy:", e.message));
}

/* ================================================================ start */
// a build that comes with the player: inline (script#bundled-build, base64) or build/gba-ds-web.zip beside the page
async function loadBundled() {
  const el = document.getElementById("bundled-build");
  try {
    let bytes = null;
    if (el) bytes = Uint8Array.from(atob(el.textContent.trim()), (c) => c.charCodeAt(0));
    else if (/^https?:$/.test(location.protocol)) { const r = await fetch("build/gba-ds-web.zip"); if (r.ok) bytes = new Uint8Array(await r.arrayBuffer()); }
    if (bytes) return takeBuild("bundled", bytes, { bundled: true });
  } catch (e) { console.warn("bundled build:", e); }
  return false;
}
(async () => {
  showSlots(); showSound();
  const saved = await kvGet("build");
  if (!(saved && saved.bytes && await takeBuild(saved.name, saved.bytes, {}))) await loadBundled();
  const recent = (await kvGet("recent")) || [];
  if (recent.length && !P.rom) {
    const b = await idb("roms", "readonly", (s) => s.get(recent[0].name)).catch(() => null);
    if (b) takeRom(recent[0].name, b, false);
  }
  showSlots(); showRecent();
  if (!navigator.storage || !navigator.storage.persist) return;
  navigator.storage.persist().catch(() => {});
})();
})();
