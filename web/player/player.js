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
// what Settings offers: each pair of resolutions once (the first device with it gives its
// inches and id), named by the resolutions, not the devices; Two GBA screens first
const RESOLUTIONS = (() => {
  const out = [], byKey = new Map(), alias = {};
  const key = (d) => d.top.slice(0, 2).join("x") + "+" + d.bottom.slice(0, 2).join("x");
  for (const d of DEVICES) {
    const k = key(d);
    if (byKey.has(k)) { alias[d.id] = byKey.get(k).id; continue; }
    const [tw, th] = d.top, [bw, bh] = d.bottom;
    const name = d.id === "gba" ? "Two GBA screens" : tw === bw && th === bh ? "Two " + tw + "×" + th + " screens" : tw + "×" + th + " and " + bw + "×" + bh;
    const r = { id: d.id, name, hint: d.id === "gba" ? "240×160 at 3×" : "", top: d.top, bottom: d.bottom };
    byKey.set(k, r); out.push(r);
  }
  const area = (r) => r.top[0] * r.top[1] + r.bottom[0] * r.bottom[1];
  out.sort((a, b) => (b.id === "gba") - (a.id === "gba") || area(a) - area(b));
  return { list: out, alias };
})();

/* ================================================================ settings */
const SET_KEY = "gbadsPlayer.settings", CTL_KEY = "gbadsPlayer.controls";
// the wide view (pack.py --wide, published at /wide; docs/WEB-PLAYER.md): the game drawn wider (16:9, 16:10) or
// taller (4:3) than the GBA's screen, as a GBA DS widescreen patch would have it, to see what any game draws
// past its screen's edges (docs/WIDESCREEN.md). Its ratio is its own setting; the rest is the player's.
const WIDE = document.documentElement.dataset.wide === "1", WIDE_KEY = "gbadsPlayer.wide", WIDE_RATIOS = ["16:9", "16:10", "4:3", "off"];
let wideRatio = (() => { try { const v = localStorage.getItem(WIDE_KEY); return WIDE_RATIOS.includes(v) ? v : "16:9"; } catch (e) { return "16:9"; } })();
const DEFAULTS = { device: "gba", custom: { top: [1024, 768, 4.3], bottom: [1024, 768, 4.3] }, arrange: "stack",
  gap: 0, smooth: true, scale: 2 / 3, volume: 80, muted: false, pauseHidden: true,
  touchShow: "auto", touchPlace: "rotate", touchSize: 100, touchVibrate: true, theme: "dark", accent: "purple",
  win2Screen: 1 };
const readJSON = (k) => { try { return JSON.parse(localStorage.getItem(k) || "null"); } catch (e) { return null; } };
const writeJSON = (k, v) => { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) {} };
const S = Object.assign({}, DEFAULTS, readJSON(SET_KEY) || {});
function migrateSettings(s) {
  // (settings v2, Nov 10, 2026: half the device's resolution by default, a quarter of
  // the drawing; settings saved before then had the device's whole, so they move once)
  if ((s.v || 1) < 2) { s.scale = DEFAULTS.scale; s.v = 2; }
  // (v3, Nov 13, 2026: Two GBA screens, no gap, and arrangement and controls following
  // the screen's rotation by default; what was still at the old defaults moves once)
  if (s.v < 3) {
    if (s.device === "rgdsplus") s.device = DEFAULTS.device;
    if (s.arrange === "auto") s.arrange = DEFAULTS.arrange;
    if (s.gap === 4) s.gap = DEFAULTS.gap;
    if (s.touchPlace === "below") s.touchPlace = DEFAULTS.touchPlace;
    s.v = 3;
  }
  // (v4, Nov 17, 2026: Top over bottom by default; "follow rotation" was the default, so it moves once)
  if (s.v < 4) {
    if (s.arrange === "rotate") s.arrange = DEFAULTS.arrange;
    s.v = 4;
  }
  // (v5, Nov 17, 2026: two thirds of the resolution by default, 2x of the game's pixels on Two GBA screens; the old default, half, moves once)
  if (s.v < 5) {
    if (s.scale === 0.5) s.scale = DEFAULTS.scale;
    s.v = 5;
  }
  if (!["rotate", "stack", "side"].includes(s.arrange)) s.arrange = DEFAULTS.arrange;
  if (RESOLUTIONS.alias[s.device]) s.device = RESOLUTIONS.alias[s.device];
  return s;
}
if ((S.v || 1) < 5) { migrateSettings(S); writeJSON(SET_KEY, S); } else migrateSettings(S);
// the theme: the keys' look (the app's Settings > Games > Theme); the page stays dark, as in the app
function applyTheme() {
  const light = S.theme === "light";
  document.documentElement.dataset.theme = light ? "light" : "dark";
  const b = $("themeBtn");
  if (b) { b.title = light ? "Dark theme" : "Light theme"; b.setAttribute("aria-label", b.title); }
  if ($("theme")) $("theme").value = light ? "light" : "dark";
  if (!GBAAccent.ids.includes(S.accent)) S.accent = "purple";
  GBAAccent.apply(S.accent, light);
  if ($("accent")) $("accent").value = S.accent;
}
const saveSettings = () => { writeJSON(SET_KEY, S); folderKeepSettings(); };
const ctl = Object.assign({ keys: null, pads: {} }, readJSON(CTL_KEY) || {});
const keyMap = () => ctl.keys || KEY_DEFAULTS;
const saveControls = () => { writeJSON(CTL_KEY, ctl); folderKeepSettings(); };

/* ================================================================ storage (IndexedDB) */
let dbp = null;
function db() {
  if (dbp) return dbp;
  dbp = new Promise((res, rej) => {
    let r;
    try { r = indexedDB.open("gba-ds-player", 1); } catch (e) { rej(e); return; }
    r.onupgradeneeded = () => { for (const s of ["kv", "roms", "files"]) if (!r.result.objectStoreNames.contains(s)) r.result.createObjectStore(s); };
    r.onsuccess = () => {
      const d = r.result;
      // "Delete everything kept here" in another tab or window: let go, or its delete (and its page) waits for this one
      d.onversionchange = () => { d.close(); if (dbp === p) dbp = null; toast("Everything this player kept was deleted in another window."); };
      res(d);
    };
    r.onerror = () => rej(r.error);
  });
  const p = dbp;
  dbp.catch(() => {});
  return dbp;
}
// at start: storage that doesn't answer (an older copy of the player open elsewhere holding it) mustn't keep the build away
const kvGetSoon = (k, ms) => Promise.race([kvGet(k), new Promise((r) => setTimeout(() => r(STORAGE_BUSY), ms))]);
const STORAGE_BUSY = {};
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
// what the app wrote: battery saves, states and the home folder into the browser; everything into the player's folder
const BROWSER_KEEPS = /^\/(saves|states|home)\//;
// a bug report's .zip, saved by the app into its bug-reports folder: handed to the browser as a download too,
// so it can be kept outside the player's own storage (and sent)
const BUG_ZIP = /^\/gba-ds\/bug-reports\/([^\/]+\.zip)$/, bugSent = new Map();
function downloadBugReports(files) {
  for (const [p, b] of files) {
    const m = BUG_ZIP.exec(p);
    if (!m || bugSent.get(p) === b.length) continue;
    bugSent.set(p, b.length);
    download(m[1], b, "application/zip");
    toast("Bug report saved as " + m[1] + ": the browser is downloading it.");
  }
}
function keepFiles(files, deleted) {
  if (!files.length && !deleted.length) return;
  downloadBugReports(files);
  folderWrite(files, deleted);
  files = files.filter(([p]) => BROWSER_KEEPS.test(p)); deleted = deleted.filter((p) => BROWSER_KEEPS.test(p));
  if (!files.length && !deleted.length) return;
  idb("files", "readwrite", (s) => { for (const [p, b] of files) s.put(b, p); for (const p of deleted) s.delete(p); }).catch(() => {});
}

