#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_FARM_PLOT_N     12
#define APP_FARM_CROP_N     6
#define APP_FARM_FRIEND_N   16
#define APP_FARM_NAME_MAX   12
#define APP_FARM_HOST_MAX   63
#define APP_FARM_HOST_DEFAULT "https://farm.netbiu.com"
#define APP_FARM_TOK_MAX    32
#define APP_FARM_SEED_MAX   999
#define APP_FARM_LV_MAX     99
#define APP_FARM_START_COIN 80
#define APP_FARM_START_SEED 4
#define APP_FARM_HAZ_SEC    600u
#define APP_FARM_HAZ_JIT    180u
#define APP_FARM_WEED_PCT   18
#define APP_FARM_PEST_PCT   12
#define APP_FARM_PEST_EAT   10
#define APP_FARM_STEAL_PCT  60
#define APP_FARM_STEAL_KEEP 20
#define APP_FARM_STEAL_MAX  2
#define APP_FARM_STEAL_DAY  10
#define APP_FARM_HELP_DAY   10
#define APP_FARM_COOL_SEC   600u
#define APP_FARM_ACT_STEAL  0
#define APP_FARM_ACT_WATER  1
#define APP_FARM_ACT_WEED   2
#define APP_FARM_ACT_PEST   3
#define APP_FARM_ACT_N      4
#define APP_FARM_MAGIC      0x314D5246u /* FRM1 */
#define APP_FARM_VER        1u

#define APP_FARM_ST_EMPTY 0
#define APP_FARM_ST_SEED  1
#define APP_FARM_ST_GROW  2
#define APP_FARM_ST_RIPE  3
#define APP_FARM_ST_DEAD  4

#define APP_FARM_C_CARROT 0
#define APP_FARM_C_CABBAGE 1
#define APP_FARM_C_TOMATO 2
#define APP_FARM_C_CORN   3
#define APP_FARM_C_BERRY  4
#define APP_FARM_C_MELON  5

typedef enum {
    APP_FARM_TOOL_SEED = 0,
    APP_FARM_TOOL_PLANT,
    APP_FARM_TOOL_WATER,
    APP_FARM_TOOL_WEED,
    APP_FARM_TOOL_PEST,
    APP_FARM_TOOL_CUT,
    APP_FARM_TOOL_N
} app_farm_tool_t;

typedef enum {
    APP_FARM_OK = 0,
    APP_FARM_NO_SEED,
    APP_FARM_NO_COIN,
    APP_FARM_LOCKED,
    APP_FARM_NEED_EMPTY,
    APP_FARM_NEED_CROP,
    APP_FARM_NEED_DRY,
    APP_FARM_NEED_WEED,
    APP_FARM_NEED_PEST,
    APP_FARM_NEED_RIPE,
    APP_FARM_HAS_HAZ,
    APP_FARM_LOCKED_CROP,
    APP_FARM_BUSY,
    APP_FARM_SELF,
    APP_FARM_COOL,
    APP_FARM_LIMIT,
    APP_FARM_NONE
} app_farm_res_t;

typedef struct {
    uint8_t crop;       /* 0..CROP_N-1, empty ignored */
    uint8_t stage;      /* EMPTY/SEED/GROW/RIPE */
    uint8_t dry;        /* 1 = 需要浇水 */
    uint8_t weed;
    uint8_t pest;
    uint8_t stolen;     /* 1 = 刚被偷,界面闪一下 */
    uint8_t yield;      /* 剩余产量 0..100 */
    uint8_t gen;        /* 种植世代, 用于离线同步 */
    uint16_t grow_left; /* 生长剩余秒; 成熟后为枯萎倒计时 */
} app_farm_plot_t;

typedef struct {
    uint16_t grow_sec;
    uint16_t seed_cost;
    uint16_t harvest;
    uint8_t unlock_lv;
    uint8_t xp;
    uint16_t wither_sec;
} app_farm_crop_info_t;

typedef struct {
    uint32_t magic;
    uint8_t ver;
    uint8_t level;
    uint16_t xp;
    uint32_t coins;
    uint32_t last_sec;
    uint32_t id;
    uint16_t seeds[APP_FARM_CROP_N];
    app_farm_plot_t plots[APP_FARM_PLOT_N];
    uint32_t friends[APP_FARM_FRIEND_N];
    uint8_t friend_n;
    uint8_t named;
    uint8_t rng;
    uint8_t pick; /* 当前选中的种子 */
    char name[APP_FARM_NAME_MAX + 1];
    char host[APP_FARM_HOST_MAX + 1];
    char token[APP_FARM_TOK_MAX + 1];
} app_farm_t;

