#!/usr/bin/env python3
from collections import deque
from pathlib import Path

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "farm"
GEN = Path.home() / ".cursor/projects/Users-pax-Documents-Code-ai-passport-farm/assets"

JOBS = [
    ("gen_ico_home.png", "ico_home.png", 28, 28),
    ("gen_ico_bag.png", "ico_bag.png", 28, 28),
    ("gen_ico_steal.png", "ico_steal.png", 28, 28),
    ("gen_ico_set.png", "ico_set.png", 28, 28),
    ("gen_ico_hand.png", "ico_hand.png", 28, 28),
    ("gen_ico_seed.png", "ico_seed.png", 28, 28),
    ("gen_ico_plant.png", "ico_plant.png", 28, 28),
    ("gen_ico_water.png", "ico_water.png", 28, 28),
    ("gen_ico_weed2.png", "ico_weed.png", 28, 28),
    ("gen_ico_pest.png", "ico_pest.png", 28, 28),
    ("gen_ico_cut.png", "ico_cut.png", 28, 28),
    ("gen_crop_seed.png", "crop_seed.png", 40, 32),
    ("gen_crop_sprout.png", "crop_sprout.png", 40, 32),
    ("gen_crop_dead.png", "crop_dead.png", 40, 32),
    ("gen_crop_carrot.png", "crop_carrot.png", 40, 32),
    ("gen_crop_cabbage.png", "crop_cabbage.png", 40, 32),
    ("gen_crop_tomato.png", "crop_tomato.png", 40, 32),
    ("gen_crop_corn.png", "crop_corn.png", 40, 32),
    ("gen_crop_berry.png", "crop_berry.png", 40, 32),
    ("gen_crop_melon.png", "crop_melon.png", 40, 32),
    ("gen_ico_weed2.png", "haz_weed.png", 20, 20),
    ("gen_haz_pest.png", "haz_pest.png", 20, 20),
    ("gen_haz_dry.png", "haz_dry.png", 20, 20),
    ("gen_plot_dirt.png", "plot_dirt.png", 36, 36),
]


def is_lawn(r, g, b):
    if r > g + 10 and r > b + 12:
        return False
    if min(r, g, b) > 210:
        return False
    if g >= r - 6 and g > b + 8 and g > 70 and b < 170:
        return True
    if g > 130 and r > 100 and b < 130 and g >= r - 25:
        return True
    return False


def knock(im):
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    barrier = Image.new("L", (w, h))
    bp = barrier.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a >= 8 and min(r, g, b) > 190 and max(r, g, b) - min(r, g, b) < 70:
                bp[x, y] = 255
    radius = max(3, min(w, h) // 80)
    if radius % 2 == 0:
        radius += 1
    barrier = barrier.filter(ImageFilter.MaxFilter(radius))
    bp = barrier.load()
    mark = [[False] * w for _ in range(h)]
    q = deque()

    def border(x, y):
        return bp[x, y] != 0

    for x in range(w):
        q.append((x, 0))
        q.append((x, h - 1))
    for y in range(h):
        q.append((0, y))
        q.append((w - 1, y))
    while q:
        x, y = q.popleft()
        if x < 0 or y < 0 or x >= w or y >= h or mark[y][x]:
            continue
        if border(x, y):
            continue
        mark[y][x] = True
        q.append((x + 1, y))
        q.append((x - 1, y))
        q.append((x, y + 1))
        q.append((x, y - 1))
    for y in range(h):
        for x in range(w):
            if mark[y][x]:
                px[x, y] = (0, 0, 0, 0)
    return im


def keep_largest(im):
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    seen = [[False] * w for _ in range(h)]
    best = []
    for y in range(h):
        for x in range(w):
            if seen[y][x] or px[x, y][3] < 8:
                continue
            blob = []
            q = deque([(x, y)])
            seen[y][x] = True
            while q:
                cx, cy = q.popleft()
                blob.append((cx, cy))
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    xx, yy = cx + dx, cy + dy
                    if xx < 0 or yy < 0 or xx >= w or yy >= h or seen[yy][xx]:
                        continue
                    if px[xx, yy][3] < 8:
                        continue
                    seen[yy][xx] = True
                    q.append((xx, yy))
            if len(blob) > len(best):
                best = blob
    keep = set(best)
    for y in range(h):
        for x in range(w):
            if (x, y) not in keep:
                px[x, y] = (0, 0, 0, 0)
    return im


def trim(im, pad=2):
    box = im.split()[-1].getbbox()
    if not box:
        return im
    x0, y0, x1, y1 = box
    return im.crop((
        max(0, x0 - pad), max(0, y0 - pad),
        min(im.width, x1 + pad), min(im.height, y1 + pad),
    ))


def harden(im):
    im = im.convert("RGBA")
    w, h = im.size
    src = im.copy()
    sp = src.load()
    px = im.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = sp[x, y]
            if a < 64:
                px[x, y] = (0, 0, 0, 0)
                continue
            edge = False
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                xx, yy = x + dx, y + dy
                if xx < 0 or yy < 0 or xx >= w or yy >= h or sp[xx, yy][3] < 64:
                    edge = True
                    break
            if edge and (a < 180 or (g > r + 6 and g > b and (r + g + b) < 630)):
                px[x, y] = (0, 0, 0, 0)
            elif edge and (r + g + b) > 570:
                px[x, y] = (255, 255, 255, 255)
            elif a > 180:
                px[x, y] = (r, g, b, 255)
    return im


def fit(im, tw, th):
    im = trim(im)
    if im.width < 2 or im.height < 2:
        return Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    scale = min(tw / im.width, th / im.height)
    nw = max(1, int(im.width * scale))
    nh = max(1, int(im.height * scale))
    im = im.resize((nw, nh), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    canvas.paste(im, ((tw - nw) // 2, (th - nh) // 2), im)
    return harden(canvas)


def src_path(name):
    p = GEN / name
    if p.exists():
        return p
    p = OUT / ("_src_" + name.replace("gen_", ""))
    return p


def main():
    missing = []
    for src_name, dst_name, w, h in JOBS:
        p = src_path(src_name)
        if not p.exists():
            missing.append(src_name)
            continue
        im = Image.open(p).convert("RGBA")
        if max(im.size) > 256:
            im.thumbnail((256, 256), Image.Resampling.LANCZOS)
        im = keep_largest(knock(im))
        out = fit(im, w, h)
        out.save(OUT / dst_name)
        print(dst_name, out.size)
    if missing:
        raise SystemExit("missing: " + ", ".join(missing))


if __name__ == "__main__":
    main()
