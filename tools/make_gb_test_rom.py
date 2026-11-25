#!/usr/bin/env python3
"""Makes three tiny Game Boy test ROMs (no copyrighted code, no tools needed):
a DMG one (.gb), a dual-mode one (.gbc: CGB flag 0x80, works on both) and a
Game Boy Color only one (-gbconly.gbc: CGB flag 0xC0), for trying the GB core
without a commercial game (issue #77) and for the regression cases (GB-2 to
GB-10). The three differ in the header (title, flag, checksums), so each has
its own app-made code (`emu_rom_code`: `g...` for the plain one, `c...` for
the other two).

Each shows four vertical stripes (the four shades / four palette colours),
scrolls sideways by one pixel a frame, plays a steady tone, and while A is
held it inverts the DMG palette and moves the picture down (so input is
visible in a screenshot). The GBC one sets its four colours white, red,
green, blue through the colour palette registers.

    tools/make_gb_test_rom.py OUTDIR      # OUTDIR/gbads-test.gb, gbads-test.gbc, gbads-test-gbconly.gbc
"""
import sys, os

LOGO = bytes.fromhex(
    "CEED6666CC0D000B03730083000C000D0008111F8889000EDCCC6EE6DDDDD999"
    "BBBB67636E0EECCCDDDC999FBBB9333E")


def program(gbc):
    code = bytearray()
    labels = {}
    fixups = []  # (position of the offset byte, label)

    def emit(*b):
        code.extend(b)

    def label(n):
        labels[n] = len(code)

    def jr(op, n):
        emit(op, 0)
        fixups.append((len(code) - 1, n))

    emit(0xF3)                    # di
    emit(0x31, 0xFE, 0xFF)        # ld sp,$FFFE
    label("w1")
    emit(0xF0, 0x44, 0xFE, 0x90)  # ldh a,(LY); cp 144
    jr(0x20, "w1")                # jr nz,w1
    emit(0xAF, 0xE0, 0x40)        # xor a; ldh (LCDC),a   (LCD off)
    emit(0x3E, 0xE4, 0xE0, 0x47)  # BGP = %11100100
    if gbc:
        emit(0x3E, 0x80, 0xE0, 0x68)  # BCPS = auto-increment from 0
        for lo, hi in ((0xFF, 0x7F), (0x1F, 0x00), (0xE0, 0x03), (0x00, 0x7C)):
            emit(0x3E, lo, 0xE0, 0x69, 0x3E, hi, 0xE0, 0x69)
    emit(0x21, 0x00, 0x80)        # ld hl,$8000
    emit(0x11, 0, 0)              # ld de,table (patched below)
    table_fix = len(code) - 2
    emit(0x06, 64)                # ld b,64
    label("cp")
    emit(0x1A, 0x22, 0x13, 0x05)  # ld a,(de); ld (hl+),a; inc de; dec b
    jr(0x20, "cp")
    emit(0x21, 0x00, 0x98)        # ld hl,$9800
    label("map")
    emit(0x7D, 0xE6, 0x03, 0x22)  # ld a,l; and 3; ld (hl+),a
    emit(0x7C, 0xFE, 0x9C)        # ld a,h; cp $9C
    jr(0x20, "map")
    emit(0x3E, 0x80, 0xE0, 0x26)  # NR52: sound on
    emit(0x3E, 0xFF, 0xE0, 0x25)  # NR51: all channels to both outputs
    emit(0x3E, 0x77, 0xE0, 0x24)  # NR50: full volume
    emit(0x3E, 0x80, 0xE0, 0x11)  # NR11: duty 50%
    emit(0x3E, 0xA0, 0xE0, 0x12)  # NR12: volume
    emit(0x3E, 0x00, 0xE0, 0x13)  # NR13
    emit(0x3E, 0x86, 0xE0, 0x14)  # NR14: trigger, frequency high bits
    emit(0x3E, 0x91, 0xE0, 0x40)  # LCDC: on, BG on, tile data $8000
    label("frame")
    emit(0xF0, 0x44, 0xFE, 0x90)  # wait for line 144
    jr(0x20, "frame")
    emit(0xF0, 0x43, 0x3C, 0xE0, 0x43)  # SCX += 1
    emit(0x3E, 0x10, 0xE0, 0x00)  # P1: select the buttons
    emit(0xF0, 0x00, 0xF0, 0x00)  # (read twice, the lines settle)
    emit(0x47)                    # ld b,a
    emit(0x3E, 0xE4, 0x0E, 0x00)  # ld a,$E4 ; ld c,0
    emit(0xCB, 0x40)              # bit 0,b   (A: 0 when held)
    jr(0x20, "up")                # jr nz,up
    emit(0x3E, 0x1B, 0x0E, 0x28)  # ld a,$1B ; ld c,40
    label("up")
    emit(0xE0, 0x47)              # BGP = a
    emit(0x79, 0xE0, 0x42)        # SCY = c
    label("w2")
    emit(0xF0, 0x44, 0xFE, 0x90)
    jr(0x28, "w2")                # jr z,w2   (until line 144 has passed)
    jr(0x18, "frame")
    label("table")
    for _ in range(8):
        emit(0x00, 0x00)
    for _ in range(8):
        emit(0xFF, 0x00)
    for _ in range(8):
        emit(0x00, 0xFF)
    for _ in range(8):
        emit(0xFF, 0xFF)
    for pos, n in fixups:
        d = labels[n] - (pos + 1)
        assert -128 <= d <= 127, n
        code[pos] = d & 0xFF
    t = 0x150 + labels["table"]
    code[table_fix], code[table_fix + 1] = t & 0xFF, t >> 8
    return bytes(code)


def make(gbc, only=False):
    """gbc: sets the colour palettes; only: the Game Boy Color only variant (CGB flag 0xC0)"""
    rom = bytearray(0x8000)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # nop; jp $0150
    rom[0x104:0x134] = LOGO
    title = b"GBADSONLY" if only else b"GBADSTEST" if gbc else b"GBADS TEST"
    rom[0x134:0x134 + len(title)] = title
    rom[0x143] = 0xC0 if only else 0x80 if gbc else 0x00  # CGB flag: Color only / works on both / plain
    rom[0x147] = 0x00                   # ROM only
    rom[0x148] = 0x00                   # 32 KB
    rom[0x149] = 0x00                   # no RAM
    rom[0x14A] = 0x01                   # not Japan
    c = 0
    for i in range(0x134, 0x14D):
        c = (c - rom[i] - 1) & 0xFF
    rom[0x14D] = c
    p = program(gbc)
    rom[0x150:0x150 + len(p)] = p
    s = (sum(rom) - rom[0x14E] - rom[0x14F]) & 0xFFFF
    rom[0x14E], rom[0x14F] = s >> 8, s & 0xFF
    return bytes(rom)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "gbads-test.gb"), "wb").write(make(False))
    open(os.path.join(out, "gbads-test.gbc"), "wb").write(make(True))
    open(os.path.join(out, "gbads-test-gbconly.gbc"), "wb").write(make(True, only=True))
    print("wrote", out + "/gbads-test.gb, gbads-test.gbc and gbads-test-gbconly.gbc")