typedef struct {
    uint32_t id;
    char name[APP_FARM_NAME_MAX + 1];
    uint8_t level;
    uint32_t coins;
    app_farm_plot_t plots[APP_FARM_PLOT_N];
} app_farm_view_t;

#define APP_FARM_MAIL_FRI   0
#define APP_FARM_MAIL_STEAL 1
#define APP_FARM_MAIL_WATER 2
#define APP_FARM_MAIL_WEED  3
#define APP_FARM_MAIL_PEST  4

typedef struct {
    uint32_t from;
    char name[APP_FARM_NAME_MAX + 1];
    uint8_t kind;
    uint16_t got;
} app_farm_mail_t;

typedef struct {
    uint32_t id;
    char name[APP_FARM_NAME_MAX + 1];
    uint8_t level;
    uint32_t coins;
} app_farm_peer_t;

const app_farm_crop_info_t *app_farm_crop(int id);
uint32_t app_farm_plot_eta_sec(const app_farm_plot_t *p);
int app_farm_plot_n(int level);
int app_farm_xp_need(int level);

uint32_t app_farm_id_from_mac(const uint8_t mac[6]);
bool app_farm_mac_parse(const char *s, uint8_t mac[6]);
void app_farm_mac_fmt(const uint8_t mac[6], char *out, size_t n);

void app_farm_reset(app_farm_t *f, uint32_t now_sec, uint32_t id, uint8_t rng);
void app_farm_wipe(app_farm_t *f, uint32_t now_sec);
bool app_farm_valid(const app_farm_t *f);
bool app_farm_import(app_farm_t *f, const void *raw, size_t n);

void app_farm_set_name(app_farm_t *f, const char *name);
const char *app_farm_name(const app_farm_t *f);

bool app_farm_crop_open(const app_farm_t *f, int crop);
int app_farm_seed_n(const app_farm_t *f, int crop);
int app_farm_next_seed(const app_farm_t *f, int from);

void app_farm_advance(app_farm_t *f, uint32_t now_sec);
int app_farm_add_xp(app_farm_t *f, int xp);

app_farm_res_t app_farm_plant(app_farm_t *f, int plot, int crop);
app_farm_res_t app_farm_water(app_farm_t *f, int plot);
app_farm_res_t app_farm_weed(app_farm_t *f, int plot);
app_farm_res_t app_farm_pest(app_farm_t *f, int plot);
app_farm_res_t app_farm_cut(app_farm_t *f, int plot, uint16_t *coins);
app_farm_res_t app_farm_buy(app_farm_t *f, int crop, int n);

bool app_farm_can_tool(const app_farm_t *f, int plot, int tool);
int app_farm_care_need(const app_farm_t *f);
int app_farm_next_plot(const app_farm_t *f, int tool, int from, int dir);
int app_farm_next_steal(const app_farm_view_t *v, int from, int dir);
int app_farm_next_guest(const app_farm_view_t *v, int act, int from, int dir);

bool app_farm_can_steal(const app_farm_plot_t *p);
bool app_farm_guest_can(const app_farm_view_t *v, int plot, int act);
app_farm_res_t app_farm_steal(app_farm_t *me, app_farm_view_t *you, int plot,
                              uint16_t *coins);
void app_farm_mark_stolen(app_farm_t *f, int plot);
int app_farm_apply_remote(app_farm_t *local, const app_farm_view_t *remote);

bool app_farm_friend_has(const app_farm_t *f, uint32_t id);
app_farm_res_t app_farm_friend_add(app_farm_t *f, uint32_t id);
bool app_farm_friend_del(app_farm_t *f, uint32_t id);

int app_farm_json_write(const app_farm_t *f, char *out, size_t n);
bool app_farm_json_read_view(const char *json, app_farm_view_t *out);
bool app_farm_json_read_self(const char *json, app_farm_t *f);
int app_farm_json_read_peers(const char *json, const char *key,
                             app_farm_peer_t *out, int max);
int app_farm_json_read_mail(const char *json, const char *key,
                            app_farm_mail_t *out, int max);
uint32_t app_farm_json_u32(const char *json, const char *key, uint32_t def);
bool app_farm_json_str(const char *json, const char *key, char *out, size_t n);
bool app_farm_json_ok(const char *json);
