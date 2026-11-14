#!/usr/bin/env python3
"""Generate flight_radar/src/airline_logos.cpp: airline tail logos for the radar.

Downloads the tail-fin logos in Jxck-S/airline-logos (radarbox_logos/<ICAO>.png,
54x54 RGBA). A logo that is really a wide wordmark (Jetstar's "Jet*") is replaced
by the flightaware_logos version when there is one, which is usually the tail
symbol. Each logo is cropped to its visible pixels, centred in a square, shrunk
to 22x22 with area averaging (premultiplied alpha) and
writes them as LVGL LV_IMG_CF_TRUE_COLOR_ALPHA pixels (RGB565 byte-swapped, to
match LV_COLOR_16_SWAP 1 in lv_conf.h, then alpha).

The logos are airline trademarks. The generated file is for your own board only:
never commit it. After generating, run
    git update-index --skip-worktree flight_radar/src/airline_logos.cpp
CI fails any push where the committed file is not the stub.

Pure Python, no Pillow. Downloads are cached in build/airline_logos_cache/.
Usage: tools/gen_airline_logos.py [ICAO ...]   (no codes = every logo available)
"""
import concurrent.futures
import json
import pathlib
import re
import struct
import sys
import urllib.request
import zlib

REPO = "Jxck-S/airline-logos"
FOLDER = "radarbox_logos"
FALLBACK = "flightaware_logos"
SIZE = 22
WORDMARK_ASPECT = 1.6   # visible width / height above this = text, not a tail
ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "flight_radar" / "src" / "airline_logos.cpp"
CACHE = ROOT / "build" / "airline_logos_cache"
UA = {"User-Agent": "flight-radar-logo-generator"}


def fetch(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=30) as r:
        return r.read()


def list_codes():
    entries = json.loads(fetch(f"https://api.github.com/repos/{REPO}/contents/{FOLDER}"))
    codes = sorted(e["name"][:-4] for e in entries
                   if e["type"] == "file" and re.fullmatch(r"[A-Z]{3}\.png", e["name"]))
    if len(entries) >= 1000:
        print("warning: the GitHub listing is capped at 1000 files; some logos may be missing", file=sys.stderr)
    return codes


def download(code, folder=FOLDER):
    path = CACHE / (f"{code}.png" if folder == FOLDER else f"{folder}_{code}.png")
    if not path.exists():
        path.write_bytes(fetch(f"https://raw.githubusercontent.com/{REPO}/main/{folder}/{code}.png"))
    return path.read_bytes()


def visible_box(w, h, rows):
    """Bounding box (x0, y0, x1, y1) of pixels that are not almost transparent, or None."""
    xs = [x for y in range(h) for x in range(w) if rows[y][x][3] > 24]
    ys = [y for y in range(h) for x in range(w) if rows[y][x][3] > 24]
    if not xs:
        return None
    return min(xs), min(ys), max(xs) + 1, max(ys) + 1


def crop_square(w, h, rows):
    """Crops to the visible pixels and pads the short side so the logo fills a square."""
    box = visible_box(w, h, rows)
    if not box:
        return w, h, rows
    x0, y0, x1, y1 = box
    side = max(x1 - x0, y1 - y0)
    clear = (0, 0, 0, 0)
    out = [[clear] * side for _ in range(side)]
    ox, oy = (side - (x1 - x0)) // 2, (side - (y1 - y0)) // 2
    for y in range(y0, y1):
        for x in range(x0, x1):
            out[oy + y - y0][ox + x - x0] = rows[y][x]
    return side, side, out


def is_wordmark(w, h, rows):
    box = visible_box(w, h, rows)
    return bool(box) and (box[2] - box[0]) > WORDMARK_ASPECT * (box[3] - box[1])


def best_logo(code, data):
    """The radarbox tail logo, or the flightaware one when radarbox's is a wordmark."""
    img = decode_png(data)
    if is_wordmark(*img):
        try:
            alt = decode_png(download(code, FALLBACK))
            if not is_wordmark(*alt):
                print(f"  {code}: wordmark, using {FALLBACK}")
                return alt
        except Exception:
            pass
    return img


