"""Builds the 3DS copy of Baba Is You's data from your own Mac copy of the game.

usage: python3 tools/prepare_sd.py [--small-audio] "<...>/Baba Is You.app" "<SD card>/3ds/baba"

Writes <dest>/Data with:
- the game's scripts, levels, palettes, images, music etc. copied as they are,
- sprites.pak: every sprite PNG in one file (the SD card is slow with thousands of tiny files,
  and each one wastes a whole cluster),
- Sounds/: the sound effects, extracted from the app (see extract_sounds.py).
--small-audio re-encodes music (stereo 32 kHz, ~80 kbps) and sound effects (mono 22 kHz): about a third of the
size, and cheaper for the 3DS to decode. Needs oggdec/oggenc from vorbis-tools (brew install vorbis-tools).
Nothing here comes from this project: it only rearranges files you already own.
"""
import concurrent.futures, os, shutil, struct, subprocess, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_sounds


def is_sprite(rel):
    parts = rel.split("/")
    return rel.endswith(".png") and len(parts) >= 2 and parts[-2] == "Sprites"


MUSIC_ARGS = ["--resample", "32000", "-q", "1"]
SOUND_ARGS = ["--resample", "22050", "--downmix", "-q", "2"]


def reencode(src, out, args):
    """oggdec src | oggenc args -> out (written via a temp file so an interrupted run never leaves half a file)."""
    tmp = out + ".tmp"
    dec = subprocess.Popen(["oggdec", "-Q", "-o", "-", src], stdout=subprocess.PIPE)
    # stderr off: oggenc warns about --downmix on sounds that are already mono; failures still show in the exit code
    enc = subprocess.run(["oggenc", "-Q", *args, "-o", tmp, "-"], stdin=dec.stdout, stderr=subprocess.DEVNULL)
    dec.stdout.close()
    if dec.wait() or enc.returncode:
        raise RuntimeError(f"re-encoding failed: {src}")
    os.replace(tmp, out)
    st = os.stat(src)
    os.utime(out, (st.st_atime, st.st_mtime)) # marks it as encoded from this exact source file


def is_music(rel):
    return rel.endswith(".ogg") and "/Music/" in "/" + rel


def main(app, dest, small_audio=False):
    if small_audio and not (shutil.which("oggdec") and shutil.which("oggenc")):
        sys.exit("--small-audio needs oggdec and oggenc: brew install vorbis-tools")
    src = os.path.join(app, "Contents/Resources/Data")
    if not os.path.isfile(os.path.join(src, "values.lua")):
        sys.exit(f"not a Baba Is You app (no Contents/Resources/Data/values.lua): {app}")
    data = os.path.join(dest, "Data")
    os.makedirs(data, exist_ok=True)

    sprites, music, copied = [], [], 0
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
            if small_audio and is_music(rel):
                music.append((full, out))
            elif not (os.path.exists(out) and os.path.getsize(out) == os.path.getsize(full)):
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

    sounds = os.path.join(data, "Sounds")
    if not small_audio:
        extract_sounds.main(app, sounds)
    else:
        with tempfile.TemporaryDirectory() as raw:
            extract_sounds.main(app, raw)
            os.makedirs(sounds, exist_ok=True)
            jobs = [(os.path.join(raw, n), os.path.join(sounds, n), SOUND_ARGS) for n in os.listdir(raw)]
            # music an earlier --small-audio run already encoded carries its source's exact mtime (plain copies don't)
            jobs += [(src, out, MUSIC_ARGS) for src, out in music
                     if not (os.path.exists(out) and os.path.getmtime(out) == os.path.getmtime(src))]
            with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
                for f in [pool.submit(reencode, *j) for j in jobs]:
                    f.result()
        print(f"re-encoded {len(jobs)} audio files")
    print(f"copied {copied} files, packed {len(sprites)} sprites ({off // 1024} KB) into {data}")


if __name__ == "__main__":
    args = sys.argv[1:]
    small = "--small-audio" in args
    args = [a for a in args if a != "--small-audio"]
    if len(args) != 2:
        sys.exit(__doc__)
    main(args[0], args[1], small)
