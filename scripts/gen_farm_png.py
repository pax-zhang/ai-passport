#!/usr/bin/env python3
"""24x24 recognizable farm stickers. Transparent, thick ink, flat fill."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "farm"
SZ = 24

INK = (31, 24, 16, 255)
ROOF = (255, 140, 122, 255)
WALL = (255, 200, 87, 255)
DOOR = (91, 70, 54, 255)
WIN = (126, 200, 227, 255)
BAG = (232, 160, 74, 255)
BAG2 = (196, 122, 42, 255)
SKIN = (255, 200, 140, 255)
SKIN2 = (255, 184, 120, 255)
GEAR = (196, 160, 106, 255)
SEED = (138, 90, 40, 255)
SEED_H = (196, 144, 72, 255)
SOIL = (139, 105, 64, 255)
GD = (58, 122, 40, 255)
G = (92, 154, 58, 255)
GL = (123, 182, 97, 255)
WATER = (107, 182, 224, 255)
WH = (231, 243, 251, 255)
BUG = (90, 64, 48, 255)
EYE = (255, 243, 224, 255)
GOLD = (255, 200, 87, 255)
CARROT = (232, 120, 48, 255)
TOM = (224, 48, 48, 255)
TOMH = (255, 160, 140, 255)
CORN = (240, 192, 80, 255)
KERN = (200, 150, 40, 255)
B1 = (232, 155, 200, 255)
B2 = (216, 80, 140, 255)
B3 = (232, 120, 168, 255)
MG = (46, 140, 64, 255)
MD = (32, 96, 44, 255)
MR = (224, 64, 80, 255)
MW = (255, 243, 224, 255)
DRY = (232, 160, 74, 255)
PITH = (255, 236, 210, 255)


def new() -> Image.Image:
    return Image.new("RGBA", (SZ, SZ), (0, 0, 0, 0))


def save(im: Image.Image, name: str) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    im.save(OUT / f"{name}.png", "PNG")


def punch_disk(im: Image.Image, cx: int, cy: int, r: int) -> None:
    px = im.load()
    for y in range(max(0, cy - r), min(SZ, cy + r + 1)):
        for x in range(max(0, cx - r), min(SZ, cx + r + 1)):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                px[x, y] = (0, 0, 0, 0)


def sunseed(d: ImageDraw.ImageDraw, cx: int, cy: int) -> None:
    """Striped sunflower seed — the usual seed silhouette."""
    d.polygon(
        [(cx, cy - 8), (cx + 5, cy - 1), (cx + 4, cy + 7), (cx - 4, cy + 7), (cx - 5, cy - 1)],
        fill=(72, 48, 24, 255),
        outline=INK,
    )
    d.polygon(
        [(cx, cy - 6), (cx + 2, cy - 1), (cx + 1, cy + 5), (cx - 1, cy + 5), (cx - 2, cy - 1)],
        fill=(245, 228, 176, 255),
    )


def ico_home() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.polygon([(12, 2), (22, 11), (2, 11)], fill=ROOF, outline=INK)
    d.rectangle((4, 11, 19, 22), fill=WALL, outline=INK)
    d.rectangle((10, 15, 13, 22), fill=DOOR, outline=INK)
    d.rectangle((6, 13, 8, 16), fill=WIN, outline=INK)
    d.rectangle((15, 13, 17, 16), fill=WIN, outline=INK)
    return im


def ico_bag() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.arc((7, 2, 16, 12), 200, 340, fill=INK, width=2)
    d.rounded_rectangle((4, 8, 19, 22), 4, fill=BAG, outline=INK)
    d.rectangle((4, 8, 19, 13), fill=BAG2, outline=INK)
    d.ellipse((10, 15, 13, 18), fill=INK)
    return im


def ico_steal() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.ellipse((11, 2, 22, 13), fill=TOM, outline=INK)
    d.polygon([(16, 2), (14, 0), (16, 3), (18, 0), (17, 3)], fill=GD, outline=INK)
    d.rounded_rectangle((1, 10, 16, 22), 4, fill=SKIN2, outline=INK)
    d.rectangle((0, 12, 4, 18), fill=SKIN2, outline=INK)
    return im


def ico_set() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.rectangle((10, 2, 13, 7), fill=GEAR, outline=INK)
    d.rectangle((10, 16, 13, 21), fill=GEAR, outline=INK)
    d.rectangle((2, 10, 7, 13), fill=GEAR, outline=INK)
    d.rectangle((16, 10, 21, 13), fill=GEAR, outline=INK)
    d.ellipse((5, 5, 18, 18), fill=GEAR, outline=INK)
    punch_disk(im, 11, 11, 3)
    d = ImageDraw.Draw(im)
    d.ellipse((8, 8, 15, 15), outline=INK)
    return im


def ico_seed() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    sunseed(d, 12, 12)
    return im


def ico_plant() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.ellipse((2, 16, 21, 23), fill=SOIL, outline=INK)
    d.line((11, 8, 11, 18), fill=GD, width=2)
    d.ellipse((3, 6, 12, 14), fill=G, outline=INK)
    d.ellipse((11, 3, 20, 12), fill=GL, outline=INK)
    return im


def ico_water() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.polygon([(12, 1), (21, 12), (18, 21), (6, 21), (3, 12)], fill=WATER, outline=INK)
    d.ellipse((4, 11, 20, 22), fill=WATER, outline=INK)
    d.ellipse((7, 13, 11, 16), fill=WH)
    return im


def ico_weed() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.polygon([(3, 22), (6, 4), (9, 22)], fill=G, outline=INK)
    d.polygon([(8, 22), (12, 2), (15, 22)], fill=GD, outline=INK)
    d.polygon([(14, 22), (18, 5), (21, 22)], fill=GL, outline=INK)
    return im


def ico_pest() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.line((4, 12, 1, 8), fill=INK, width=2)
    d.line((19, 12, 22, 8), fill=INK, width=2)
    d.line((4, 17, 1, 20), fill=INK, width=2)
    d.line((19, 17, 22, 20), fill=INK, width=2)
    d.ellipse((4, 10, 19, 21), fill=BUG, outline=INK)
    d.ellipse((6, 4, 12, 12), fill=BUG, outline=INK)
    d.ellipse((11, 4, 17, 12), fill=BUG, outline=INK)
    d.point((8, 7), fill=EYE)
    d.point((14, 7), fill=EYE)
    return im


def ico_cut() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.arc((3, 2, 22, 20), 200, 40, fill=GOLD, width=5)
    d.rectangle((3, 14, 8, 22), fill=SEED, outline=INK)
    return im


def ico_hand() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((6, 12, 18, 22), 4, fill=SKIN2, outline=INK)
    d.rounded_rectangle((5, 5, 8, 14), 2, fill=SKIN, outline=INK)
    d.rounded_rectangle((9, 3, 12, 14), 2, fill=SKIN, outline=INK)
    d.rounded_rectangle((13, 4, 16, 14), 2, fill=SKIN, outline=INK)
    d.rounded_rectangle((17, 7, 20, 15), 2, fill=SKIN, outline=INK)
    return im


def crop_seed() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.ellipse((2, 16, 21, 23), fill=SOIL, outline=INK)
    sunseed(d, 12, 10)
    return im


def crop_sprout() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.line((11, 10, 11, 22), fill=GD, width=2)
    d.ellipse((3, 8, 12, 16), fill=G, outline=INK)
    d.ellipse((11, 4, 20, 14), fill=GL, outline=INK)
    return im


def crop_carrot() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.polygon([(12, 22), (6, 8), (18, 8)], fill=CARROT, outline=INK)
    d.polygon([(12, 8), (7, 1), (11, 8)], fill=G, outline=INK)
    d.polygon([(12, 8), (12, 1), (15, 8)], fill=GD, outline=INK)
    d.polygon([(12, 8), (17, 1), (18, 8)], fill=GL, outline=INK)
    return im


def crop_cabbage() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.ellipse((1, 6, 13, 20), fill=G, outline=INK)
    d.ellipse((10, 5, 22, 20), fill=GL, outline=INK)
    d.ellipse((5, 8, 18, 22), fill=G, outline=INK)
    d.ellipse((8, 11, 15, 18), fill=(181, 220, 140, 255), outline=INK)
    return im


def crop_tomato() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.ellipse((3, 6, 20, 22), fill=TOM, outline=INK)
    d.ellipse((6, 9, 11, 13), fill=TOMH)
    d.polygon([(12, 7), (8, 2), (10, 7), (12, 3), (14, 7), (16, 2), (12, 7)], fill=GD, outline=INK)
    return im


def crop_corn() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.polygon([(7, 10), (1, 5), (6, 17)], fill=G, outline=INK)
    d.polygon([(16, 10), (22, 5), (17, 17)], fill=GL, outline=INK)
    d.ellipse((7, 2, 16, 20), fill=CORN, outline=INK)
    d.rectangle((10, 19, 13, 23), fill=GD, outline=INK)
    for y in range(5, 18, 3):
        d.point((10, y), fill=KERN)
        d.point((13, y + 1), fill=KERN)
    return im


def crop_berry() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.line((11, 2, 11, 8), fill=GD, width=2)
    d.ellipse((7, 1, 12, 6), fill=G, outline=INK)
    d.ellipse((11, 1, 16, 6), fill=GL, outline=INK)
    d.ellipse((3, 8, 12, 17), fill=B1, outline=INK)
    d.ellipse((11, 7, 20, 16), fill=B2, outline=INK)
    d.ellipse((7, 13, 16, 22), fill=B3, outline=INK)
    return im


def crop_melon() -> Image.Image:
    im = new()
    d = ImageDraw.Draw(im)
    d.polygon([(12, 2), (1, 21), (22, 21)], fill=MR, outline=INK)
    d.polygon([(1, 21), (22, 21), (20, 17), (3, 17)], fill=MG, outline=INK)
    d.line((4, 17, 19, 17), fill=MW, width=1)
    d.ellipse((8, 10, 10, 13), fill=INK)
    d.ellipse((12, 7, 14, 10), fill=INK)
    d.ellipse((15, 11, 17, 14), fill=INK)
    return im


def haz_weed() -> Image.Image:
    return ico_weed()


def haz_pest() -> Image.Image:
    return ico_pest()


def haz_dry() -> Image.Image:
    """Water-drop badge: planted plots start thirsty, not dead."""
    im = new()
    d = ImageDraw.Draw(im)
    d.polygon([(12, 2), (21, 14), (12, 22), (3, 14)], fill=WATER, outline=INK)
    d.ellipse((4, 10, 20, 22), fill=WATER, outline=INK)
    d.ellipse((7, 13, 11, 16), fill=WH)
    return im


FNS = {
    "ico_home": ico_home,
    "ico_bag": ico_bag,
    "ico_steal": ico_steal,
    "ico_set": ico_set,
    "ico_seed": ico_seed,
    "ico_plant": ico_plant,
    "ico_water": ico_water,
    "ico_weed": ico_weed,
    "ico_pest": ico_pest,
    "ico_cut": ico_cut,
    "ico_hand": ico_hand,
    "crop_seed": crop_seed,
    "crop_sprout": crop_sprout,
    "crop_carrot": crop_carrot,
    "crop_cabbage": crop_cabbage,
    "crop_tomato": crop_tomato,
    "crop_corn": crop_corn,
    "crop_berry": crop_berry,
    "crop_melon": crop_melon,
    "haz_weed": haz_weed,
    "haz_pest": haz_pest,
    "haz_dry": haz_dry,
}


def main() -> None:
    for name, fn in FNS.items():
        save(fn(), name)
    print(f"wrote {len(FNS)} 24x24 stickers in {OUT}")


if __name__ == "__main__":
    main()
