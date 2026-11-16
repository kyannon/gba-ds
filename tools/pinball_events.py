#!/usr/bin/env python3
"""Builds Pokemon Pinball: Ruby & Sapphire (USA, BPPE) save states for the
game's events, by steering the game through its own code paths with the
harness (no in-game play needed):

  tools/pinball_events.py [--prefix=XX] ROM FIXTURE_STATES_DIR OUTDIR [NAME...]

ROM is one release (gba-ds-tests' roms/CODE.zip, unzipped); the harness runs
a copy of it in OUTDIR. FIXTURE_STATES_DIR is gba-ds-tests' states/pokemon-pinball
(XX-ruby-top.ss and XX-sapphire-play.ss are the starting points, XX being
--prefix: us (the default, BPPE), jp, jp1 (Japan's Rev 1), eu...). For a
release with no such states, the ones named by `boot` below are made from
the ROM alone first (the boot's key presses; Europe's language menu too:
English), and the starting points are those. Writes OUTDIR/NAME.ss for
each of (default: all):

  boot                XX-title, XX-field-select, XX-sapphire-start,
                      XX-ruby-bottom, XX-ruby-top, XX-sapphire-play,
                      XX-ruby-pause, XX-ruby-catch (CATCH 'EM mode)

  ruby-egg-wander     egg hatch mode, the hatched Pokemon wandering
  ruby-egg-catch      ... hit twice: its catch animation
  ruby-evolution      EVO mode, three items collected, the ball in the
                      centre hole: the evolution scene about to play
  sapphire-default    Sapphire, the ball launched and in play
  sapphire-egg-wander, sapphire-egg-catch, sapphire-evolution
  ruby-travel         Travel mode, a lane hit, the ball in the centre hole:
                      the confirmation, then (A) the travel scene
  ruby-jirachi        Jirachi's catch mode (the slot rigged to it), the
                      ball free: Jirachi flying about
  ruby-jirachi-catch  ... hit three times: its catch scene
  bonus-dusclops, bonus-kecleon, bonus-kyogre, bonus-groudon,
  bonus-rayquaza, bonus-spheal
                      a bonus field, 4 seconds in (from Ruby's centre
                      hole: the slot rigged to GO TO BONUS, A at the
                      Bonus / Cancel prompt, the field chosen just before)

How (names and offsets from the pret/pokepinballrs decompilation; S is
gCurrentPinballGame, 0x02000000 in every state seen; docs/PINBALL.md):
  - a mode starts as RequestBoardStateTransition does it: S+0x14
    (nextBoardState: 2 bonus hole, 4 Catch 'Em, 5 egg hatch, 6 EVO,
    7 travel) and S+0x16 = 2 (the dispatcher's "changing");
  - the ball (S+0x1334) is moved as the game moves it: whole pixels
    (+0x10), half pixels (+0x28, +0x2C), Q24.8 (+0x34, +0x3C), no
    velocity (+0x30). Meanwhile it's kept up the table so it doesn't
    drain (a drain hands it to the ball saver, which then moves it);
  - targets: the hatched Pokemon at S+0x1FC / 0x200 (tenths of a pixel,
    hit within 9 px of +12, +14, twice), EVO items at S+0x258 / 0x25A
    (+8, +8), the centre hole at 120, 268;
  - Travel: the lane hit set (S+0x6BC = 1, left), the ball dropped into
    the centre hole once (dropping it again starts the hole's catch
    over), A tapped at the prompt (it takes a fresh press on one frame);
  - Jirachi: the slot's prize 21; its hits are real collisions (a 48 x 48
    map from S+0xC4 / 0xC6): the ball dropped just above it, moving down
    (velocity y +0x32);
  - the slot (S+0x6F8: six prizes) all set to 42 (a bonus stage), A to
    stop it; at the boss hole's prompt (boardState 3, sub-state 4)
    nextBonusField (S+0x284: 2 Dusclops ... 7 Spheal), then A.
Each step re-runs the script from the start (the harness is exact), so
later steps can use values read in earlier runs.
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HARNESS = os.path.join(ROOT, "build", "host", "harness")
S = 0x02000000
BALL = 0x02001334
GMAIN_FIELD = 0x0200B0C4  # gMain.selectedField
FIELDS = {"dusclops": 2, "kecleon": 3, "kyogre": 4, "groudon": 5, "rayquaza": 6, "spheal": 7}


class Game:
    def __init__(self, rom, workdir):
        self.rom = rom
        self.dir = workdir

    def run(self, lines, peeks=()):
        script = list(lines) + ["peek %#x %d" % (a, n) for a, n in peeks]
        path = os.path.join(self.dir, "steps.txt")
        with open(path, "w") as f:
            f.write("\n".join(script) + "\n")
        # (relative paths: the harness cuts long ones short)
        out = subprocess.run([HARNESS, self.rom, ".", "steps.txt"], cwd=self.dir,
                             capture_output=True, text=True).stdout
        vals = []
        for a, n in peeks:
            m = re.search(r"%08X: ((?:[0-9A-F]{2} ?)+)" % a, out)
            b = bytes.fromhex(m.group(1).replace(" ", "")) if m else b""
            vals.append(int.from_bytes(b[:n], "little"))
        return vals

    def board(self, lines):
        st, sub = self.run(lines, [(S + 0x13, 1), (S + 0x17, 1)])
        return st, sub


def tp(x, y):
    return ["poke %#x 4 0" % (BALL + 0x30),
            "poke %#x 4 %d" % (BALL + 0x10, x | y << 16),
            "poke %#x 4 %d" % (BALL + 0x28, 2 * x | 2 * y << 16), "poke %#x 4 %d" % (BALL + 0x2C, 2 * x | 2 * y << 16),
            "poke %#x 4 %d" % (BALL + 0x34, x << 8), "poke %#x 4 %d" % (BALL + 0x38, y << 8),
            "poke %#x 4 %d" % (BALL + 0x3C, x << 8), "poke %#x 4 %d" % (BALL + 0x40, y << 8)]


def request(state):
    return ["poke %#x 1 %d" % (S + 0x14, state), "poke %#x 1 2" % (S + 0x16)]


def s16(v):
    return v - 65536 if v > 32767 else v


HOLD = (120, 60)  # up the table, out of every target's way


def egg(g, base, wander, catch):
    L = ["load ./%s" % base, "wait 2"] + request(5)
    for _ in range(40):
        L += tp(*HOLD) + ["wait 30"]
        if g.board(L)[1] == 3:  # wandering
            break
    L += tp(*HOLD) + ["wait 30", "save ./%s.ss" % wander]
    for _ in range(2):
        x, y = g.run(L, [(S + 0x1FC, 4), (S + 0x200, 4)])
        L += tp(x // 10 + 12, y // 10 + 14) + ["wait 9"]
    L += ["wait 30", "save ./%s.ss" % catch]
    g.run(L)
    return g.board(L) == (5, 5)


def evolution(g, base, name):
    L = ["load ./%s" % base, "wait 2"] + request(6)
    for _ in range(80):
        sub = g.board(L)[1]
        if sub == 2:  # an item out: onto it
            x, y = g.run(L, [(S + 0x258, 2), (S + 0x25A, 2)])
            L += tp(s16(x) + 8, s16(y) + 8) + ["wait 30"]
        elif sub == 4:  # the centre hole open
            L += tp(120, 268) + ["wait 10"]
            for _ in range(20):
                if g.board(L)[1] != 4:
                    break
                L += ["wait 10"]
        elif sub >= 5:
            break
        else:
            L += tp(*HOLD) + ["wait 20"]
    L += ["save ./%s.ss" % name]
    g.run(L)
    return g.board(L)[0] == 6 and g.board(L)[1] >= 5


def travel(g, base, name):
    L = ["load ./%s" % base, "wait 2"] + request(7)
    for _ in range(60):
        sub = g.board(L)[1]
        if sub == 1:  # the lanes lit: one hit
            L += ["poke %#x 1 1" % (S + 0x6BC)] + tp(*HOLD) + ["wait 20"]
        elif sub == 3:  # the centre hole open: once
            L += tp(120, 268)
            for _ in range(30):
                L += ["wait 10"]
                if g.board(L)[1] != 3:
                    break
        elif sub == 4:
            L += ["wait 10"]
        elif sub == 5:  # the confirmation
            break
        else:
            L += tp(*HOLD) + ["wait 20"]
    L += ["save ./%s.ss" % name]
    g.run(L)
    return g.board(L) == (7, 5)


def slot_prize(prize):
    return ["poke %#x 2 %d" % (S + 0x6F8 + 2 * i, prize) for i in range(6)] + \
           ["poke %#x 2 %d" % (S + 0x6EC, prize | prize << 8), "hold A 4"]


def jirachi(g, base, flying, caught):
    L = ["load ./%s" % base, "wait 2"] + request(2) + ["wait 60"] + tp(120, 268) + ["wait 60"] + slot_prize(21)
    for _ in range(120):
        if g.board(L) == (8, 3):  # its hit phase
            break
        L += ["wait 20"]
    for _ in range(30):  # the ball out of the hole
        if g.run(L, [(S + 0x25, 1)])[0] == 0:
            break
        L += ["wait 10"]
    L += ["wait 30", "save ./%s.ss" % flying]
    for _ in range(40):
        cx, cy = g.run(L, [(S + 0xC4, 2), (S + 0xC6, 2)])
        L += tp(cx + 24, cy - 6) + ["poke %#x 2 300" % (BALL + 0x32), "wait 8"]
        if g.run(L, [(S + 0x5A4, 1)])[0] == 2:  # its catch scene
            break
    L += ["wait 30", "save ./%s.ss" % caught]
    g.run(L)
    return g.run(L, [(S + 0x5A4, 1)])[0] == 2


def sapphire_default(g, base, name):
    L = ["load ./%s" % base]
    for _ in range(60):
        L += ["wait 20"]
        st, sub = g.board(L)
        chute, = g.run(L, [(S + 0x20, 1)])
        if st == 1 and not chute:
            break
        if chute or (st == 0 and sub == 4):
            L += ["hold A 50"]  # launch
    L += tp(*HOLD) + ["wait 10", "save ./%s.ss" % name]
    g.run(L)
    return g.board(L)[0] == 1


def bonus(g, base, field, name):
    L = ["load ./%s" % base, "wait 2"] + request(2) + ["wait 60"] + tp(120, 268) + ["wait 60"]
    L += slot_prize(42)
    for _ in range(40):
        L += ["wait 30"]
        if g.board(L) == (3, 4):  # Bonus / Cancel
            break
    L += ["wait 30", "poke %#x 1 %d" % (S + 0x284, field), "hold A 4"]
    for _ in range(30):
        L += ["wait 30"]
        if g.run(L, [(GMAIN_FIELD, 1)])[0] == field:
            break
    L += ["wait 240", "save ./%s.ss" % name]
    g.run(L)
    return g.run(L, [(GMAIN_FIELD, 1)])[0] == field


def rom_europe(path):
    with open(path, "rb") as f:
        f.seek(0xAF)
        return f.read(1) == b"P"  # (the game code's region letter: a language menu comes first)


def boot(g, p, europe):
    """The states a release's cases start from, made from the ROM alone."""
    ss = lambda n: "./%s-%s.ss" % (p, n)
    L = ["wait 400", "tap A 1", "wait 120", "tap START 1", "wait 150"] if europe else ["wait 300", "tap START 1", "wait 120"]
    L += ["save " + ss("title"), "tap START 1", "wait 120", "tap A 1", "wait 90", "save " + ss("field-select")]
    g.run(L)
    ruby = ["load " + ss("field-select"), "tap A 1", "wait 90", "tap A 1", "wait 90", "tap A 1", "wait 200",
            "hold A 50", "wait 360", "save " + ss("ruby-bottom")]
    g.run(ruby)
    g.run(["load " + ss("field-select"), "tap RIGHT 1", "wait 30", "tap A 1", "wait 90", "tap A 1", "wait 90", "tap A 1",
           "wait 60", "save " + ss("sapphire-start")])
    top = ["load " + ss("ruby-bottom"), "wait 2"] + tp(*HOLD) + ["wait 120", "save " + ss("ruby-top")]
    g.run(top)
    sapphire_default(g, "%s-sapphire-start.ss" % p, "%s-sapphire-play" % p)
    g.run(["load " + ss("ruby-bottom"), "tap START 1", "wait 30", "save " + ss("ruby-pause")])
    g.run(["load " + ss("ruby-top")] + request(4) + tp(*HOLD) + ["wait 300", "save " + ss("ruby-catch")])
    return all(os.path.exists(os.path.join(g.dir, "%s-%s.ss" % (p, n)))
               for n in ("title", "field-select", "sapphire-start", "ruby-bottom", "ruby-top", "sapphire-play", "ruby-pause", "ruby-catch"))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--prefix=")]
    prefix = ([a[9:] for a in sys.argv[1:] if a.startswith("--prefix=")] or ["us"])[0]
    if len(args) < 3:
        sys.exit(__doc__)
    rom, fixtures, out = (os.path.abspath(a) for a in args[:3])
    want = set(args[3:])
    os.makedirs(out, exist_ok=True)
    starts = ("%s-ruby-top.ss" % prefix, "%s-sapphire-play.ss" % prefix)
    for n in starts:
        if os.path.exists(os.path.join(fixtures, n)) and os.path.abspath(fixtures) != out:
            shutil.copy(os.path.join(fixtures, n), out)
    # (a copy: the harness writes the battery save beside the ROM)
    work_rom = os.path.join(out, "pinball.gba")
    shutil.copy(rom, work_rom)
    g = Game(work_rom, out)
    boot_only = want == {"boot"}
    if "boot" in want or not all(os.path.exists(os.path.join(out, n)) for n in starts):
        print("%-40s %s" % ("boot", "ok" if boot(g, prefix, rom_europe(rom)) else "FAILED"))
        want.discard("boot")
    if boot_only:
        want = {"-"}  # (no event jobs)
    jobs = [("ruby-egg-wander ruby-egg-catch", lambda: egg(g, starts[0], "ruby-egg-wander", "ruby-egg-catch")),
            ("ruby-evolution", lambda: evolution(g, starts[0], "ruby-evolution")),
            ("ruby-travel", lambda: travel(g, starts[0], "ruby-travel")),
            ("ruby-jirachi ruby-jirachi-catch", lambda: jirachi(g, starts[0], "ruby-jirachi", "ruby-jirachi-catch")),
            ("sapphire-default", lambda: sapphire_default(g, starts[1], "sapphire-default")),
            ("sapphire-egg-wander sapphire-egg-catch", lambda: egg(g, "sapphire-default.ss", "sapphire-egg-wander", "sapphire-egg-catch")),
            ("sapphire-evolution", lambda: evolution(g, "sapphire-default.ss", "sapphire-evolution"))]
    jobs += [("bonus-" + f, (lambda f=f: bonus(g, starts[0], FIELDS[f], "bonus-" + f))) for f in FIELDS]
    for names, job in jobs:
        if want and not want & set(names.split()):
            continue
        if names.startswith("sapphire-") and names != "sapphire-default" and not os.path.exists(os.path.join(out, "sapphire-default.ss")):
            sapphire_default(g, starts[1], "sapphire-default")
        ok = job()
        print("%-40s %s" % (names, "ok" if ok else "FAILED"))
    for n in ("steps.txt", "pinball.gba", "pinball.sav"):
        if os.path.exists(os.path.join(out, n)):
            os.remove(os.path.join(out, n))


if __name__ == "__main__":
    main()
