/* GBA DS web player: reading zip files (dev builds, zipped ROMs, bug
 * reports) with the browser's own inflate (DecompressionStream), so the
 * player needs nothing from elsewhere. Stored and deflated entries; no
 * zip64 or encryption. */
"use strict";
const GBAZip = (() => {
  async function inflate(bytes) {
    const ds = new DecompressionStream("deflate-raw");
    const out = new Response(new Blob([bytes]).stream().pipeThrough(ds));
    return new Uint8Array(await out.arrayBuffer());
  }
  // entries of the zip in `bytes`: [{ name, size, dir, read() -> Promise<Uint8Array> }]
  function open(bytes) {
    const d = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let eocd = -1;
    for (let i = bytes.length - 22; i >= Math.max(0, bytes.length - 22 - 65535); i--) {
      if (d.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) throw new Error("not a zip file");
    const count = d.getUint16(eocd + 10, true);
    let p = d.getUint32(eocd + 16, true);
    const dec = new TextDecoder();
    const entries = [];
    for (let i = 0; i < count; i++) {
      if (d.getUint32(p, true) !== 0x02014b50) throw new Error("damaged zip directory");
      const method = d.getUint16(p + 10, true);
      const csize = d.getUint32(p + 20, true), size = d.getUint32(p + 24, true);
      const nlen = d.getUint16(p + 28, true), xlen = d.getUint16(p + 30, true), clen = d.getUint16(p + 32, true);
      const local = d.getUint32(p + 42, true);
      const name = dec.decode(bytes.subarray(p + 46, p + 46 + nlen));
      p += 46 + nlen + xlen + clen;
      entries.push({
        name, size, dir: name.endsWith("/"),
        async read() {
          const ln = d.getUint16(local + 26, true), lx = d.getUint16(local + 28, true);
          const data = bytes.subarray(local + 30 + ln + lx, local + 30 + ln + lx + csize);
          if (method === 0) return data.slice();
          if (method === 8) return inflate(data);
          throw new Error(name + ": compressed in a way this reader can't open (" + method + ")");
        },
      });
    }
    return entries;
  }
  const isZip = (b) => b.length > 4 && b[0] === 0x50 && b[1] === 0x4b && b[2] === 3 && b[3] === 4;
  return { open, isZip };
})();
if (typeof module !== "undefined") module.exports = GBAZip;
