# Farm (this firmware)

English | [简体中文](APPS.zh_CN.md)

This image is a **standalone farm game**. It boots into the fields. Alerts, Walkie, Weather, and TOTP are not in this image. **Language, Wi-Fi, Bluetooth, clock, screen, sound, player ID, and firmware update are set inside the game** — do not flash `main` first. After the first flash, the first boot toasts **hold OK to go back**.

Pins and BSP limits stay in the [README](../README.md) and the [AI Hardware Development Guide](AI_HARDWARE_DEVELOPMENT_GUIDE.md).

The matching server is `server/` (Next.js + MySQL). Devices always use `https://farm.netbiu.com`. For a local server, change `APP_FARM_HOST_DEFAULT` and rebuild.

Stickers are drawn in Ardot with **no background**, exported as **PNG**, then imported with `python3 scripts/png_to_lvgl.py`. See `assets/farm/README.md`.

## Buttons

`UP`, `DOWN`, and `OK` share the GPIO0 ADC ladder.

| Action | Effect |
| --- | --- |
| Short `UP` / `DOWN` | Bottom tabs: Home / Bag / Steal / Set. After OK, move among that page's actions or plots |
| Short `OK` | Open the current tab. Home: pick a tool, then pick a plot. Seed opens an icon tray of owned seeds. Bag buys or selects a seed. Steal: random visit, friend list, or rank. Settings open the highlighted page |
| Long `OK` | Leave plot pick, friend actions, a settings page, a visit, or a keyboard |
| Long `UP` / `DOWN` | Bag: Items / Shop. Steal: Random / Friends / Rank. On a keyboard, hold to keep moving and then jump a row |
| BLE pairing code | `OK` confirm, `DOWN` reject |
| Any key while the panel sleeps | Wake (first click is consumed) |

The header shows the farm name, level, coins, local time, and battery. A small Wi-Fi mark appears when the radio is on.

## Home

Six tools: Seed, Plant, Water, Weed, Bug, Pick. OK on a tool then UP/DOWN to a plot and OK again. Seed first opens a type tray; confirming a seed goes straight to planting plots. Plot pick skips plots that cannot take that tool; if none can, a toast says so (no empty plots, no weeding needed, and so on).

| Tool | What it does |
| --- | --- |
| Seed | Open an icon tray, pick an owned seed, then plant on an empty plot |
| Plant | Spend 1 seed on an empty unlocked plot; the plot starts dry |
| Water | Start the grow timer |
| Weed / Bug | Clear that hazard; harvest is blocked while either remains |
| Pick | Harvest a ripe clean plot for coins and XP |

Start with 6 plots, 80 coins, and 4 carrot seeds. Level 3 unlocks 9 plots, level 6 unlocks 12. Crops: carrot / cabbage (Lv.1), tomato (3), corn (5), berry (8), melon (12). Unwatered plots do not grow. Weeds and bugs can appear while a crop is in the ground. Catch-up after power-off is capped at 8 real hours.

## Bag

Long UP/DOWN switches Items / Shop. Items lists owned seeds (OK selects the planting seed). Shop lists every crop; locked rows show the required level; OK buys one seed.

## Steal

Needs Wi-Fi and a server host. Long UP/DOWN switches Random / Friends / Rank.

| Page | What it does |
| --- | --- |
| Random | OK looks for another recent farm (prefers ripe plots). The visit looks like Home, but the only tool is a hand. OK on a ripe plot steals ~60% of its harvest coins. Long OK leaves. At most 10 steals per person per day; at most 2 steals per target per 10 minutes |
| Friends | Inbox at the top (OK accepts, long DOWN declines). Then friends (OK → Enter / Delete). Last row is Add (type a 6-digit ID) |
| Rank | Top farms by coins. OK opens a read-only visit (no steal or help) |

Someone stealing from you clears that ripe plot. The next sync toasts that a crop was stolen.

## Settings

Always available.

| Page | What it does |
| --- | --- |
| Language | English / 简体中文 (default), saved to NVS |
| ID | 6-digit id from the Wi-Fi STA MAC (FNV-1a). Same formula as the server |
| Wi-Fi | Power, auto-reconnect, scan, join (3-key password), forget |
| Bluetooth | Power, quiet, advertise, forget a bond |
| Date & Time | NTP on/off (on by default) and server. With NTP off, dial the clock and **Set clock** |
| Screen | Brightness 10–100%; auto-sleep never / 15 / 30 / 60 / 120 s. The panel sleeps on that page or Home after the timeout |
| Sound | Mute; volume 0–100% |
| Update | Current and latest version; Check; Install after confirm |
| Reset farm | Clears plots, seeds, and coins. Keeps ID, token, and server host |

State is stored in NVS (`app` / `farm`). The OTA channel is `demo/farm`.

## Server

`POST /api/register` binds MAC → ID and issues a token. Later calls send `Authorization: Bearer <token>`. `PUT /api/farm` uploads the local farm; `GET` pulls steals. Random / visit / steal / friends / inbox / rank are under `/api/`.

## Idle sleep (device)

After the auto-sleep interval from Settings (or never, if that was 0): backlight off, panel sleep, radios off. A key wakes the CPU. Wi-Fi resumes if the screen stays on for 30 s. Sleep still runs on the Screen / Sound pages. It waits while a keyboard, visit, network job, firmware apply, BLE pairing code, or a busy Wi-Fi / Bluetooth / clock / update page is open.