/* ================================================================ a folder of the player's
   As GBA DS's data folder on a handheld (Linux: data-folder.cfg; Android:
   Settings > Folders): the player's games, battery saves, states (with GBA
   DS's settings), screenshots, cheats, overlays, logos, patches, bug reports
   and the web player's own settings, in a folder on this device that the
   player chooses (File System Access: Chrome and Edge on computers, Chrome
   on Android 132 and later; not Firefox or Safari). The browser keeps its
   copy as before; with the folder, the folder is what a run starts from. */
const canFolder = typeof window.showDirectoryPicker === "function";
// the app's paths -> the folder's (as on a handheld; the app's own folder, /gba-ds, has the data folders)
const FOLDER_MAP = [["/roms/", "roms/"], ["/saves/", "saves/"], ["/states/", "states/"], ["/shots/", "screenshots/"],
  ["/gba-ds/cheats/", "cheats/"], ["/gba-ds/overlays/", "overlays/"], ["/gba-ds/logos/", "logos/"], ["/gba-ds/patches/", "patches/"], ["/gba-ds/bug-reports/", "bug-reports/"]];
const FOLDER_SETTINGS = "web-player-settings.json";
const F = { handle: null, ok: false, queue: Promise.resolve(), lost: false, settingsTimer: 0 };
const toFolder = (p) => { for (const [a, b] of FOLDER_MAP) if (p.startsWith(a)) return b + p.slice(a.length); return null; };
// read and write permission for the folder (asking needs a click or a key: Play, Reconnect)
async function folderAccess(ask) {
  if (!F.handle) return false;
  try {
    let s = await F.handle.queryPermission({ mode: "readwrite" });
    if (s !== "granted" && ask) s = await F.handle.requestPermission({ mode: "readwrite" });
    F.ok = s === "granted";
  } catch (e) { F.ok = false; }
  if (F.ok) F.lost = false;
  showFolder();
  return F.ok;
}
async function folderDir(rel, create) {
  let d = F.handle;
  for (const part of rel.split("/").slice(0, -1)) if (part) d = await d.getDirectoryHandle(part, { create });
  return d;
}
function folderFailed(e) {
  console.warn("folder:", e);
  if (F.lost || !e || (e.name !== "NotAllowedError" && e.name !== "SecurityError")) return;
  F.lost = true; F.ok = false;
  toast("GBA DS lost access to your folder: reconnect it in Settings (this browser still keeps your saves).", true);
  showFolder();
}
// files from the app ([path, bytes]) into the folder, and files it deleted out of it, in order
function folderWrite(files, deleted) {
  if (!F.ok) return F.queue;
  const put = files.map(([p, b]) => [toFolder(p), b]).filter((x) => x[0]);
  const del = (deleted || []).map(toFolder).filter(Boolean);
  if (!put.length && !del.length) return F.queue;
  F.queue = F.queue.then(async () => {
    for (const [rel, b] of put) {
      try {
        const fh = await (await folderDir(rel, true)).getFileHandle(rel.split("/").pop(), { create: true });
        const w = await fh.createWritable();
        await w.write(b); await w.close();
      } catch (e) { folderFailed(e); if (F.lost) return; }
    }
    for (const rel of del) { try { await (await folderDir(rel, false)).removeEntry(rel.split("/").pop()); } catch (e) {} }
  });
  return F.queue;
}
const isRomName = (n) => /\.(gba|agb|bin|zip|7z)$/i.test(n);
// what the folder has, as the app's paths, [path, bytes, modified] (games only when asked, and as
// files GBA DS reads from the folder when it opens them: never all in memory; never screenshots
// or bug reports: the app only writes those). The real dates keep GBA DS's roms.cache good between runs.
async function folderFiles(withRoms) {
  const out = [];
  async function walk(d, vp) {
    for await (const [name, h] of d.entries()) {
      if (name.startsWith(".")) continue;
      if (h.kind === "directory") { await walk(h, vp + name + "/"); continue; }
      if (vp === "/roms/" && !isRomName(name)) continue;
      const f = await h.getFile();
      out.push([vp + name, vp === "/roms/" ? f : new Uint8Array(await f.arrayBuffer()), f.lastModified]);
    }
  }
  for (const [vp, rel] of FOLDER_MAP) {
    if ((vp === "/roms/" && !withRoms) || vp === "/shots/" || vp === "/gba-ds/bug-reports/") continue;
    let d;
    try { d = await F.handle.getDirectoryHandle(rel.slice(0, -1)); } catch (e) { continue; }
    try { await walk(d, vp); } catch (e) { folderFailed(e); }
  }
  return out;
}
async function folderHas(rel) {
  try { await (await folderDir(rel, false)).getFileHandle(rel.split("/").pop()); return true; } catch (e) { return false; }
}
// the web player's own settings (screens, layout, controls) beside GBA DS's
function folderKeepSettings() {
  if (!F.ok) return;
  clearTimeout(F.settingsTimer);
  F.settingsTimer = setTimeout(() => {
    const b = new TextEncoder().encode(JSON.stringify({ settings: S, controls: ctl }, null, 1));
    F.queue = F.queue.then(async () => {
      try { const w = await (await F.handle.getFileHandle(FOLDER_SETTINGS, { create: true })).createWritable(); await w.write(b); await w.close(); }
      catch (e) { folderFailed(e); }
    });
  }, 300);
}
async function folderTakeSettings() {
  try {
    const j = JSON.parse(await (await (await F.handle.getFileHandle(FOLDER_SETTINGS)).getFile()).text());
    if (j.settings) { Object.assign(S, j.settings); migrateSettings(S); writeJSON(SET_KEY, S); applyTheme(); }
    if (j.controls) { Object.assign(ctl, j.controls); writeJSON(CTL_KEY, ctl); }
    return true;
  } catch (e) { return false; }
}
async function chooseFolder() {
  let h;
  try { h = await window.showDirectoryPicker({ id: "gba-ds", mode: "readwrite", startIn: "documents" }); }
  catch (e) { if (e.name !== "AbortError") toast("No folder: " + e.message, true); return; }
  if (P.worker) await request({ t: "sync" });
  F.handle = h; F.ok = true; F.lost = false;
  await kvSet("folder", h);
  // a folder used before (here or in another browser) brings its settings; else ours go in
  const had = await folderTakeSettings();
  if (!had) folderKeepSettings();
  // what this browser kept goes in too, where the folder hasn't its own (one file at a
  // time, each written before the next is read: never ten games in memory at once)
  let copied = 0;
  for (const [p, b] of await allFiles()) { const rel = toFolder(p); if (rel && !(await folderHas(rel))) { await folderWrite([[p, b]]); copied++; } }
  for (const r of (await kvGet("recent")) || []) {
    const rel = "roms/" + r.name.replace(/[\/\\]/g, "_");
    if (await folderHas(rel)) continue;
    const b = await idb("roms", "readonly", (s) => s.get(r.name)).catch(() => null);
    if (b) { await folderWrite([["/" + rel, b]]); copied++; }
  }
  await F.queue;
  showFolder(); showSettings(); showRecent();
  toast("GBA DS keeps its files in “" + h.name + "” from now on" + (copied ? " (" + copied + " copied from this browser)." : "."));
  if (P.running) restart(true, "Restarted to use your folder.");
  else if (had) { P.win = null; relayout(); }
}
async function forgetFolder() {
  await F.queue;
  await idb("kv", "readwrite", (s) => s.delete("folder")).catch(() => {});
  const name = F.handle ? F.handle.name : "";
  Object.assign(F, { handle: null, ok: false, lost: false });
  showFolder(); showRecent();
  toast("The folder" + (name ? " “" + name + "”" : "") + " keeps its files; this browser's copy is used from now on.");
}
function showFolder() {
  const n = $("folderNote");
  if (!n) return;
  const name = F.handle ? "“" + F.handle.name + "”" : "";
  $("folderPick").hidden = !canFolder;
  $("folderPick").textContent = F.handle ? "Choose another folder…" : "Choose a folder…";
  $("folderReconnect").hidden = !F.handle || F.ok;
  $("folderForget").hidden = !F.handle;
  n.textContent = !canFolder
    ? "Your games, saves and states are kept in this browser. In Chrome or Edge (Chrome on Android too) you can keep them in a folder on this device instead."
    : !F.handle
    ? "Keep your games, saves, states, settings, screenshots, cheats, overlays, logos and patches in a folder on this device, as GBA DS does on a handheld. They stay if this browser's data is cleared, and you can add your own overlays, cheats and patches there."
    : F.ok
    ? "Kept in your folder " + name + ": roms, saves, states (with GBA DS's settings), screenshots, cheats, overlays, logos, patches and bug-reports, and this player's settings."
    : "Your folder " + name + " needs your permission again: press Reconnect, or Play asks for it.";
  $("keptNote").textContent = F.handle
    ? "This browser keeps a copy of your battery saves, states and GBA DS's settings too."
    : "Battery saves, states and GBA DS's own settings are kept here between visits.";
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
// the second window (issue #93; "the second window" below)
const W2 = { win: null, canvas: null, rect: null, poll: 0 };
// the keyboard's listeners, kept so the second window gets the same ones
const KEY_LISTENERS = { keydown: [], keyup: [] };
const onKey = (ev, l) => { KEY_LISTENERS[ev].push(l); window.addEventListener(ev, l, true); };

// what a build's web version can do, from what it exports
function buildCaps(module) {
  const names = new Set(WebAssembly.Module.exports(module).map((e) => e.name));
  return { hasAudio: names.has("web_audio_pull"), hasButtons: names.has("web_buttons"), hasCaptureWorker: names.has("web_capture_job"), hasClimbWorker: names.has("web_climb_job") };
}
const NO_SOUND = "No sound: this build's web version was made without it.";
const NO_BUTTONS = "Keyboard only: X and Y can't be pressed with this build.";
const NO_CAPTURE_WORKER = "This build updates the second screen inside the game, so the game pauses each time it does (builds from GBA DS 1.5 on update it alongside).";
function warnCaps(b) {
  if (!b.caps.hasAudio) toast(NO_SOUND, true);
  else if (!b.caps.hasButtons) toast(NO_BUTTONS, true);
  else if (!b.caps.hasCaptureWorker) toast(NO_CAPTURE_WORKER, true);
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
function takeRom(name, bytes, remember, mtime) {
  P.rom = { name, bytes, mtime: mtime || 0, code: romCode(bytes), title: romTitle(bytes) || baseName(name) };
  if (remember !== false) rememberRom(P.rom);
  showSlots();
}
async function rememberRom(r) {
  try {
    await idb("roms", "readwrite", (s) => s.put(r.bytes, r.name));
    folderWrite([["/roms/" + r.name.replace(/[\/\\]/g, "_"), r.bytes]]);
    const list = ((await kvGet("recent")) || []).filter((x) => x.name !== r.name);
    list.unshift({ name: r.name, code: r.code, title: r.title, t: Date.now(), mtime: r.mtime || 0 });
    for (const old of list.splice(10)) idb("roms", "readwrite", (s) => s.delete(old.name)).catch(() => {});
    await kvSet("recent", list);
    showRecent();
  } catch (e) {}
}

// what dropped files are, in an order that lets each make sense: builds, games, saves, states
async function takeFiles(list) {
  const items = [];
  for (const f of list) items.push({ name: f.name, bytes: new Uint8Array(await f.arrayBuffer()), mtime: f.lastModified || 0 });
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
        if (romE) { const b = await romE.read(); if (looksLikeRom(b)) { takeRom(romE.name.split("/").pop(), b, true, it.mtime); newGame = true; continue; } }
        toast(it.name + ": no GBA DS build, game or bug report inside.", true);
      } else if (ext === "wasm") {
        if (await takeBareWasm(it.name, it.bytes)) { newBuild = true; toast("Web build loaded (no settings file with it: GBA DS's defaults)."); warnCaps(P.build); }
      } else if (["gba", "agb", "bin"].includes(ext) || looksLikeRom(it.bytes)) {
        takeRom(it.name, it.bytes, true, it.mtime); newGame = true;
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
  if (S.device === "custom") return { id: "custom", name: "Custom screens", top: S.custom.top, bottom: S.custom.bottom };
  return RESOLUTIONS.list.find((d) => d.id === S.device) || RESOLUTIONS.list[0];
}
// which way the screen is turned: the device's reported orientation, else its
// rotation angle, else its reported resolution (?rotation=portrait|landscape for tests)
function rotation() {
  const q = location.search.match(/[?&]rotation=(portrait|landscape)\b/);
  if (q) return q[1];
  const o = screen.orientation && screen.orientation.type;
  if (o) return o.startsWith("portrait") ? "portrait" : "landscape";
  if (typeof window.orientation === "number") return Math.abs(window.orientation) === 90 ? "landscape" : "portrait";
  return screen.width < screen.height ? "portrait" : "landscape";
}
// "Follow screen rotation": upright, the screens stacked and the controls below; on its side, side by side, the controls beside
const arrangement = () => S.arrange === "rotate" ? (rotation() === "portrait" ? "stack" : "side") : S.arrange;
const touchPlacement = () => S.touchPlace === "rotate" ? (rotation() === "portrait" ? "below" : "beside") : S.touchPlace;
if (screen.orientation && screen.orientation.addEventListener) screen.orientation.addEventListener("change", () => relayout());
window.addEventListener("orientationchange", () => setTimeout(relayout, 50));
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
// GBA DS's settings file for the web: the build's, then the player's own defaults, which
// come after it so they win; the pause menu's choices (states/prefs.cfg) still win over both.
// Full Width: the pictures fill the screens' width at any resolution and detail (Pixel
// Perfect's whole-number scale leaves a border round them, a wide one at half the detail).
const WEB_DEFAULTS = "\n# the web player's defaults\nscale = fit\nmap_scale = fit\n";
function webConfig() {
  const own = P.build.files.find(([rel]) => rel === "gba-ds.cfg");
  const add = new TextEncoder().encode(WEB_DEFAULTS);
  if (!own) return add;
  const out = new Uint8Array(own[1].length + add.length);
  out.set(own[1]); out.set(add, own[1].length);
  return out;
}
// where the two screens go on the page (CSS px), for the arrangement chosen, by their pixels (a screen with more pixels is drawn larger)
function placeScreens(win, W, H) {
  const [a, b] = win.screens;
  const dims = (s) => [s.w, s.h]; // by pixels: a screen with more pixels is drawn larger
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
  return try_(arrangement() === "side").r;
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
// one screen, as large as fits (the other is in the second window)
function placeOne(s, W, H, pad) {
  const k = Math.min((W - pad * 2) / s.w, (H - pad * 2) / s.h);
  return { x: (W - s.w * k) / 2, y: (H - s.h * k) / 2, w: s.w * k, h: s.h * k };
}
function relayout() {
  const stage = $("stage"), W = stage.clientWidth, H = stage.clientHeight;
  if (!W || !H) return;
  const win = P.win || appWindow();
  const box = GBATouch.place(W, H, { show: touchShown(), placement: touchPlacement(), size: S.touchSize / 100, vibrate: !!S.touchVibrate, safe: safeArea() });
  const out = w2Open() ? w2Screen() : -1;  // the screen in the second window, if it's open
  let rects;
  if (out < 0) rects = placeScreens(win, box.w, box.h);
  else { rects = [null, null]; rects[1 - out] = placeOne(win.screens[1 - out], box.w, box.h, Math.min(box.w, box.h) < 500 ? 8 : 24); }
  rects = rects.map((r) => r && { x: r.x + box.x, y: r.y + box.y, w: r.w, h: r.h });
  P.rects = rects;
  const dpr = window.devicePixelRatio || 1;
  const bz = $("bezels");
  bz.textContent = "";
  if (P.running || P.booting) for (const r of rects) {
    if (!r) continue;
    const d = document.createElement("div");
    d.className = "bezel";
    Object.assign(d.style, { left: r.x + "px", top: r.y + "px", width: r.w + "px", height: r.h + "px" });
    bz.appendChild(d);
  }
  const layout2 = out < 0 ? null : w2Layout(win, out);
  showWin2Btn();
  if (!P.worker) return;
  P.worker.postMessage({ t: "layout", layout: glLayout(win, rects, W, H, dpr), layout2 });
}
// a layout for the worker: the canvas's size and where each screen of the app's window goes on it (device pixels)
function glLayout(win, rects, W, H, dpr) {
  const R = (v) => Math.round(v * dpr);
  const out = [];
  win.screens.forEach((s, i) => {
    const r = rects[i];
    if (r) out.push({ src: [s.x, s.y, s.w, s.h], dst: [R(r.x), R(r.y), R(r.x + r.w) - R(r.x), R(r.y + r.h) - R(r.y)] });
  });
  return { cw: R(W), ch: R(H), smooth: !!S.smooth, rects: out };
}
new ResizeObserver(() => relayout()).observe($("stage"));
matchMedia("(resolution: 1dppx)").addEventListener?.("change", relayout);

/* ================================================================ the second window (issue #93)
   One of the two screens in a window of its own, to put on another monitor:
   the bottom one (the touchscreen) by default, or the top one (Settings >
   Screens). The page then shows only the other screen, as large as it fits.
   The worker draws into the window's canvas with a second WebGL2 context, so
   opening or closing it doesn't restart the game. Keys, controllers and
   touches work from either window; double-clicking either makes it full
   screen. Where the browser can place windows (Window Management: Chrome and
   Edge on computers) and there's more than one monitor, the window opens on
   another one. The window is an about:blank page this one writes into, so it
   needs no file of its own (the one-file player has none to give). */
const w2Open = () => !!W2.win && !W2.win.closed;
const w2Screen = () => (S.win2Screen === 0 ? 0 : 1);
// computers only: a phone or tablet has one screen and no windows to move
const w2Possible = () => typeof window.open === "function" && !coarse.matches && typeof OffscreenCanvas !== "undefined";
function showWin2Btn() {
  const b = $("win2Btn");
  if (!b) return;
  b.hidden = !w2Possible();
  const on = w2Open();
  b.classList.toggle("on", on);
  b.setAttribute("aria-pressed", on ? "true" : "false");
  b.title = on ? "Bring the " + (w2Screen() ? "bottom" : "top") + " screen back to this window"
    : "Open the " + (w2Screen() ? "bottom" : "top") + " screen in a second window (for another monitor)";
}
function w2Title() {
  if (!w2Open()) return;
  W2.win.document.title = "GBA DS: " + (w2Screen() ? "bottom" : "top") + " screen";
  // (double-click for full screen only on the top screen: on the bottom one, the clicks are touches)
  W2.win.gbadsDblclick = w2Screen() === 0;
}
// Full screen in the second window: a button (shown while the mouse moves) and, on the top
// screen, a double-click. This is a script of the window's own: Firefox lets a page go full
// screen only from its own code answering its own click, and refused it from a listener of
// this page's attached to that window (the owner's test, Firefox on macOS, Nov 25, 2026).
const W2_FULL_SCREEN = `(() => {
  const d = document, el = d.documentElement;
  const fsEl = () => d.fullscreenElement || d.webkitFullscreenElement;
  const toggle = () => {
    if (fsEl()) (d.exitFullscreen || d.webkitExitFullscreen).call(d);
    else { const r = (el.requestFullscreen || el.webkitRequestFullscreen).call(el); if (r && r.catch) r.catch(() => {}); }
  };
  const b = d.createElement("button");
  b.id = "fs"; b.type = "button"; b.className = "away";
  const label = () => { b.textContent = fsEl() ? "Exit full screen" : "Full screen"; };
  label();
  b.addEventListener("click", (e) => { e.stopPropagation(); toggle(); });
  d.body.appendChild(b);
  let t = 0;
  const show = () => { b.classList.remove("away"); clearTimeout(t); t = setTimeout(() => b.classList.add("away"), 2500); };
  d.addEventListener("mousemove", show);
  d.addEventListener("fullscreenchange", label);
  d.addEventListener("webkitfullscreenchange", label);
  d.addEventListener("dblclick", (e) => { e.preventDefault(); if (window.gbadsDblclick) toggle(); });
  show();
})();`;
function w2FullScreen(nw) {
  const sc = nw.document.createElement("script");
  sc.textContent = W2_FULL_SCREEN;
  nw.document.body.appendChild(sc);
}
// the other monitor's free area, when the browser may tell us (asks once, if there is one)
async function otherMonitor(ask) {
  if (!window.getScreenDetails || !(screen.isExtended === true)) return null;
  try {
    if (!ask) {
      const st = await navigator.permissions.query({ name: "window-management" });
      if (st.state !== "granted") return null;
    }
    const d = await window.getScreenDetails();
    const cur = d.currentScreen;
    const other = d.screens.find((x) => x !== cur && !(x.left === cur.left && x.top === cur.top));
    return other ? { left: other.availLeft, top: other.availTop, width: other.availWidth, height: other.availHeight } : null;
  } catch (e) { return null; }
}
// the window's size for a screen: its shape, about 60 % of the monitor's height
function w2Size(s, mon) {
  const H = Math.round((mon ? mon.height : screen.availHeight) * 0.6), Wmax = Math.round((mon ? mon.width : screen.availWidth) * 0.8);
  let h = H, w = Math.round(H * s.w / s.h);
  if (w > Wmax) { w = Wmax; h = Math.round(w * s.h / s.w); }
  return { w, h };
}
async function openWin2() {
  if (w2Open()) { w2Close(); return; }
  const win = P.win || appWindow(), s = win.screens[w2Screen()];
  // (asked before the window opens only when the browser already may: a prompt would use up the click)
  const mon = await otherMonitor(false);
  const { w, h } = w2Size(s, mon);
  let feat = "popup,width=" + w + ",height=" + h;
  if (mon) feat += ",left=" + Math.round(mon.left + (mon.width - w) / 2) + ",top=" + Math.round(mon.top + (mon.height - h) / 2);
  const nw = window.open("", "gbadsSecondScreen", feat);
  if (!nw) { toast("The browser blocked the second window: allow pop-ups for this page, then try again.", true); return; }
  w2Setup(nw);
  if (!mon && window.getScreenDetails && screen.isExtended === true) {
    // more than one monitor, and the browser can place windows: ask, then move it over
    const m = await otherMonitor(true);
    if (m && w2Open()) {
      const z = w2Size(s, m);
      try { nw.moveTo(Math.round(m.left + (m.width - z.w) / 2), Math.round(m.top + (m.height - z.h) / 2)); nw.resizeTo(z.w, z.h); } catch (e) {}
    }
  }
}
function w2Setup(nw) {
  W2.win = nw;
  const d = nw.document;
  // (built with the DOM: about:blank has its head and body already; a window kept from before is emptied)
  d.head.textContent = ""; d.body.textContent = "";
  const meta = d.createElement("meta");
  meta.name = "viewport"; meta.content = "width=device-width,initial-scale=1";
  d.head.appendChild(meta);
  const st = d.createElement("style");
  st.textContent = "[hidden]{display:none!important}html,body{margin:0;height:100%;background:#000;overflow:hidden;font:14px/1.4 system-ui,sans-serif;color:#cfc8e6}" +
    "canvas{position:fixed;inset:0;width:100%;height:100%;display:block;touch-action:none}canvas.touch{cursor:crosshair}" +
    "#hint{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);padding:8px 14px;border-radius:10px;background:rgba(20,16,40,.88);transition:opacity .6s;pointer-events:none;text-align:center}" +
    "#idle{position:fixed;inset:0;display:grid;place-items:center;pointer-events:none;opacity:.6}" +
    "#fs{position:fixed;top:12px;right:12px;z-index:2;padding:7px 14px;border:1px solid rgba(255,255,255,.25);border-radius:10px;background:rgba(20,16,40,.88);color:#cfc8e6;font:inherit;cursor:pointer;transition:opacity .4s}" +
    "#fs.away{opacity:0;pointer-events:none}";
  d.head.appendChild(st);
  const hint = d.createElement("div");
  hint.id = "hint";
  hint.textContent = "Move the mouse for the Full screen button. Close this window to bring the screen back.";
  d.body.appendChild(hint);
  nw.setTimeout(() => { hint.style.opacity = "0"; }, 5000);
  const idle = d.createElement("div");
  idle.id = "idle";
  idle.textContent = "The " + (w2Screen() ? "bottom" : "top") + " screen shows here while a game is playing.";
  d.body.appendChild(idle);
  w2Title();
  // keys and the window's own goings-on, as on the page
  for (const ev of ["keydown", "keyup"]) for (const l of KEY_LISTENERS[ev]) nw.addEventListener(ev, l, true);
  nw.addEventListener("blur", () => keysDown.clear());
  nw.addEventListener("resize", () => relayout());
  d.addEventListener("visibilitychange", visibility);
  w2FullScreen(nw);
  nw.addEventListener("wheel", (e) => { if (e.ctrlKey || e.metaKey) noZoom(e); }, { passive: false });
  nw.addEventListener("contextmenu", (e) => e.preventDefault());
  nw.addEventListener("gamepadconnected", () => {});  // (some browsers give a window controllers only once it listens)
  nw.addEventListener("pagehide", () => { if (W2.win === nw) w2Gone(); });
  const loop = () => { if (W2.win !== nw || nw.closed) return; tickOnce(); nw.requestAnimationFrame(loop); };
  nw.requestAnimationFrame(loop);
  clearInterval(W2.poll);
  W2.poll = setInterval(() => { if (W2.win && W2.win.closed) w2Gone(); }, 500);
  // its canvas: handed to the worker now when a game is playing, else at the next start
  const c = w2Canvas();
  if (P.worker && P.running) {
    const oc = c.transferControlToOffscreen();
    const win = P.win || appWindow();
    P.worker.postMessage({ t: "view2", canvas: oc, layout: w2Layout(win, w2Screen()) }, [oc]);
  }
  relayout();
  visibility();
}
// a new canvas in the second window (a canvas hands its drawing to one worker only)
function w2Canvas() {
  const d = W2.win.document, c = d.createElement("canvas");
  if (W2.canvas && W2.canvas.parentNode) W2.canvas.replaceWith(c); else d.body.prepend(c);
  W2.canvas = c;
  bindTouch(c, () => w2Screen() === 1 ? W2.rect : null);
  const idle = d.getElementById("idle");
  if (idle) idle.hidden = !!(P.running || P.booting);
  return c;
}
// the screen in the second window: as large as fits, centred, the rest black
function w2Layout(win, i) {
  const nw = W2.win, W = nw.innerWidth, H = nw.innerHeight;
  if (!W || !H) return null;
  const r = placeOne(win.screens[i], W, H, 0);
  W2.rect = r;
  const idle = nw.document.getElementById("idle");
  if (idle) idle.hidden = !!(P.running || P.booting);
  const rects = [null, null];
  rects[i] = r;
  return glLayout(win, rects, W, H, nw.devicePixelRatio || 1);
}
function w2Close() {
  const nw = W2.win;
  if (!nw) return;
  w2Gone();
  try { if (!nw.closed) nw.close(); } catch (e) {}
}
// the second window closed: its screen comes back to the page
function w2Gone() {
  if (!W2.win) return;
  W2.win = null; W2.canvas = null; W2.rect = null;
  clearInterval(W2.poll);
  keysDown.clear();
  if (P.worker) P.worker.postMessage({ t: "view2" });
  relayout();
  visibility();
}
coarse.addEventListener?.("change", showWin2Btn);

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
  stopClimb();
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
  bindTouch(c, () => P.rects && P.rects[1]);
  return c;
}
async function boot(opts) {
  opts = opts || {};
  if (!P.build) { toast("Now drop a GBA DS build (.zip) with a web build inside.", true); showSlots(); return; }
  if (!P.rom && !opts.library) { toast("Now drop a GBA game (.gba).", true); showSlots(); return; }
  if (P.booting) return;
  P.booting = true;
  // (asked first, while the click that started this still counts)
  const useFolder = !!F.handle && await folderAccess(true);
  if (F.handle && !useFolder) toast("Your folder “" + F.handle.name + "” wasn't allowed: playing from this browser's copy. Reconnect it in Settings.", true);
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
  for (const [rel, b] of P.build.files) if (rel !== "gba-ds.cfg") files.push(["/gba-ds/" + rel, b]);
  files.push(["/gba-ds/gba-ds.cfg", webConfig()]);
  // the folder's files when there is one (the browser's home folder still), else the browser's
  for (const [p, b] of await allFiles()) if (!useFolder || p.startsWith("/home/")) files.push([p, b]);
  if (useFolder) { try { for (const f of await folderFiles(!!opts.library)) files.push(f); } catch (e) { folderFailed(e); } }
  const args = ["/gba-ds/gba-ds", "--asset-dir=/gba-ds", "--state-dir=/states", "--save-dir=/saves", "--screenshot-dir=/shots", "--rom-dir=/roms", ...win.args];
  if (opts.library) {
    const recent = (await kvGet("recent")) || [];
    const have = new Set(files.map(([p]) => p));
    for (const r of recent) {
      const p = "/roms/" + r.name.replace(/[\/\\]/g, "_");
      if (have.has(p)) continue;
      const b = await idb("roms", "readonly", (s) => s.get(r.name)).catch(() => null);
      if (b) files.push([p, b, r.mtime || 0]);
    }
  } else {
    const romName = P.rom.name.replace(/[\/\\]/g, "_");
    files.push(["/roms/" + romName, P.rom.bytes, P.rom.mtime]);
    if (P.save) { files.push(["/saves/" + baseName(romName) + ".sav", P.save.bytes]); P.save = null; }
    if (state) { files.push(["/load.ss", state]); args.push("--load-state=/load.ss"); }
    args.push("/roms/" + romName);
  }
  const debug = /[?&]debug\b/.test(location.search);
  const env = { GBADS_CONFIG: "/gba-ds/gba-ds.cfg", GBADS_WINDOW: win.w + "x" + win.h, HOME: "/home", ...win.env };
  if (debug) env.GBADS_DEBUG = "1";
  if (WIDE && wideRatio !== "off") env.GBADS_WIDE = wideRatio; /* (the wide view: from the game's first frame) */
  // map captures in a worker of their own, from the start (?nocapw: in the game, to compare)
  const capWorker = !!P.build.caps.hasCaptureWorker && !/[?&]nocapw\b/.test(location.search);
  if (capWorker) env.GBADS_CAPTURE_OFFLOAD = "1";
  // DK: King of Swing's level above in a worker of its own where there's a second core (?noclimbw: in step in the game, to compare)
  const climbWorker = !!P.build.caps.hasClimbWorker && (navigator.hardwareConcurrency || 2) > 1 && !/[?&]noclimbw\b/.test(location.search);
  if (climbWorker) env.GBADS_CLIMB_OFFLOAD = "1";
  else env.GBADS_CLIMB_INSTEP = "1";
  P.capFiles = files.filter(([p]) => p.startsWith("/gba-ds/") || p.startsWith("/roms/"));
  P.capEnv = { GBADS_CONFIG: env.GBADS_CONFIG, HOME: "/home", ...(debug ? { GBADS_DEBUG: "1" } : {}) };
  P.log.length = 0;
  let worker;
  try { worker = new Worker(sourceURL("worker", ["runtime"])); }
  catch (e) { P.booting = false; toast("The player's worker didn't start: " + e.message, true); return; }
  P.worker = worker;
  P.caps = null;
  worker.onmessage = (e) => onWorker(worker, e.data);
  worker.onerror = (e) => { if (P.worker === worker) { const why = workerError(e); addLog("worker error: " + why + " (" + navigator.userAgent + ")"); toast("GBA DS stopped: " + why, true); endRun(worker); } };
  const canvas = freshCanvas().transferControlToOffscreen();
  const c2 = w2Open() ? w2Canvas() : null, canvas2 = c2 ? c2.transferControlToOffscreen() : null;
  document.body.classList.add("playing");
  $("start").hidden = true;
  P.running = true; P.paused = false; P.autoPaused = false; P.library = !!opts.library;
  showPaused();
  showNow(opts.library);
  relayout();
  const stage = $("stage"), dpr = window.devicePixelRatio || 1;
  worker.postMessage({ t: "boot", wasm: P.build.module, files, args, env, canvas, captureWorker: capWorker, climbWorker, debug, canvas2,
    layout: { cw: Math.round(stage.clientWidth * dpr), ch: Math.round(stage.clientHeight * dpr), smooth: !!S.smooth, rects: [] } }, canvas2 ? [canvas, canvas2] : [canvas]);
  relayout();
  if (A.node) linkSound();
  P.booting = false;
  lastMask = -1;
}
// a worker's error event: some browsers give no message (its script didn't load, or the error wasn't an Error)
function workerError(e) {
  if (e && e.message) return e.message + (e.filename ? " (" + e.filename.replace(/^.*\//, "") + ":" + e.lineno + ")" : "");
  if (e && e.error) return String(e.error.stack || e.error);
  return "the browser gave no reason (its worker may not have been allowed to start" + (location.protocol === "file:" ? " from a file on disk: try the player on its website" : "") + ")";
}
/* ---- DK's level above in a worker of its own (a second copy of the app, the captures' way) */
const CLB = { w: null, key: "", busy: false, next: null, dead: false };
function stopClimb() {
  if (CLB.w) CLB.w.terminate();
  Object.assign(CLB, { w: null, key: "", busy: false, next: null, dead: false });
}
function climbJob(m) {
  if (CLB.dead) return;
  const key = m.rom + "|" + m.code;
  if (!CLB.w || CLB.key !== key) {
    if (CLB.w) CLB.w.terminate();
    let w;
    try { w = new Worker(sourceURL("worker", ["runtime"])); }
    catch (e) { climbFailed("the climb worker didn't start: " + e.message); return; }
    Object.assign(CLB, { w, key, busy: false, next: null });
    w.onmessage = (e) => onClimb(w, e.data);
    w.onerror = (e) => { if (CLB.w === w) climbFailed(workerError(e)); };
    const env = Object.assign({}, P.capEnv, { GBADS_CLIMB_SERVE: m.code });
    const mine = P.capFiles.some(([p]) => p === m.rom);
    const files = P.capFiles.filter(([p]) => !p.startsWith("/roms/") || !mine || p === m.rom);
    w.postMessage({ t: "bootClimb", wasm: P.build.module, files, args: ["/gba-ds/gba-ds", "--asset-dir=/gba-ds", m.rom], env });
  }
  // (one at a time: the game asks for a newer frame only when it has the last one's picture)
  if (CLB.busy) { CLB.next = m; return; }
  CLB.busy = true;
  CLB.w.postMessage(m, [m.state, m.oam]);
}
function onClimb(w, m) {
  if (w !== CLB.w) return;
  switch (m.t) {
    case "log": addLog("[climb] " + m.line); break;
    case "climbresult":
      CLB.results = (CLB.results || 0) + 1;
      if (P.worker) P.worker.postMessage(m, m.state ? [m.buf, m.state] : [m.buf]);
      CLB.busy = false;
      if (CLB.next) { const n = CLB.next; CLB.next = null; climbJob(n); }
      break;
    case "exit": climbFailed(m.error || "it closed (" + m.code + ")"); break;
  }
}
// the copy is lost: the game goes on one screen with the logo
function climbFailed(why) {
  addLog("climb worker stopped: " + why + "; the level above is gone from here on");
  if (CLB.w) CLB.w.terminate();
  Object.assign(CLB, { w: null, busy: false, next: null, dead: true });
  if (P.worker) P.worker.postMessage({ t: "climboff" });
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
    w.onerror = (e) => { if (CAP.w === w) captureFailed(workerError(e)); };
    const env = Object.assign({}, P.capEnv, { GBADS_CAPTURE_SERVE: m.code, GBADS_CAPTURE_TIGHT: m.tight ? "1" : "0" });
    const mine = P.capFiles.some(([p]) => p === m.rom);
    const files = P.capFiles.filter(([p]) => !p.startsWith("/roms/") || !mine || p === m.rom);
    w.postMessage({ t: "bootCapture", wasm: P.build.module, files, args: ["/gba-ds/gba-ds", "--asset-dir=/gba-ds", m.rom], env });
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
    case "climbjob": CLB.jobs = (CLB.jobs || 0) + 1; climbJob(m); break;
    // (the pictures' buffer, back to the climb worker to be used again)
    case "climbreturn": if (CLB.w) CLB.w.postMessage(m, [m.buf]); break;
    // (the pictures' buffer, back to the capture worker to be used again)
    case "capreturn": if (CAP.w) CAP.w.postMessage(m, [m.buf]); break;
    case "log": addLog(m.line); break;
    case "wide": if (!m.ok) toast("This GBA DS build has no wide view: it needs v1.6.5 or later.", true); break;
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
  stopClimb();
  setTimeout(() => worker.terminate(), 200);
  P.worker = null;
  showStopped();
}
function showStopped() {
  P.running = false; P.paused = false;
  GBATouch.release();
  document.body.classList.remove("playing");
  // the canvas keeps the game's last picture (the worker owns it): swap in a blank one
  freshCanvas();
  if (w2Open()) w2Canvas();
  $("start").hidden = false;
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
}
function showNow(library) {
  $("nowPlaying").hidden = !P.running;
  if (!P.running) return;
  $("nowTitle").textContent = library || !P.rom ? "Game list" : P.rom.title;
  $("nowSub").textContent = (P.rom && !library ? P.rom.code + "  ·  " : "") + "GBA DS " + P.build.version + "  ·  " + device().name;
}
// hidden: this page, and the second window too when it's open (a game on one monitor and its
// other screen on another keeps going while either can be seen)
function allHidden() { return document.hidden && (!w2Open() || W2.win.document.hidden); }
function visibility() {
  if (!P.running) return;
  if (allHidden() && S.pauseHidden && !P.paused) setPaused(true, true);
  else if (!allHidden() && P.autoPaused) setPaused(false);
}
document.addEventListener("visibilitychange", visibility);
// (a page being closed or reloaded may not live to get the files back from the worker: they are asked
// for as soon as the page is hidden, which comes first, and the worker also sends a setting at once)
const syncNow = () => { if (P.worker) P.worker.postMessage({ t: "sync" }); };
window.addEventListener("pagehide", syncNow);
// (the second window goes with the page)
window.addEventListener("pagehide", () => w2Close());
document.addEventListener("visibilitychange", () => { if (document.hidden) syncNow(); });

/* ================================================================ the start screen */
function showSlots() {
  const b = P.build, r = P.rom;
  // no build: a key to choose one; a build: a card like Your files, with a button for another
  $("slotBuild").parentElement.hidden = !!b;
  $("buildCard").hidden = !b;
  $("buildName").textContent = b ? ": Version " + b.version : "";
  $("slotGame").classList.toggle("have", !!r);
  $("gameName").textContent = r ? r.title : "No game yet";
  $("gameHint").textContent = r ? r.code + " · " + r.name + (P.state ? " · State: " + P.state.name : P.save ? " · Save: " + P.save.name : "") : "Drag and drop or choose a game";
  $("exportSave").disabled = !r;
  showFolder();
  $("forgetBuild").hidden = !b || b.bundled;
  $("playBtn").disabled = !(b && r);
  $("playBtn").textContent = r ? "Play " + r.title : "Play";
}
async function showRecent() {
  const list = (await kvGet("recent")) || [];
  $("recent").hidden = !list.length;
  $("listBtn").hidden = !(list.length || F.handle) || !P.build;
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
      takeRom(it.name, bytes, true, it.mtime);
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
for (const id of ["slotBuild", "newBuild", "slotGame", "openBtn"]) $(id).onclick = () => $("fileInput").click();
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
$("win2Btn").onclick = () => openWin2();
$("fullBtn").onclick = () => {
  const el = $("stage");
  if (document.fullscreenElement) document.exitFullscreen();
  else (el.requestFullscreen || el.webkitRequestFullscreen).call(el).catch?.(() => {});
};

/* ================================================================ touch: the bottom screen, as on the device */
// (a canvas, and where the bottom screen is on it in CSS px, or nothing when it isn't on that canvas)
function touchAt(e, canvas, r) {
  if (!r || !P.win) return null;
  const c = canvas.getBoundingClientRect();
  const px = e.clientX - c.left, py = e.clientY - c.top, s = P.win.screens[1];
  const fx = (px - r.x) / r.w, fy = (py - r.y) / r.h;
  const inside = fx >= 0 && fx <= 1 && fy >= 0 && fy <= 1;
  const cx = Math.min(1, Math.max(0, fx)), cy = Math.min(1, Math.max(0, fy));
  return { inside, x: (s.x + cx * s.w) / P.win.w, y: (s.y + cy * s.h) / P.win.h };
}
function bindTouch(c, rect) {
  let touching = false;
  const touchAt_ = (e) => touchAt(e, c, rect());
  c.addEventListener("pointerdown", (e) => {
    if (!P.worker || P.paused) return;
    const t = touchAt_(e);
    if (!t || !t.inside) return;
    touching = true;
    P.worker.postMessage({ t: "touch", type: 1, x: t.x, y: t.y });
    try { c.setPointerCapture(e.pointerId); } catch (err) {}
    e.preventDefault();
  });
  c.addEventListener("pointermove", (e) => {
    const t = touchAt_(e);
    c.classList.toggle("touch", !!(t && t.inside));
    if (touching && P.worker && t) P.worker.postMessage({ t: "touch", type: 3, x: t.x, y: t.y });
  });
  for (const ev of ["pointerup", "pointercancel"]) c.addEventListener(ev, (e) => {
    if (!touching) return;
    touching = false;
    const t = touchAt_(e);
    if (P.worker && t) P.worker.postMessage({ t: "touch", type: 2, x: t.x, y: t.y });
  });
  c.addEventListener("contextmenu", (e) => e.preventDefault());
}
bindTouch($("screen"), () => P.rects && P.rects[1]);

/* ================================================================ no zooming the page
   A quick double tap on the controls, or two fingers on the screens, would
   zoom the page under the player's hands. The viewport tag and CSS's
   touch-action cover most browsers; these cover the rest: Safari's pinch
   (gesture* events, and it ignores user-scalable=no), a second finger's
   moves, a double tap anywhere, and Ctrl/Cmd with the wheel, a trackpad
   pinch or + / - on computers. Ctrl/Cmd + 0 (back to 100 %) still works. */
const noZoom = (e) => { if (e.cancelable) e.preventDefault(); };
for (const ev of ["gesturestart", "gesturechange", "gestureend"]) document.addEventListener(ev, noZoom, { passive: false });
document.addEventListener("touchmove", (e) => { if (e.touches.length > 1 || (e.scale !== undefined && e.scale !== 1)) noZoom(e); }, { passive: false });
document.addEventListener("dblclick", noZoom, { passive: false });
window.addEventListener("wheel", (e) => { if (e.ctrlKey || e.metaKey) noZoom(e); }, { passive: false });
onKey("keydown", (e) => {
  if ((e.ctrlKey || e.metaKey) && !e.altKey && ["+", "=", "-", "_"].includes(e.key)) noZoom(e);
});

/* ================================================================ input: keyboard and controllers */
const keysDown = new Set();
const typing = (e) => { const t = e.target; return t && (t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA"); };
const anyDialog = () => [...document.querySelectorAll("dialog")].some((d) => d.open);
let capture = null; // asking for one key or button: { kind: "key" | "pad", done(binding|null) }
onKey("keydown", (e) => {
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
});
onKey("keyup", (e) => { keysDown.delete(e.code); });
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
// (with the second window open, its view of the controllers too: some browsers give a
// controller's buttons only to the window that has the focus)
function pads() {
  const a = [...(navigator.getGamepads ? navigator.getGamepads() : [])];
  if (w2Open()) {
    try {
      [...W2.win.navigator.getGamepads()].forEach((p, i) => { if (p && (!a[i] || p.timestamp > a[i].timestamp)) a[i] = p; });
    } catch (e) {}
  }
  return a.filter(Boolean);
}
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
function tickOnce() {
  const mask = currentMask();
  if (P.worker && P.running) {
    const send = anyDialog() || P.paused ? 0 : mask;
    if (send !== lastMask) { P.worker.postMessage({ t: "buttons", mask: send }); lastMask = send; }
  }
  if ($("controls").open) showHeld(mask);
  if (capture && capture.kind === "pad") capture.poll();
}
// (the second window's frames run it too: a page that can't be seen gets none)
function tick() { tickOnce(); requestAnimationFrame(tick); }
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
for (const d of [...RESOLUTIONS.list, { id: "custom", name: "Custom…" }]) {
  const o = document.createElement("option");
  o.value = d.id;
  o.textContent = d.name + (d.hint ? " (" + d.hint + ")" : "");
  $("device").appendChild(o);
}
const CUSTOM = [["cTopW", "top", 0], ["cTopH", "top", 1], ["cTopIn", "top", 2], ["cBotW", "bottom", 0], ["cBotH", "bottom", 1], ["cBotIn", "bottom", 2]];
function showSettings() {
  $("device").value = S.device;
  $("customDevice").hidden = S.device !== "custom";
  for (const [id, k, i] of CUSTOM) $(id).value = S.custom[k][i];
  $("arrange").value = S.arrange; $("gap").value = S.gap; $("win2Screen").value = String(w2Screen());
  $("smooth").value = S.smooth ? "1" : "0"; $("scale").value = String(S.scale);
  $("theme").value = S.theme === "light" ? "light" : "dark";
  $("volume").value = S.volume; $("pauseHidden").checked = !!S.pauseHidden;
  $("touchShow").value = S.touchShow; $("touchPlace").value = S.touchPlace; $("touchSize").value = S.touchSize; $("touchVibrate").checked = !!S.touchVibrate;
  $("aboutNote").textContent = "Player " + VERSION + (P.build ? " · GBA DS " + P.build.version : "") +
    (P.caps ? " · sound " + (P.caps.hasAudio ? "yes" : "no") + ", every button " + (P.caps.hasButtons ? "yes" : "no") : "") + " · " + navigator.userAgent.replace(/^Mozilla\/5\.0 /, "");
}
let restartTimer = 0;
const restartSoon = (note) => { clearTimeout(restartTimer); restartTimer = setTimeout(() => { if (P.running) restart(true, note); else { P.win = null; relayout(); } }, 500); };
$("device").onchange = () => { S.device = $("device").value; saveSettings(); showSettings(); P.win = null; restartSoon("Restarted for " + device().name.replace(/^Two/, "two") + "."); };
for (const [id, k, i] of CUSTOM) $(id).onchange = () => {
  const v = Number($(id).value);
  if (!(v > 0)) return;
  S.custom = JSON.parse(JSON.stringify(S.custom)); S.custom[k][i] = v; saveSettings();
  if (S.device === "custom") restartSoon("Restarted for the custom screens.");
};
$("arrange").onchange = () => { S.arrange = $("arrange").value; saveSettings(); relayout(); };
$("gap").oninput = () => { S.gap = Number($("gap").value); saveSettings(); relayout(); };
$("win2Screen").onchange = () => { S.win2Screen = Number($("win2Screen").value); saveSettings(); w2Title(); relayout(); };
$("smooth").onchange = () => { S.smooth = $("smooth").value === "1"; saveSettings(); relayout(); };
$("scale").onchange = () => { S.scale = Number($("scale").value); saveSettings(); restartSoon("Restarted at the new resolution."); };
const setTheme = (t) => { S.theme = t === "light" ? "light" : "dark"; saveSettings(); applyTheme(); };
$("themeBtn").onclick = () => setTheme(S.theme === "light" ? "dark" : "light");
$("theme").onchange = () => setTheme($("theme").value);
$("accent").onchange = () => { S.accent = $("accent").value; saveSettings(); applyTheme(); };
applyTheme();
$("volume").oninput = () => { S.volume = Number($("volume").value); S.muted = false; saveSettings(); showSound(); };
$("touchShow").onchange = () => { S.touchShow = $("touchShow").value; saveSettings(); relayout(); };
$("touchPlace").onchange = () => { S.touchPlace = $("touchPlace").value; saveSettings(); relayout(); };
$("touchSize").oninput = () => { S.touchSize = Number($("touchSize").value); saveSettings(); relayout(); };
$("touchVibrate").onchange = () => { S.touchVibrate = $("touchVibrate").checked; saveSettings(); relayout(); };
$("pauseHidden").onchange = () => { S.pauseHidden = $("pauseHidden").checked; saveSettings(); };
$("settingsBtn").onclick = () => { showSettings(); $("settings").showModal(); };
if (WIDE) {
  // the wide view's ratio: kept, and given to the game playing now straight away
  const sel = $("wideSel");
  sel.value = wideRatio;
  sel.onchange = () => {
    wideRatio = WIDE_RATIOS.includes(sel.value) ? sel.value : "16:9";
    try { localStorage.setItem(WIDE_KEY, wideRatio); } catch (e) {}
    if (P.worker) P.worker.postMessage({ t: "wide", ratio: wideRatio });
    sel.blur(); /* (so the game's keys go to the game again) */
  };
}
$("folderPick").onclick = () => chooseFolder();
$("folderReconnect").onclick = async () => {
  if (!(await folderAccess(true))) { toast("The folder wasn't allowed.", true); return; }
  toast("Reconnected to “" + F.handle.name + "”.");
  if (P.running) restart(true, "Restarted to use your folder.");
};
$("folderForget").onclick = () => forgetFolder();
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
window.gbadsPlayer = { P, S, F, sound: A, capture: CAP, climb: CLB, currentMask: () => currentMask(), rotation: () => rotation() };

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
  // the player's folder, if one was chosen (allowed already, or asked for at Play)
  const saved = await kvGetSoon("build", 3000);
  if (!(saved && saved.bytes && await takeBuild(saved.name, saved.bytes, {}))) await loadBundled();
  if (saved === STORAGE_BUSY) { toast("This player is open in another tab or window: close it to see your games and saves here.", true); return; }
  if (canFolder) { const h = await kvGet("folder"); if (h && h.queryPermission) { F.handle = h; await folderAccess(false); } }
  const recent = (await kvGet("recent")) || [];
  if (recent.length && !P.rom) {
    const b = await idb("roms", "readonly", (s) => s.get(recent[0].name)).catch(() => null);
    if (b) takeRom(recent[0].name, b, false, recent[0].mtime);
  }
  showSlots(); showRecent();
  if (!navigator.storage || !navigator.storage.persist) return;
  navigator.storage.persist().catch(() => {});
})();
})();
