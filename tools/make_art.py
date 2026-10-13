"""Draws the original home-menu art for the .cia: meta/icon.png (48x48), meta/banner.png (256x128) and
meta/banner.wav (a short chime). No game assets are used, so these can be committed and shared.

usage: python3 tools/make_art.py
"""
import math, os, struct, wave, zlib

# 5x7 pixel font, just the letters used
FONT = {
    "B": ["1111.", "1...1", "1...1", "1111.", "1...1", "1...1", "1111."],
    "A": [".111.", "1...1", "1...1", "11111", "1...1", "1...1", "1...1"],
    "3": ["1111.", "....1", "....1", ".111.", "....1", "....1", "1111."],
    "D": ["1111.", "1...1", "1...1", "1...1", "1...1", "1...1", "1111."],
    "S": [".1111", "1....", "1....", ".111.", "....1", "....1", "1111."],
}
BG, EDGE = (24, 26, 38), (40, 44, 62)
PINK, YELLOW, WHITE, DARK = (217, 57, 106), (237, 226, 133), (255, 255, 255), (21, 24, 31)


def canvas(w, h, c):
    return [[c] * w for _ in range(h)]


def rect(img, x, y, w, h, c):
    for j in range(y, y + h):
        for i in range(x, x + w):
            img[j][i] = c


def text(img, s, x, y, scale, c, gap=None):
    gap = scale if gap is None else gap
    for ch in s:
        for j, row in enumerate(FONT[ch]):
            for i, bit in enumerate(row):
                if bit == "1":
                    rect(img, x + i * scale, y + j * scale, scale, scale, c)
        x += 5 * scale + gap


def tile(img, x, y, w, h, c):
    rect(img, x, y, w, h, c)
    rect(img, x, y + h - 2, w, 2, tuple(v * 3 // 4 for v in c)) # a little depth at the bottom


def png(path, img):
    raw = b"".join(b"\0" + bytes(v for px in row for v in px) for row in img)
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", len(img[0]), len(img), 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def icon():
    img = canvas(48, 48, BG)
    rect(img, 0, 0, 48, 2, EDGE)
    tile(img, 1, 5, 46, 20, PINK)
    text(img, "BABA", 2, 8, 2, WHITE, gap=1) # 4 x 10px letters + 3 gaps = 43px
    tile(img, 6, 27, 36, 19, YELLOW)
    text(img, "3DS", 8, 30, 2, DARK, gap=1)
    return img


def banner():
    img = canvas(256, 128, BG)
    for x in range(0, 256, 16): # faint grid, like a level floor
        rect(img, x, 0, 1, 128, EDGE)
    for y in range(0, 128, 16):
        rect(img, 0, y, 256, 1, EDGE)
    tile(img, 24, 20, 208, 48, PINK)
    text(img, "BABA", 52, 26, 6, WHITE)
    tile(img, 72, 76, 112, 36, YELLOW)
    text(img, "3DS", 84, 80, 4, DARK)
    return img


def chime(path, rate=32000):
    # two soft notes (E5 then A5), well under the 3s banner sound limit
    out = []
    for start, freq in ((0.0, 659.25), (0.18, 880.0)):
        for n in range(int(rate * 0.9)):
            t = n / rate
            out.append((int((start) * rate) + n, math.sin(2 * math.pi * freq * t) * math.exp(-t * 5) * 0.35))
    frames = [0.0] * (int(rate * 1.2))
    for i, v in out:
        if i < len(frames):
            frames[i] += v
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v)) * 32767)) for v in frames))


if __name__ == "__main__":
    meta = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "meta")
    os.makedirs(meta, exist_ok=True)
    png(os.path.join(meta, "icon.png"), icon())
    png(os.path.join(meta, "banner.png"), banner())
    chime(os.path.join(meta, "banner.wav"))
    print("wrote meta/icon.png, meta/banner.png, meta/banner.wav")
