#!/usr/bin/env python3
"""Convert assets/farm/*.png (RGBA, no background) to LVGL RGB565A8 C."""

from __future__ import annotations

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "assets" / "farm"
OUT = ROOT / "main" / "app_farm_img.c"
HDR = ROOT / "main" / "app_farm_img.h"

# name, array symbol, w, h
STICKERS = [
    ("ico_home", "app_farm_ico_home", 28, 28),
    ("ico_bag", "app_farm_ico_bag", 28, 28),
    ("ico_steal", "app_farm_ico_steal", 28, 28),
    ("ico_set", "app_farm_ico_set", 28, 28),
    ("ico_seed", "app_farm_ico_seed", 28, 28),
    ("ico_plant", "app_farm_ico_plant", 28, 28),
    ("ico_water", "app_farm_ico_water", 28, 28),
    ("ico_weed", "app_farm_ico_weed", 28, 28),
    ("ico_pest", "app_farm_ico_pest", 28, 28),
    ("ico_cut", "app_farm_ico_cut", 28, 28),
    ("ico_hand", "app_farm_ico_hand", 28, 28),
    ("crop_seed", "app_farm_crop_seed", 40, 32),
    ("crop_sprout", "app_farm_crop_sprout", 40, 32),
    ("crop_dead", "app_farm_crop_dead", 40, 32),
    ("crop_carrot", "app_farm_crop_carrot", 40, 32),
    ("crop_cabbage", "app_farm_crop_cabbage", 40, 32),
    ("crop_tomato", "app_farm_crop_tomato", 40, 32),
    ("crop_corn", "app_farm_crop_corn", 40, 32),
    ("crop_berry", "app_farm_crop_berry", 40, 32),
    ("crop_melon", "app_farm_crop_melon", 40, 32),
    ("haz_weed", "app_farm_haz_weed", 20, 20),
    ("haz_pest", "app_farm_haz_pest", 20, 20),
    ("haz_dry", "app_farm_haz_dry", 20, 20),
    ("plot_dirt", "app_farm_plot_dirt", 36, 36),
]


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def encode(im: Image.Image) -> bytes:
    im = im.convert("RGBA")
    w, h = im.size
    rgb = bytearray()
    a8 = bytearray()
    px = im.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 8:
                r = g = b = a = 0
            v = rgb565(r, g, b)
            rgb += bytes((v & 0xFF, v >> 8))
            a8.append(a)
    return bytes(rgb) + bytes(a8)


def dump_bytes(name: str, data: bytes) -> str:
    lines = [
        f"static const LV_ATTRIBUTE_MEM_ALIGN uint8_t {name}_map[] = {{",
    ]
    row = []
    for i, b in enumerate(data):
        row.append(f"0x{b:02x}")
        if len(row) == 16:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    return "\n".join(lines)


def desc(sym: str, w: int, h: int) -> str:
    return f"""    {{
        .header.magic = LV_IMAGE_HEADER_MAGIC,
        .header.cf = LV_COLOR_FORMAT_RGB565A8,
        .header.w = {w},
        .header.h = {h},
        .header.stride = {w * 2},
        .data_size = sizeof({sym}_map),
        .data = {sym}_map,
    }}"""


def main() -> None:
    blobs = []
    missing = []
    for file_stem, sym, w, h in STICKERS:
        path = SRC / f"{file_stem}.png"
        if not path.exists():
            missing.append(str(path))
            continue
        im = Image.open(path)
        if im.size != (w, h):
            im = im.resize((w, h), Image.Resampling.LANCZOS)
        blobs.append((sym, w, h, encode(im)))
    if missing:
        raise SystemExit("missing PNGs:\n  " + "\n  ".join(missing))

    hdr = """#pragma once

#include "lvgl.h"

enum {
    APP_FARM_ICO_HOME = 0,
    APP_FARM_ICO_BAG,
    APP_FARM_ICO_STEAL,
    APP_FARM_ICO_SET,
    APP_FARM_ICO_SEED,
    APP_FARM_ICO_PLANT,
    APP_FARM_ICO_WATER,
    APP_FARM_ICO_WEED,
    APP_FARM_ICO_PEST,
    APP_FARM_ICO_CUT,
    APP_FARM_ICO_HAND,
    APP_FARM_ICO_N
};

enum {
    APP_FARM_IMG_SEED = 0,
    APP_FARM_IMG_SPROUT,
    APP_FARM_IMG_DEAD,
    APP_FARM_IMG_CARROT,
    APP_FARM_IMG_CABBAGE,
    APP_FARM_IMG_TOMATO,
    APP_FARM_IMG_CORN,
    APP_FARM_IMG_BERRY,
    APP_FARM_IMG_MELON,
    APP_FARM_IMG_CROP_N
};

enum {
    APP_FARM_HAZ_WEED = 0,
    APP_FARM_HAZ_PEST,
    APP_FARM_HAZ_DRY,
    APP_FARM_HAZ_N
};

extern const lv_image_dsc_t app_farm_ico_img[APP_FARM_ICO_N];
extern const lv_image_dsc_t app_farm_crop_img[APP_FARM_IMG_CROP_N];
extern const lv_image_dsc_t app_farm_haz_img[APP_FARM_HAZ_N];
extern const lv_image_dsc_t app_farm_plot_dirt;
"""
    HDR.write_text(hdr, encoding="utf-8")

    parts = [
        '#include "app_farm_img.h"',
        "",
        "#ifndef LV_ATTRIBUTE_MEM_ALIGN",
        "#define LV_ATTRIBUTE_MEM_ALIGN",
        "#endif",
        "",
        "/* Ardot transparent PNG → RGB565A8. No background on export. */",
        "",
    ]
    for sym, _w, _h, data in blobs:
        parts.append(dump_bytes(sym, data))
        parts.append("")

    ico = [desc(s, w, h) for s, w, h, _ in blobs[:11]]
    crop = [desc(s, w, h) for s, w, h, _ in blobs[11:20]]
    haz = [desc(s, w, h) for s, w, h, _ in blobs[20:23]]
    plot_s, plot_w, plot_h, _ = blobs[23]
    parts.append("const lv_image_dsc_t app_farm_ico_img[APP_FARM_ICO_N] = {")
    parts.append(",\n".join(ico))
    parts.append("};")
    parts.append("")
    parts.append("const lv_image_dsc_t app_farm_crop_img[APP_FARM_IMG_CROP_N] = {")
    parts.append(",\n".join(crop))
    parts.append("};")
    parts.append("")
    parts.append("const lv_image_dsc_t app_farm_haz_img[APP_FARM_HAZ_N] = {")
    parts.append(",\n".join(haz))
    parts.append("};")
    parts.append("")
    parts.append("const lv_image_dsc_t app_farm_plot_dirt =")
    parts.append(desc(plot_s, plot_w, plot_h) + ";")
    parts.append("")
    OUT.write_text("\n".join(parts), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)} from {len(blobs)} PNGs")


if __name__ == "__main__":
    main()
