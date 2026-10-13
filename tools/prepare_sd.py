"""Builds the 3DS copy of Baba Is You's data from your own Mac copy of the game.

usage: python3 tools/prepare_sd.py "<...>/Baba Is You.app" "<SD card>/3ds/baba"

Writes <dest>/Data with:
- the game's scripts, levels, palettes, images, music etc. copied as they are,
- sprites.pak: every sprite PNG in one file (the SD card is slow with thousands of tiny files,
  and each one wastes a whole cluster),
- Sounds/: the sound effects, extracted from the app (see extract_sounds.py).
Nothing here comes from this project: it only rearranges files you already own.
"""
import os, shutil, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_sounds


def is_sprite(rel):
    parts = rel.split("/")
    return rel.endswith(".png") and len(parts) >= 2 and parts[-2] == "Sprites"


def main(app, dest):
    src = os.path.join(app, "Contents/Resources/Data")
    if not os.path.isfile(os.path.join(src, "values.lua")):
        sys.exit(f"not a Baba Is You app (no Contents/Resources/Data/values.lua): {app}")
    data = os.path.join(dest, "Data")
    os.makedirs(data, exist_ok=True)

    sprites, copied = [], 0
    for root, dirs, files in os.walk(src):
        dirs[:] = [d for d in dirs if d != "Temp"]
        for name in files:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            if is_sprite(rel):
                sprites.append(rel)
                continue
            out = os.path.join(data, rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            if not (os.path.exists(out) and os.path.getsize(out) == os.path.getsize(full)):
                shutil.copyfile(full, out)
            copied += 1

    # "BPK1", u32 count, count x (u32 offset, u32 size, u16 len, name) sorted by name, then the PNG bytes
    sprites.sort()
    names = [s.encode() for s in sprites]
    index_size = 8 + sum(10 + len(n) for n in names)
    blobs, entries, off = [], [], index_size
    for rel, name in zip(sprites, names):
        blob = open(os.path.join(src, rel), "rb").read()
        entries.append(struct.pack("<IIH", off, len(blob), len(name)) + name)
        blobs.append(blob)
        off += len(blob)
    tmp = os.path.join(data, "sprites.pak.tmp")
    with open(tmp, "wb") as f:
        f.write(b"BPK1" + struct.pack("<I", len(names)))
        f.writelines(entries)
        f.writelines(blobs)
    os.replace(tmp, os.path.join(data, "sprites.pak"))

    extract_sounds.main(app, os.path.join(data, "Sounds"))
    print(f"copied {copied} files, packed {len(sprites)} sprites ({off // 1024} KB) into {data}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
