#include "app_meow_ui.h"

#include "app.h"
#include "app_i18n.h"
#include "app_ota.h"
#include "app_prefs.h"
#include "app_meow.h"
#include "app_meow_link.h"
#include "app_meow_rhythm.h"
#include "app_meow_run.h"
#include "app_meow_match.h"
#include "app_meow_set.h"
#include "app_meow_web.h"
#include "app_time.h"
#include "app_tone.h"
#include "app_ui.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_ble.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pm.h"
#include "bsp_wifi.h"
#include "ui_pixel.h"

#include "lvgl.h"

#include "esp_event.h"
#include "esp_timer.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define LCD_W  APP_VIEW_W
#define LCD_H  APP_VIEW_H
#define NAV_H  44
#define PAGE_H (LCD_H - NAV_H)

#define TAB_HOME 0
#define TAB_SHOP 1
#define TAB_GAME 2
#define TAB_DEX  3
#define TAB_SET  4
#define TAB_N    5

#define DEX_CAT_PET 0
#define DEX_CAT_ACH 1
#define DEX_CAT_ITM 2
#define DEX_CAT_COL 3
#define DEX_CAT_N   4
#define DEX_PET_N   11
#define DEX_ACH_N   8

#define SUB_MAX  16

#define COL_BG     0xFFF7EA
#define COL_BG2    0x3A322C
#define COL_INK    0x5B4636
#define COL_MUTE   0x8A7460
#define COL_CORAL  0xFF8C7A
#define COL_GOLD   0xFFC857
#define COL_GOLD2  0xFFE4A0
#define COL_COIN   0xFFF3D6
#define COL_COINX  0xB8860B
#define COL_GEM    0xE7F3FB
#define COL_GEMX   0x3E7CA6
#define COL_BLUE   0x6BB6E0
#define COL_GREEN  0x7BB661
#define COL_PINK   0xE89BC8
#define COL_SLOT   0xF2E8D6
#define COL_LOCK   0xEAE0CE
#define COL_LOCKX  0xB8A88E
#define COL_RING   0xFFE9DF
#define COL_WAIT   0xF0C050
#define COL_WHITE  0xFFFFFF
#define COL_CARD   0xFFFFFF
#define COL_BLUSH  0xE87878
#define COL_LEAF   0x5C9A3A
#define COL_LEAF2  0x3A7028
#define COL_SEED   0xC09048
#define COL_EMBER  0xE07030
#define COL_FLAME  0xF0B848
#define COL_TEAL   0x4A9A8A
#define COL_SKY    0x8EC8F0
#define COL_BURR   0x8A5A30
#define COL_STONE  0x8A8478
#define COL_MOSS   0x6A8A40
#define COL_FROG   0x6A9A50
#define COL_GHOST  0xC8D4A0
#define COL_HILL   0xB5E08A
#define COL_HILL2  0x3D5A48
#define COL_TABBY  0xF08C40
#define COL_TABBY2 0xC86828
#define COL_MUZZ   0xFFF4E6
#define COL_NOSE   0xC87878
#define COL_CHEEK  0xF2A8A0
#define COL_CREAM  0xF3E6B8
#define COL_FACE   COL_CORAL
#define COL_SEL    COL_CORAL
#define COL_CLOUD  COL_WHITE
#define COL_LCD    COL_BG
#define COL_LCD2   COL_BG2
#define COL_ALERT  0xE03030

extern const lv_image_dsc_t app_meow_pet_img[11];
extern const lv_image_dsc_t app_meow_ico_img[16];
extern const lv_image_dsc_t app_meow_good_img[APP_MEOW_G_N];
extern const lv_image_dsc_t app_meow_stage_img[5];
extern const lv_image_dsc_t app_meow_haz_img[APP_MEOW_HAZ_N];
extern const lv_image_dsc_t app_meow_souv_img[APP_MEOW_SOUV_N];

enum {
    ICO_HOME = 0, ICO_BAG, ICO_GAME, ICO_DEX, ICO_SET,
    ICO_FEED, ICO_BATH, ICO_PLAY, ICO_HEAL,
    ICO_WIFI, ICO_BT, ICO_CLOCK, ICO_BAT, ICO_FISH, ICO_HEART,
    ICO_LAMP
};

ESP_EVENT_DEFINE_BASE(MEOW_EVENT);
#define MEOW_BLE_WAKE   1
#define MEOW_ALERT_WAKE 2
#define MEOW_BED_WAKE   3
#define ALERT_RECALL_US (180LL * 1000000)
#define IDLE_PERF_MS    2000u
#define IDLE_PAINT_MS   1000u
#define WIFI_WAKE_MS    30000u

typedef enum {
    MODE_CARE = 0,
    MODE_PLAY,
    MODE_LINK,
    MODE_RHYTHM,
    MODE_RUN,
    MODE_MATCH,
    MODE_RESULT
} meow_mode_t;

static app_meow_t s_pet;
static bool s_ready;
static uint32_t s_saved_sec;
static bool s_dirty;
static bool s_asleep;
static uint32_t s_idle_ms;
static uint32_t s_awake_ms;
static bool s_wifi_wait;
static uint32_t s_still_ms;
static uint32_t s_tick_ms = 250;
static bool s_wake_skip;
static esp_timer_handle_t s_alert_tm;
static esp_timer_handle_t s_bed_tm;

static lv_obj_t *s_scr, *s_lcd, *s_ibar, *s_stage;
static lv_timer_t *s_timer, *s_lang_timer;
static int s_sel;
static int s_menu = -1;
static int s_sub;
static int s_bag_cat;
static int s_dex_cat;
static meow_mode_t s_mode;
static char s_flash[48];
static char s_flash2[48];
static char s_line[48];
static int s_flash_left;
static int s_blink;
static int s_bob;
static int s_play_best;
static int s_play_run;
static int s_rhy_best;
static int s_run_best;
static int s_mat_best;
static app_meow_rhy_t s_rhy;
static app_meow_mat_t s_mat;
static app_meow_run_t s_run;
static uint32_t s_run_t0;
static uint32_t s_rhy_t0;
static uint32_t s_mat_last;
static uint8_t s_rhy_held;
static int s_pad_x;
static int s_fish_x;
static int s_fish_y;
static int s_fish_vx;
static bool s_fish_koi;
static uint32_t s_catch_at;
static int s_catch_x, s_catch_y;
static int s_catch_pts;
static int s_over_score;
static uint8_t s_over_got[APP_MEOW_G_N];
static uint8_t s_over_kind;
static int s_over_souv = -1;
static bool s_trip_edit;
static uint8_t s_trip_take[APP_MEOW_G_N];
static bool s_want_back_hint;
static bool s_name_edit;
static char s_name_buf[APP_MEOW_NAME_MAX + 1];
static int s_kb_sel, s_kb_set;
static int s_kb_hold_btn = -1;
static int s_kb_hold_ms;
static lv_timer_t *s_kb_hold_tm;

#define PAD_W       84
#define PAD_H       22
#define PAD_Y       278
#define PAD_STEP    24
#define FISH_SZ     24
#define FISH_TOP    40
#define FISH_V0     40
#define FISH_SWIM   16
#define FISH_PTS    30
#define FISH_KOI_PTS 50
#define FISH_FX_MS  280
#define PLAY_MARG   8
#define GAME_N      5
#define MAT_CELL    36
#define MAT_ICON    24
#define MAT_TOP     52
#define RUN_TOP     36
#define RUN_KIT_Y   248
#define RUN_LANE_W  68
#define RUN_TRACK_W (RUN_LANE_W * 3)
#define RUN_TRACK_X ((LCD_W - RUN_TRACK_W) / 2)
#define RUN_KIT_SZ  44
#define RUN_OBJ_SZ  28

static uint32_t now_sec(void)
{
    time_t t = time(NULL);
    if (t >= (time_t)APP_MEOW_WALL_SEC) return (uint32_t)t;
    int64_t us = esp_timer_get_time();
    if (us < 0) us = 0;
    return 1u + (uint32_t)(us / 1000000);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static int now_hour(void)
{
    time_t t = time(NULL);
    if (t < (time_t)APP_MEOW_WALL_SEC) return -1;
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_hour;
}

static void save_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("app", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "meow", &s_pet, sizeof(s_pet));
    nvs_commit(h);
    nvs_close(h);
    s_saved_sec = s_pet.last_sec;
    s_dirty = false;
}

static void load_nvs(void)
{
    app_meow_reset(&s_pet, now_sec(), (uint8_t)(now_sec() ^ 0x5Au));
    nvs_handle_t h;
    if (nvs_open("app", NVS_READONLY, &h) != ESP_OK) return;
    uint8_t raw[sizeof(app_meow_t)];
    size_t n = sizeof(raw);
    if (nvs_get_blob(h, "meow", raw, &n) == ESP_OK) {
        app_meow_import(&s_pet, raw, n);
    }
    nvs_close(h);
    if (s_pet.named && !s_pet.name[0]) {
        app_meow_set_name(&s_pet, app_str(APP_STR_MEOW_SP1));
        s_dirty = true;
    }
}

static bool back_hint_seen(void)
{
    nvs_handle_t h;
    uint8_t v = 0;

    if (nvs_open("app", NVS_READONLY, &h) != ESP_OK) return false;
    nvs_get_u8(h, "sys_okh", &v);
    nvs_close(h);
    return v != 0;
}

