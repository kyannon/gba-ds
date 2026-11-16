"""Gen 3 Pokemon structs (100 bytes, as in FireRed / LeafGreen / Ruby / Sapphire / Emerald).

build_mon() makes a valid, encrypted party member; checksum_ok() / scan() find
party-like records in a RAM dump without knowing any address.
"""
import struct

ORDERS = ["GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA", "AGEM", "AGME", "AEGM", "AEMG",
          "AMGE", "AMEG", "EGAM", "EGMA", "EAGM", "EAMG", "EMGA", "EMAG", "MGAE", "MGEA",
          "MAGE", "MEGA", "MEAG", "MAEG"]

def text(s, n):
    out = bytearray()
    for c in s:
        if c == " ": out.append(0x00)
        elif "A" <= c <= "Z": out.append(0xBB + ord(c) - 65)
        elif "a" <= c <= "z": out.append(0xD5 + ord(c) - 97)
        elif "0" <= c <= "9": out.append(0xA1 + ord(c) - 48)
        elif c == "-": out.append(0xAE)
        else: raise ValueError(c)
    out.append(0xFF)
    return bytes(out + b"\xFF" * (n - len(out)))[:n]

def build_mon(species, level, name, hp, maxhp, personality, otid=0x12345678, moves=(33, 45, 0, 0),
              pp=(35, 40, 0, 0), exp=0, item=0, status=0, stats=None, otname="RED", ivs=0x3FFFFFFF & 0x1ABCDEF0):
    growth = struct.pack("<HHIBBH", species, item, exp, 0, 70, 0)
    attacks = struct.pack("<4H4B", *moves, *pp)
    evs = struct.pack("<6B5BB", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    misc = struct.pack("<BBHII", 0, 0, (level & 0x7F) | (3 << 7) | (4 << 11), ivs, 0)
    subs = {"G": growth, "A": attacks, "E": evs, "M": misc}
    order = ORDERS[personality % 24]
    data = b"".join(subs[c] for c in order)
    assert len(data) == 48
    csum = sum(struct.unpack("<24H", data)) & 0xFFFF
    key = (personality ^ otid) & 0xFFFFFFFF
    enc = b"".join(struct.pack("<I", w ^ key) for w in struct.unpack("<12I", data))
    if stats is None:
        stats = (maxhp // 2 + 5, maxhp // 2, maxhp // 2, maxhp // 2, maxhp // 2)
    head = struct.pack("<II", personality, otid) + text(name, 10) + bytes([2, 0x02]) + text(otname, 7) + bytes([0])
    head += struct.pack("<HH", csum, 0)
    tail = struct.pack("<IBBHH5H", status, level, 0, hp, maxhp, *stats)
    m = head + enc + tail
    assert len(m) == 100, len(m)
    return m

def decrypt(rec):
    pers, otid = struct.unpack("<II", rec[0:8])
    key = pers ^ otid
    return b"".join(struct.pack("<I", w ^ key) for w in struct.unpack("<12I", rec[0x20:0x50]))

def checksum_ok(rec):
    if len(rec) < 100: return False
    pers, otid = struct.unpack("<II", rec[0:8])
    if pers == 0 and otid == 0: return False
    csum = struct.unpack("<H", rec[0x1C:0x1E])[0]
    data = decrypt(rec)
    if (sum(struct.unpack("<24H", data)) & 0xFFFF) != csum: return False
    return True

def scan(ram, base, step=4, minlevel=1):
    """All 4-aligned offsets in `ram` (mapped at `base`) holding a valid Pokemon record
    with sane level/HP, as (address, species, level, hp, maxhp)."""
    out = []
    for off in range(0, len(ram) - 100 + 1, step):
        rec = ram[off:off + 100]
        if checksum_ok(rec):
            level, = struct.unpack("<B", rec[0x54:0x55])
            hp, maxhp = struct.unpack("<HH", rec[0x56:0x5A])
            if 1 <= level <= 100 and 0 < maxhp < 1000 and hp <= maxhp:
                data = decrypt(rec)
                pers = struct.unpack("<I", rec[0:4])[0]
                order = ORDERS[pers % 24]
                g = data[order.index("G") * 12:][:12]
                species = struct.unpack("<H", g[0:2])[0]
                if 1 <= species <= 411:
                    out.append((base + off, species, level, hp, maxhp))
    return out
