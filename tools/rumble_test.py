#!/usr/bin/env python3
"""Rumble test (issue #83): builds a tiny GBA ROM that switches the cartridge's rumble pin on and
off (what Drill Dozer and WarioWare: Twisted do; also with a game code mGBA doesn't know and
8-bit stores, as Pokemon Pinball's rumble patch does), runs it headless in the app and checks that
the app's log says the game asked for rumble and the motor is asked for
the game's level times --rumble_strength.

    tools/rumble_test.py [--bin build/host/gba-ds]

Needs llvm-mc and llvm-objcopy. The ROM is only a test signal: no game, no copyright."""
import argparse, os, subprocess, sys, tempfile

ASM = """
	.arm
	.text
	.global _start
_start:
	b start
	.space 0xBC          @ the header (the app doesn't check the logo or the sums)
start:
	ldr r0, =0x080000C4  @ the cartridge's GPIO: data, direction, control
	mov r1, #8           @ pin 3 is the motor
	mov r2, #1
loop:
	STORE r2, [r0, #4]   @ control: readable
	STORE r1, [r0, #2]   @ direction: pin 3 out
	STORE r1, [r0]       @ on
	bl delay
	mov r3, #0
	STORE r3, [r0]       @ off
	bl delay
	b loop
delay:
	ldr r4, =150000
1:	subs r4, r4, #1
	bne 1b
	bx lr
	.ltorg
"""

def build(path, code=b"V49E", store="strh"):
    with tempfile.TemporaryDirectory() as d:
        s, o, b = (os.path.join(d, n) for n in ("t.s", "t.o", "t.bin"))
        open(s, "w").write(ASM.replace("STORE", store))
        subprocess.check_call(["llvm-mc", "-triple=armv4t-none-eabi", "-filetype=obj", s, "-o", o])
        subprocess.check_call(["llvm-objcopy", "-O", "binary", "-j", ".text", o, b])
        rom = bytearray(open(b, "rb").read())
    rom[0xA0:0xAC] = b"RUMBLETEST\0\0"
    rom[0xAC:0xB0] = code   # V49E: a game code mGBA gives the rumble cartridge (Drill Dozer's); BPPE: Pokemon Pinball, which mGBA
                            # doesn't know, as a rumble patch makes it (the app switches the pin on itself)
    rom[0xB0:0xB2] = b"01"
    rom[0xB2] = 0x96
    rom += b"\0" * (0x200 - len(rom)) if len(rom) < 0x200 else b""
    open(path, "wb").write(rom)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default="build/host/gba-ds")
    a = ap.parse_args()
    ok = True
    with tempfile.TemporaryDirectory() as d:
        script = os.path.join(d, "s.txt")
        open(script, "w").write("1 -\n200 QUIT\n")
        # (strb: Pokemon Pinball's rumble patch writes the pins a byte at a time; a cartridge bus takes it
        # as a 16-bit write, mGBA on its own drops it)
        for code, store, strength, motor in ((b"V49E", "strh", 100, "1.00"), (b"V49E", "strh", 50, "0.50"),
                                             (b"V49E", "strh", 0, "0.00"), (b"BPPE", "strh", 100, "1.00"),
                                             (b"BPPE", "strb", 100, "1.00"), (b"BPPE", "strb", 50, "0.50")):
            rom = os.path.join(d, "rumbletest.gba")
            build(rom, code, store)
            env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy",
                       GBADS_WINDOW="1024x1536", GBADS_SYNC="1", GBADS_NOPACE="1")
            out = subprocess.run([a.bin, "--state-dir=" + os.path.join(d, "st%s%s%d" % (code.decode(), store, strength)), "--quick_bar_hint=0",
                                  "--rumble_strength=%d" % strength, "--input-script=" + script, rom],
                                 env=env, capture_output=True, text=True, timeout=120).stderr
            asked = [l for l in out.splitlines() if l.startswith("rumble: the game asked for rumble")]
            # the log says what the game asked and what the motor is asked for (the game's level times the strength)
            good = bool(asked) and ("strength %d%%, motor %s)" % (strength, motor)) in asked[0]
            print("%s %s %s strength %3d%%: %s" % ("ok  " if good else "FAIL", code.decode(), store, strength, asked[0] if asked else "no rumble asked"))
            ok = ok and good
    sys.exit(0 if ok else 1)

main()
