#!/usr/bin/env python3
"""RetroArch save state files, for making and checking test fixtures (issue #39, #78).

A RetroArch state wraps the core's own bytes ("MEM " block of a "RASTATE" file), usually
compressed as RZIP: "#RZIPv" version "#", u32 chunk size, u64 size, then each chunk as a
u32 size and its data. Version 1 is zlib's deflate, version 2 zstd (RetroArch 1.21 and
later, "Save State Compression"). Same layout as src/emu.c reads and writes.

  retroarch_state.py wrap IN.ss OUT.state [--rzip 1|2|none]   mGBA's state -> RetroArch's
  retroarch_state.py unwrap IN.state OUT.ss                   RetroArch's -> mGBA's (any RZIP)
  retroarch_state.py info FILE                                what a file is
  retroarch_state.py forge-bios IN.ss OUT.ss [--pc 0x138] [--hle]
        the state as if it had been saved by a core with the official GBA BIOS while the CPU
        was inside it (the BIOS checksum 0xBAAE187F, the program counter in the BIOS: what
        mGBA refuses when the core runs without that BIOS, issue #78); --hle: as if saved
        without it, for a core that has it. Not a real BIOS state: just the two fields.

zstd (version 2) goes through the system's libzstd (ctypes), so no Python package is needed.
Run with python3 -I.
"""
import ctypes
import ctypes.util
import struct
import sys
import zlib

CHUNK = 128 * 1024  # (RetroArch's)
BIOS_CHECKSUM = 0xBAAE187F  # (mGBA's GBA_BIOS_CHECKSUM: the official BIOS)
BIOS_AT, PC_AT = 4, 32 + 15 * 4  # GBASerializedState: biosChecksum; cpu.gprs[15]


def _zstd():
    name = ctypes.util.find_library("zstd") or "libzstd.so.1"
    lib = ctypes.CDLL(name)
    lib.ZSTD_compressBound.restype = ctypes.c_size_t
    lib.ZSTD_compressBound.argtypes = [ctypes.c_size_t]
    lib.ZSTD_compress.restype = ctypes.c_size_t
    lib.ZSTD_compress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    lib.ZSTD_decompress.restype = ctypes.c_size_t
    lib.ZSTD_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    lib.ZSTD_isError.restype = ctypes.c_uint
    lib.ZSTD_isError.argtypes = [ctypes.c_size_t]
    return lib


def zstd_compress(data, level=3):
    lib = _zstd()
    cap = lib.ZSTD_compressBound(len(data))
    out = ctypes.create_string_buffer(cap)
    n = lib.ZSTD_compress(out, cap, data, len(data), level)
    if lib.ZSTD_isError(n):
        raise SystemExit("zstd: compress failed")
    return out.raw[:n]


def zstd_decompress(data, size):
    lib = _zstd()
    out = ctypes.create_string_buffer(size)
    n = lib.ZSTD_decompress(out, size, data, len(data))
    if lib.ZSTD_isError(n) or n != size:
        raise SystemExit("zstd: decompress failed")
    return out.raw


def rzip_pack(data, version):
    out = bytearray(b"#RZIPv" + bytes([version]) + b"#")
    out += struct.pack("<IQ", CHUNK, len(data))
    for off in range(0, len(data), CHUNK):
        chunk = data[off:off + CHUNK]
        z = zlib.compress(chunk, 6) if version == 1 else zstd_compress(chunk)
        out += struct.pack("<I", len(z)) + z
    return bytes(out)


def rzip_unpack(b):
    version = b[6]
    chunk, total = struct.unpack_from("<IQ", b, 8)
    out, at = bytearray(), 20
    while len(out) < total:
        (zn,) = struct.unpack_from("<I", b, at)
        z = b[at + 4:at + 4 + zn]
        want = min(chunk, total - len(out))
        out += zlib.decompress(z) if version == 1 else zstd_decompress(z, want)
        at += 4 + zn
    return bytes(out), version


def rastate_wrap(core):
    pad = (len(core) + 7) & ~7
    return b"RASTATE\x01" + b"MEM " + struct.pack("<I", len(core)) + core + bytes(pad - len(core)) + b"END " + bytes(4)


def rastate_unwrap(b):
    if not b.startswith(b"RASTATE\x01"):
        return b
    at = 8
    while at + 8 <= len(b):
        tag, sz = b[at:at + 4], struct.unpack_from("<I", b, at + 4)[0]
        if tag == b"END ":
            break
        if tag == b"MEM ":
            return b[at + 8:at + 8 + sz]
        at += 8 + ((sz + 7) & ~7)
    raise SystemExit("no MEM block")


def unwrap(b):
    if b.startswith(b"#RZIPv") and b[7:8] == b"#":
        b, _ = rzip_unpack(b)
    return rastate_unwrap(b)


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    cmd = argv[1]
    opt = lambda name, d: argv[argv.index(name) + 1] if name in argv else d
    if cmd == "wrap":
        core = open(argv[2], "rb").read()
        mode = opt("--rzip", "1")
        out = rastate_wrap(core)
        if mode != "none":
            out = rzip_pack(out, int(mode))
        open(argv[3], "wb").write(out)
    elif cmd == "unwrap":
        open(argv[3], "wb").write(unwrap(open(argv[2], "rb").read()))
    elif cmd == "info":
        b = open(argv[2], "rb").read()
        kind = "RZIP v%d" % b[6] if b.startswith(b"#RZIPv") else "uncompressed"
        core = unwrap(b)
        magic, bios, crc = struct.unpack_from("<III", core, 0)
        pc = struct.unpack_from("<I", core, PC_AT)[0]
        print("%s, %s, core %d bytes, state magic %08x, BIOS checksum %08x, ROM CRC32 %08x, PC %08x"
              % (argv[2], kind, len(core), magic, bios, crc, pc))
    elif cmd == "forge-bios":
        s = bytearray(open(argv[2], "rb").read())
        if "--hle" in argv:
            struct.pack_into("<I", s, PC_AT, int(opt("--pc", "0x138"), 0))  # (its own BIOS checksum stays)
        else:
            struct.pack_into("<I", s, BIOS_AT, BIOS_CHECKSUM)
            struct.pack_into("<I", s, PC_AT, int(opt("--pc", "0x138"), 0))
        open(argv[3], "wb").write(s)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
