/* GBA DS web player: on-screen controls, for touchscreens. The device's
 * buttons laid out below the two screens, or beside them (the D-pad on the
 * left, the face buttons on the right, as on a GBA), in space of their own:
 * the screens fit into what's left, whichever way they're arranged. L2 and
 * R2 sit inside L and R: held, they rewind and fast-forward, as on the device.
 *
 *   GBATouch.place(W, H, opts) -> the screens' box { x, y, w, h }
 *   GBATouch.mask              the buttons held now (ui.h's bits)
 *
 * opts: { show, placement: "below" | "beside", size (1 = 100 %), vibrate,
 * safe: { top, right, bottom, left } }. Multi-touch; a finger can slide
 * from one button to the next, and round the D-pad (diagonals too). */
"use strict";
const GBATouch = (() => {
  // ui.h's bits
  const B = { a: 1 << 0, b: 1 << 1, x: 1 << 2, y: 1 << 3, start: 1 << 4, select: 1 << 5, home: 1 << 6, l: 1 << 7, r: 1 << 8, l2: 1 << 9, r2: 1 << 10,
    up: 1 << 12, down: 1 << 13, left: 1 << 14, right: 1 << 15 };
  let root = null, ctl = [], mask = 0, opts = { vibrate: true };
  const held = new Map(); // pointer -> bits

  function el(cls, html, bits) {
    const e = document.createElement("div");
    e.className = "tc " + cls;
    if (html) e.innerHTML = html;
    if (bits) e.dataset.bits = bits;
    root.appendChild(e);
    return e;
  }
  function build(container) {
    root = document.createElement("div");
    root.id = "touch";
    root.setAttribute("aria-hidden", "true");
    container.appendChild(root);
    const dpad = el("dpad", '<i class="arm v"></i><i class="arm h"></i><b class="ar u"></b><b class="ar d"></b><b class="ar l"></b><b class="ar r"></b><i class="hub"></i>');
    ctl.push({ el: dpad, kind: "dpad" });
    for (const [k, label] of [["x", "X"], ["y", "Y"], ["a", "A"], ["b", "B"]]) ctl.push({ el: el("face f-" + k, "<span>" + label + "</span>", B[k]), kind: "btn", bits: B[k], round: true, group: "face" });
    ctl.push({ el: el("shoulder sl", "<span>L</span>", B.l), kind: "btn", bits: B.l });
    ctl.push({ el: el("shoulder sr", "<span>R</span>", B.r), kind: "btn", bits: B.r });
    ctl.push({ el: el("shoulder s2 sl2", "<span>L2</span>", B.l2), kind: "btn", bits: B.l2 });
    ctl.push({ el: el("shoulder s2 sr2", "<span>R2</span>", B.r2), kind: "btn", bits: B.r2 });
    for (const [k, label] of [["select", "Select"], ["start", "Start"], ["home", "Menu"]]) ctl.push({ el: el("pill p-" + k, "<span>" + label + "</span>", B[k]), kind: "btn", bits: B[k] });
    root.addEventListener("pointerdown", down);
    root.addEventListener("pointermove", move);
    root.addEventListener("pointerup", up);
    root.addEventListener("pointercancel", up);
    root.addEventListener("lostpointercapture", up);
    root.addEventListener("contextmenu", (e) => e.preventDefault());
  }
  const find = (name) => ctl.find((c) => c.el.classList.contains(name));
  function put(c, x, y, w, h) {
    c.r = { x, y, w, h };
    Object.assign(c.el.style, { left: x + "px", top: y + "px", width: w + "px", height: h + "px" });
  }

  /* ---- where everything goes */
  function place(W, H, o) {
    opts = o;
    if (!root) build(document.getElementById("stage"));
    root.hidden = !o.show;
    root.dataset.placement = o.placement;
    if (!o.show) { release(); return { x: 0, y: 0, w: W, h: H }; }
    const s = Object.assign({ top: 0, right: 0, bottom: 0, left: 0 }, o.safe || {});
    // sizes in CSS px at 100 %, scaled to the screen and the setting, then down to fit
    let u = (o.size || 1) * Math.max(0.78, Math.min(1.25, Math.min(W, H) / 400));
    const size = (u) => ({ D: 128 * u, F: 138 * u, fb: 52 * u, SW: 76 * u, S2: 56 * u, SH: 32 * u, PW: 56 * u, PH: 24 * u, g: 10 * u, pad: 12 * u });
    let z = size(u), box;
    if (o.placement === "beside") {
      // a column each side: L and L2, the D-pad, Select and Menu | R2 and R, the face buttons, Start
      const colW = (z) => z.pad * 2 + Math.max(z.D, z.F, z.PW * 2 + z.g, z.SW + z.g + z.S2);
      const avail = W - s.left - s.right;
      if (colW(z) * 2 > avail * 0.62) { u *= avail * 0.62 / (colW(z) * 2); z = size(u); }
      const cw = colW(z), top = s.top + z.pad, bot = H - s.bottom - z.pad;
      const lx = s.left, rx = W - s.right - cw, mid = (top + bot) / 2;
      put(find("sl"), lx + z.pad, top, z.SW, z.SH);
      put(find("sr"), rx + cw - z.pad - z.SW, top, z.SW, z.SH);
      put(find("sl2"), lx + z.pad + z.SW + z.g, top, z.S2, z.SH);
      put(find("sr2"), rx + cw - z.pad - z.SW - z.g - z.S2, top, z.S2, z.SH);
      put(find("dpad"), lx + (cw - z.D) / 2, mid - z.D / 2, z.D, z.D);
      face(rx + (cw - z.F) / 2, mid - z.F / 2, z);
      put(find("p-select"), lx + cw / 2 - z.PW - z.g / 2, bot - z.PH, z.PW, z.PH);
      put(find("p-home"), lx + cw / 2 + z.g / 2, bot - z.PH, z.PW, z.PH);
      put(find("p-start"), rx + (cw - z.PW) / 2, bot - z.PH, z.PW, z.PH);
      box = { x: lx + cw, y: s.top, w: rx - lx - cw, h: H - s.top - s.bottom };
    } else {
      // a band below: L, L2, Menu, R2, R | the D-pad, Select over Start, the face buttons
      const need = (z) => z.pad * 2 + Math.max(z.D + z.F + z.PW + z.g * 4, (z.SW + z.g + z.S2) * 2 + z.PW + z.g * 2);
      const avail = W - s.left - s.right;
      if (need(z) > avail) { u *= avail / need(z); z = size(u); }
      const band = z.pad + z.SH + z.g + Math.max(z.D, z.F) + z.pad + s.bottom;
      const y0 = H - band, x0 = s.left, x1 = W - s.right, cx = (x0 + x1) / 2;
      const ry = y0 + z.pad + z.SH + z.g, rh = Math.max(z.D, z.F);
      put(find("sl"), x0 + z.pad, y0 + z.pad, z.SW, z.SH);
      put(find("sr"), x1 - z.pad - z.SW, y0 + z.pad, z.SW, z.SH);
      put(find("sl2"), x0 + z.pad + z.SW + z.g, y0 + z.pad, z.S2, z.SH);
      put(find("sr2"), x1 - z.pad - z.SW - z.g - z.S2, y0 + z.pad, z.S2, z.SH);
      put(find("p-home"), cx - z.PW / 2, y0 + z.pad + (z.SH - z.PH) / 2, z.PW, z.PH);
      put(find("dpad"), x0 + z.pad, ry + (rh - z.D) / 2, z.D, z.D);
      face(x1 - z.pad - z.F, ry + (rh - z.F) / 2, z);
      put(find("p-select"), cx - z.PW / 2, ry + rh / 2 - z.PH - z.g / 2, z.PW, z.PH);
      put(find("p-start"), cx - z.PW / 2, ry + rh / 2 + z.g / 2, z.PW, z.PH);
      box = { x: s.left, y: s.top, w: W - s.left - s.right, h: y0 - s.top };
    }
    root.style.setProperty("--u", u);
    return box;
  }
  // the four face buttons as on the device: X top, Y left, A right, B bottom
  function face(x, y, z) {
    const c = z.F / 2, r = z.fb / 2, d = c - r;
    const at = { x: [c, c - d], y: [c - d, c], a: [c + d, c], b: [c, c + d] };
    for (const k of ["x", "y", "a", "b"]) put(find("f-" + k), x + at[k][0] - r, y + at[k][1] - r, z.fb, z.fb);
  }

  /* ---- touches */
  // the bits under a point: the D-pad by direction (8 ways), buttons with some slack
  function hit(px, py) {
    const rr = root.getBoundingClientRect();
    const x = px - rr.left, y = py - rr.top;
    let bits = 0;
    for (const c of ctl) {
      const r = c.r;
      if (!r) continue;
      if (c.kind === "dpad") {
        const cx = r.x + r.w / 2, cy = r.y + r.h / 2, dx = x - cx, dy = y - cy, m = Math.hypot(dx, dy);
        if (m > r.w * 0.75 || m < r.w * 0.1) continue;
        const a = Math.atan2(dy, dx) / Math.PI * 4; // -4..4, 0 right, 2 down
        const sec = (Math.round(a) + 8) % 8;       // 0 r, 1 dr, 2 d, 3 dl, 4 l, 5 ul, 6 u, 7 ur
        bits |= [B.right, B.right | B.down, B.down, B.down | B.left, B.left, B.left | B.up, B.up, B.up | B.right][sec];
        continue;
      }
      const slack = c.round ? r.w * 0.12 : 6;
      if (c.round) {
        const cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        if (Math.hypot(x - cx, y - cy) <= r.w / 2 + slack) bits |= c.bits;
      } else if (x >= r.x - slack && x <= r.x + r.w + slack && y >= r.y - slack && y <= r.y + r.h + slack) bits |= c.bits;
    }
    return bits;
  }
  function down(e) {
    if (!e.target.closest(".tc")) return;
    e.preventDefault();
    try { e.target.closest(".tc").setPointerCapture(e.pointerId); } catch (err) {}
    held.set(e.pointerId, hit(e.clientX, e.clientY));
    update(true);
  }
  function move(e) {
    if (!held.has(e.pointerId)) return;
    e.preventDefault();
    const b = hit(e.clientX, e.clientY);
    if (b !== held.get(e.pointerId)) { held.set(e.pointerId, b); update(true); }
  }
  function up(e) {
    if (!held.delete(e.pointerId)) return;
    update(false);
  }
  function release() { if (held.size) { held.clear(); update(false); } }
  function update(pressed) {
    let m = 0;
    for (const b of held.values()) m |= b;
    if (pressed && opts.vibrate && (m & ~mask) && navigator.vibrate) { try { navigator.vibrate(8); } catch (e) {} }
    mask = m;
    for (const c of ctl) {
      if (c.kind === "dpad") {
        for (const [k, bit] of [["u", B.up], ["d", B.down], ["l", B.left], ["r", B.right]]) c.el.querySelector(".ar." + k).classList.toggle("on", !!(m & bit));
      } else c.el.classList.toggle("on", !!(m & c.bits));
    }
  }
  return { place, release, get mask() { return mask; } };
})();
if (typeof module !== "undefined") module.exports = GBATouch;
