#!/usr/bin/env python3
"""The checksum GBA DS knows a patch file by (docs/patches.md): CRC32 of the
file without its last four bytes (a UPS or BPS ends with its own checksum,
so the whole file's CRC32 is the same for every one of them).

  tools/patch_id.py FILE...
"""
import sys, zlib

for path in sys.argv[1:]:
    b = open(path, "rb").read()
    print("%08x %d %s" % (zlib.crc32(b[:-4]) if len(b) > 4 else 0, len(b), path))
