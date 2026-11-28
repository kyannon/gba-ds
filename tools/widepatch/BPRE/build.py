#!/usr/bin/env python3
"""Pokémon FireRed (BPRE, 1.0) widescreen 16:9 patch (docs/WIDESCREEN.md, PK-9).

    build.py ROM [OUT_DIR]

ROM: Pokemon - FireRed Version (USA, Europe), the cartridge dump (CRC32 dd88761c).
Compiles camera.c (clang, thumbv4t; ld.lld, llvm-nm, llvm-objcopy) into free space
at 0x08F00000, puts a jump to it at the start of each field_camera.c function it
replaces, and changes the overworld's three map backgrounds to 512 x 256 (their
buffers, their screenblocks) and how far past the screen's sides people are still
drawn. Writes OUT_DIR (default build/widepatch/BPRE) fixes.ips (the game's changes)
and patched.gba (the ROM with them, without the marker), then the patch players
get, widescreen/BPRE/Pokemon FireRed (16-9).ips: the fixes and the marker
(tools/widescreen.py ips), the marker at 0xFFFFE0 (the ROM's 0xFF padding).
"""
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
PATCH = os.path.join(ROOT, "widescreen", "BPRE", "Pokemon FireRed (16-9).ips")
MARKER_AT = 0xFFFFE0
CODE_AT = 0x08F00000
BPRE_CRC = 0xDD88761C  # Pokemon - FireRed Version (USA, Europe), 1.0

# field_camera.c functions replaced, the game's address -> camera.c's function
HOOKS = {
    0x0805A5B8: "ws_tilemap_move",   # tilemap_move_something
    0x0805A5F4: "ws_update_scroll",  # FieldUpdateBgTilemapScroll
    0x0805A658: "ws_pixel_offset",   # FieldCameraGetPixelOffsetAtGround
    0x0805A6A8: "ws_draw_whole",     # DrawWholeMapViewInternal
    0x0805A72C: "ws_redraw_slices",  # RedrawMapSlicesForCameraUpdate
    0x0805A778: "ws_slice_north",    # RedrawMapSliceNorth
    0x0805A7D8: "ws_slice_south",    # RedrawMapSliceSouth
    0x0805A824: "ws_slice_east",     # RedrawMapSliceEast
    0x0805A87C: "ws_slice_west",     # RedrawMapSliceWest
    0x0805AAE8: "ws_map_pos",        # MapPosToBgTilemapOffset
}


def bg_template(bg, char, mapbase, size, prio):
    return struct.pack("<HH", bg | char << 2 | mapbase << 4 | size << 9 | prio << 12, 0)


# (address, original bytes, new bytes)
EDITS = [
    # sOverworldBgTemplates: BG1-3 512 x 256 in screenblocks 29-30, 27-28, 25-26 (BG0 stays
    # in 31; 24 and below hold BG0's tiles, the text windows' frames among them)
    (0x0826D320,
     bg_template(0, 2, 31, 0, 0) + bg_template(1, 0, 29, 0, 1) + bg_template(2, 0, 28, 0, 2) + bg_template(3, 0, 30, 0, 3),
     bg_template(0, 2, 31, 0, 0) + bg_template(1, 0, 29, 1, 1) + bg_template(2, 0, 27, 1, 2) + bg_template(3, 0, 25, 1, 3)),
    # InitOverworldBgs, InitOverworldBgs_NoResetHeap: the three tilemap buffers 0x1000 bytes
    # (lsls r4, r4, #4 -> #5: 0x80 << 5)
    (0x080562F2, bytes.fromhex("2401"), bytes.fromhex("6401")),
    (0x0805638E, bytes.fromhex("2401"), bytes.fromhex("6401")),
    # CalcWhetherObjectIsOffscreen: hidden from x 288 (was 256): asrs r0, #16 -> #17, cmp #0x8f
    (0x08067AC8, bytes.fromhex("0014ff28"), bytes.fromhex("40148f28")),
    # ... and left of x -48 (was -16; the S.S. Anne -64, was -32)
    (0x08067B08, struct.pack("<I", 0xFFF0), struct.pack("<I", 0xFFD0)),
]


