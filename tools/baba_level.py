"""Parse Baba Is You .l level files: ACHTUNG! header, MAP chunk, LAYR chunk with zlib tile layers."""
import struct, sys, zlib

def parse(path):
    b = open(path, "rb").read()
    assert b[:8] == b"ACHTUNG!", "not a .l file"
    version, = struct.unpack_from("<H", b, 8)
    i = b.index(b"LAYR")
    nlayers, = struct.unpack_from("<H", b, i + 8)
    pos, layers = i + 10, []
    for _ in range(nlayers):
        w, h = struct.unpack_from("<II", b, pos)
        # shortcut: skip the per-layer header (offsets/zoom floats) by searching for MAIN; decode it if a renderer needs scroll/zoom
        m = b.index(b"MAIN", pos)
        size, = struct.unpack_from("<I", b, m + 4)
        tiles = struct.unpack(f"<{w*h}H", zlib.decompress(b[m + 8:m + 8 + size]))
        d, extra = m + 8 + size, {}
        if b[d:d + 4] != b"DATA":  # last layer may omit DATA (seen in baba/106level.l)
            layers.append({"w": w, "h": h, "tiles": tiles, "data": extra})
            pos = d
            continue
        # DATA: u8 count, then per entry u32 id + u32 size + zlib bytes (id 0 = facing direction per tile)
        count = b[d + 4]
        q = d + 5
        for _ in range(count):
            did, dsize = struct.unpack_from("<II", b, q)
            extra[did] = zlib.decompress(b[q + 8:q + 8 + dsize])
            assert len(extra[did]) == w * h, (did, len(extra[did]), w, h)
            q += 8 + dsize
        layers.append({"w": w, "h": h, "tiles": tiles, "data": extra})
        pos = q
    return version, layers

if __name__ == "__main__":
    for p in sys.argv[1:]:
        v, layers = parse(p)
        L = layers[0]
        print(p, "version", v, "layers", len(layers), f"{L['w']}x{L['h']}")
        for y in range(L["h"]):
            print("".join("." if t == 0xFFFF else chr(48 + t % 75) for t in L["tiles"][y*L["w"]:(y+1)*L["w"]]))

def tile_names(values_lua):
    """Map tile id -> object name from tileslist in values.lua. Tile id = tile[0] + tile[1]*256."""
    import re
    src = open(values_lua, encoding="utf-8").read()
    src = src[src.index("tileslist ="):]
    out = {}
    for name, x, y in re.findall(r'name = "([^"]+)".*?tile = \{(\d+), (\d+)\}', src, re.S):
        out.setdefault(int(x) + int(y) * 256, name)
    return out
