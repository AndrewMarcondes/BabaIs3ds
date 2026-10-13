"""Extract Baba Is You's sound effects from the Mac build (Chowdren) into <out>/<name>.ogg.

usage: python3 tools/extract_sounds.py "<...>/Baba Is You.app" <out dir>

Assets.dat starts with a table of (u32 offset, u32 size) per asset; each sound is [u32 2][u32 len][ogg].
Sound names live in the executable as a gperf perfect-hash table: a 739-slot array of name pointers
and a parallel int32 array of sound ids. Sound id N is asset FIRST_SOUND + N.
"""
import os, struct, sys

NAMES_OFF, SLOTS, IDS_OFF = 6890944, 739, 0x61E6B0  # file offsets in Contents/MacOS/Chowdren (x86_64, __TEXT at 0x100000000)
FIRST_SOUND, NSOUNDS = 3206, 245
BASE = 0x100000000

def main(app, out):
    exe = open(os.path.join(app, "Contents/MacOS/Chowdren"), "rb").read()
    dat = open(os.path.join(app, "Contents/Resources/Assets.dat"), "rb").read()

    def cstr(va):
        o = va - BASE
        if not 0 <= o < len(exe):
            raise ValueError("bad pointer: different game version?")
        return exe[o:exe.index(b"\0", o)].decode("latin1")

    names = {}
    for i in range(SLOTS):
        name = cstr(struct.unpack_from("<Q", exe, NAMES_OFF + i * 8)[0])
        if name:
            names[struct.unpack_from("<i", exe, IDS_OFF + i * 4)[0]] = name
    assert sorted(names) == list(range(NSOUNDS)), "sound table mismatch: different game version?"

    os.makedirs(out, exist_ok=True)
    for sid, name in names.items():
        off, size = struct.unpack_from("<II", dat, (FIRST_SOUND + sid) * 8)
        kind, length = struct.unpack_from("<II", dat, off)
        ogg = dat[off + 8:off + 8 + length]
        assert kind == 2 and ogg[:4] == b"OggS" and length == size - 8, f"unexpected asset for {name}"
        with open(os.path.join(out, name + ".ogg"), "wb") as f:
            f.write(ogg)
    print(f"extracted {len(names)} sounds to {out}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
