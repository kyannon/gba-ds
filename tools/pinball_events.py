#!/usr/bin/env python3
"""Builds Pokemon Pinball: Ruby & Sapphire (USA, BPPE) save states for the
game's events, by steering the game through its own code paths with the
harness (no in-game play needed):

  tools/pinball_events.py ROM FIXTURE_STATES_DIR OUTDIR [NAME...]

ROM is the USA release (gba-ds-tests' roms/BPPE.zip, unzipped); the
harness runs a copy of it in OUTDIR. FIXTURE_STATES_DIR is gba-ds-tests' states/pokemon-pinball (us-ruby-top.ss
and us-sapphire-play.ss are the starting points). Writes OUTDIR/NAME.ss for
each of (default: all):

  ruby-egg-wander     egg hatch mode, the hatched Pokemon wandering
  ruby-egg-catch      ... hit twice: its catch animation
  ruby-evolution      EVO mode, three items collected, the ball in the
                      centre hole: the evolution scene about to play
  sapphire-default    Sapphire, the ball launched and in play
  sapphire-egg-wander, sapphire-egg-catch, sapphire-evolution
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
    L += ["poke %#x 2 42" % (S + 0x6F8 + 2 * i) for i in range(6)] + ["poke %#x 2 0x2A2A" % (S + 0x6EC), "hold A 4"]
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


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    rom, fixtures, out = (os.path.abspath(a) for a in sys.argv[1:4])
    want = set(sys.argv[4:])
    os.makedirs(out, exist_ok=True)
    for n in ("us-ruby-top.ss", "us-sapphire-play.ss"):
        shutil.copy(os.path.join(fixtures, n), out)
    # (a copy: the harness writes the battery save beside the ROM)
    work_rom = os.path.join(out, "pinball.gba")
    shutil.copy(rom, work_rom)
    g = Game(work_rom, out)
    jobs = [("ruby-egg-wander ruby-egg-catch", lambda: egg(g, "us-ruby-top.ss", "ruby-egg-wander", "ruby-egg-catch")),
            ("ruby-evolution", lambda: evolution(g, "us-ruby-top.ss", "ruby-evolution")),
            ("sapphire-default", lambda: sapphire_default(g, "us-sapphire-play.ss", "sapphire-default")),
            ("sapphire-egg-wander sapphire-egg-catch", lambda: egg(g, "sapphire-default.ss", "sapphire-egg-wander", "sapphire-egg-catch")),
            ("sapphire-evolution", lambda: evolution(g, "sapphire-default.ss", "sapphire-evolution"))]
    jobs += [("bonus-" + f, (lambda f=f: bonus(g, "us-ruby-top.ss", FIELDS[f], "bonus-" + f))) for f in FIELDS]
    for names, job in jobs:
        if want and not want & set(names.split()):
            continue
        if names.startswith("sapphire-") and names != "sapphire-default" and not os.path.exists(os.path.join(out, "sapphire-default.ss")):
            sapphire_default(g, "us-sapphire-play.ss", "sapphire-default")
        ok = job()
        print("%-40s %s" % (names, "ok" if ok else "FAILED"))
    for n in ("steps.txt", "pinball.gba", "pinball.sav", "us-ruby-top.ss", "us-sapphire-play.ss"):
        if os.path.exists(os.path.join(out, n)):
            os.remove(os.path.join(out, n))


if __name__ == "__main__":
    main()