def compile_code(work):
    src = os.path.join(HERE, "camera.c")
    obj = os.path.join(work, "camera.o")
    elf = os.path.join(work, "camera.elf")
    ld = os.path.join(work, "camera.ld")
    with open(ld, "w") as f:
        f.write("SECTIONS { . = 0x%08X; .text : { *(.text*) *(.rodata*) } "
                "/DISCARD/ : { *(.ARM.*) *(.comment) *(.data*) *(.bss*) } }\n" % CODE_AT)
    subprocess.run(["clang", "--target=thumbv4t-none-eabi", "-mthumb", "-Os", "-ffreestanding",
                    "-fno-builtin", "-nostdlib", "-fomit-frame-pointer", "-Wall", "-Werror",
                    "-c", src, "-o", obj], check=True)
    subprocess.run(["ld.lld", "-T", ld, "-e", "0", obj, "-o", elf], check=True)
    syms = {}
    for line in subprocess.run(["llvm-nm", elf], capture_output=True, text=True, check=True).stdout.splitlines():
        a, t, name = line.split()
        syms[name] = int(a, 16)
    binf = os.path.join(work, "camera.bin")
    subprocess.run(["llvm-objcopy", "-O", "binary", "--only-section=.text", elf, binf], check=True)
    return open(binf, "rb").read(), syms


def stub(at, target):
    """ldr r3, =target; bx r3 (r3 is free: none of the replaced functions takes 4 arguments)."""
    if at % 4 == 0:
        return bytes.fromhex("004b1847") + struct.pack("<I", target | 1)
    return bytes.fromhex("014b1847c046") + struct.pack("<I", target | 1)


def ips(orig, new):
    out = bytearray(b"PATCH")
    i, n = 0, len(new)
    while i < n:
        if i < len(orig) and new[i] == orig[i]:
            i += 1
            continue
        j = i
        while j < n and j - i < 0xFFFF and not (j < len(orig) and new[j] == orig[j]
                                                and new[j + 1:j + 9] == orig[j + 1:j + 9]):
            j += 1
        if i == 0x454F46:  # "EOF" as an offset: start one byte earlier
            i -= 1
        out += struct.pack(">I", i)[1:] + struct.pack(">H", j - i) + new[i:j]
        i = j
    out += b"EOF"
    return bytes(out)


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    rom_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "build", "widepatch", "BPRE")
    os.makedirs(out_dir, exist_ok=True)
    orig = open(rom_path, "rb").read()
    if zlib.crc32(orig) != BPRE_CRC:
        sys.exit("not Pokemon FireRed (USA, Europe) 1.0: CRC32 %08X" % zlib.crc32(orig))
    rom = bytearray(orig)
    code, syms = compile_code(out_dir)
    off = CODE_AT & 0xFFFFFF
    if any(b != 0xFF for b in rom[off:off + len(code)]):
        sys.exit("the code's place isn't free")
    rom[off:off + len(code)] = code
    for at, name in HOOKS.items():
        s = stub(at, syms[name])
        rom[at & 0xFFFFFF:(at & 0xFFFFFF) + len(s)] = s
    for at, before, after in EDITS:
        o = at & 0xFFFFFF
        if bytes(rom[o:o + len(before)]) != before:
            sys.exit("unexpected bytes at %08X: %s" % (at, rom[o:o + len(before)].hex()))
        rom[o:o + len(after)] = after
    open(os.path.join(out_dir, "patched.gba"), "wb").write(rom)
    open(os.path.join(out_dir, "fixes.ips"), "wb").write(ips(orig, bytes(rom)))
    print("code %d bytes at %08X; %d hooks, %d edits" % (len(code), CODE_AT, len(HOOKS), len(EDITS)))
    if any(b != 0xFF for b in orig[MARKER_AT:MARKER_AT + 32]):
        sys.exit("the marker's place isn't free")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "widescreen.py"), "ips", hex(MARKER_AT), "16:9",
                    "-o", PATCH, "--with", os.path.join(out_dir, "fixes.ips")], check=True)


if __name__ == "__main__":
    main()