static void back_hint_mark(void)
{
    nvs_handle_t h;

    if (nvs_open("app", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "sys_okh", 1);
    nvs_commit(h);
    nvs_close(h);
}

static void sync_pet(void)
{
    uint32_t before = s_pet.last_sec;
    uint8_t st = s_pet.stage;
    uint8_t trip = s_pet.trip_st;
    app_meow_advance_night(&s_pet, now_sec(), now_hour(),
                           (int)app_prefs()->meow_bed,
                           (int)app_prefs()->meow_wake);
    if (s_pet.last_sec != before || s_pet.stage != st) s_dirty = true;
    if (trip == APP_MEOW_TRIP_AWAY && s_pet.trip_st == APP_MEOW_TRIP_BACK) {
        s_dirty = true;
    }
    if (s_dirty && (s_pet.last_sec - s_saved_sec >= 30 ||
                    s_pet.stage == APP_MEOW_DEAD)) {
        save_nvs();
    }
}

static lv_obj_t *px(lv_obj_t *p, int x, int y, int w, int h, uint32_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    ui_pixel_strip_theme(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
    return o;
}

static uint32_t mix(uint32_t a, uint32_t b, int t)
{
    int ar = (int)((a >> 16) & 255), ag = (int)((a >> 8) & 255), ab = (int)(a & 255);
    int br = (int)((b >> 16) & 255), bg = (int)((b >> 8) & 255), bb = (int)(b & 255);
    int r = ar + (br - ar) * t / 8;
    int g = ag + (bg - ag) * t / 8;
    int bl = ab + (bb - ab) * t / 8;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static uint32_t lift(uint32_t c)
{
    return mix(c, 0xFFFFFF, 4);
}

static uint32_t dim(uint32_t c)
{
    return mix(c, 0x000000, 3);
}

static void rrect(lv_layer_t *layer, int x, int y, int w, int h, int r, uint32_t c)
{
    if (w < 2 || h < 2) return;
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_hex(c);
    rd.bg_opa = LV_OPA_COVER;
    rd.radius = r;
    rd.border_width = 0;
    rd.outline_width = 0;
    rd.shadow_width = 0;
    lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_draw_rect(layer, &rd, &a);
}

static void oval(lv_layer_t *layer, int x, int y, int w, int h, uint32_t c)
{
    if (w < 2 || h < 2) return;
    int r = w < h ? w : h;
    rrect(layer, x, y, w, h, r / 2, c);
}

static void ring(lv_layer_t *layer, int x, int y, int w, int h, uint32_t c)
{
    if (w < 4 || h < 4) return;
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_opa = LV_OPA_TRANSP;
    rd.border_width = 2;
    rd.border_color = lv_color_hex(c);
    rd.radius = (int32_t)((w < h ? w : h) / 2);
    rd.outline_width = 0;
    rd.shadow_width = 0;
    lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_draw_rect(layer, &rd, &a);
}

static void draw_xp_ring(lv_layer_t *layer, int cx, int cy, int r, int pct,
                         uint32_t fg)
{
    lv_draw_arc_dsc_t d;

    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    lv_draw_arc_dsc_init(&d);
    d.width = 6;
    d.opa = LV_OPA_COVER;
    d.rounded = 1;
    d.center.x = cx;
    d.center.y = cy;
    d.radius = (uint16_t)r;
    d.color = lv_color_hex(COL_LOCK);
    d.start_angle = 0;
    d.end_angle = 360;
    lv_draw_arc(layer, &d);
    if (pct <= 0) return;
    d.color = lv_color_hex(fg);
    if (pct >= 100) {
        d.start_angle = 0;
        d.end_angle = 360;
    } else {
        d.start_angle = 270;
        d.end_angle = 270 + pct * 360 / 100;
    }
    lv_draw_arc(layer, &d);
}

static void lobe(lv_layer_t *layer, int x, int y, int w, int h, uint32_t c)
{
    oval(layer, x + 1, y + 2, w, h, dim(c));
    oval(layer, x, y, w, h, c);
    oval(layer, x + w / 4, y + h / 6, w / 3, h / 3, lift(c));
}

static void draw_poop(lv_layer_t *layer, int x, int y)
{
    lobe(layer, x + 4, y, 12, 10, COL_BURR);
    lobe(layer, x, y + 6, 20, 12, COL_BURR);
}

static void draw_zzz(lv_layer_t *layer, int x, int y)
{
    oval(layer, x, y + 8, 8, 8, COL_SKY);
    oval(layer, x + 8, y + 2, 10, 10, COL_SKY);
    oval(layer, x + 16, y - 4, 12, 12, COL_SKY);
    oval(layer, x + 10, y + 4, 4, 4, COL_WHITE);
    oval(layer, x + 19, y - 1, 5, 5, COL_WHITE);
}

static void draw_sick(lv_layer_t *layer, int x, int y)
{
    oval(layer, x, y, 16, 16, COL_BLUSH);
    oval(layer, x + 6, y + 3, 4, 10, COL_WHITE);
    oval(layer, x + 3, y + 6, 10, 4, COL_WHITE);
}

#define HOME_N 4
#define HOME_LIGHT 3
static const uint8_t HOME_ACT[HOME_N] = {
    APP_MEOW_FEED, APP_MEOW_BATH, APP_MEOW_HEAL, APP_MEOW_LIGHT
};
static const int HOME_ICO[HOME_N] = {
    ICO_FEED, ICO_BATH, ICO_HEAL, ICO_LAMP
};
#define SET_N 10
#define SET_NAME 1
#define SET_BED 2
#define SET_WIPE 9
static const app_str_id_t SET_STR[SET_N] = {
    APP_STR_LANGUAGE, APP_STR_MEOW_NAME, APP_STR_PET_HOURS, APP_STR_WIFI,
    APP_STR_BLUETOOTH, APP_STR_DATETIME, APP_STR_SCREEN, APP_STR_SOUND,
    APP_STR_UPDATE, APP_STR_MEOW_WIPE
};
static const app_str_id_t BAG_CAT_STR[3] = {
    APP_STR_MEOW_IT_FOOD, APP_STR_MEOW_WEAR, APP_STR_MEOW_GEAR
};
static const app_str_id_t DEX_CAT_STR[DEX_CAT_N] = {
    APP_STR_MEOW_DEX_PET, APP_STR_MEOW_ACH, APP_STR_MEOW_DEX_ITM,
    APP_STR_MEOW_DEX_COL
};

static int dex_cat_n(int cat);

static int focused(void)
{
    return s_menu >= 0;
}

static bool minigame(void)
{
    return s_mode == MODE_PLAY || s_mode == MODE_RHYTHM || s_mode == MODE_RUN ||
           s_mode == MODE_MATCH || s_mode == MODE_RESULT;
}

static int inner_n(void)
{
    if (s_mode != MODE_CARE) return 0;
    if (s_sel == TAB_HOME) {
        if (s_pet.stage == APP_MEOW_EGG) return 0;
        if (s_pet.stage == APP_MEOW_DEAD) return 1;
        if (s_pet.trip_st == APP_MEOW_TRIP_AWAY) return 0;
        return HOME_N;
    }
    if (s_sel == TAB_SET) return SET_N;
    if (s_sel == TAB_SHOP) {
        int n = app_meow_owned_n(&s_pet, s_bag_cat);
        return n > 0 ? n : 1;
    }
    if (s_sel == TAB_GAME) {
        if (s_trip_edit) {
            int n = app_meow_owned_n(&s_pet, APP_MEOW_CAT_FOOD);
            return n > 0 ? n + 1 : 1;
        }
        return GAME_N;
    }
    if (s_sel == TAB_DEX) return dex_cat_n(s_dex_cat);
    return 0;
}

static void bag_cat_shift(int dir)
{
    s_bag_cat = (s_bag_cat + APP_MEOW_CAT_N + dir) % APP_MEOW_CAT_N;
    s_sub = 0;
}

static void dex_cat_shift(int dir)
{
    s_dex_cat = (s_dex_cat + DEX_CAT_N + dir) % DEX_CAT_N;
    s_sub = 0;
}

static const char *status_text(void);
static const char *default_name(void);
static app_str_id_t species_id(void);
static app_str_id_t stage_id(void);
static int play_pad_w(void);
static void draw_souv_ico(lv_layer_t *layer, int id, int x, int y, int sz);
static void paint(void);
static void name_from_web(void);

static int pet_kind(void)
{
    if (s_pet.stage == APP_MEOW_EGG) return 0;
    if (s_pet.species >= 1 && s_pet.species <= 10) return (int)s_pet.species;
    if (s_pet.stage == APP_MEOW_BABY) return 1;
    if (s_pet.stage == APP_MEOW_CHILD) return 2;
    if (s_pet.stage == APP_MEOW_TEEN) return s_pet.form ? 4 : 3;
    if (s_pet.stage == APP_MEOW_DEAD) return 10;
    return 5;
}

static void draw_pet_img(lv_layer_t *layer, int x, int y, int w, int h,
                         int kind, bool dead, bool sleep)
{
    lv_draw_image_dsc_t d;
    lv_area_t a;

    if (kind < 0) kind = 0;
    if (kind > 10) kind = 10;
    lv_draw_image_dsc_init(&d);
    d.src = &app_meow_pet_img[kind];
    /* dest 小于 84 时必须缩放,否则只画出立绘左上角一截。 */
    if (w != 84 || h != 84) {
        d.scale_x = w * 256 / 84;
        d.scale_y = h * 256 / 84;
        d.pivot.x = 0;
        d.pivot.y = 0;
    }
    if (dead) {
        d.recolor = lv_color_hex(COL_GHOST);
        d.recolor_opa = LV_OPA_70;
    } else if (sleep) {
        d.recolor = lv_color_hex(COL_BG2);
        d.recolor_opa = LV_OPA_30;
    }
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + 83;
    a.y2 = y + 83;
    lv_draw_image(layer, &d, &a);
}

static void draw_ico(lv_layer_t *layer, int x, int y, int w, int h, int id)
{
    lv_draw_image_dsc_t d;
    lv_area_t a;

    if (id < 0) id = 0;
    if (id > 15) id = 15;
    lv_draw_image_dsc_init(&d);
    d.src = &app_meow_ico_img[id];
    if (w != 24 || h != 24) {
        d.scale_x = w * 256 / 24;
        d.scale_y = h * 256 / 24;
        d.pivot.x = 0;
        d.pivot.y = 0;
    }
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + 23;
    a.y2 = y + 23;
    lv_draw_image(layer, &d, &a);
}

static void draw_fish(lv_layer_t *layer, int x, int y, int w, int h, bool koi)
{
    lv_draw_image_dsc_t d;
    lv_area_t a;

    lv_draw_image_dsc_init(&d);
    d.src = &app_meow_ico_img[ICO_FISH];
    if (w != 24 || h != 24) {
        d.scale_x = w * 256 / 24;
        d.scale_y = h * 256 / 24;
        d.pivot.x = 0;
        d.pivot.y = 0;
    }
    if (koi) {
        oval(layer, x - 2, y + 2, w + 4, h, COL_GOLD2);
        d.recolor = lv_color_hex(COL_GOLD);
        d.recolor_opa = LV_OPA_COVER;
    }
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + 23;
    a.y2 = y + 23;
    lv_draw_image(layer, &d, &a);
}

static int pet_sprite_size(void)
{
    return 84;
}

static void draw_halo(lv_layer_t *layer, int cx, int oy)
{
    lv_draw_rect_dsc_t rd;
    lv_area_t a;

    /* 扁椭圆光环浮在头顶,镂空边框,避免实心金饼。 */
    ring(layer, cx - 22, oy - 10, 44, 16, COL_GOLD2);
    lv_draw_rect_dsc_init(&rd);
    rd.bg_opa = LV_OPA_TRANSP;
    rd.border_width = 3;
    rd.border_color = lv_color_hex(COL_GOLD);
    rd.radius = 8;
    rd.outline_width = 0;
    rd.shadow_width = 0;
    a.x1 = cx - 20;
    a.y1 = oy - 8;
    a.x2 = cx + 19;
    a.y2 = oy + 5;
    lv_draw_rect(layer, &rd, &a);
    oval(layer, cx + 6, oy - 8, 10, 3, COL_WHITE);
}

/* Ardot circular stickers: 0 egg, 1-10 species. */
static void draw_pet(lv_layer_t *layer, int ox, int oy, int sz)
{
    int cx = ox + sz / 2;
    int kind = pet_kind();
    bool sleep = s_pet.sleeping && s_pet.stage != APP_MEOW_EGG;
    bool dead = (s_pet.stage == APP_MEOW_DEAD);

    if (sleep) oy += 4;

    draw_pet_img(layer, ox, oy, sz, sz, kind, dead, sleep);
    if (dead) draw_halo(layer, cx, oy);
}

static void draw_txt(lv_layer_t *layer, int x, int y, int w, int h,
                    const char *s, uint32_t c, lv_text_align_t al)
{
    if (!s || !s[0] || w < 2 || h < 2) return;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1;
    d.font = ui_pixel_font_14();
    d.color = lv_color_hex(c);
    d.opa = LV_OPA_COVER;
    d.align = al;
    lv_area_t a = { x, y, x + w - 1, y + h - 1 };
    lv_draw_label(layer, &d, &a);
}

static void draw_bar(lv_layer_t *layer, int x, int y, int w, int h,
                    int pct, uint32_t col)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    rrect(layer, x, y, w, h, h / 2, COL_SLOT);
    int fw = w * pct / 100;
    if (pct > 0 && fw < h) fw = h;
    if (pct > 0) rrect(layer, x, y, fw, h, h / 2, col);
}

static void draw_stat_row(lv_layer_t *layer, int bx, int y, const char *lab,
                          int pct, uint32_t col, int val, int frac)
{
    char num[8];

    (void)frac;
    draw_txt(layer, bx + 16, y, 42, 16, lab, COL_MUTE, LV_TEXT_ALIGN_LEFT);
    snprintf(num, sizeof(num), "%d", val);
    draw_bar(layer, bx + 60, y + 3, 128, 10, pct, col);
    draw_txt(layer, bx + 190, y, 34, 16, num, COL_INK, LV_TEXT_ALIGN_RIGHT);
}


static void ico_star(lv_layer_t *layer, int x, int y, uint32_t c)
{
    oval(layer, x + 5, y, 6, 16, c);
    oval(layer, x, y + 5, 16, 6, c);
    oval(layer, x + 2, y + 2, 12, 12, c);
    oval(layer, x + 6, y + 6, 4, 4, COL_WHITE);
}


static void draw_good_sz(lv_layer_t *layer, int id, int x, int y, int sz)
{
    lv_draw_image_dsc_t d;
    lv_area_t a;

    if (id < 0 || id >= APP_MEOW_G_N) {
        ico_star(layer, x + (sz - 16) / 2, y + (sz - 16) / 2, COL_GOLD);
        return;
    }
    lv_draw_image_dsc_init(&d);
    d.src = &app_meow_good_img[id];
    if (sz != 28) {
        d.scale_x = sz * 256 / 28;
        d.scale_y = sz * 256 / 28;
        d.pivot.x = 0;
        d.pivot.y = 0;
    }
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + 27;
    a.y2 = y + 27;
    lv_draw_image(layer, &d, &a);
}

static void draw_good(lv_layer_t *layer, int id, int x, int y)
{
    draw_good_sz(layer, id, x, y, 28);
}

static const char *good_name(int id)
{
    if (id < 0 || id >= APP_MEOW_G_N) return "";
    return app_str((app_str_id_t)(APP_STR_MEOW_GD0 + id));
}

static bool night_ui(void)
{
    if (minigame()) return false;
    return s_pet.lights_off && s_pet.sleeping;
}

static uint32_t ui_ink(void)
{
    return night_ui() ? COL_CREAM : COL_INK;
}

static uint32_t ui_bg(void)
{
    return night_ui() ? COL_BG2 : COL_BG;
}

static uint32_t ui_card(void)
{
    return night_ui() ? mix(COL_BG2, COL_WHITE, 3) : COL_WHITE;
}

static int pet_lv(void)
{
    return app_meow_level(&s_pet);
}

static int xp_pct(void)
{
    return app_meow_xp_pct(&s_pet);
}

static int fullness_pct(void)
{
    return (int)s_pet.hunger;
}

static int mood_pct(void)
{
    return (int)s_pet.happy;
}

static int clean_pct(void)
{
    return app_meow_clean(&s_pet);
}

static int health_pct(void)
{
    return (int)s_pet.health;
}

static bool dex_pet_on(int slot)
{
    if (s_pet.stage == APP_MEOW_DEAD) {
        return slot == 0 || s_pet.species == (uint8_t)slot;
    }
    if (slot == 0) return true;
    if (slot >= 1 && slot <= 10) {
        if (s_pet.species == (uint8_t)slot) return true;
        if (slot == 1 && s_pet.stage >= APP_MEOW_BABY) return true;
        if (slot == 2 && s_pet.stage >= APP_MEOW_CHILD) return true;
        if (slot == 3 && s_pet.stage >= APP_MEOW_TEEN && !s_pet.form) {
            return true;
        }
        if (slot == 4 && s_pet.stage >= APP_MEOW_TEEN && s_pet.form) {
            return true;
        }
        return false;
    }
    return false;
}

static bool dex_ach_on(int id)
{
    if (s_pet.stage == APP_MEOW_DEAD || s_pet.stage <= APP_MEOW_EGG) return false;
    if (id == 0) return true;
    if (id == 1) return s_pet.happy >= APP_MEOW_STAT_MAX;
    if (id == 2) return s_pet.stage == APP_MEOW_ADULT;
    if (id == 3) return s_pet.hunger >= APP_MEOW_STAT_MAX;
    if (id == 4) return s_pet.poop == 0 && s_pet.dirt == 0;
    if (id == 5) return s_pet.health >= APP_MEOW_STAT_MAX && !s_pet.sick;
    if (id == 6) return s_play_best >= 100;
    if (id == 7) return s_rhy_best >= 100;
    return false;
}

static bool dex_itm_on(int id)
{
    return app_meow_inv(&s_pet, id) > 0;
}

static bool dex_col_on(int id)
{
    return app_meow_souv_on(&s_pet, id);
}

static int dex_cat_n(int cat)
{
    if (cat == DEX_CAT_PET) return DEX_PET_N;
    if (cat == DEX_CAT_ACH) return DEX_ACH_N;
    if (cat == DEX_CAT_ITM) return APP_MEOW_G_N;
    return APP_MEOW_SOUV_N;
}

static bool dex_slot_on(int cat, int slot)
{
    if (cat == DEX_CAT_PET) return dex_pet_on(slot);
    if (cat == DEX_CAT_ACH) return dex_ach_on(slot);
    if (cat == DEX_CAT_ITM) return dex_itm_on(slot);
    return dex_col_on(slot);
}

static int dex_cat_got(int cat)
{
    int n = dex_cat_n(cat), got = 0, i;

    for (i = 0; i < n; i++) {
        if (dex_slot_on(cat, i)) got++;
    }
    return got;
}

static bool dex_on(int slot)
{
    return dex_slot_on(s_dex_cat, slot);
}

static const char *dex_blurb(int slot)
{
    if (s_dex_cat == DEX_CAT_PET) {
        if (slot == 0) return app_str(APP_STR_MEOW_DX_EGG);
        if (slot >= 1 && slot <= 10) {
            return app_str((app_str_id_t)(APP_STR_MEOW_DX_SP1 + slot - 1));
        }
        return "";
    }
    if (s_dex_cat == DEX_CAT_ACH) {
        if (slot < 0 || slot >= DEX_ACH_N) return "";
        return app_str((app_str_id_t)(APP_STR_MEOW_DX_AH0 + slot));
    }
    if (s_dex_cat == DEX_CAT_ITM) {
        if (slot < 0 || slot >= APP_MEOW_G_N) return "";
        return app_str((app_str_id_t)(APP_STR_MEOW_DX_GD0 + slot));
    }
    if (slot < 0 || slot >= APP_MEOW_SOUV_N) return "";
    return app_str((app_str_id_t)(APP_STR_MEOW_DX_SV0 + slot));
}

static void dex_blurb_split(int slot, char *a, size_t an, char *b, size_t bn)
{
    const char *s = dex_on(slot) ? dex_blurb(slot) : "?";
    const char *nl;
    size_t n;

    a[0] = 0;
    b[0] = 0;
    if (!s || !s[0] || an == 0 || bn == 0) return;
    nl = strchr(s, '\n');
    if (!nl) {
        strncpy(a, s, an - 1);
        a[an - 1] = 0;
        return;
    }
    n = (size_t)(nl - s);
    if (n >= an) n = an - 1;
    memcpy(a, s, n);
    a[n] = 0;
    strncpy(b, nl + 1, bn - 1);
    b[bn - 1] = 0;
}

static void clock_txt(char *out, size_t n)
{
    time_t t = time(NULL);
    if (t < (time_t)1700000000) {
        snprintf(out, n, "--:--");
        return;
    }
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

static int radio_w(void)
{
    int w = 0;

    if (bsp_wifi_enabled()) w += 14;
    if (bsp_ble_enabled()) w += 14;
    return w;
}

static void pill_xs(int bx, int *clk_x, int *bat_x)
{
    int bat_w = 48, clk_w = 52, gap = 4, right = 8;

    *bat_x = bx + LCD_W - right - bat_w;
    *clk_x = *bat_x - gap - clk_w;
}

static void draw_radio(lv_layer_t *layer, int x, int y)
{
    if (bsp_wifi_enabled()) {
        draw_ico(layer, x, y, 12, 12, ICO_WIFI);
        x += 14;
    }
    if (bsp_ble_enabled()) {
        draw_ico(layer, x, y, 12, 12, ICO_BT);
    }
}

static void draw_pills(lv_layer_t *layer, int bx, int by)
{
    char clk[8], bat[8];
    int soc = bsp_battery_soc();
    int bat_x, clk_x;

    pill_xs(bx, &clk_x, &bat_x);
    clock_txt(clk, sizeof(clk));
    if (soc < 0) snprintf(bat, sizeof(bat), "--");
    else {
        if (soc > 100) soc = 100;
        snprintf(bat, sizeof(bat), "%d%%", soc);
    }

    rrect(layer, clk_x, by, 52, 20, 10, COL_COIN);
    draw_txt(layer, clk_x + 2, by + 2, 48, 16, clk, COL_COINX,
             LV_TEXT_ALIGN_CENTER);
    rrect(layer, bat_x, by, 48, 20, 10, COL_GEM);
    draw_txt(layer, bat_x + 2, by + 2, 44, 16, bat, COL_GEMX,
             LV_TEXT_ALIGN_CENTER);
}

static const char *default_name(void)
{
    if (s_pet.stage == APP_MEOW_EGG && s_pet.species == 0) {
        return app_str(APP_STR_MEOW_SP1);
    }
    return app_str(species_id());
}

static const char *pet_title(void)
{
    if (s_pet.name[0]) return s_pet.name;
    return default_name();
}

static void draw_header(lv_layer_t *layer, int bx, int by)
{
    int clk_x, bat_x, rw, rx, tw;

    pill_xs(bx, &clk_x, &bat_x);
    rw = radio_w();
    rx = rw ? clk_x - 4 - rw : clk_x;
    tw = rx - (bx + 12) - 4;
    if (tw < 48) tw = 48;
    draw_txt(layer, bx + 12, by + 10, tw, 20, pet_title(), ui_ink(),
             LV_TEXT_ALIGN_LEFT);
    if (rw) draw_radio(layer, rx, by + 13);
    draw_pills(layer, bx, by + 10);
}

static void draw_alert_edge(lv_layer_t *layer, int x, int y, int w, int h)
{
    uint8_t peak = app_meow_alert_peak(&s_pet);
    uint32_t c;
    int t;

    if (minigame() || s_sel == TAB_GAME) return;
    if (s_flash_left <= 0) return;
    if (peak < APP_MEOW_ALERT_WARN) return;
    if (peak >= APP_MEOW_ALERT_CRIT) {
        c = s_blink ? COL_ALERT : mix(COL_ALERT, COL_CORAL, 4);
        t = s_blink ? 6 : 4;
    } else if (peak >= APP_MEOW_ALERT_HIT) {
        c = s_blink ? COL_ALERT : mix(COL_ALERT, COL_CORAL, 3);
        t = 5;
    } else {
        c = s_blink ? COL_ALERT : mix(COL_ALERT, COL_BG, 5);
        t = s_blink ? 4 : 3;
    }
    rrect(layer, x, y, w, t, 0, c);
    rrect(layer, x, y + h - t, w, t, 0, c);
    rrect(layer, x, y, t, h, 0, c);
    rrect(layer, x + w - t, y, t, h, 0, c);
}

static void draw_flash(lv_layer_t *layer, int bx, int by)
{
    int lines, h, y;

    if ((minigame() || s_sel == TAB_GAME) && !s_name_edit) return;
    if (s_flash_left <= 0 || !s_flash[0]) return;
    lines = s_flash2[0] ? 2 : 1;
    h = 4 + lines * 16;
    if (s_name_edit) y = by + LCD_H - 4 - h;
    else if (s_sel == TAB_HOME) y = by + 140;
    else y = by + PAGE_H - 2 - h;
    rrect(layer, bx + 12, y, 216, h, 10, COL_CORAL);
    draw_txt(layer, bx + 16, y + 2, 208, 16, s_flash, COL_WHITE,
             LV_TEXT_ALIGN_CENTER);
    if (s_flash2[0]) {
        draw_txt(layer, bx + 16, y + 18, 208, 16, s_flash2, COL_WHITE,
                 LV_TEXT_ALIGN_CENTER);
    }
}

static uint32_t stage_col(void)
{
    switch (s_pet.stage) {
    case APP_MEOW_EGG: return COL_SEED;
    case APP_MEOW_BABY: return COL_PINK;
    case APP_MEOW_CHILD: return COL_LEAF;
    case APP_MEOW_TEEN: return COL_CORAL;
    case APP_MEOW_ADULT: return COL_GOLD;
    default: return COL_LOCKX;
    }
}

static void draw_stage_badge(lv_layer_t *layer, int bx, int by)
{
    lv_draw_image_dsc_t d;
    lv_area_t a;
    int id;

    if (s_pet.stage == APP_MEOW_DEAD) return;
    id = (int)s_pet.stage;
    if (id < 0) id = 0;
    if (id > 4) id = 4;
    lv_draw_image_dsc_init(&d);
    d.src = &app_meow_stage_img[id];
    a.x1 = bx + 10;
    a.y1 = by + 32;
    a.x2 = a.x1 + 31;
    a.y2 = a.y1 + 31;
    lv_draw_image(layer, &d, &a);
    snprintf(s_line, sizeof(s_line), "Lv.%d", pet_lv());
    rrect(layer, bx + 7, by + 66, 38, 16, 8, COL_GOLD);
    draw_txt(layer, bx + 7, by + 66, 38, 16, s_line, COL_INK,
             LV_TEXT_ALIGN_CENTER);
    if (s_pet.stage == APP_MEOW_EGG) {
        snprintf(s_line, sizeof(s_line), "%u/%u",
                 (unsigned)s_pet.hatch_min, (unsigned)APP_MEOW_HATCH_SEC);
    } else {
        snprintf(s_line, sizeof(s_line), "%d/%d",
                 app_meow_xp(&s_pet), app_meow_xp_need(pet_lv()));
    }
    draw_txt(layer, bx + 2, by + 82, 48, 16, s_line, COL_MUTE,
             LV_TEXT_ALIGN_CENTER);
}

static void draw_stage_pill(lv_layer_t *layer, int x, int y, int w)
{
    uint32_t bg = stage_col();
    rrect(layer, x, y, w, 16, 8, bg);
    draw_txt(layer, x, y, w, 16, app_str(stage_id()), COL_WHITE,
             LV_TEXT_ALIGN_CENTER);
}

static void draw_pet_circle(lv_layer_t *layer, int bx, int by)
{
    rrect(layer, bx + 64, by + 42, 112, 112, 56, COL_SLOT);
    draw_xp_ring(layer, bx + 120, by + 98, 54, xp_pct(), stage_col());
    rrect(layer, bx + 72, by + 50, 96, 96, 48, ui_card());
    int bob = 0, wob = 0;
    if (s_pet.stage == APP_MEOW_EGG) wob = ((s_bob / 2) & 1) ? 2 : -2;
    else if (!s_pet.sleeping && s_pet.stage != APP_MEOW_DEAD) {
        bob = (s_bob & 1) * 2;
    }
    int sz = pet_sprite_size();
    int px = bx + 72 + (96 - sz) / 2 + wob;
    int py = by + 50 + (96 - sz) / 2 + bob;
    draw_pet(layer, px, py, sz);
    if (s_pet.trip_st == APP_MEOW_TRIP_AWAY) {
        draw_ico(layer, bx + 136, by + 114, 24, 24, ICO_BAG);
    }
    draw_stage_badge(layer, bx, by);
    draw_stage_pill(layer, bx + 86, by + 134, 68);
    if (s_pet.sleeping && s_pet.stage != APP_MEOW_EGG &&
        s_pet.stage != APP_MEOW_DEAD) {
        draw_zzz(layer, px + sz - 8, py + 4 - ((s_bob / 2) & 1) * 3);
    }
    if (s_pet.sick && s_pet.stage > APP_MEOW_EGG &&
        s_pet.stage < APP_MEOW_DEAD) {
        draw_sick(layer, px + sz - 10, py + 6);
    }
    if (s_pet.poop && s_pet.stage != APP_MEOW_EGG) {
        draw_poop(layer, bx + 24, by + 130);
        if (s_pet.poop > 1) draw_poop(layer, bx + 48, by + 138);
    }
}

static void draw_home_btns(lv_layer_t *layer, int bx, int by)
{
    int h = 32;
    int y = by + PAGE_H - h - 4;
    if (s_pet.stage == APP_MEOW_DEAD) {
        bool sel = focused() && s_sub == 0;
        rrect(layer, bx + 16, y, 208, h, 10, sel ? COL_CORAL : COL_WHITE);
        draw_txt(layer, bx + 16, y + 8, 208, 16, app_str(APP_STR_MEOW_AGAIN),
                 sel ? COL_WHITE : COL_INK, LV_TEXT_ALIGN_CENTER);
        return;
    }
    if (s_pet.stage == APP_MEOW_EGG) return;
    if (s_pet.trip_st == APP_MEOW_TRIP_AWAY) return;
    int w = 46, gap = 8, ico = 24;
    int total = HOME_N * w + (HOME_N - 1) * gap;
    int x0 = bx + (LCD_W - total) / 2;
    for (int i = 0; i < HOME_N; i++) {
        int x = x0 + i * (w + gap);
        bool sel = focused() && s_sub == i;
        rrect(layer, x, y, w, h, 10, sel ? COL_RING : COL_WHITE);
        if (sel) {
            lv_draw_rect_dsc_t rd;
            lv_draw_rect_dsc_init(&rd);
            rd.bg_opa = LV_OPA_TRANSP;
            rd.border_width = 2;
            rd.border_color = lv_color_hex(COL_CORAL);
            rd.radius = 10;
            lv_area_t a = { x, y, x + w - 1, y + h - 1 };
            lv_draw_rect(layer, &rd, &a);
        }
        draw_ico(layer, x + (w - ico) / 2, y + (h - ico) / 2, ico, ico,
                 HOME_ICO[i]);
    }
}

static void page_home(lv_layer_t *layer, int bx, int by)
{
    draw_header(layer, bx, by);
    draw_pet_circle(layer, bx, by);
    if (!(s_flash_left > 0 && s_flash[0])) {
        const char *st = status_text();
        if (st && st[0]) {
            draw_txt(layer, bx + 16, by + 156, 208, 16, st,
                     COL_MUTE, LV_TEXT_ALIGN_CENTER);
        }
    }
    int my = by + 174;
    draw_stat_row(layer, bx, my, app_str(APP_STR_MEOW_STAT_FOOD),
                  fullness_pct(), COL_GOLD, (int)s_pet.hunger, 0);
    draw_stat_row(layer, bx, my + 16, app_str(APP_STR_MEOW_STAT_CLEAN),
                  clean_pct(), COL_BLUE, clean_pct(), 0);
    draw_stat_row(layer, bx, my + 32, app_str(APP_STR_MEOW_STAT_HEALTH),
                  health_pct(), COL_GREEN, (int)s_pet.health, 0);
    draw_stat_row(layer, bx, my + 48, app_str(APP_STR_MEOW_D_HAPPY),
                  mood_pct(), COL_CORAL, (int)s_pet.happy, 0);
    draw_home_btns(layer, bx, by);
}

static void page_set(lv_layer_t *layer, int bx, int by)
{
    int n = SET_N;
    int start;
    int vis = 6;

    draw_header(layer, bx, by);

    start = focused() ? s_sub - 3 : 0;
    if (start < 0) start = 0;
    if (start > n - vis) start = n > vis ? n - vis : 0;
    for (int i = 0; i < vis; i++) {
        int idx = start + i;
        int y;
        bool sel;
        const char *lab;

        if (idx >= n) break;
        y = by + 44 + i * 26;
        sel = focused() && (idx == s_sub);
        rrect(layer, bx + 16, y, 208, 24, 12, sel ? COL_CORAL : ui_card());
        lab = app_str(SET_STR[idx]);
        if (idx == 0) {
            snprintf(s_line, sizeof(s_line), "%s  %s", lab,
                     app_lang_name(app_lang()));
            lab = s_line;
        } else if (idx == SET_NAME) {
            snprintf(s_line, sizeof(s_line), "%s  %s", lab, pet_title());
            lab = s_line;
        } else if (idx == SET_BED) {
            snprintf(s_line, sizeof(s_line), "%s  %02d–%02d", lab,
                     (int)app_prefs()->meow_bed, (int)app_prefs()->meow_wake);
            lab = s_line;
        }
        draw_txt(layer, bx + 26, y + 4, 188, 16, lab,
                 sel ? COL_WHITE : COL_INK, LV_TEXT_ALIGN_LEFT);
    }
}

static void draw_cat_tabs(lv_layer_t *layer, int bx, int by,
                          const app_str_id_t *labs, int n, int cur)
{
    int cx0 = bx + 16;
    for (int i = 0; i < n; i++) {
        int x = cx0 + i * 70;
        bool on = (i == cur);
        rrect(layer, x, by + 44, 64, 22, 11, on ? COL_CORAL : COL_WHITE);
        draw_txt(layer, x, by + 47, 64, 16, app_str(labs[i]),
                 on ? COL_WHITE : COL_INK, LV_TEXT_ALIGN_CENTER);
    }
}

static void draw_dex_tabs(lv_layer_t *layer, int bx, int by)
{
    const int gap = 4;
    int inner = LCD_W - 16;
    int tw = (inner - gap * (DEX_CAT_N - 1)) / DEX_CAT_N;
    int total = DEX_CAT_N * tw + gap * (DEX_CAT_N - 1);
    int cx0 = bx + (LCD_W - total) / 2;

    for (int i = 0; i < DEX_CAT_N; i++) {
        int x = cx0 + i * (tw + gap);
        bool on = (i == s_dex_cat);
        uint32_t fg = on ? COL_WHITE : COL_INK;
        uint32_t sub = on ? COL_WHITE : COL_MUTE;

        rrect(layer, x, by + 40, tw, 32, 11, on ? COL_CORAL : COL_WHITE);
        draw_txt(layer, x, by + 42, tw, 16, app_str(DEX_CAT_STR[i]),
                 fg, LV_TEXT_ALIGN_CENTER);
        snprintf(s_line, sizeof(s_line), "%d/%d", dex_cat_got(i),
                 dex_cat_n(i));
        draw_txt(layer, x, by + 56, tw, 14, s_line, sub,
                 LV_TEXT_ALIGN_CENTER);
    }
}

static void draw_grid_card(lv_layer_t *layer, int x, int y, int w, int h,
                          bool sel, bool on)
{
    rrect(layer, x, y, w, h, 10, sel ? COL_RING : (on ? COL_WHITE : COL_LOCK));
    if (sel) {
        lv_draw_rect_dsc_t rd;
        lv_draw_rect_dsc_init(&rd);
        rd.bg_opa = LV_OPA_TRANSP;
        rd.border_width = 2;
        rd.border_color = lv_color_hex(COL_CORAL);
        rd.radius = 10;
        lv_area_t a = { x, y, x + w - 1, y + h - 1 };
        lv_draw_rect(layer, &rd, &a);
    }
}

static void draw_good_card(lv_layer_t *layer, int x, int y, int w, int h,
                           int id, bool sel, int take)
{
    int have = (int)app_meow_inv(&s_pet, id);
    int dmax = app_meow_good_dur_max(id);
    int dcur = (int)app_meow_dur(&s_pet, id);
    int pad = (h <= 68) ? 4 : 8;

    draw_grid_card(layer, x, y, w, h, sel, true);
    draw_good(layer, id, x + pad, y + pad);
    draw_txt(layer, x + pad + 32, y + pad, w - pad - 36, 16, good_name(id),
             COL_INK, LV_TEXT_ALIGN_LEFT);
    if (take >= 0) {
        snprintf(s_line, sizeof(s_line), app_str(APP_STR_MEOW_TRIP_PACK),
                 take, have);
    } else {
        snprintf(s_line, sizeof(s_line), "x%u", (unsigned)have);
    }
    draw_txt(layer, x + pad + 32, y + pad + 16, w - pad - 36, 16, s_line,
             COL_COINX, LV_TEXT_ALIGN_LEFT);
    snprintf(s_line, sizeof(s_line), app_str(APP_STR_MEOW_DUR), dcur, dmax);
    draw_txt(layer, x + pad, y + h - pad - 16, w - pad * 2, 16, s_line,
             COL_MUTE, LV_TEXT_ALIGN_LEFT);
}

static void draw_go_pill(lv_layer_t *layer, int bx, int by, const char *txt,
                         bool lit)
{
    rrect(layer, bx + 16, by + 208, 208, 36, 18,
          lit ? mix(COL_CORAL, COL_WHITE, 2) : COL_CORAL);
    draw_txt(layer, bx + 16, by + 216, 208, 20, txt, COL_WHITE,
             LV_TEXT_ALIGN_CENTER);
}

static void draw_score_pills(lv_layer_t *layer, int bx, int by, int best,
                             int run)
{
    rrect(layer, bx + 16, by, 100, 20, 10, COL_COIN);
    snprintf(s_line, sizeof(s_line), app_str(APP_STR_MEOW_BEST), best);
    draw_txt(layer, bx + 16, by + 2, 100, 16, s_line, COL_COINX,
             LV_TEXT_ALIGN_CENTER);
    rrect(layer, bx + 124, by, 100, 20, 10, COL_GEM);
    snprintf(s_line, sizeof(s_line), app_str(APP_STR_MEOW_RUN), run);
    draw_txt(layer, bx + 124, by + 2, 100, 16, s_line, COL_GEMX,
             LV_TEXT_ALIGN_CENTER);
}

static void page_shop(lv_layer_t *layer, int bx, int by)
{
    draw_header(layer, bx, by);
    draw_cat_tabs(layer, bx, by, BAG_CAT_STR, APP_MEOW_CAT_N, s_bag_cat);

    uint8_t ids[APP_MEOW_G_N];
    int n = app_meow_owned_list(&s_pet, s_bag_cat, ids, APP_MEOW_G_N);
    if (n <= 0) {
        rrect(layer, bx + 16, by + 80, 208, 80, 16, COL_WHITE);
        draw_txt(layer, bx + 16, by + 108, 208, 20, app_str(APP_STR_MEOW_BAG_EMPTY),
                 COL_MUTE, LV_TEXT_ALIGN_CENTER);
        return;
    }

    int vis = 4;
    int start = 0;
    if (n > vis) {
        start = s_sub - 2;
        if (start < 0) start = 0;
        if (start > n - vis) start = n - vis;
    }
    for (int i = 0; i < vis; i++) {
        int idx = start + i;
        if (idx >= n) break;
        int id = (int)ids[idx];
        int col = i % 2, row = i / 2;
        int x = bx + 16 + col * 108;
        int y = by + 74 + row * 86;
        bool sel = focused() && s_sub == idx;
        draw_good_card(layer, x, y, 100, 80, id, sel, -1);
    }
}

#define RHY_HIT_Y  246
#define RHY_TOP    40
#define RHY_LANE_W 88
#define RHY_X0     24
#define RHY_X1     128
#define RHY_REC_H  40

static uint32_t rhy_now(void)
{
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    return ms - s_rhy_t0;
}

static int rhy_y(uint32_t t_ms, uint32_t now)
{
    int span = RHY_HIT_Y - RHY_TOP;
    return RHY_HIT_Y - (int)((int32_t)(t_ms - now) * span /
                             (int)s_rhy.travel_ms);
}

static void draw_rhy_pop(lv_layer_t *layer, int cx, int cy, uint32_t age,
                         int grade)
{
    int r, s;
    uint32_t fc;

    if (age >= APP_MEOW_RHY_FX) return;
    r = 12 + (int)age / 6;
    if (r > 40) r = 40;
    fc = (grade == APP_MEOW_RHY_G_PERF) ? COL_GOLD : COL_WHITE;
    ring(layer, cx - r, cy - r, r * 2, r * 2, fc);
    if (age < 80) {
        ring(layer, cx - r + 6, cy - r + 6, r * 2 - 12, r * 2 - 12, COL_CORAL);
    }
    if (age < 160) {
        s = 24 - (int)age / 8;
        if (s < 8) s = 8;
        rrect(layer, cx - s, cy - 12, s * 2, 24, 12, fc);
        oval(layer, cx - 6, cy - 8, 12, 12, COL_WHITE);
    }
}

static void draw_catch_pop(lv_layer_t *layer, int cx, int cy, uint32_t age)
{
    int r, lift, sz;

    if (age >= FISH_FX_MS) return;
    r = 12 + (int)age / 6;
    if (r > 40) r = 40;
    ring(layer, cx - r, cy - r, r * 2, r * 2, COL_GOLD);
    if (age < 80) {
        ring(layer, cx - r + 6, cy - r + 6, r * 2 - 12, r * 2 - 12, COL_CORAL);
    }
    if (age < 160) {
        sz = FISH_SZ - (int)age / 12;
        if (sz < 10) sz = 10;
        draw_fish(layer, cx - sz / 2, cy - sz / 2, sz, sz, s_catch_pts == FISH_KOI_PTS);
    }
    lift = (int)age / 8;
    snprintf(s_line, sizeof(s_line), "+%d", s_catch_pts > 0 ? s_catch_pts : FISH_PTS);
    draw_txt(layer, cx - 20, cy - 28 - lift, 40, 16, s_line,
             (s_catch_pts == FISH_KOI_PTS) ? COL_GOLD : COL_CORAL,
             LV_TEXT_ALIGN_CENTER);
}

static void draw_run_pop(lv_layer_t *layer, int cx, int cy, int good,
                         uint32_t age)
{
    int r, sz, lift;

    if (age >= APP_MEOW_RUN_FX_MS) return;
    r = 12 + (int)age / 6;
    if (r > 40) r = 40;
    ring(layer, cx - r, cy - r, r * 2, r * 2, COL_GOLD);
    if (age < 80) {
        ring(layer, cx - r + 6, cy - r + 6, r * 2 - 12, r * 2 - 12, COL_CORAL);
    }
    if (age < 160) {
        sz = RUN_OBJ_SZ - (int)age / 12;
        if (sz < 10) sz = 10;
        draw_good_sz(layer, good, cx - sz / 2, cy - sz / 2, sz);
    }
    lift = (int)age / 8;
    draw_txt(layer, cx - 16, cy - 28 - lift, 32, 16, "+1", COL_GOLD,
             LV_TEXT_ALIGN_CENTER);
}

static void page_rhythm(lv_layer_t *layer, int bx, int by)
{
    static const int LX[2] = { RHY_X0, RHY_X1 };
    uint32_t now = rhy_now();
    int i, grade;
    const char *gstr;

    draw_score_pills(layer, bx, by + 6, s_rhy_best, s_rhy.score);
    if (s_rhy.combo >= 2) {
        snprintf(s_line, sizeof(s_line), "x%d", s_rhy.combo);
        draw_txt(layer, bx + 176, by + 28, 48, 16, s_line, COL_CORAL,
                 LV_TEXT_ALIGN_RIGHT);
    }
    grade = s_rhy.last_grade;
    if (grade == APP_MEOW_RHY_G_PERF) gstr = app_str(APP_STR_MEOW_PERF);
    else if (grade == APP_MEOW_RHY_G_GOOD) gstr = app_str(APP_STR_MEOW_GOOD);
    else if (grade == APP_MEOW_RHY_G_MISS) gstr = app_str(APP_STR_MEOW_MISS);
    else gstr = "";
    if (gstr[0]) {
        draw_txt(layer, bx + 16, by + 28, 208, 16, gstr,
                 (grade == APP_MEOW_RHY_G_MISS) ? COL_MUTE : COL_GOLD,
                 LV_TEXT_ALIGN_CENTER);
    }

    rrect(layer, bx + 16, by + RHY_HIT_Y - 8, 208, RHY_REC_H + 8, 12,
          COL_GOLD2);
    for (i = 0; i < 2; i++) {
        int x = bx + LX[i];
        bool near = app_meow_rhy_near(&s_rhy, i, now);
        bool down = (s_rhy_held & (1u << i)) != 0;
        bool pop = false;
        int k;
        uint32_t rec, rim;

        for (k = 0; k < s_rhy.n_n; k++) {
            if (s_rhy.n[k].lane == (uint8_t)i && s_rhy.fx_at[k] &&
                now < s_rhy.fx_at[k] + APP_MEOW_RHY_FX) {
                pop = true;
                break;
            }
        }
        rec = pop ? COL_GOLD : (down ? COL_CORAL : (near ? COL_GOLD : COL_WHITE));
        rim = pop ? COL_WHITE : (down ? COL_INK : (near ? COL_CORAL : COL_COINX));

        rrect(layer, x, by + RHY_TOP, RHY_LANE_W, RHY_HIT_Y - RHY_TOP,
              12, COL_RING);
        rrect(layer, x + 4, by + RHY_HIT_Y - 6, RHY_LANE_W - 8, RHY_REC_H,
              10, rim);
        rrect(layer, x + 8, by + RHY_HIT_Y - 2, RHY_LANE_W - 16, RHY_REC_H - 8,
              8, rec);
        if (near && (s_bob & 1)) {
            rrect(layer, x + 18, by + RHY_HIT_Y + 4, RHY_LANE_W - 36, 8, 4,
                  COL_WHITE);
        }
        draw_txt(layer, x, by + RHY_HIT_Y + RHY_REC_H, RHY_LANE_W, 16,
                 i ? "DOWN" : "UP", near ? COL_INK : COL_MUTE,
                 LV_TEXT_ALIGN_CENTER);
    }
    rrect(layer, bx + 22, by + RHY_HIT_Y + 6, 196, 3, 1, COL_INK);

    for (i = 0; i < s_rhy.n_n; i++) {
        const app_meow_rhy_note_t *n = &s_rhy.n[i];
        int x = bx + LX[n->lane] + 12;
        int nw = RHY_LANE_W - 24;
        int y0, y1, h;
        uint32_t c;

        if (s_rhy.st[i] == APP_MEOW_RHY_MISS) continue;
        if (s_rhy.st[i] == APP_MEOW_RHY_HIT) {
            draw_rhy_pop(layer, x + nw / 2, by + RHY_HIT_Y + 8,
                         now - s_rhy.fx_at[i], s_rhy.fx_grade[i]);
            continue;
        }
        y0 = by + rhy_y(n->t_ms, now);
        if (n->hold_ms) y1 = by + rhy_y((uint32_t)n->t_ms + n->hold_ms, now);
        else y1 = y0 - 22;
        if (y0 < by + 8 || y1 > by + LCD_H) continue;
        if (y1 < by + 8) y1 = by + 8;
        if (y0 > by + LCD_H - 8) y0 = by + LCD_H - 8;
        h = y0 - y1;
        if (h < 18) h = 18;
        c = (s_rhy.st[i] == APP_MEOW_RHY_OPEN) ? COL_GOLD
            : (n->lane ? COL_BLUE : COL_CORAL);
        if (n->hold_ms) {
            rrect(layer, x + 6, y1, nw - 12, h, 6, c);
        }
        rrect(layer, x, y0 - 22, nw, 24, 12, c);
        oval(layer, x + nw / 2 - 5, y0 - 16, 10, 10, COL_WHITE);
        if (s_rhy.st[i] == APP_MEOW_RHY_OPEN && s_rhy.fx_at[i] &&
            now < s_rhy.fx_at[i] + APP_MEOW_RHY_FX) {
            draw_rhy_pop(layer, x + nw / 2, by + RHY_HIT_Y + 8,
                         now - s_rhy.fx_at[i], s_rhy.fx_grade[i]);
        }
    }
    rrect(layer, bx + 22, by + RHY_HIT_Y + 6, 196, 3, 1, COL_INK);
}

static const char *result_game(void)
{
    if (s_over_kind == 1) return app_str(APP_STR_MEOW_BEAT);
    if (s_over_kind == 2) return app_str(APP_STR_MEOW_KIT);
    if (s_over_kind == 3) return app_str(APP_STR_MEOW_MAT);
    if (s_over_kind == 4) return app_str(APP_STR_MEOW_TRIP);
    return app_str(APP_STR_MEOW_FISH);
}

static void page_result(lv_layer_t *layer, int bx, int by)
{
    int best = (s_over_kind == 1) ? s_rhy_best :
               ((s_over_kind == 2) ? s_run_best :
                ((s_over_kind == 3) ? s_mat_best : s_play_best));
    bool ok = (s_bob & 1) != 0;
    bool scored = (s_over_kind <= 3);
    int i, kinds = 0;

    draw_header(layer, bx, by);
    snprintf(s_line, sizeof(s_line), "%s · %s", result_game(),
             app_str(APP_STR_MEOW_RESULT));
    draw_txt(layer, bx + 16, by + 32, 208, 20, s_line, COL_INK,
             LV_TEXT_ALIGN_CENTER);
    if (scored) {
        draw_score_pills(layer, bx, by + 54, best, s_over_score);
    }

    for (i = 0; i < APP_MEOW_G_N; i++) {
        if (s_over_got[i]) kinds++;
    }
    if (s_over_souv >= 0) kinds++;
    draw_txt(layer, bx + 16, by + 92, 208, 16, app_str(APP_STR_MEOW_REWARD),
             COL_MUTE, LV_TEXT_ALIGN_CENTER);
    if (kinds <= 0) {
        rrect(layer, bx + 16, by + 112, 208, 86, 16, COL_WHITE);
        draw_txt(layer, bx + 16, by + 146, 208, 20, app_str(APP_STR_MEOW_EMPTY),
                 COL_MUTE, LV_TEXT_ALIGN_CENTER);
    } else {
        int cols = (kinds <= 1) ? 1 : ((kinds == 2 || kinds == 4) ? 2 : 3);
        int rows = (kinds + cols - 1) / cols;
        int cw = (cols == 3) ? 64 : 96;
        int ch = (rows <= 1) ? 86 : 62;
        int gap = 8;
        int grid_w = cols * cw + (cols - 1) * gap;
        int x0 = bx + (LCD_W - grid_w) / 2;
        int y0 = by + 110;
        int k = 0;

        if (s_over_souv >= 0) {
            int y = y0;
            int x = x0;
            int gy = y + ((rows <= 1) ? 8 : 2);
            int ny = gy + 28;
            rrect(layer, x, y, cw, ch, 12, COL_WHITE);
            draw_souv_ico(layer, s_over_souv, x + (cw - 28) / 2, gy, 28);
            draw_txt(layer, x + 2, ny, cw - 4, 16,
                     app_str((app_str_id_t)(APP_STR_MEOW_SV0 + s_over_souv)),
                     COL_INK, LV_TEXT_ALIGN_CENTER);
            draw_txt(layer, x + 2, ny + 16, cw - 4, 16, "x1", COL_COINX,
                     LV_TEXT_ALIGN_CENTER);
            k++;
        }
        for (i = 0; i < APP_MEOW_G_N; i++) {
            int col, x, y, gy, ny;
            if (!s_over_got[i]) continue;
            col = k % cols;
            y = y0 + (k / cols) * (ch + gap);
            x = x0 + col * (cw + gap);
            gy = y + ((rows <= 1) ? 8 : 2);
            ny = gy + 28;
            rrect(layer, x, y, cw, ch, 12, COL_WHITE);
            draw_good(layer, i, x + (cw - 28) / 2, gy);
            draw_txt(layer, x + 2, ny, cw - 4, 16, good_name(i), COL_INK,
                     LV_TEXT_ALIGN_CENTER);
            snprintf(s_line, sizeof(s_line), "x%u", (unsigned)s_over_got[i]);
            draw_txt(layer, x + 2, ny + 16, cw - 4, 16, s_line, COL_COINX,
                     LV_TEXT_ALIGN_CENTER);
            k++;
            if (k >= 6) break;
        }
    }

    rrect(layer, bx + 50, by + 276, 140, 36, 18,
          ok ? mix(COL_CORAL, COL_WHITE, 2) : COL_CORAL);
    draw_txt(layer, bx + 50, by + 284, 140, 20, app_str(APP_STR_MEOW_CONFIRM),
             COL_WHITE, LV_TEXT_ALIGN_CENTER);
}

static int trip_take_n(void)
{
    int n = 0;
    for (int i = 0; i < APP_MEOW_G_N; i++) n += (int)s_trip_take[i];
    return n;
}

static void trip_time_txt(char *out, size_t n, int sec)
{
    if (sec < 0) sec = 0;
    snprintf(out, n, app_str(APP_STR_MEOW_TRIP_AWAY), sec / 60, sec % 60);
}

static void page_trip_pack(lv_layer_t *layer, int bx, int by)
{
    uint8_t ids[APP_MEOW_G_N];
    int n = app_meow_owned_list(&s_pet, APP_MEOW_CAT_FOOD, ids, APP_MEOW_G_N);
    int pack = trip_take_n();
    int vis = 4;
    int start = 0;
    int card = (s_sub < n) ? s_sub : (n > 0 ? n - 1 : 0);
    const char *go;

    draw_header(layer, bx, by);
    draw_txt(layer, bx + 16, by + 36, 208, 16, app_str(APP_STR_MEOW_TRIP_HINT),
             COL_MUTE, LV_TEXT_ALIGN_CENTER);
    if (n <= 0) {
        rrect(layer, bx + 16, by + 80, 208, 80, 16, COL_WHITE);
        draw_txt(layer, bx + 16, by + 108, 208, 20,
                 app_str(APP_STR_MEOW_BAG_EMPTY), COL_MUTE,
                 LV_TEXT_ALIGN_CENTER);
        return;
    }
    if (n > vis) {
        start = card - 2;
        if (start < 0) start = 0;
        if (start > n - vis) start = n - vis;
    }
    for (int i = 0; i < vis; i++) {
        int idx = start + i;
        int col = i % 2, row = i / 2;
        int x = bx + 12 + col * 102;
        int y = by + 52 + row * 70;
        bool sel = focused() && s_sub == idx;

        if (idx >= n) break;
        draw_good_card(layer, x, y, 96, 64, (int)ids[idx], sel,
                       (int)s_trip_take[ids[idx]]);
    }
    if (pack > 0) {
        int sec = app_meow_trip_sec(app_meow_trip_take_gain(s_trip_take));
        snprintf(s_line, sizeof(s_line), "%s  %d/%d  %d:%02d",
                 app_str(APP_STR_MEOW_TRIP_GO), pack, APP_MEOW_TRIP_PACK_MAX,
                 sec / 60, sec % 60);
        go = s_line;
    } else {
        go = app_str(APP_STR_MEOW_TRIP_GO);
    }
    draw_go_pill(layer, bx, by, go, focused() && s_sub == n);
}

static void draw_haz(lv_layer_t *layer, int x, int y, int sz, int id)
{
    lv_draw_image_dsc_t d;
    lv_area_t a;

    int pulse = (s_bob & 1);

    if (id < 0 || id >= APP_MEOW_HAZ_N) id = 0;
    oval(layer, x - 5 - pulse, y - 5 - pulse, sz + 10 + pulse * 2,
         sz + 10 + pulse * 2, 0xF07070);
    ring(layer, x - 3, y - 3, sz + 6, sz + 6, COL_ALERT);
    lv_draw_image_dsc_init(&d);
    d.src = &app_meow_haz_img[id];
    if (sz != 28) {
        d.scale_x = sz * 256 / 28;
        d.scale_y = sz * 256 / 28;
        d.pivot.x = 0;
        d.pivot.y = 0;
    }
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + 27;
    a.y2 = y + 27;
    lv_draw_image(layer, &d, &a);
}

static int run_lane_x(int bx, int lane)
{
    return bx + RUN_TRACK_X + lane * RUN_LANE_W;
}

static int run_obj_y(int by, const app_meow_run_obj_t *o, uint32_t now)
{
    int p = app_meow_run_prog(o, now);
    int span = RUN_KIT_Y - RUN_TOP;

    if (p < 0) p = 0;
    if (p > 1100) p = 1100;
    return by + RUN_TOP + span * p / 1000;
}

static void page_run(lv_layer_t *layer, int bx, int by)
{
    uint32_t now = now_ms() - s_run_t0;
    int den = 6 * 10 / (10 + app_meow_run_spd_n(now));
    int off, i, y, lane;
    int kx, ky;

    if (den < 1) den = 1;
    off = (int)((now / (uint32_t)den) % 20);
    rrect(layer, bx, by, LCD_W, LCD_H, 0, COL_HILL);
    rrect(layer, bx + RUN_TRACK_X - 6, by + RUN_TOP - 8,
          RUN_TRACK_W + 12, LCD_H - RUN_TOP + 8, 10, COL_HILL2);
    for (lane = 0; lane < APP_MEOW_RUN_LANES; lane++) {
        rrect(layer, run_lane_x(bx, lane) + 4, by + RUN_TOP,
              RUN_LANE_W - 8, LCD_H - RUN_TOP - 8, 8, COL_CREAM);
    }
    for (y = by + RUN_TOP - off; y < by + LCD_H; y += 20) {
        for (lane = 1; lane < APP_MEOW_RUN_LANES; lane++) {
            rrect(layer, run_lane_x(bx, lane) - 2, y, 4, 10, 2, COL_WHITE);
        }
    }
    draw_score_pills(layer, bx, by + 8, s_run_best, (int)s_run.items);
    if (s_run.last_good >= 0 && now >= s_run.last_at &&
        now - s_run.last_at < APP_MEOW_RUN_TIP_MS) {
        snprintf(s_line, sizeof(s_line), "%s +1", good_name(s_run.last_good));
        rrect(layer, bx + 36, by + 30, 168, 18, 9, COL_GOLD);
        draw_txt(layer, bx + 40, by + 31, 160, 16, s_line, COL_INK,
                 LV_TEXT_ALIGN_CENTER);
    }
    for (i = 0; i < s_run.n; i++) {
        const app_meow_run_obj_t *o = &s_run.o[i];
        int ox, oy;

        if (o->st != APP_MEOW_RUN_LIVE) continue;
        ox = run_lane_x(bx, o->lane) + (RUN_LANE_W - RUN_OBJ_SZ) / 2;
        oy = run_obj_y(by, o, now);
        if (o->kind == APP_MEOW_RUN_HAZ) {
            draw_haz(layer, ox, oy, RUN_OBJ_SZ, o->good);
        }
        else draw_good(layer, o->good, ox, oy);
    }
    kx = run_lane_x(bx, s_run.lane) + (RUN_LANE_W - RUN_KIT_SZ) / 2;
    ky = by + RUN_KIT_Y - (s_bob & 1) * 2;
    draw_pet(layer, kx, ky, RUN_KIT_SZ);
    if (s_run.last_good >= 0 && now >= s_run.last_at &&
        now - s_run.last_at < APP_MEOW_RUN_FX_MS) {
        int cx = run_lane_x(bx, s_run.last_lane) + RUN_LANE_W / 2;
        int cy = by + RUN_KIT_Y + RUN_KIT_SZ / 2;
        draw_run_pop(layer, cx, cy, s_run.last_good, now - s_run.last_at);
    }
}

static void page_match(lv_layer_t *layer, int bx, int by)
{
    int board = MAT_CELL * APP_MEOW_MAT_W;
    int x0 = bx + (LCD_W - board) / 2;
    int y0 = by + MAT_TOP;
    int r, c;
    int sec = app_meow_mat_sec(&s_mat);
    int tbar;
    uint32_t tc;

    draw_score_pills(layer, bx, by + 6, s_mat_best, s_mat.score);
    tbar = sec * 100 / (APP_MEOW_MAT_TIME0 / 1000);
    if (tbar > 100) tbar = 100;
    tc = (sec <= 5) ? COL_ALERT : COL_TEAL;
    rrect(layer, bx + 16, by + 30, 168, 16, 8, COL_SLOT);
    if (tbar > 0) {
        int tw = 168 * tbar / 100;
        if (tw < 16) tw = 16;
        rrect(layer, bx + 16, by + 30, tw, 16, 8, tc);
    }
    snprintf(s_line, sizeof(s_line), "%d", sec);
    draw_txt(layer, bx + 184, by + 30, 40, 16, s_line, tc, LV_TEXT_ALIGN_RIGHT);

    {
        int vanish = (s_mat.anim == APP_MEOW_MAT_ST_VANISH);
        int falling = (s_mat.anim == APP_MEOW_MAT_ST_FALL);
        int at = 0;
        int dur = vanish ? APP_MEOW_MAT_VANISH_MS : APP_MEOW_MAT_FALL_MS;

        if ((vanish || falling) && dur > 0) {
            at = (int)s_mat.anim_ms * 256 / dur;
            if (at > 256) at = 256;
        }
        for (r = 0; r < APP_MEOW_MAT_H; r++) {
            for (c = 0; c < APP_MEOW_MAT_W; c++) {
                int x = x0 + c * MAT_CELL;
                int y = y0 + r * MAT_CELL;
                int id = (int)s_mat.cell[r][c];
                bool cur = (r == (int)s_mat.cur_r && c == (int)s_mat.cur_c);
                bool sel = (s_mat.sel_r == r && s_mat.sel_c == c);
                bool marked = vanish && s_mat.mark[r][c];
                uint32_t bg = marked ? COL_CORAL :
                              (sel ? COL_GOLD2 : (cur ? COL_RING : COL_WHITE));
                int iz = MAT_ICON;
                int ix, iy;

                rrect(layer, x + 1, y + 1, MAT_CELL - 2, MAT_CELL - 2, 8, bg);
                if (cur && !s_mat.anim) {
                    rrect(layer, x + 1, y + 1, MAT_CELL - 2, 3, 1, COL_CORAL);
                    rrect(layer, x + 1, y + MAT_CELL - 4, MAT_CELL - 2, 3, 1,
                          COL_CORAL);
                }
                if (id < 0 || id >= APP_MEOW_MAT_KIND) continue;
                if (marked) {
                    iz = MAT_ICON * (256 - at) / 256;
                    if (iz < 2) continue;
                }
                ix = x + (MAT_CELL - iz) / 2;
                iy = y + (MAT_CELL - iz) / 2;
                if (falling && s_mat.fall[r][c]) {
                    iy -= (int)s_mat.fall[r][c] * MAT_CELL * (256 - at) / 256;
                    if (iy + iz <= y0) continue;
                }
                draw_good_sz(layer, app_meow_mat_good(&s_mat, id), ix, iy, iz);
            }
        }
    }
    draw_txt(layer, bx + 8, by + 274, 224, 36,
             app_str(APP_STR_MEOW_MAT_HINT), COL_MUTE, LV_TEXT_ALIGN_CENTER);
}

static void page_game(lv_layer_t *layer, int bx, int by)
{
    int best = (s_sub == 1) ? s_rhy_best :
               ((s_sub == 2) ? s_run_best :
                ((s_sub == 3) ? s_mat_best : s_play_best));
    bool rhy = (s_sub == 1);
    bool kit = (s_sub == 2);
    bool mat = (s_sub == 3);
    bool trip = (s_sub == 4);
    int i;
    const char *go;
    const char *title;

    if (s_mode == MODE_PLAY) {
        int pw = play_pad_w();
        int px = bx + s_pad_x;
        int fy = by + FISH_TOP + s_fish_y;

        draw_score_pills(layer, bx, by + 8, s_play_best, s_play_run);
        draw_fish(layer, bx + s_fish_x, fy, FISH_SZ, FISH_SZ, s_fish_koi);
        if (s_catch_at) {
            uint32_t age = now_ms() - s_catch_at;
            if (age < FISH_FX_MS) {
                draw_catch_pop(layer, bx + s_catch_x, by + s_catch_y, age);
            }
        }
        rrect(layer, px, by + PAD_Y, pw, PAD_H, 11, COL_CORAL);
        oval(layer, px + pw / 2 - 16, by + PAD_Y + 4, 8, 8, COL_WHITE);
        oval(layer, px + pw / 2 - 4, by + PAD_Y + 6, 8, 8, COL_WHITE);
        oval(layer, px + pw / 2 + 8, by + PAD_Y + 4, 8, 8, COL_WHITE);
        return;
    }
    if (s_mode == MODE_RHYTHM) {
        page_rhythm(layer, bx, by);
        return;
    }
    if (s_mode == MODE_RUN) {
        page_run(layer, bx, by);
        return;
    }
    if (s_mode == MODE_MATCH) {
        page_match(layer, bx, by);
        return;
    }
    if (s_mode == MODE_RESULT) {
        page_result(layer, bx, by);
        return;
    }
    if (s_trip_edit) {
        page_trip_pack(layer, bx, by);
        return;
    }

    draw_header(layer, bx, by);

    draw_score_pills(layer, bx, by + 40, best, s_play_run);

    rrect(layer, bx + 16, by + 68, 208, 118, 16, COL_RING);
    if (trip) title = app_str(APP_STR_MEOW_TRIP);
    else if (rhy) title = app_str(APP_STR_MEOW_BEAT);
    else if (kit) title = app_str(APP_STR_MEOW_KIT);
    else if (mat) title = app_str(APP_STR_MEOW_MAT);
    else title = app_str(APP_STR_MEOW_FISH);
    draw_txt(layer, bx + 36, by + 74, 168, 18, title, COL_INK,
             LV_TEXT_ALIGN_CENTER);
    if (trip) {
        int hop = (s_bob * 4) % 36;
        int psz = 48;
        int bob = (s_bob & 1) * 2;
        draw_pet(layer, bx + 40 + hop, by + 104 - bob, psz);
        rrect(layer, bx + 40, by + 160, 160, 6, 3, COL_SLOT);
        rrect(layer, bx + 40, by + 160, 20 + hop * 3, 6, 3, COL_TEAL);
        if (s_pet.trip_st == APP_MEOW_TRIP_AWAY) {
            trip_time_txt(s_line, sizeof(s_line),
                          app_meow_trip_sec_left(&s_pet, now_sec()));
            draw_txt(layer, bx + 36, by + 170, 168, 16, s_line, COL_TEAL,
                     LV_TEXT_ALIGN_CENTER);
        }
    } else if (rhy) {
        int fy = by + 100 + (s_bob * 5) % 40;
        rrect(layer, bx + 40, fy, 28, 36, 8, COL_CORAL);
        rrect(layer, bx + 148, fy + 8, 28, 18, 8, COL_BLUE);
        rrect(layer, bx + 36, by + 164, 40, 8, 4, COL_CORAL);
        rrect(layer, bx + 144, by + 164, 40, 8, 4, COL_BLUE);
    } else if (kit) {
        int hop = (s_bob * 5) % 36;
        int bob = (s_bob & 1) * 2;
        rrect(layer, bx + 40, by + 98, 48, 70, 8, COL_CREAM);
        rrect(layer, bx + 96, by + 98, 48, 70, 8, COL_CREAM);
        rrect(layer, bx + 152, by + 98, 48, 70, 8, COL_CREAM);
        draw_good(layer, APP_MEOW_G_ONIGIRI, bx + 106, by + 104 + hop / 2);
        draw_haz(layer, bx + 158, by + 108, 28, 2);
        draw_haz(layer, bx + 52, by + 118, 28, 0);
        draw_pet(layer, bx + 88, by + 128 - bob, 48);
    } else if (mat) {
        int gr, gc;
        for (gr = 0; gr < 3; gr++) {
            for (gc = 0; gc < 3; gc++) {
                int gx = bx + 72 + gc * 32;
                int gy = by + 98 + gr * 28;
                rrect(layer, gx, gy, 28, 26, 6, COL_WHITE);
                draw_good_sz(layer, (gr + gc) % APP_MEOW_MAT_KIND, gx + 2, gy - 1, 24);
            }
        }
    } else {
        int fy = by + 96 + (s_bob * 6) % 50;
        int px = bx + (LCD_W - PAD_W) / 2;
        draw_fish(layer, bx + 44, fy - 4, 24, 24, false);
        draw_fish(layer, bx + 146, fy + 4, 24, 24, true);
        if ((s_bob & 1) == 0) {
            draw_txt(layer, bx + 70, fy, 28, 14, "+30", COL_CORAL,
                     LV_TEXT_ALIGN_LEFT);
        }
        rrect(layer, px, by + 164, PAD_W, PAD_H, 11, COL_CORAL);
        oval(layer, px + PAD_W / 2 - 16, by + 168, 8, 8, COL_WHITE);
        oval(layer, px + PAD_W / 2 - 4, by + 170, 8, 8, COL_WHITE);
        oval(layer, px + PAD_W / 2 + 8, by + 168, 8, 8, COL_WHITE);
    }
    for (i = 0; i < GAME_N; i++) {
        uint32_t dc = (i == s_sub) ? COL_CORAL : COL_LOCK;
        oval(layer, bx + 86 + i * 14, by + 190, 8, 8, dc);
    }

    if (trip && s_pet.trip_st == APP_MEOW_TRIP_AWAY) {
        go = s_line;
        trip_time_txt(s_line, sizeof(s_line),
                      app_meow_trip_sec_left(&s_pet, now_sec()));
    } else if (trip && s_pet.trip_st == APP_MEOW_TRIP_BACK) {
        go = app_str(APP_STR_MEOW_TRIP_BACK);
    } else if (trip) {
        go = app_str(APP_STR_MEOW_TRIP_GO);
    } else {
        go = app_str(APP_STR_MEOW_START);
    }
    draw_go_pill(layer, bx, by, go, focused());
}

static void draw_souv_ico(lv_layer_t *layer, int id, int x, int y, int sz)
{
    lv_draw_image_dsc_t d;
    lv_area_t a;
    int s = sz > 0 ? sz : 28;

    if (id < 0 || id >= APP_MEOW_SOUV_N) return;
    lv_draw_image_dsc_init(&d);
    d.src = &app_meow_souv_img[id];
    if (s != 28) {
        d.scale_x = s * 256 / 28;
        d.scale_y = s * 256 / 28;
        d.pivot.x = 0;
        d.pivot.y = 0;
    }
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + 27;
    a.y2 = y + 27;
    lv_draw_image(layer, &d, &a);
}

static void draw_ach_ico(lv_layer_t *layer, int id, int x, int y, int sz)
{
    static const int ICO[DEX_ACH_N] = {
        ICO_PLAY, ICO_HEART, ICO_DEX, ICO_FEED,
        ICO_BATH, ICO_HEAL, ICO_FISH, ICO_GAME
    };
    int s = sz > 0 ? sz : 28;
    int ico;

    if (id == 2) {
        lv_draw_image_dsc_t d;
        lv_area_t a;

        lv_draw_image_dsc_init(&d);
        d.src = &app_meow_stage_img[APP_MEOW_ADULT];
        if (s != 32) {
            d.scale_x = s * 256 / 32;
            d.scale_y = s * 256 / 32;
            d.pivot.x = 0;
            d.pivot.y = 0;
        }
        a.x1 = x;
        a.y1 = y;
        a.x2 = x + 31;
        a.y2 = y + 31;
        lv_draw_image(layer, &d, &a);
        return;
    }
    ico = s > 4 ? s - 4 : s;
    if (id >= 0 && id < DEX_ACH_N) {
        draw_ico(layer, x + (s - ico) / 2, y + (s - ico) / 2, ico, ico, ICO[id]);
    }
}

static void draw_dex_tip(lv_layer_t *layer, int bx, int by,
                         const char *a, const char *b)
{
    int two = (b && b[0]);
    int h = two ? 36 : 20;
    int y = by + PAGE_H - 4 - h;

    rrect(layer, bx + 12, y, 216, h, 10, COL_CORAL);
    draw_txt(layer, bx + 16, y + 2, 208, 16, a, COL_WHITE,
             LV_TEXT_ALIGN_CENTER);
    if (two) {
        draw_txt(layer, bx + 16, y + 18, 208, 16, b, COL_WHITE,
                 LV_TEXT_ALIGN_CENTER);
    }
}

static void page_dex(lv_layer_t *layer, int bx, int by)
{
    int n = inner_n();
    int vis = 9;
    int start = 0;
    const int cols = 3, cw = 64, ch = 46, gapx = 8, gapy = 4, ico = 28;
    char tip1[48], tip2[48];

    draw_header(layer, bx, by);
    draw_dex_tabs(layer, bx, by);

    if (n > vis) {
        start = s_sub - 3;
        if (start < 0) start = 0;
        if (start > n - vis) start = n - vis;
    }
    for (int i = 0; i < vis; i++) {
        int idx = start + i;
        int col, row, x, y, iy;
        bool on, sel;

        if (idx >= n) break;
        col = i % cols;
        row = i / cols;
        x = bx + 16 + col * (cw + gapx);
        y = by + 78 + row * (ch + gapy);
        on = dex_on(idx);
        sel = focused() && s_sub == idx;
        draw_grid_card(layer, x, y, cw, ch, sel, on);
        if (!on) {
            draw_txt(layer, x, y + (ch - 16) / 2, cw, 16, "?", COL_LOCKX,
                     LV_TEXT_ALIGN_CENTER);
        } else {
            int sz = (s_dex_cat == DEX_CAT_PET) ? 36 : ico;
            iy = y + (s_dex_cat == DEX_CAT_ITM ? 4 : (ch - sz) / 2);
            if (s_dex_cat == DEX_CAT_PET) {
                draw_pet_img(layer, x + (cw - sz) / 2, iy, sz, sz, idx,
                             false, false);
            } else if (s_dex_cat == DEX_CAT_ACH) {
                draw_ach_ico(layer, idx, x + (cw - sz) / 2, iy, sz);
            } else if (s_dex_cat == DEX_CAT_ITM) {
                draw_good_sz(layer, idx, x + (cw - ico) / 2, iy, ico);
                snprintf(s_line, sizeof(s_line), "x%u",
                         (unsigned)app_meow_inv(&s_pet, idx));
                draw_txt(layer, x, y + ch - 15, cw, 14, s_line,
                         sel ? COL_INK : COL_MUTE, LV_TEXT_ALIGN_CENTER);
            } else {
                draw_souv_ico(layer, idx, x + (cw - ico) / 2, iy, ico);
            }
        }
    }
    if (focused()) {
        dex_blurb_split(s_sub, tip1, sizeof(tip1), tip2, sizeof(tip2));
        draw_dex_tip(layer, bx, by, tip1, tip2);
    }
}

static void ibar_draw(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    if (app_meow_set_open_now()) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    rrect(layer, box.x1, box.y1, LCD_W, NAV_H, 0, COL_WHITE);
    int mark = s_sel;
    for (int i = 0; i < TAB_N; i++) {
        int x = box.x1 + i * 48;
        int cx = x + 24;
        bool on = (i == mark);
        static const int TAB_ICO[TAB_N] = {
            ICO_HOME, ICO_BAG, ICO_GAME, ICO_DEX, ICO_SET
        };
        if (on) rrect(layer, cx - 16, box.y1 + 8, 32, 28, 10, COL_CORAL);
        draw_ico(layer, cx - 12, box.y1 + 10, 24, 24, TAB_ICO[i]);
    }
    draw_alert_edge(layer, box.x1, box.y1, LCD_W, NAV_H);
}

static void page_name(lv_layer_t *layer, int bx, int by)
{
    const char *const *keys = app_kb_keys(s_kb_set);
    const char *shown = s_name_buf[0] ? s_name_buf : default_name();
    int kw = 36, kh = 22, gap = 0;
    int x0 = bx + 12, y0 = by + 78;

    draw_txt(layer, bx + 16, by + 10, 208, 18, app_str(APP_STR_MEOW_NAME),
             ui_ink(), LV_TEXT_ALIGN_CENTER);
    draw_txt(layer, bx + 16, by + 28, 208, 16, app_str(APP_STR_MEOW_NAME_HINT),
             COL_MUTE, LV_TEXT_ALIGN_CENTER);
    rrect(layer, bx + 16, by + 46, 208, 24, 12, COL_WHITE);
    draw_txt(layer, bx + 24, by + 50, 192, 16, shown,
             s_name_buf[0] ? COL_INK : COL_MUTE, LV_TEXT_ALIGN_LEFT);
    for (int i = 0; i < KB_N; i++) {
        int c = i % KB_COLS, r = i / KB_COLS;
        int x = x0 + c * kw, y = y0 + r * kh;
        bool sel = (i == s_kb_sel);
        rrect(layer, x + gap, y + gap, kw - 2, kh - 2, 6,
              sel ? COL_CORAL : COL_WHITE);
        draw_txt(layer, x, y + 2, kw - 2, 16, keys[i],
                 sel ? COL_WHITE : COL_INK, LV_TEXT_ALIGN_CENTER);
    }
}

static void stage_draw(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    if (app_meow_set_open_now()) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    int bx = box.x1, by = box.y1;
    int tab = s_sel;
    if (s_name_edit) {
        page_name(layer, bx, by);
        draw_flash(layer, bx, by);
        return;
    }
    if (minigame() || s_mode == MODE_LINK) tab = TAB_GAME;
    if (tab == TAB_SHOP) page_shop(layer, bx, by);
    else if (tab == TAB_GAME) page_game(layer, bx, by);
    else if (tab == TAB_DEX) page_dex(layer, bx, by);
    else if (tab == TAB_SET) page_set(layer, bx, by);
    else page_home(layer, bx, by);
    if (s_mode == MODE_LINK) {
        int cx = bx + 120, cy = by + 120;
        int pulse = (s_bob % 5) * 6;
        for (int i = 0; i < 3; i++) {
            int r = 26 + pulse + i * 16;
            ring(layer, cx - r, cy - r, r * 2, r * 2,
                 (i == 0) ? COL_TEAL : COL_SKY);
        }
    }
    draw_flash(layer, bx, by);
    if (s_mode == MODE_CARE && !app_meow_set_open_now() &&
        ((app_ota_prompt() && app_prefs()->ota_auto) ||
         app_ota_state() == APP_OTA_APPLYING)) {
        const char *hint = app_ota_state() == APP_OTA_APPLYING
            ? app_str(APP_STR_OTA_HOLD) : app_str(APP_STR_OTA_READY);
        rrect(layer, bx + 16, by + 88, 208, 52, 12, COL_FACE);
        draw_txt(layer, bx + 24, by + 94, 192, 16, status_text(),
                 COL_WHITE, LV_TEXT_ALIGN_CENTER);
        draw_txt(layer, bx + 24, by + 114, 192, 16, hint,
                 COL_WHITE, LV_TEXT_ALIGN_CENTER);
    }
    draw_alert_edge(layer, bx, by, LCD_W,
                    minigame() ? LCD_H : PAGE_H);
}

static void ble_off(void)
{
    if (bsp_ble_state() == BSP_BLE_PAIRING) return;
    (void)bsp_ble_set_enabled(false);
}

static void alert_tm_cb(void *arg);
static void bed_tm_cb(void *arg);
static void call_bedtime(void);
static void flash_for(const char *s, int tone, int ticks);

static void arm_alert_recall(void)
{
    if (app_meow_alert_peak(&s_pet) < APP_MEOW_ALERT_HIT) {
        if (s_alert_tm) esp_timer_stop(s_alert_tm);
        return;
    }
    if (!s_alert_tm) {
        const esp_timer_create_args_t a = {
            .callback = alert_tm_cb,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "meow_alert",
        };
        if (esp_timer_create(&a, &s_alert_tm) != ESP_OK) return;
    }
    esp_timer_stop(s_alert_tm);
    esp_timer_start_once(s_alert_tm, ALERT_RECALL_US);
}

static void disarm_alert_recall(void)
{
    if (s_alert_tm) esp_timer_stop(s_alert_tm);
}

static void disarm_bed_timer(void)
{
    if (s_bed_tm) esp_timer_stop(s_bed_tm);
}

static void arm_bed_timer(void)
{
    time_t t;
    struct tm tm;
    int bed = (int)app_prefs()->meow_bed;
    int wake = (int)app_prefs()->meow_wake;
    int now_s, bed_s, d;

    disarm_bed_timer();
    if (now_hour() < 0 || bed == wake) return;
    if (app_meow_asleep_at(now_hour(), bed, wake)) return;

    t = time(NULL);
    localtime_r(&t, &tm);
    now_s = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    bed_s = bed * 3600;
    d = bed_s - now_s;
    if (d <= 0) d += 86400;
    if (!s_bed_tm) {
        const esp_timer_create_args_t a = {
            .callback = bed_tm_cb,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "meow_bed",
        };
        if (esp_timer_create(&a, &s_bed_tm) != ESP_OK) return;
    }
    esp_timer_start_once(s_bed_tm, (int64_t)d * 1000000LL);
}

static void sleep_now(void)
{
    if (s_asleep) return;
    if (s_dirty) save_nvs();
    s_asleep = true;
    s_wifi_wait = false;
    s_awake_ms = 0;
    ble_off();
    bsp_display_backlight(0);
    lv_refr_now(NULL);
    bsp_lvgl_flush_enable(false);
    bsp_display_sleep(true);
    bsp_audio_standby();
    bsp_wifi_radio_suspend();
    bsp_button_sleep_gpio(true);
    bsp_lvgl_tick_enable(false);
    bsp_pm_set_sleeping(true);
    arm_alert_recall();
    arm_bed_timer();
}

static void wake_now(void)
{
    s_idle_ms = 0;
    s_still_ms = 0;
    disarm_alert_recall();
    disarm_bed_timer();
    if (!s_asleep) return;
    s_asleep = false;
    s_awake_ms = 0;
    s_wifi_wait = true;
    bsp_pm_set_perf(true);
    bsp_pm_set_sleeping(false);
    bsp_button_sleep_gpio(false);
    bsp_lvgl_tick_enable(true);
    bsp_display_sleep(false);
    bsp_lvgl_flush_enable(true);
    lv_obj_invalidate(s_scr);
    lv_refr_now(NULL);
    app_prefs_apply_display();
    s_tick_ms = 250;
    if (s_timer) lv_timer_set_period(s_timer, 250);
}

static void on_gpio_wake(void)
{
    if (!bsp_lvgl_lock(1000)) return;
    s_wake_skip = true;
    wake_now();
    bsp_lvgl_unlock();
}

static void on_ble_evt(void *h, esp_event_base_t b, int32_t id, void *d)
{
    (void)h;
    (void)b;
    (void)id;
    (void)d;
    if (!bsp_lvgl_lock(1000)) return;
    wake_now();
    bsp_lvgl_unlock();
}

static void on_ble_activity(void)
{
    if (!s_asleep) return;
    esp_event_post(MEOW_EVENT, MEOW_BLE_WAKE, NULL, 0, 0);
}

static void alert_tm_cb(void *arg)
{
    (void)arg;
    esp_event_post(MEOW_EVENT, MEOW_ALERT_WAKE, NULL, 0, 0);
}

static void bed_tm_cb(void *arg)
{
    (void)arg;
    esp_event_post(MEOW_EVENT, MEOW_BED_WAKE, NULL, 0, 0);
}

static app_str_id_t stage_id(void)
{
    switch (s_pet.stage) {
    case APP_MEOW_EGG: return APP_STR_MEOW_EGG;
    case APP_MEOW_BABY: return APP_STR_MEOW_BABY;
    case APP_MEOW_CHILD: return APP_STR_MEOW_CHILD;
    case APP_MEOW_TEEN: return APP_STR_MEOW_TEEN;
    case APP_MEOW_ADULT: return APP_STR_MEOW_ADULT;
    default: return APP_STR_MEOW_DEAD;
    }
}

static app_str_id_t species_id(void)
{
    if (s_pet.species >= 1 && s_pet.species <= 10) {
        return (app_str_id_t)(APP_STR_MEOW_SP1 + s_pet.species - 1);
    }
    return stage_id();
}

static const char *status_text(void)
{
    if (s_flash_left > 0 && s_flash[0]) return s_flash;
    if (app_ota_state() == APP_OTA_APPLYING) {
        snprintf(s_line, sizeof(s_line), app_str(APP_STR_OTA_APPLYING),
                 app_ota_progress());
        return s_line;
    }
    if (app_ota_prompt() && app_prefs()->ota_auto && s_mode == MODE_CARE) {
        snprintf(s_line, sizeof(s_line), app_str(APP_STR_OTA_NEW),
                 app_ota_new_ver());
        return s_line;
    }
    if (bsp_ble_state() == BSP_BLE_PAIRING) {
        static char pair[24];
        snprintf(pair, sizeof(pair), "%s %06lu", app_str(APP_STR_BT_CODE),
                 (unsigned long)bsp_ble_passkey());
        return pair;
    }
    if (s_mode == MODE_RESULT) return "";
    if (s_mode == MODE_PLAY) return app_str(APP_STR_MEOW_PLAY_HINT);
    if (s_mode == MODE_RHYTHM) return app_str(APP_STR_MEOW_BEAT_HINT);
    if (s_mode == MODE_RUN) return app_str(APP_STR_MEOW_KIT_HINT);
    if (s_mode == MODE_MATCH) return app_str(APP_STR_MEOW_MAT_HINT);
    if (s_mode == MODE_LINK) return app_str(APP_STR_MEOW_LOOK);
    if (s_pet.trip_st == APP_MEOW_TRIP_BACK) return app_str(APP_STR_MEOW_TRIP_BACK);
    if (s_pet.trip_st == APP_MEOW_TRIP_AWAY) {
        trip_time_txt(s_line, sizeof(s_line),
                      app_meow_trip_sec_left(&s_pet, now_sec()));
        return s_line;
    }
    int peak = app_meow_alert_peak(&s_pet);
    if (peak >= APP_MEOW_ALERT_WARN) {
        int d = 0, best = 0;
        for (int i = 0; i < APP_MEOW_D_N; i++) {
            int v = app_meow_danger_lv(&s_pet, i);
            if (v > best) {
                best = v;
                d = i;
            }
        }
        snprintf(s_line, sizeof(s_line), "%s %d%%",
                 app_str((app_str_id_t)(APP_STR_MEOW_D_HUNGER + d)),
                 best >= APP_MEOW_ALERT_CRIT ? APP_MEOW_ALERT_PCT_CRIT :
                 (best >= APP_MEOW_ALERT_HIT ? APP_MEOW_ALERT_PCT_HIT :
                  APP_MEOW_ALERT_PCT_WARN));
        return s_line;
    }
    if (s_pet.stage == APP_MEOW_DEAD) return app_str(APP_STR_MEOW_DEAD);
    if (s_pet.stage == APP_MEOW_EGG) return app_str(APP_STR_MEOW_HATCH);
    if (s_pet.sleeping) {
        return app_str(s_pet.lights_off ? APP_STR_MEOW_SLEEP
                                        : APP_STR_MEOW_SLEEP_NOW);
    }
    if (s_pet.sick) {
        if (s_pet.ailment >= APP_MEOW_AI_WORM &&
            s_pet.ailment <= APP_MEOW_AI_COUGH) {
            return app_str((app_str_id_t)(APP_STR_MEOW_AI_WORM +
                                          s_pet.ailment - 1));
        }
        return app_str(APP_STR_MEOW_SICK);
    }
    if (s_pet.poop) return app_str(APP_STR_MEOW_POOP);
    if (s_pet.hunger == 0) return app_str(APP_STR_MEOW_HUNGRY);
    if (s_pet.happy == 0) return app_str(APP_STR_MEOW_SAD);
    if (s_pet.lights_off) return app_str(APP_STR_MEOW_DARK);
    return app_str(APP_STR_MEOW_OK);
}

static void hide_obj(lv_obj_t *o, bool hid)
{
    if (!o) return;
    if (hid) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void close_menu(void)
{
    s_menu = -1;
    s_sub = 0;
    s_trip_edit = false;
}

static void kb_hold_stop(void)
{
    s_kb_hold_btn = -1;
    s_kb_hold_ms = 0;
    if (s_kb_hold_tm) {
        lv_timer_delete(s_kb_hold_tm);
        s_kb_hold_tm = NULL;
    }
}

static void kb_hold_tick(lv_timer_t *t)
{
    int dir, step;

    (void)t;
    if (!s_name_edit || s_kb_hold_btn < 0) return;
    s_kb_hold_ms += 80;
    if (s_kb_hold_ms < 280) return;
    dir = (s_kb_hold_btn == BSP_BTN_UP) ? -1 : 1;
    step = (s_kb_hold_ms >= 800) ? KB_COLS : 1;
    app_ui_move(&s_kb_sel, KB_N, dir * step);
    paint();
}

static void name_open(void)
{
    s_name_edit = true;
    if (s_pet.name[0]) {
        ui_pixel_utf8_copy(s_name_buf, sizeof(s_name_buf), s_pet.name);
    } else {
        s_name_buf[0] = 0;
    }
    s_kb_sel = 0;
    s_kb_set = 0;
    s_kb_hold_btn = -1;
    s_kb_hold_ms = 0;
    if (!s_kb_hold_tm) s_kb_hold_tm = lv_timer_create(kb_hold_tick, 80, NULL);
    app_meow_web_set_target(s_name_buf, sizeof(s_name_buf), name_from_web);
}

static void name_close_web(void)
{
    app_meow_web_clear_target();
    app_meow_web_qr_close();
}

static void name_cancel(void)
{
    s_name_edit = false;
    kb_hold_stop();
    name_close_web();
}

static void name_commit(void)
{
    app_meow_set_name(&s_pet, s_name_buf[0] ? s_name_buf : default_name());
    s_pet.named = 1;
    s_name_edit = false;
    kb_hold_stop();
    name_close_web();
    s_dirty = true;
    save_nvs();
}

static void name_from_web(void)
{
    if (s_name_buf[0]) name_commit();
    paint();
}

static void paint(void)
{
    if (!s_lcd) return;
    if (s_pet.stage == APP_MEOW_EGG && focused() && inner_n() == 0) {
        close_menu();
    }
    int n = inner_n();
    if (focused() && n > 0 && s_sub >= n) s_sub = 0;
    bool set = app_meow_set_open_now();
    bool play = minigame();
    hide_obj(s_ibar, set || play || s_name_edit);
    hide_obj(s_stage, set);
    if (s_stage) {
        lv_obj_set_size(s_stage, LCD_W,
                        (play || s_name_edit) ? LCD_H : PAGE_H);
    }
    if (s_scr) {
        lv_obj_set_style_bg_color(s_scr, lv_color_hex(ui_bg()), 0);
    }
    if (s_lcd) {
        lv_obj_set_style_bg_color(s_lcd, lv_color_hex(ui_bg()), 0);
    }
    if (s_stage && !set) lv_obj_invalidate(s_stage);
    if (s_ibar && !set) lv_obj_invalidate(s_ibar);
}

static void poll_alerts(void)
{
    int d = 0, lv = 0;
    if (minigame()) return;
    if (!app_meow_alert_poll(&s_pet, &d, &lv)) return;
    s_dirty = true;
    if (lv < APP_MEOW_ALERT_WARN || d < 0) return;
    snprintf(s_flash, sizeof(s_flash), "%s %d%%",
             app_str((app_str_id_t)(APP_STR_MEOW_D_HUNGER + d)),
             lv >= APP_MEOW_ALERT_CRIT ? APP_MEOW_ALERT_PCT_CRIT :
             (lv >= APP_MEOW_ALERT_HIT ? APP_MEOW_ALERT_PCT_HIT :
              APP_MEOW_ALERT_PCT_WARN));
    s_flash2[0] = 0;
    s_flash_left = 8;
    int tone = APP_TONE_BEEP;
    if (lv >= APP_MEOW_ALERT_CRIT) tone = APP_TONE_TRIPLE;
    else if (lv >= APP_MEOW_ALERT_HIT) tone = APP_TONE_DOUBLE;
    app_tone_play(tone);
}

static void call_now(void)
{
    int d = 0, best = 0;

    for (int i = 0; i < APP_MEOW_D_N; i++) {
        int v = app_meow_danger_lv(&s_pet, i);
        if (v > best) {
            best = v;
            d = i;
        }
    }
    if (best < APP_MEOW_ALERT_HIT) return;
    snprintf(s_flash, sizeof(s_flash), "%s %d%%",
             app_str((app_str_id_t)(APP_STR_MEOW_D_HUNGER + d)),
             best >= APP_MEOW_ALERT_CRIT ? APP_MEOW_ALERT_PCT_CRIT :
             APP_MEOW_ALERT_PCT_HIT);
    s_flash2[0] = 0;
    s_flash_left = 8;
    app_tone_play(best >= APP_MEOW_ALERT_CRIT ? APP_TONE_TRIPLE : APP_TONE_DOUBLE);
    paint();
}

static void on_alert_wake(void *h, esp_event_base_t b, int32_t id, void *d)
{
    (void)h;
    (void)b;
    (void)id;
    (void)d;
    if (!bsp_lvgl_lock(1000)) return;
    if (s_asleep) wake_now();
    call_now();
    bsp_lvgl_unlock();
}

static void call_bedtime(void)
{
    if (!app_meow_bed_call(&s_pet)) return;
    if (s_asleep) wake_now();
    flash_for(app_str(APP_STR_MEOW_SLEEP_NOW), APP_TONE_CHIME, 8);
}

static void on_bed_wake(void *h, esp_event_base_t b, int32_t id, void *d)
{
    (void)h;
    (void)b;
    (void)id;
    (void)d;
    if (!bsp_lvgl_lock(1000)) return;
    sync_pet();
    call_bedtime();
    bsp_lvgl_unlock();
}

static void flash_for(const char *s, int tone, int ticks)
{
    char tmp[sizeof(s_flash)];

    tmp[0] = 0;
    if (s && s[0]) {
        strncpy(tmp, s, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
    }
    memcpy(s_flash, tmp, sizeof(s_flash));
    s_flash_left = s_flash[0] ? (ticks > 0 ? ticks : 2) : 0;
    if (tone >= 0) app_tone_play(tone);
    paint();
}

static void flash(const char *s, int tone)
{
    s_flash2[0] = 0;
    flash_for(s, tone, 2);
}

static const char *cant_txt(void)
{
    if (s_pet.trip_st == APP_MEOW_TRIP_AWAY) {
        trip_time_txt(s_line, sizeof(s_line),
                      app_meow_trip_sec_left(&s_pet, now_sec()));
        return s_line;
    }
    if (s_pet.stage == APP_MEOW_EGG) return app_str(APP_STR_MEOW_HATCH);
    if (s_pet.stage == APP_MEOW_DEAD) return app_str(APP_STR_MEOW_DEAD);
    if (app_meow_rest_lock(&s_pet)) return app_str(APP_STR_MEOW_LIGHT_FIRST);
    if (s_pet.sleeping) return app_str(APP_STR_MEOW_SLEEP_NOW);
    return app_str(APP_STR_MEOW_REFUSE);
}

static const char *act_fail_txt(app_meow_res_t r)
{
    if (r == APP_MEOW_SLEEP) {
        return app_str(app_meow_rest_lock(&s_pet) ? APP_STR_MEOW_LIGHT_FIRST :
                       APP_STR_MEOW_SLEEP_NOW);
    }
    if (r == APP_MEOW_EGG_WAIT) return app_str(APP_STR_MEOW_HATCH);
    if (r == APP_MEOW_GONE) return app_str(APP_STR_MEOW_DEAD);
    if (r == APP_MEOW_FULL) return app_str(APP_STR_MEOW_FULL);
    if (r == APP_MEOW_EMPTY) return app_str(APP_STR_MEOW_EMPTY);
    if (r == APP_MEOW_NONE) {
        if (s_pet.trip_st == APP_MEOW_TRIP_AWAY) return cant_txt();
        return app_str(APP_STR_MEOW_NONE);
    }
    return app_str(APP_STR_MEOW_REFUSE);
}

static void flash_lines(const char *a, const char *b, int tone, int ticks)
{
    s_flash2[0] = 0;
    if (b && b[0]) {
        strncpy(s_flash2, b, sizeof(s_flash2) - 1);
        s_flash2[sizeof(s_flash2) - 1] = 0;
    }
    flash_for(a, tone, ticks);
}

static int append_delta(char *buf, int cap, int used, const char *lab, int d)
{
    int n;
    const char *sep = (app_lang() == APP_LANG_ZH) ? "，" : ", ";

    if (d == 0 || used < 0 || used >= cap - 1) return used;
    n = snprintf(buf + used, (size_t)(cap - used), "%s%s %+d",
                 used ? sep : "", lab, d);
    if (n < 0) return used;
    return used + n;
}

static void flash_use_ok(int good, int nuse, uint8_t hung, uint8_t hap,
                         uint8_t hp, int clean0)
{
    int n = 0;

    if (nuse <= 0) nuse = 1;
    snprintf(s_flash, sizeof(s_flash), app_str(APP_STR_MEOW_USED),
             good_name(good), nuse);
    s_flash2[0] = 0;
    n = append_delta(s_flash2, (int)sizeof(s_flash2), n,
                     app_str(APP_STR_MEOW_STAT_FOOD),
                     (int)s_pet.hunger - (int)hung);
    n = append_delta(s_flash2, (int)sizeof(s_flash2), n,
                     app_str(APP_STR_MEOW_STAT_CLEAN),
                     app_meow_clean(&s_pet) - clean0);
    n = append_delta(s_flash2, (int)sizeof(s_flash2), n,
                     app_str(APP_STR_MEOW_STAT_HEALTH),
                     (int)s_pet.health - (int)hp);
    append_delta(s_flash2, (int)sizeof(s_flash2), n,
                 app_str(APP_STR_MEOW_D_HAPPY),
                 (int)s_pet.happy - (int)hap);
    flash_for(s_flash, APP_TONE_MEOW, 8);
}

static void flash_loot(int it, const char *prefix, int tone)
{
    if (it < 0) {
        flash(prefix, tone);
        return;
    }
    if (prefix && prefix[0]) {
        snprintf(s_line, sizeof(s_line), "%s  %s", prefix, good_name(it));
    } else {
        snprintf(s_line, sizeof(s_line), "%s %s", app_str(APP_STR_MEOW_GOT),
                 good_name(it));
    }
    flash(s_line, tone);
}

static int play_pad_w(void)
{
    return PAD_W * (10 - app_meow_play_tier(s_play_run)) / 10;
}

static int play_fish_v(void)
{
    return FISH_V0 * (10 + app_meow_play_tier(s_play_run)) / 10;
}

static void play_clamp_fish(void)
{
    int min_x = PLAY_MARG;
    int max_x = LCD_W - PLAY_MARG - FISH_SZ;

    if (max_x < min_x) max_x = min_x;
    if (s_fish_x < min_x) {
        s_fish_x = min_x;
        s_fish_vx = -s_fish_vx;
    } else if (s_fish_x > max_x) {
        s_fish_x = max_x;
        s_fish_vx = -s_fish_vx;
    }
}

static void play_clamp_pad(void)
{
    int w = play_pad_w();
    int max = LCD_W - PLAY_MARG - w;
    if (max < PLAY_MARG) max = PLAY_MARG;
    if (s_pad_x < PLAY_MARG) s_pad_x = PLAY_MARG;
    if (s_pad_x > max) s_pad_x = max;
}

static void play_spawn(void)
{
    int room = LCD_W - PLAY_MARG * 2 - FISH_SZ;
    int pct = app_meow_play_swim_pct(s_play_run);

    if (room < 1) room = 1;
    s_pet.rng = (uint8_t)(s_pet.rng * 37u + 17u);
    s_fish_x = PLAY_MARG + (int)s_pet.rng % room;
    s_fish_y = 0;
    s_fish_vx = 0;
    s_fish_koi = false;
    if (pct > 0) {
        s_pet.rng = (uint8_t)(s_pet.rng * 37u + 17u);
        if ((int)(s_pet.rng % 100u) < pct) {
            s_fish_vx = (s_pet.rng & 1u) ? FISH_SWIM : -FISH_SWIM;
        }
    }
    pct = app_meow_play_koi_pct(s_play_run);
    if (pct > 0) {
        s_pet.rng = (uint8_t)(s_pet.rng * 37u + 17u);
        if ((int)(s_pet.rng % 100u) < pct) s_fish_koi = true;
    }
}

static void set_tick_ms(uint32_t ms)
{
    s_tick_ms = ms;
    if (s_timer) lv_timer_set_period(s_timer, ms);
}

static void play_begin(void)
{
    s_play_run = 0;
    s_over_kind = 0;
    s_flash[0] = 0;
    s_flash_left = 0;
    s_pad_x = (LCD_W - play_pad_w()) / 2;
    s_catch_at = 0;
    s_catch_pts = 0;
    play_spawn();
    set_tick_ms(250);
}

static void show_result(int kind, int score)
{
    wake_now();
    app_tone_gate(false);
    set_tick_ms(250);
    s_over_kind = (uint8_t)kind;
    s_over_score = score;
    app_meow_last_prizes(s_over_got);
    if (kind != 4) s_over_souv = -1;
    s_play_run = 0;
    s_mode = MODE_RESULT;
    s_sel = TAB_GAME;
    s_sub = kind;
    s_trip_edit = false;
    s_menu = -1;
    s_flash[0] = 0;
    s_flash_left = 0;
    app_tone_play(APP_TONE_CHIME);
    paint();
}

static void trip_settle(void)
{
    if (s_pet.trip_st != APP_MEOW_TRIP_BACK) return;
    if (minigame() || s_mode == MODE_LINK) return;
    app_meow_trip_claim(&s_pet);
    s_over_souv = app_meow_last_souv();
    s_dirty = true;
    save_nvs();
    show_result(4, 0);
}

static void mini_finish(int score, int missed)
{
    uint8_t kind = (s_mode == MODE_RHYTHM) ? 1 : 0;

    if (score > 0) app_meow_play_apply(&s_pet, 1);
    else if (missed) app_meow_play_apply(&s_pet, 0);
    app_meow_play_prize(&s_pet, score);
    s_dirty = true;
    save_nvs();
    show_result(kind, score);
}

static void result_exit(void)
{
    s_mode = MODE_CARE;
    s_sel = TAB_GAME;
    s_menu = TAB_GAME;
    s_sub = s_over_kind;
    paint();
    trip_settle();
}

static void play_finish(int missed)
{
    if (s_play_run > s_play_best) s_play_best = s_play_run;
    mini_finish(s_play_run, missed);
}

static void rhy_begin(void)
{
    s_pet.rng = (uint8_t)(s_pet.rng * 37u + 17u);
    app_meow_rhy_make(&s_rhy, s_pet.rng);
    s_rhy_t0 = (uint32_t)(esp_timer_get_time() / 1000);
    s_rhy_held = 0;
    s_play_run = 0;
    s_over_kind = 1;
    s_flash[0] = 0;
    s_flash_left = 0;
    close_menu();
    s_mode = MODE_RHYTHM;
    s_sel = TAB_GAME;
    set_tick_ms(40);
    app_tone_gate(true);
    paint();
}

static void rhy_finish(int missed)
{
    s_play_run = s_rhy.score;
    if (s_play_run > s_rhy_best) s_rhy_best = s_play_run;
    mini_finish(s_play_run, missed);
}

static void rhy_quit(void)
{
    rhy_finish(s_rhy.hits == 0 && s_rhy.misses > 0);
}

static void run_begin(void)
{
    s_pet.rng = (uint8_t)(s_pet.rng * 37u + 17u);
    app_meow_run_make(&s_run, s_pet.rng);
    s_run_t0 = now_ms();
    s_play_run = 0;
    s_over_kind = 2;
    s_flash[0] = 0;
    s_flash_left = 0;
    close_menu();
    s_mode = MODE_RUN;
    s_sel = TAB_GAME;
    s_sub = 2;
    set_tick_ms(40);
    paint();
}

static void run_finish(int crashed)
{
    int n = app_meow_run_got_n(&s_run);

    if (n > s_run_best) s_run_best = n;
    if (n > 0) app_meow_play_apply(&s_pet, 1);
    else if (crashed) app_meow_play_apply(&s_pet, 0);
    app_meow_run_prize(&s_pet, s_run.got);
    s_dirty = true;
    save_nvs();
    show_result(2, n);
}

static void run_tick(void)
{
    uint32_t now;
    int ev;

    if (s_mode != MODE_RUN) return;
    now = now_ms() - s_run_t0;
    /* 16 种商品等概率，不走玩耍掉落权重。 */
    ev = app_meow_run_step(&s_run, now, -1);
    s_play_run = (int)s_run.items;
    if (s_play_run > s_run_best) s_run_best = s_play_run;
    if (ev == APP_MEOW_RUN_EV_ITEM) app_tone_play(APP_TONE_CHIME);
    if (ev == APP_MEOW_RUN_EV_HAZ) {
        app_tone_play(APP_TONE_BEEP);
        run_finish(1);
    }
}

static void mat_begin(void)
{
    s_pet.rng = (uint8_t)(s_pet.rng * 37u + 17u);
    app_meow_mat_make(&s_mat, s_pet.rng);
    s_play_run = 0;
    s_over_kind = 3;
    s_flash[0] = 0;
    s_flash_left = 0;
    close_menu();
    s_mode = MODE_MATCH;
    s_sel = TAB_GAME;
    s_sub = 3;
    s_mat_last = now_ms();
    set_tick_ms(40);
    paint();
}

static void mat_finish(int timed_out)
{
    int score = s_mat.score;

    if (score > s_mat_best) s_mat_best = score;
    if (score > 0) app_meow_play_apply(&s_pet, 1);
    else if (timed_out) app_meow_play_apply(&s_pet, 0);
    app_meow_play_prize(&s_pet, score);
    s_dirty = true;
    save_nvs();
    show_result(3, score);
}

static void mat_tick(void)
{
    uint32_t now, dt;
    int ev;

    if (s_mode != MODE_MATCH) return;
    now = now_ms();
    dt = now - s_mat_last;
    s_mat_last = now;
    if (dt == 0) return;
    if (dt > 200) dt = 200;
    s_play_run = s_mat.score;
    if (s_play_run > s_mat_best) s_mat_best = s_play_run;
    ev = app_meow_mat_tick(&s_mat, dt);
    if (ev == APP_MEOW_MAT_EV_OVER) {
        mat_finish(1);
    } else if (ev == APP_MEOW_MAT_EV_CLEAR || ev == APP_MEOW_MAT_EV_NEXT) {
        app_tone_play(APP_TONE_CHIME);
    }
}

static void rhy_tick(void)
{
    uint32_t now;
    int p;

    if (s_mode != MODE_RHYTHM) return;
    now = rhy_now();
    app_meow_rhy_tick(&s_rhy, now);
    p = app_meow_rhy_poll_cue(&s_rhy, now);
    if (p >= 0) app_tone_note(app_meow_rhy_hz(p), 55);
    s_play_run = s_rhy.score;
    if (s_play_run > s_rhy_best) s_rhy_best = s_play_run;
    if (app_meow_rhy_done(&s_rhy, now)) {
        rhy_finish(s_rhy.hits == 0);
    }
}

static void play_quit(void)
{
    play_finish(0);
}

static void play_miss(void)
{
    play_finish(1);
}

static void play_catch(void)
{
    s_catch_x = s_fish_x + FISH_SZ / 2;
    s_catch_y = FISH_TOP + s_fish_y + FISH_SZ / 2;
    s_catch_at = now_ms();
    s_catch_pts = s_fish_koi ? FISH_KOI_PTS : FISH_PTS;
    s_play_run += s_catch_pts;
    if (s_play_run > s_play_best) s_play_best = s_play_run;
    play_clamp_pad();
    play_spawn();
    set_tick_ms(40);
    flash(NULL, APP_TONE_CHIME);
}

static void play_tick(void)
{
    if (s_mode != MODE_PLAY) return;
    if (s_catch_at) {
        if ((uint32_t)(now_ms() - s_catch_at) < FISH_FX_MS) return;
        s_catch_at = 0;
        set_tick_ms(250);
    }
    s_fish_y += play_fish_v();
    if (s_fish_vx) {
        s_fish_x += s_fish_vx;
        play_clamp_fish();
    }
    if (FISH_TOP + s_fish_y + FISH_SZ < PAD_Y) return;
    if (s_fish_x + FISH_SZ > s_pad_x && s_fish_x < s_pad_x + play_pad_w()) {
        play_catch();
    } else {
        play_miss();
    }
}

static void drain_ancs(void)
{
    bsp_ble_notif_t n;
    while (bsp_ble_take_notif(&n)) {}
}

static void __attribute__((unused)) begin_link(int kind)
{
    if (!app_meow_can_link(&s_pet)) {
        flash(cant_txt(), APP_TONE_BEEP);
        return;
    }
    if (!app_meow_link_seek(&s_pet, kind)) {
        flash(app_str(APP_STR_MEOW_REFUSE), APP_TONE_BEEP);
        return;
    }
    s_mode = MODE_LINK;
    s_flash[0] = 0;
    s_flash_left = 0;
    app_tone_play(APP_TONE_BEEP);
    paint();
}

static void finish_link(int r)
{
    s_mode = MODE_CARE;
    close_menu();
    ble_off();
    if (r == APP_MEOW_LINK_VISIT) {
        int it = app_meow_loot(&s_pet, APP_MEOW_LOOT_VISIT);
        s_dirty = true;
        save_nvs();
        flash_loot(it, app_str(APP_STR_MEOW_GUEST), APP_TONE_CHIME);
        return;
    }
    if (r == APP_MEOW_LINK_WIN) {
        int it = app_meow_loot(&s_pet, APP_MEOW_LOOT_WIN);
        s_dirty = true;
        save_nvs();
        flash_loot(it, app_str(APP_STR_MEOW_WIN), APP_TONE_CHIME);
        return;
    }
    if (r == APP_MEOW_LINK_LOSE) {
        s_dirty = true;
        save_nvs();
        flash(app_str(APP_STR_MEOW_LOSE), APP_TONE_BEEP);
        return;
    }
    if (r == APP_MEOW_LINK_DRAW) {
        int it = app_meow_loot(&s_pet, APP_MEOW_LOOT_DRAW);
        s_dirty = true;
        save_nvs();
        flash_loot(it, app_str(APP_STR_MEOW_DRAW), APP_TONE_BEEP);
        return;
    }
    if (r == APP_MEOW_LINK_NONE) {
        flash(app_str(APP_STR_MEOW_NO_PEER), APP_TONE_BEEP);
        return;
    }
    flash(app_str(APP_STR_MEOW_REFUSE), APP_TONE_BEEP);
}

static void run_care(app_meow_act_t act)
{
    if (act == APP_MEOW_PLAY) {
        if (!app_meow_can(&s_pet, APP_MEOW_PLAY)) {
            flash(cant_txt(), APP_TONE_BEEP);
            return;
        }
        close_menu();
        s_mode = MODE_PLAY;
        play_begin();
        paint();
        return;
    }
    int gid = app_meow_pick(&s_pet, act);
    uint8_t hung = s_pet.hunger;
    uint8_t hap = s_pet.happy;
    uint8_t hp = s_pet.health;
    int clean0 = app_meow_clean(&s_pet);
    app_meow_res_t r = app_meow_act(&s_pet, act);
    const char *msg = NULL;
    int tone = APP_TONE_BEEP;
    if (r == APP_MEOW_OK) {
        s_dirty = true;
        if (act == APP_MEOW_WALK) {
            int it = app_meow_loot(&s_pet, APP_MEOW_LOOT_WALK);
            save_nvs();
            flash_loot(it, NULL, APP_TONE_CHIME);
            return;
        }
        if (act == APP_MEOW_LIGHT) {
            save_nvs();
            flash(app_str(s_pet.lights_off ? APP_STR_MEOW_DARK :
                          APP_STR_MEOW_LIT), APP_TONE_CHIME);
            return;
        }
        save_nvs();
        if (gid >= 0) {
            flash_use_ok(gid, 1, hung, hap, hp, clean0);
            return;
        }
        tone = APP_TONE_CHIME;
    } else {
        msg = act_fail_txt(r);
        if (r == APP_MEOW_NONE && s_pet.trip_st != APP_MEOW_TRIP_AWAY) {
            tone = -1;
        }
    }
    flash(msg, tone);
}

static void lang_apply(lv_timer_t *t)
{
    (void)t;
    s_lang_timer = NULL;
    app_prefs_save_lang();
    paint();
}

static void cycle_lang(void)
{
    app_prefs_t *p = app_prefs();
    p->lang = (uint8_t)((p->lang + 1) % APP_LANG_N);
    app_lang_set((app_lang_t)p->lang);
    if (s_lang_timer) lv_timer_reset(s_lang_timer);
    else {
        s_lang_timer = lv_timer_create(lang_apply, 20, NULL);
        lv_timer_set_repeat_count(s_lang_timer, 1);
    }
    paint();
}

static void do_act(void)
{
    if (bsp_ble_state() == BSP_BLE_PAIRING && bsp_ble_pair_needs_confirm()) {
        bsp_ble_pair_reply(true);
        flash(NULL, APP_TONE_BEEP);
        return;
    }
    if (!focused()) {
        if (s_sel == TAB_HOME && s_pet.stage == APP_MEOW_EGG) {
            s_sel = TAB_SET;
            s_menu = TAB_SET;
            s_sub = 0;
            paint();
            return;
        }
        if (inner_n() <= 0) {
            paint();
            return;
        }
        s_menu = s_sel;
        s_sub = 0;
        if (s_sel == TAB_HOME && s_pet.sleeping &&
            s_pet.hunger > APP_MEOW_ALERT_PCT_WARN &&
            !s_pet.sick && !s_pet.poop) {
            s_sub = HOME_LIGHT;
        }
        if (s_sel == TAB_HOME && s_pet.sleeping && !s_pet.lights_off &&
            s_pet.stage != APP_MEOW_EGG && s_pet.stage != APP_MEOW_DEAD) {
            flash_for(app_str(APP_STR_MEOW_SLEEP_NOW), APP_TONE_CHIME, 8);
            return;
        }
        paint();
        return;
    }
    if (s_sel == TAB_HOME) {
        if (s_pet.stage == APP_MEOW_DEAD) {
            app_meow_reset(&s_pet, now_sec(), (uint8_t)(s_pet.rng + 1));
            s_sel = 0;
            close_menu();
            s_dirty = true;
            save_nvs();
            paint();
            return;
        }
        if (s_sub >= 0 && s_sub < HOME_N) run_care((app_meow_act_t)HOME_ACT[s_sub]);
        return;
    }
    if (s_sel == TAB_SET) {
        static const meow_set_id_t MAP[] = {
            MEOW_SET_BED, MEOW_SET_WIFI, MEOW_SET_BLE, MEOW_SET_CLOCK,
            MEOW_SET_SCREEN, MEOW_SET_SOUND, MEOW_SET_OTA
        };
        if (s_sub == 0) {
            cycle_lang();
            return;
        }
        if (s_sub == SET_NAME) {
            name_open();
            paint();
            return;
        }
        if (s_sub >= 2 && s_sub <= 8) {
            app_meow_set_open(s_lcd, MAP[s_sub - 2]);
            paint();
            return;
        }
        if (s_sub == SET_WIPE) {
            uint8_t rng = (uint8_t)(s_pet.rng + 1);

            app_meow_wipe(&s_pet, now_sec(), rng);
            s_play_best = 0;
            s_play_run = 0;
            s_rhy_best = 0;
            s_run_best = 0;
            s_mat_best = 0;
            s_trip_edit = false;
            memset(s_trip_take, 0, sizeof(s_trip_take));
            s_sel = TAB_HOME;
            close_menu();
            s_dirty = true;
            save_nvs();
            flash(app_str(APP_STR_MEOW_DEAD), APP_TONE_BEEP);
            return;
        }
        paint();
        return;
    }
    if (s_sel == TAB_SHOP) {
        uint8_t ids[APP_MEOW_G_N];
        int n = app_meow_owned_list(&s_pet, s_bag_cat, ids, APP_MEOW_G_N);
        if (n <= 0) {
            flash(app_str(APP_STR_MEOW_BAG_EMPTY), APP_TONE_BEEP);
            return;
        }
        if (s_sub < 0 || s_sub >= n) return;
        int id = (int)ids[s_sub];
        uint8_t hung = s_pet.hunger;
        uint8_t hap = s_pet.happy;
        uint8_t hp = s_pet.health;
        int clean0 = app_meow_clean(&s_pet);
        app_meow_res_t r = app_meow_use(&s_pet, id);
        const char *msg = NULL;
        int tone = APP_TONE_BEEP;
        if (r == APP_MEOW_OK) {
            s_dirty = true;
            save_nvs();
            if (app_meow_good_use(id) == APP_MEOW_USE_DIG) {
                int got = app_meow_last_got();
                flash_use_ok(id, 1, hung, hap, hp, clean0);
                if (got >= 0) {
                    int used = (int)strlen(s_flash2);
                    snprintf(s_flash2 + used, sizeof(s_flash2) - (size_t)used,
                             "%s%s %s", used ? " " : "",
                             app_str(APP_STR_MEOW_GOT), good_name(got));
                    paint();
                }
                return;
            }
            flash_use_ok(id, 1, hung, hap, hp, clean0);
            return;
        }
        msg = act_fail_txt(r);
        if (r == APP_MEOW_NONE && s_pet.trip_st != APP_MEOW_TRIP_AWAY) {
            tone = -1;
        }
        flash(msg, tone);
        return;
    }
    if (s_sel == TAB_GAME) {
        if (s_trip_edit) {
            uint8_t ids[APP_MEOW_G_N];
            int n = app_meow_owned_list(&s_pet, APP_MEOW_CAT_FOOD, ids,
                                       APP_MEOW_G_N);
            if (n <= 0) {
                flash(app_str(APP_STR_MEOW_BAG_EMPTY), APP_TONE_BEEP);
                return;
            }
            if (s_sub == n) {
                app_meow_res_t r;
                if (trip_take_n() <= 0) {
                    flash(app_str(APP_STR_MEOW_EMPTY), APP_TONE_BEEP);
                    return;
                }
                r = app_meow_trip_start(&s_pet, s_trip_take);
                if (r == APP_MEOW_OK) {
                    s_trip_edit = false;
                    s_sub = 4;
                    s_dirty = true;
                    save_nvs();
                    flash(app_str(APP_STR_MEOW_TRIP), APP_TONE_CHIME);
                    return;
                }
                flash(act_fail_txt(r), APP_TONE_BEEP);
                return;
            }
            if (s_sub >= 0 && s_sub < n) {
                int id = (int)ids[s_sub];
                int have = (int)app_meow_inv(&s_pet, id);
                if (s_trip_take[id] < have &&
                    trip_take_n() < APP_MEOW_TRIP_PACK_MAX) {
                    s_trip_take[id]++;
                }
                paint();
            }
            return;
        }
        if (s_sub == 0) {
            run_care(APP_MEOW_PLAY);
            s_sel = TAB_GAME;
            return;
        }
        if (s_sub == 1) {
            if (!app_meow_can(&s_pet, APP_MEOW_PLAY)) {
                flash(cant_txt(), APP_TONE_BEEP);
                return;
            }
            rhy_begin();
            return;
        }
        if (s_sub == 2) {
            if (!app_meow_can(&s_pet, APP_MEOW_PLAY)) {
                flash(cant_txt(), APP_TONE_BEEP);
                return;
            }
            run_begin();
            return;
        }
        if (s_sub == 3) {
            if (!app_meow_can(&s_pet, APP_MEOW_PLAY)) {
                flash(cant_txt(), APP_TONE_BEEP);
                return;
            }
            mat_begin();
            return;
        }
        if (s_sub == 4) {
            if (s_pet.trip_st == APP_MEOW_TRIP_BACK) {
                trip_settle();
                return;
            }
            if (s_pet.trip_st == APP_MEOW_TRIP_AWAY) {
                trip_time_txt(s_line, sizeof(s_line),
                              app_meow_trip_sec_left(&s_pet, now_sec()));
                flash(s_line, APP_TONE_BEEP);
                return;
            }
            if (!app_meow_trip_can(&s_pet)) {
                flash((s_pet.sleeping || s_pet.stage == APP_MEOW_EGG ||
                       s_pet.stage == APP_MEOW_DEAD) ?
                      cant_txt() :
                      (app_meow_owned_n(&s_pet, APP_MEOW_CAT_FOOD) ?
                       app_str(APP_STR_MEOW_REFUSE) :
                       app_str(APP_STR_MEOW_BAG_EMPTY)), APP_TONE_BEEP);
                return;
            }
            memset(s_trip_take, 0, sizeof(s_trip_take));
            s_trip_edit = true;
            s_sub = 0;
            paint();
            return;
        }
        return;
    }
    if (s_sel == TAB_DEX) {
        if (dex_on(s_sub)) {
            char a[48], b[48];
            dex_blurb_split(s_sub, a, sizeof(a), b, sizeof(b));
            flash_lines(a, b, APP_TONE_BEEP, 12);
        } else {
            flash(app_str(APP_STR_MEOW_NONE), APP_TONE_BEEP);
        }
    }
}

static bool idle_hold(void)
{
    if (minigame()) return true;
    if (s_name_edit || app_meow_web_qr_visible()) return true;
    if (s_mode == MODE_LINK || app_meow_link_busy()) return true;
    if (s_flash_left > 0) return true;
    if (app_meow_set_open_now() || app_meow_set_busy()) return true;
    if (bsp_ble_state() == BSP_BLE_PAIRING) return true;
    if (app_ota_busy() || (app_ota_prompt() && app_prefs()->ota_auto)) {
        return true;
    }
    return false;
}

static uint32_t tick_ms(void)
{
    return s_tick_ms ? s_tick_ms : 250;
}

static bool ui_live(void)
{
    return minigame() || s_mode == MODE_LINK || s_name_edit ||
           s_flash_left > 0 || app_ota_busy() || app_meow_set_busy();
}

static void tune_pm(void)
{
    if (s_asleep) return;
    bsp_pm_set_perf(ui_live() || s_idle_ms < IDLE_PERF_MS);
}

static void on_tick(lv_timer_t *t)
{
    uint32_t dt = tick_ms();
    bool flash_tick;

    (void)t;
    if (app_meow_set_open_now()) {
        app_time_tick();
        tune_pm();
        return;
    }
    if (!s_ready) {
        load_nvs();
        s_ready = true;
        s_want_back_hint = !back_hint_seen();
        sync_pet();
        save_nvs();
    } else if (!s_name_edit) {
        uint8_t was = s_pet.sleeping;
        sync_pet();
        if (!was && s_pet.sleeping) call_bedtime();
    }
    if (s_ready && s_want_back_hint) {
        s_want_back_hint = false;
        back_hint_mark();
        flash_for(app_str(APP_STR_MEOW_HOLD_BACK), APP_TONE_CHIME, 16);
    }
    play_tick();
    rhy_tick();
    run_tick();
    mat_tick();
    flash_tick = s_flash_left > 0;
    if (s_flash_left > 0) s_flash_left--;
    s_blink ^= 1;
    s_bob++;
    trip_settle();
    poll_alerts();
    app_time_tick();
    app_meow_web_poll();
    drain_ancs();
    if (s_mode == MODE_LINK) {
        int r = app_meow_link_poll(&s_pet);
        if (r != APP_MEOW_LINK_WAIT) finish_link(r);
    }
    {
        bool allow = app_prefs()->ota_auto &&
                     !s_asleep && !minigame() && s_mode == MODE_CARE &&
                     !app_meow_set_open_now();
        app_ota_tick(allow);
    }
    if (!s_asleep && !app_meow_set_open_now()) {
        s_awake_ms += dt;
        if (ui_live() || flash_tick) {
            paint();
            s_still_ms = 0;
        } else {
            s_still_ms += dt;
            if (s_still_ms >= IDLE_PAINT_MS) {
                paint();
                s_still_ms = 0;
            }
        }
    }

    uint16_t lim = app_prefs()->sleep_sec;
    if (!s_asleep && s_wifi_wait && s_awake_ms >= WIFI_WAKE_MS) {
        bool hold = idle_hold();
        bool will_sleep = (lim != 0) && !hold &&
                          (s_idle_ms + dt >= (uint32_t)lim * 1000);
        if (!will_sleep) {
            s_wifi_wait = false;
            if (bsp_wifi_enabled()) bsp_wifi_radio_resume();
        }
    }
    if (lim == 0 || s_asleep) {
        tune_pm();
        return;
    }
    if (idle_hold()) {
        s_idle_ms = 0;
        tune_pm();
        return;
    }
    s_idle_ms += dt;
    tune_pm();
    if (s_idle_ms >= (uint32_t)lim * 1000) sleep_now();
}

void app_meow_start(void)
{
    s_scr = lv_obj_create(NULL);
    ui_pixel_strip_theme(s_scr);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_font(s_scr, ui_pixel_font_14(), 0);

    s_lcd = px(s_scr, APP_VIEW_X, APP_VIEW_Y, LCD_W, LCD_H, COL_BG);
    lv_obj_set_style_radius(s_lcd, 0, 0);
    lv_obj_set_style_border_width(s_lcd, 0, 0);

    s_stage = lv_obj_create(s_lcd);
    ui_pixel_strip_theme(s_stage);
    lv_obj_set_pos(s_stage, 0, 0);
    lv_obj_set_size(s_stage, LCD_W, PAGE_H);
    lv_obj_set_style_bg_opa(s_stage, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_stage, stage_draw, LV_EVENT_DRAW_MAIN, NULL);

    s_ibar = lv_obj_create(s_lcd);
    ui_pixel_strip_theme(s_ibar);
    lv_obj_set_pos(s_ibar, 0, PAGE_H);
    lv_obj_set_size(s_ibar, LCD_W, NAV_H);
    lv_obj_set_style_bg_opa(s_ibar, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_ibar, ibar_draw, LV_EVENT_DRAW_MAIN, NULL);

    s_sel = 0;
    s_menu = -1;
    s_sub = 0;
    s_mode = MODE_CARE;
    s_asleep = false;
    s_idle_ms = 0;
    s_still_ms = 0;
    bsp_button_set_wake_cb(on_gpio_wake);
    bsp_ble_set_activity_cb(on_ble_activity);
    esp_event_handler_register(MEOW_EVENT, MEOW_BLE_WAKE, on_ble_evt, NULL);
    esp_event_handler_register(MEOW_EVENT, MEOW_ALERT_WAKE, on_alert_wake, NULL);
    esp_event_handler_register(MEOW_EVENT, MEOW_BED_WAKE, on_bed_wake, NULL);
    app_meow_link_start();
    ble_off();
    app_ota_init();
    s_timer = lv_timer_create(on_tick, 250, NULL);
    app_meow_web_init(s_scr);
    lv_screen_load(s_scr);
    paint();
}

void app_meow_on_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_asleep) {
        if (ev == BSP_BTN_PRESS || ev == BSP_BTN_CLICK) {
            wake_now();
            s_wake_skip = true;
        }
        return;
    }
    s_idle_ms = 0;
    s_still_ms = 0;
    tune_pm();

    if (s_wake_skip) {
        if (ev == BSP_BTN_CLICK) s_wake_skip = false;
        return;
    }

    if (app_meow_web_qr_visible()) {
        app_meow_web_qr_key(btn, ev);
        paint();
        return;
    }

    if (bsp_ble_state() == BSP_BLE_PAIRING && bsp_ble_pair_needs_confirm()) {
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            bsp_ble_pair_reply(true);
            flash(NULL, APP_TONE_BEEP);
            return;
        }
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) {
            bsp_ble_pair_reply(false);
            flash(NULL, APP_TONE_BEEP);
            return;
        }
    }

    if (app_ota_state() == APP_OTA_APPLYING && !app_meow_set_open_now()) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            app_ota_cancel();
            flash(NULL, APP_TONE_BEEP);
        }
        return;
    }
    if (app_ota_prompt() && app_prefs()->ota_auto &&
        s_mode == MODE_CARE && !app_meow_set_open_now()) {
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_OK) {
            app_ota_apply();
            flash(NULL, APP_TONE_CHIME);
            paint();
            return;
        }
        if (ev == BSP_BTN_CLICK && btn == BSP_BTN_DOWN) {
            app_ota_skip();
            flash(NULL, APP_TONE_BEEP);
            paint();
            return;
        }
    }

    if (s_name_edit) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            name_cancel();
            paint();
            return;
        }
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            if (ev == BSP_BTN_PRESS) {
                s_kb_hold_btn = (int)btn;
                s_kb_hold_ms = 0;
                app_ui_move(&s_kb_sel, KB_N, btn == BSP_BTN_UP ? -1 : 1);
                paint();
            } else if (ev == BSP_BTN_RELEASE && s_kb_hold_btn == (int)btn) {
                s_kb_hold_btn = -1;
                s_kb_hold_ms = 0;
            }
            return;
        }
        if (ev != BSP_BTN_CLICK) return;
        if (btn == BSP_BTN_OK) {
            int r = app_kb_click(s_name_buf, sizeof(s_name_buf),
                                 &s_kb_sel, &s_kb_set);
            if (r == 1) {
                char clip[APP_MEOW_NAME_MAX + 1];
                app_meow_name_copy(clip, sizeof(clip), s_name_buf);
                memcpy(s_name_buf, clip, sizeof(clip));
            }
            if (r == 3) name_cancel();
            else if (r == 2) name_commit();
            else if (r == 4) {
                char url[36];
                if (app_meow_web_url(url, sizeof(url))) app_meow_web_qr_open();
                else flash_for(app_str(APP_STR_QR_NEED), APP_TONE_BEEP, 8);
            }
            paint();
        }
        return;
    }

    if (app_meow_set_open_now()) {
        app_meow_set_on_key(btn, ev);
        paint();
        return;
    }

    if (s_mode == MODE_RESULT) {
        if ((ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG) && btn == BSP_BTN_OK) {
            result_exit();
        }
        return;
    }
    if (s_mode == MODE_PLAY) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            play_quit();
            return;
        }
        if (ev == BSP_BTN_PRESS) {
            if (btn == BSP_BTN_UP) s_pad_x -= PAD_STEP;
            else if (btn == BSP_BTN_DOWN) s_pad_x += PAD_STEP;
            play_clamp_pad();
            paint();
        }
        return;
    }
    if (s_mode == MODE_MATCH) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            if (app_meow_mat_busy(&s_mat)) return;
            if (app_meow_mat_selected(&s_mat)) {
                app_meow_mat_unsel(&s_mat);
                paint();
            } else {
                mat_finish(0);
            }
            return;
        }
        if (app_meow_mat_busy(&s_mat)) return;
        if (ev == BSP_BTN_LONG && (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
            app_meow_mat_move(&s_mat, btn == BSP_BTN_UP ? 0 : 1, 1);
            paint();
            return;
        }
        if (ev == BSP_BTN_CLICK) {
            int evm;

            if (btn == BSP_BTN_UP) app_meow_mat_move(&s_mat, 0, 0);
            else if (btn == BSP_BTN_DOWN) app_meow_mat_move(&s_mat, 1, 0);
            else if (btn == BSP_BTN_OK) {
                evm = app_meow_mat_ok(&s_mat);
                if (evm == APP_MEOW_MAT_EV_CLEAR ||
                    evm == APP_MEOW_MAT_EV_NEXT) {
                    s_play_run = s_mat.score;
                    if (s_play_run > s_mat_best) s_mat_best = s_play_run;
                    app_tone_play(APP_TONE_CHIME);
                } else if (evm == APP_MEOW_MAT_EV_REVERT) {
                    app_tone_play(APP_TONE_BEEP);
                }
            }
            paint();
        }
        return;
    }
    if (s_mode == MODE_RUN) {
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            run_finish(0);
            return;
        }
        if (ev == BSP_BTN_PRESS) {
            if (btn == BSP_BTN_UP) app_meow_run_move(&s_run, -1);
            else if (btn == BSP_BTN_DOWN) app_meow_run_move(&s_run, 1);
            paint();
        }
        return;
    }
    if (s_mode == MODE_RHYTHM) {
        int lane = -1;
        if (ev == BSP_BTN_LONG && btn == BSP_BTN_OK) {
            rhy_quit();
            return;
        }
        if (btn == BSP_BTN_UP) lane = 0;
        else if (btn == BSP_BTN_DOWN) lane = 1;
        if (lane >= 0) {
            if (ev == BSP_BTN_PRESS) {
                s_rhy_held |= (uint8_t)(1u << lane);
                app_meow_rhy_press(&s_rhy, lane, rhy_now());
                s_play_run = s_rhy.score;
                paint();
            } else if (ev == BSP_BTN_RELEASE) {
                s_rhy_held &= (uint8_t)~(1u << lane);
                app_meow_rhy_release(&s_rhy, lane, rhy_now());
                s_play_run = s_rhy.score;
                paint();
            }
        }
        return;
    }

    if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_OK) {
            if (s_mode == MODE_CARE && focused()) {
                close_menu();
                paint();
            }
            return;
        }
        if (s_mode == MODE_CARE && focused() && s_trip_edit &&
            (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
            uint8_t ids[APP_MEOW_G_N];
            int n = app_meow_owned_list(&s_pet, APP_MEOW_CAT_FOOD, ids,
                                       APP_MEOW_G_N);
            if (btn == BSP_BTN_DOWN) {
                app_meow_res_t r;

                if (trip_take_n() <= 0) {
                    flash(app_str(APP_STR_MEOW_EMPTY), APP_TONE_BEEP);
                    return;
                }
                r = app_meow_trip_start(&s_pet, s_trip_take);
                if (r == APP_MEOW_OK) {
                    s_trip_edit = false;
                    s_sub = 4;
                    s_dirty = true;
                    save_nvs();
                    flash(app_str(APP_STR_MEOW_TRIP), APP_TONE_CHIME);
                    return;
                }
                flash(act_fail_txt(r), APP_TONE_BEEP);
                return;
            }
            if (s_sub >= 0 && s_sub < n) {
                int id = (int)ids[s_sub];
                if (s_trip_take[id] > 0) s_trip_take[id]--;
            }
            paint();
            return;
        }
        if (s_mode == MODE_CARE && focused() &&
            (s_sel == TAB_SHOP || s_sel == TAB_DEX) &&
            (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
            if (s_sel == TAB_SHOP) {
                bag_cat_shift(btn == BSP_BTN_UP ? -1 : 1);
            } else {
                dex_cat_shift(btn == BSP_BTN_UP ? -1 : 1);
            }
            paint();
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) return;

    if (s_mode == MODE_LINK) {
        if (btn == BSP_BTN_OK) {
            app_meow_link_cancel();
            s_mode = MODE_CARE;
            ble_off();
            paint();
        }
        return;
    }

    if (focused()) {
        int sn = inner_n();
        if (sn <= 0) {
            close_menu();
            paint();
            return;
        }
        if (s_sel == TAB_SHOP) {
            int n = app_meow_owned_n(&s_pet, s_bag_cat);
            if (btn == BSP_BTN_UP) {
                if (n > 0) s_sub = (s_sub + n - 1) % n;
                paint();
                return;
            }
            if (btn == BSP_BTN_DOWN) {
                if (n > 0) s_sub = (s_sub + 1) % n;
                paint();
                return;
            }
            if (btn == BSP_BTN_OK) do_act();
            return;
        }
        if (btn == BSP_BTN_UP) {
            s_sub = (s_sub + sn - 1) % sn;
            paint();
            return;
        }
        if (btn == BSP_BTN_DOWN) {
            s_sub = (s_sub + 1) % sn;
            paint();
            return;
        }
        if (btn == BSP_BTN_OK) do_act();
        return;
    }
    if (btn == BSP_BTN_UP) {
        s_sel = (s_sel + TAB_N - 1) % TAB_N;
        paint();
        return;
    }
    if (btn == BSP_BTN_DOWN) {
        s_sel = (s_sel + 1) % TAB_N;
        paint();
        return;
    }
    if (btn == BSP_BTN_OK) do_act();
}