def decode_png(data):
    """Returns (w, h, rows of (r, g, b, a) tuples). 8-bit, non-interlaced only."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, plte, trns = 8, b"", None, None
    w = h = depth = ctype = interlace = None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    if depth != 8 or interlace:
        raise ValueError(f"unsupported PNG (bit depth {depth}, interlace {interlace})")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * channels
    prev = bytearray(stride)
    rows, i = [], 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):   # undo the per-row filter
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        px = []
        for x in range(w):
            v = line[x * channels:(x + 1) * channels]
            if ctype == 6:
                px.append(tuple(v))
            elif ctype == 2:
                px.append((v[0], v[1], v[2], 255))
            elif ctype == 0:
                px.append((v[0], v[0], v[0], 255))
            elif ctype == 4:
                px.append((v[0], v[0], v[0], v[1]))
            else:   # palette
                r, g, b = plte[v[0]]
                a = trns[v[0]] if trns and v[0] < len(trns) else 255
                px.append((r, g, b, a))
        rows.append(px)
    return w, h, rows


def shrink(w, h, rows, size):
    """Area-average resample to size x size, premultiplied so transparent pixels don't darken edges."""
    out = []
    for oy in range(size):
        y0, y1 = oy * h / size, (oy + 1) * h / size
        line = []
        for ox in range(size):
            x0, x1 = ox * w / size, (ox + 1) * w / size
            r = g = b = a = area = 0.0
            for sy in range(int(y0), min(h, int(y1 + 0.999999))):
                wy = min(y1, sy + 1) - max(y0, sy)
                for sx in range(int(x0), min(w, int(x1 + 0.999999))):
                    wgt = wy * (min(x1, sx + 1) - max(x0, sx))
                    pr, pg, pb, pa = rows[sy][sx]
                    r += pr * pa * wgt
                    g += pg * pa * wgt
                    b += pb * pa * wgt
                    a += pa * wgt
                    area += wgt
            if a > 0:
                line.append((r / a, g / a, b / a, a / area))
            else:
                line.append((0, 0, 0, 0))
        out.append(line)
    return out


def to_lvgl(px):
    """RGB565 byte-swapped (LV_COLOR_16_SWAP 1) followed by alpha, row-major."""
    out = bytearray()
    for line in px:
        for r, g, b, a in line:
            c = (int(r + 0.5) >> 3 << 11) | (int(g + 0.5) >> 2 << 5) | (int(b + 0.5) >> 3)
            out += bytes((c >> 8, c & 0xFF, min(255, int(a + 0.5))))
    return out


def main():
    CACHE.mkdir(parents=True, exist_ok=True)
    codes = sorted(set(a.upper() for a in sys.argv[1:])) or list_codes()
    print(f"{len(codes)} airlines")
    logos = {}
    with concurrent.futures.ThreadPoolExecutor(8) as pool:
        for code, result in zip(codes, pool.map(lambda c: _safe(download, c), codes)):
            if isinstance(result, Exception):
                print(f"  skip {code}: {result}", file=sys.stderr)
                continue
            try:
                logos[code] = to_lvgl(shrink(*crop_square(*best_logo(code, result)), SIZE))
            except Exception as e:   # odd formats: skip, never abort the whole table
                print(f"  skip {code}: {e}", file=sys.stderr)

    lines = [
        "// airline_logos.cpp: GENERATED by tools/gen_airline_logos.py. Do not edit.",
        "// Airline trademarks from Jxck-S/airline-logos, for your own board only.",
        "// NEVER COMMIT THIS FILE: restore the stub with",
        "//   git checkout -- flight_radar/src/airline_logos.cpp",
        '#include "airline_logos.h"',
        "",
    ]
    for i, code in enumerate(sorted(logos)):
        data = logos[code]
        assert len(data) == SIZE * SIZE * 3
        hexes = ",".join(f"0x{v:02x}" for v in data)
        lines.append(f"static const uint8_t L{i}[] = {{{hexes}}};   // {code}")
    lines.append("")
    lines.append("const AirlineLogoRow AIRLINE_LOGOS[] = {")
    for i, code in enumerate(sorted(logos)):
        lines.append(f'  {{"{code}", L{i}}},')
    if not logos:
        lines.append('  {"", nullptr},')
    lines.append("};")
    lines.append(f"const size_t AIRLINE_LOGOS_COUNT = {len(logos)};")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT.relative_to(ROOT)}: {len(logos)} logos, {len(logos) * SIZE * SIZE * 3 // 1024} KB of pixels")
    print("now run: git update-index --skip-worktree flight_radar/src/airline_logos.cpp")


def _safe(fn, arg):
    try:
        return fn(arg)
    except Exception as e:
        return e


if __name__ == "__main__":
    main()
