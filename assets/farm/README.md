# Farm stickers (Ardot → PNG → firmware)

Draw every sticker in Ardot on a **transparent** frame. Do **not** put a cream/grass/white background on the export. Export **PNG** (RGBA).

Same path as Meow: Ardot file → transparent PNG → `scripts/png_to_lvgl.py` → `main/app_farm_img.c`.

## Ardot

File `718233732936873`（宠物养成），page **农场贴纸**. Source frames are 128×128 vectors with no fill; export PNG 1× then downscale to 24×24.

1. New frame per sticker. Frame fill: none / 0% opacity. No rectangle behind the art.
2. Export each frame as PNG (1x). Name must match the table.
3. Drop 24×24 files into this folder, then:

```bash
python3 scripts/png_to_lvgl.py
```

## Files (all 24×24 unless noted)

| File | Use |
| --- | --- |
| `ico_home.png` | Tab Home |
| `ico_bag.png` | Tab Bag |
| `ico_steal.png` | Tab Steal (hand) |
| `ico_set.png` | Tab Settings |
| `ico_seed.png` | Tool: pick seed |
| `ico_plant.png` | Tool: plant |
| `ico_water.png` | Tool: water |
| `ico_weed.png` | Tool: weed |
| `ico_pest.png` | Tool: pest |
| `ico_cut.png` | Tool: harvest |
| `ico_hand.png` | Visit: steal |
| `crop_seed.png` | Just planted |
| `crop_sprout.png` | Growing |
| `crop_carrot.png` | Ripe carrot |
| `crop_cabbage.png` | Ripe cabbage |
| `crop_tomato.png` | Ripe tomato |
| `crop_corn.png` | Ripe corn |
| `crop_berry.png` | Ripe berry |
| `crop_melon.png` | Ripe melon |
| `haz_weed.png` | Weed overlay |
| `haz_pest.png` | Pest overlay |
| `haz_dry.png` | Needs water (drop; plots start dry after planting) |

Placeholders in this folder are transparent PNGs so the firmware builds. Replace them with Ardot exports and regenerate.
