#!/usr/bin/env python3
"""Castlevania: Aria of Sorrow (USA, A2CE) widescreen patches for GBA DS.

    build.py ROM [--out-dir widescreen/A2CE]

ROM: the USA cartridge dump (CRC32 35536183). Writes
"Castlevania - Aria of Sorrow (16-9).ips": the game's fixes
(wide.c plus the edits below) and the GBA DS widescreen marker
(tools/widescreen.py). Needs clang (ARM target), ld.lld and llvm-objcopy.

What it changes (docs/games/castlevania-aria-of-sorrow.md, CVA-6):
- the six routines that stream a column or a row of tiles into the
  playfield's maps as the camera moves (sub_0803F3F8 right, sub_0803F4C8
  left, sub_0803F580 / sub_0803FA10 down, sub_0803F650 / sub_0803FAE0 up, as
  named in the cvaos decompilation): their map address comes from wide.c, the
  columns three tiles further out, the rows 37 tiles instead of 31, and the
  tile lookup is clamped to the room;
- sub_0803F710 (a layer's full redraw) is replaced by wide.c's full_redraw;
  its body holds the long-call stubs into wide.c (Thumb BL reaches 4 MB, the
  code is at 0x08700000);
- the vblank handler's call at 0x0800032A goes through wide_vblank, which puts
  the 512-wide layout into BG0-2CNT after the game's DMA.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import widescreen  # noqa: E402

ROM_CRC = 0x35536183
CODE_AT = 0x08700000          # the ROM's trailing zeros start at 0x08651164
MARKER_AT = 0x7FFFE0          # (file offset) the last 32 bytes, zeros
STUBS_AT = 0x0803F710         # sub_0803F710's body
STUBS_END = 0x0803F7B8        # (its literal pool after that, left as it was)
NAME = "Castlevania - Aria of Sorrow"

# Long-call stubs, 16 bytes each, in this order from STUBS_AT. The first is
# at sub_0803F710's own address, so its callers reach full_redraw.
STUBS = ["full_redraw", "wide_col_right", "wide_col_left", "wide_addr",
         "wide_row_queue", "wide_lookup", "wide_vblank"]

# In-place edits: (address, end, Thumb assembly). `v_NAME` is NAME's stub. Each
# block is padded to `end` with a branch over the rest when it's shorter.
EDITS = [
    # right: r5 layer, r7 struct; col = (camX + 0xF7) >> 3 (wide: + 3)
    (0x0803F450, 0x0803F476, """
        ldrh r0, [r7, #6]
        adds r0, #0xF7
        lsrs r1, r0, #3
        ldrh r0, [r7, #10]
        lsrs r4, r0, #3
        adds r0, r5, #0
        adds r2, r4, #0
        bl v_wide_col_right
        mov r10, r0
        mov r9, r1
    """),
    (0x0803F480, 0x0803F484, "bl v_wide_lookup"),
    # left: r3 layer, r6 struct; col = (camX - 8) >> 3 (wide: - 3)
    (0x0803F508, 0x0803F530, """
        ldrh r0, [r6, #6]
        subs r0, #8
        lsls r0, r0, #13
        lsrs r1, r0, #16
        ldrh r0, [r6, #10]
        lsrs r4, r0, #3
        adds r0, r3, #0
        adds r2, r4, #0
        bl v_wide_col_left
        mov r10, r0
        mov r9, r1
    """),
    (0x0803F53A, 0x0803F53E, "bl v_wide_lookup"),
]


def row_edit(start, end, layer, struct_reg, down, count_at, count_reg, lookup_at, queue_at):
    row = ("adds r0, #0xA7\n lsrs r0, r0, #3" if down else
           "subs r0, #8\n lsls r0, r0, #13\n lsrs r0, r0, #16")
    return [
        (start, end, f"""
            ldrh r0, [{struct_reg}, #6]
            lsrs r0, r0, #3
            subs r0, #3
            lsls r0, r0, #16
            lsrs r5, r0, #16
            ldrh r0, [{struct_reg}, #10]
            {row}
            mov r9, r0
            adds r0, {layer}, #0
            adds r1, r5, #0
            mov r2, r9
            bl v_wide_addr
            mov r10, r0
        """),
        (count_at, count_at + 2, f"movs {count_reg}, #0x24"),
        (lookup_at, lookup_at + 4, "bl v_wide_lookup"),
        (queue_at - 6, queue_at - 4, "movs r1, #0x25"),
        (queue_at, queue_at + 4, "bl v_wide_row_queue"),
    ]


EDITS += row_edit(0x0803F5D8, 0x0803F5FE, "r4", "r7", True, 0x0803F600, "r6", 0x0803F608, 0x0803F624)   # down
EDITS += row_edit(0x0803F692, 0x0803F6BA, "r4", "r7", False, 0x0803F6BC, "r6", 0x0803F6C4, 0x0803F6E0)  # up
EDITS += row_edit(0x0803FA68, 0x0803FA8E, "r4", "r7", True, 0x0803FA90, "r6", 0x0803FA98, 0x0803FAB4)   # down (sub_0803FA10)
EDITS += row_edit(0x0803FB20, 0x0803FB48, "r3", "r6", False, 0x0803FB4A, "r7", 0x0803FB52, 0x0803FB6E)  # up (sub_0803FAE0)
EDITS += [(0x0800032A, 0x0800032E, "bl v_wide_vblank")]

# What each edited place held (a check that the ROM is the one these are for).
EXPECT = {0x0803F480: "c2f788fa", 0x0803F624: "c1f7acff", 0x0800032A: "01f05bf9",
          0x0803F600: "1e26", 0x0803FB4A: "1e27", 0x0803FAAE: "1f21"}


def run(*args):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"{args[0]} failed:\n{r.stdout}{r.stderr}")
    return r.stdout


def stub(target):
    # Thumb: bx pc; nop  ARM: ldr ip, [pc]; bx ip; .word target (Thumb bit set)
    return struct.pack("<HHIII", 0x4778, 0x46C0, 0xE59FC000, 0xE12FFF1C, target | 1)


def build(rom):
    if zlib.crc32(rom) != ROM_CRC:
        sys.exit("not the USA cartridge dump of Aria of Sorrow (CRC32 35536183)")
    for at, hx in EXPECT.items():
        if rom[at - 0x08000000:at - 0x08000000 + len(hx) // 2].hex() != hx:
            sys.exit(f"unexpected bytes at 0x{at:08X}")
    if any(rom[CODE_AT - 0x08000000:CODE_AT - 0x08000000 + 0x2000]) or any(rom[MARKER_AT:MARKER_AT + 32]):
        sys.exit("the space the patch uses isn't empty")
    work = tempfile.mkdtemp(prefix="aos-wide-")
    # wide.c at CODE_AT
    run("clang", "--target=armv4t-none-eabi", "-mcpu=arm7tdmi", "-mthumb", "-Os", "-ffreestanding",
        "-fno-builtin", "-fno-jump-tables", "-Wall", "-Werror", "-c", os.path.join(HERE, "wide.c"),
        "-o", os.path.join(work, "wide.o"))
    # the edits, each in a section at its address
    asm = [".syntax unified", ".thumb"]
    lds = ["SECTIONS {"]
    for i, (at, end, body) in sorted(enumerate(EDITS), key=lambda e: e[1][0]):
        asm += [f'.section .e{i}, "ax"', ".thumb_func", f"e{i}:"]
        asm += [l.strip() for l in body.strip().splitlines()]
        asm += [f"b.n e{i}_end" if end - at > 4 else "", f".org {end - at}", f"e{i}_end:"]
        lds.append(f"  .e{i} 0x{at:08X} : {{ edits.o(.e{i}) }}")
    lds.append(f"  .text 0x{CODE_AT:08X} : {{ wide.o(.text*) wide.o(.rodata*) }}")
    lds.append("  /DISCARD/ : { *(.ARM.*) *(.comment) }")
    lds.append("}")
    for j, name in enumerate(STUBS):
        lds.append(f"v_{name} = 0x{STUBS_AT + 16 * j + 1:08X};  /* Thumb */")
    open(os.path.join(work, "edits.s"), "w").write("\n".join(asm) + "\n")
    open(os.path.join(work, "wide.ld"), "w").write("\n".join(lds) + "\n")
    run("clang", "--target=armv4t-none-eabi", "-mcpu=arm7tdmi", "-c", os.path.join(work, "edits.s"),
        "-o", os.path.join(work, "edits.o"))
    elf = os.path.join(work, "wide.elf")
    run("ld.lld", "-T", os.path.join(work, "wide.ld"), "--no-undefined", "-o", elf,
        os.path.join(work, "wide.o"), os.path.join(work, "edits.o"))
    syms = {}
    for line in run("llvm-nm", elf).splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    out = bytearray(rom)

    def put(addr, data):
        o = addr - 0x08000000
        out[o:o + len(data)] = data

    sec = os.path.join(work, "sec.bin")
    run("llvm-objcopy", "-O", "binary", "--only-section=.text", elf, sec)
    code = open(sec, "rb").read()
    if len(code) > 0x2000:
        sys.exit("wide.c's code is bigger than the room checked for it")
    put(CODE_AT, code)
    for i, (at, end, _) in enumerate(EDITS):
        run("llvm-objcopy", "-O", "binary", f"--only-section=.e{i}", elf, sec)
        data = open(sec, "rb").read()
        if len(data) != end - at:
            sys.exit(f"edit at 0x{at:08X}: {len(data)} bytes, room for {end - at}")
        put(at, data)
    stubs = b"".join(stub(syms[n]) for n in STUBS)
    if STUBS_AT + len(stubs) > STUBS_END:
        sys.exit("too many stubs")
    put(STUBS_AT, stubs)
    return bytes(out), code, syms


def ips_from(old, new):
    recs, i = [], 0
    while i < len(new):
        if new[i] == old[i]:
            i += 1
            continue
        j = i
        while j < len(new) and (new[j] != old[j] or (j + 1 < len(new) and new[j + 1] != old[j + 1])):
            j += 1
        while i < j:
            n = min(j - i, 0xFFFF)
            recs.append((i, new[i:i + n], False))
            i += n
    return widescreen.ips_write(recs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("--out-dir", default=os.path.join(ROOT, "widescreen", "A2CE"))
    ap.add_argument("--patched", help="also write the patched ROM (16:9) here")
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()
    new, code, syms = build(rom)
    fixes = ips_from(rom, new)
    os.makedirs(a.out_dir, exist_ok=True)
    for ratio in ("16:9",):
        recs = widescreen.ips_records(fixes)
        recs.append((MARKER_AT, widescreen.marker(*widescreen.parse_ratio(ratio)), False))
        path = os.path.join(a.out_dir, f"{NAME} ({ratio.replace(':', '-')}).ips")
        open(path, "wb").write(widescreen.ips_write(recs))
        print(f"{path}: {ratio}, {'%d x %d' % widescreen.picture(*widescreen.parse_ratio(ratio))}")
    if a.patched:
        p = bytearray(new)
        p[MARKER_AT:MARKER_AT + 32] = widescreen.marker(16, 9)
        open(a.patched, "wb").write(p)
    print(f"code: {len(code)} bytes at 0x{CODE_AT:08X}; " +
          ", ".join(f"{n} 0x{syms[n]:08X}" for n in STUBS))


if __name__ == "__main__":
    main()
