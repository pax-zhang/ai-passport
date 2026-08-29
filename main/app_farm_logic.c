#include "app_farm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(APP_FARM_HOST_DEFAULT) - 1 <= APP_FARM_HOST_MAX,
               "APP_FARM_HOST_DEFAULT too long");

static const app_farm_crop_info_t CROPS[APP_FARM_CROP_N] = {
    {  1800,  10,  18,  1,  6,  1200 },
    {  3600,  15,  28,  1,  8,  1800 },
    {  7200,  25,  48,  3, 12,  2400 },
    { 14400,  40,  80,  5, 16,  3600 },
    { 21600,  60, 130,  8, 22,  5400 },
    { 28800, 100, 220, 12, 30,  7200 },
};

static uint32_t roll(app_farm_t *f, uint32_t mod)
{
    f->rng = (uint8_t)(f->rng * 33u + 17u);
    if (mod == 0) return 0;
    return (uint32_t)f->rng % mod;
}

static void clear_plot(app_farm_plot_t *p)
{
    memset(p, 0, sizeof(*p));
}

static void trim_name(char *s)
{
    size_t n, i;

    if (!s) return;
    n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

const app_farm_crop_info_t *app_farm_crop(int id)
{
    if (id < 0 || id >= APP_FARM_CROP_N) return NULL;
    return &CROPS[id];
}

uint32_t app_farm_plot_eta_sec(const app_farm_plot_t *p)
{
    uint32_t left;

    if (!p) return 0;
    if (p->stage != APP_FARM_ST_SEED && p->stage != APP_FARM_ST_GROW) return 0;
    left = p->grow_left;
    if (p->weed) left *= 2u;
    return left;
}

int app_farm_plot_n(int level)
{
    if (level >= 6) return APP_FARM_PLOT_N;
    if (level >= 3) return 9;
    return 6;
}

int app_farm_xp_need(int level)
{
    if (level < 1) level = 1;
    if (level >= APP_FARM_LV_MAX) return 0;
    return 20 + level * 15;
}

uint32_t app_farm_id_from_mac(const uint8_t mac[6])
{
    uint32_t h = 2166136261u;
    int i;

    if (!mac) return 0;
    for (i = 0; i < 6; i++) {
        h ^= mac[i];
        h *= 16777619u;
    }
    return 100000u + (h % 900000u);
}

static int hex_nib(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool app_farm_mac_parse(const char *s, uint8_t mac[6])
{
    int i, hi, lo;

    if (!s || !mac) return false;
    for (i = 0; i < 6; i++) {
        hi = hex_nib(s[0]);
        lo = hex_nib(s[1]);
        if (hi < 0 || lo < 0) return false;
        mac[i] = (uint8_t)((hi << 4) | lo);
        s += 2;
        if (i < 5) {
            if (*s != ':' && *s != '-') return false;
            s++;
        }
    }
    return *s == 0;
}

void app_farm_mac_fmt(const uint8_t mac[6], char *out, size_t n)
{
    if (!out || n < 18) return;
    snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void app_farm_reset(app_farm_t *f, uint32_t now_sec, uint32_t id, uint8_t rng)
{
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->magic = APP_FARM_MAGIC;
    f->ver = APP_FARM_VER;
    f->level = 1;
    f->coins = APP_FARM_START_COIN;
    f->last_sec = now_sec;
    f->id = id;
    f->rng = rng ? rng : 0x5Au;
    f->seeds[APP_FARM_C_CARROT] = APP_FARM_START_SEED;
    f->pick = APP_FARM_C_CARROT;
    strncpy(f->host, APP_FARM_HOST_DEFAULT, APP_FARM_HOST_MAX);
    f->host[APP_FARM_HOST_MAX] = 0;
}

void app_farm_wipe(app_farm_t *f, uint32_t now_sec)
{
    uint32_t id;
    char host[APP_FARM_HOST_MAX + 1];
    char token[APP_FARM_TOK_MAX + 1];
    uint8_t rng;

    if (!f) return;
    id = f->id;
    rng = f->rng;
    memcpy(host, f->host, sizeof(host));
    memcpy(token, f->token, sizeof(token));
    app_farm_reset(f, now_sec, id, rng);
    memcpy(f->host, host, sizeof(host));
    memcpy(f->token, token, sizeof(token));
}

bool app_farm_valid(const app_farm_t *f)
{
    int i;

    if (!f || f->magic != APP_FARM_MAGIC) return false;
    if (f->ver != APP_FARM_VER) return false;
    if (f->level < 1 || f->level > APP_FARM_LV_MAX) return false;
    if (f->pick >= APP_FARM_CROP_N) return false;
    if (f->friend_n > APP_FARM_FRIEND_N) return false;
    for (i = 0; i < APP_FARM_PLOT_N; i++) {
        const app_farm_plot_t *p = &f->plots[i];
        if (p->stage > APP_FARM_ST_DEAD) return false;
        if (p->crop >= APP_FARM_CROP_N && p->stage != APP_FARM_ST_EMPTY) {
            return false;
        }
    }
    return true;
}

bool app_farm_import(app_farm_t *f, const void *raw, size_t n)
{
    if (!f || !raw || n != sizeof(app_farm_t)) return false;
    memcpy(f, raw, sizeof(*f));
    if (!app_farm_valid(f)) return false;
    f->name[APP_FARM_NAME_MAX] = 0;
    f->host[APP_FARM_HOST_MAX] = 0;
    f->token[APP_FARM_TOK_MAX] = 0;
    if (!f->host[0]) {
        strncpy(f->host, APP_FARM_HOST_DEFAULT, APP_FARM_HOST_MAX);
        f->host[APP_FARM_HOST_MAX] = 0;
    }
    return true;
}

void app_farm_set_name(app_farm_t *f, const char *name)
{
    if (!f) return;
    f->name[0] = 0;
    if (!name) return;
    strncpy(f->name, name, APP_FARM_NAME_MAX);
    f->name[APP_FARM_NAME_MAX] = 0;
    trim_name(f->name);
    f->named = f->name[0] ? 1 : 0;
}

const char *app_farm_name(const app_farm_t *f)
{
    if (!f) return "";
    return f->name;
}

bool app_farm_crop_open(const app_farm_t *f, int crop)
{
    const app_farm_crop_info_t *c = app_farm_crop(crop);

    if (!f || !c) return false;
    return f->level >= c->unlock_lv;
}

int app_farm_seed_n(const app_farm_t *f, int crop)
{
    if (!f || crop < 0 || crop >= APP_FARM_CROP_N) return 0;
    return (int)f->seeds[crop];
}

int app_farm_next_seed(const app_farm_t *f, int from)
{
    int i, idx;

    if (!f) return -1;
    for (i = 0; i < APP_FARM_CROP_N; i++) {
        idx = (from + 1 + i) % APP_FARM_CROP_N;
        if (f->seeds[idx] > 0 && app_farm_crop_open(f, idx)) return idx;
    }
    return -1;
}

int app_farm_add_xp(app_farm_t *f, int xp)
{
    int need, up = 0;

    if (!f || xp <= 0) return 0;
    if (f->level >= APP_FARM_LV_MAX) return 0;
    f->xp = (uint16_t)(f->xp + xp);
    for (;;) {
        need = app_farm_xp_need((int)f->level);
        if (need <= 0 || f->xp < (uint16_t)need) break;
        f->xp = (uint16_t)(f->xp - need);
        f->level++;
        up++;
        if (f->level >= APP_FARM_LV_MAX) {
            f->xp = 0;
            break;
        }
    }
    return up;
}

static void wither_plot(app_farm_plot_t *p)
{
    uint8_t crop = p->crop;
    uint8_t gen = p->gen;

    clear_plot(p);
    p->crop = crop;
    p->gen = gen;
    p->stage = APP_FARM_ST_DEAD;
}

static uint32_t grow_dt(app_farm_t *f, const app_farm_plot_t *p, uint32_t dt)
{
    uint32_t g;

    if (!p->weed) return dt;
    g = dt / 2u;
    if ((dt & 1u) && roll(f, 2) == 0) g++;
    return g;
}

static uint16_t grow_sec_of(const app_farm_plot_t *p)
{
    const app_farm_crop_info_t *c = app_farm_crop((int)p->crop);

    return c ? c->grow_sec : 1800;
}

static uint16_t ripe_sec(const app_farm_plot_t *p)
{
    const app_farm_crop_info_t *c = app_farm_crop((int)p->crop);

    return c ? c->wither_sec : 1200;
}

static void make_ripe(app_farm_plot_t *p)
{
    p->stage = APP_FARM_ST_RIPE;
    p->grow_left = ripe_sec(p);
    if (p->yield == 0) p->yield = 100;
}

static uint16_t harvest_got(const app_farm_plot_t *p)
{
    const app_farm_crop_info_t *c = app_farm_crop((int)p->crop);
    uint32_t y, got;

    if (!c) return 0;
    y = p->yield > 100 ? 100 : p->yield;
    if (y == 0) return 0;
    got = (uint32_t)c->harvest * y / 100u;
    if (got < 1) got = 1;
    return (uint16_t)got;
}

static uint8_t steal_take(uint8_t yield)
{
    uint8_t take;

    if (yield <= APP_FARM_STEAL_KEEP) return 0;
    take = (uint8_t)((uint16_t)yield * APP_FARM_STEAL_PCT / 100u);
    if (take < 1) take = 1;
    if ((int)yield - (int)take < APP_FARM_STEAL_KEEP) {
        take = (uint8_t)(yield - APP_FARM_STEAL_KEEP);
    }
    return take;
}

static void spawn_haz(app_farm_t *f, app_farm_plot_t *p)
{
    if (!p->weed && roll(f, 100) < APP_FARM_WEED_PCT) p->weed = 1;
    if (!p->pest && roll(f, 100) < APP_FARM_PEST_PCT) p->pest = 1;
}

static void pest_eat(app_farm_plot_t *p)
{
    if (!p->pest || p->stage != APP_FARM_ST_RIPE || p->yield == 0) return;
    if (p->yield <= APP_FARM_PEST_EAT) p->yield = 0;
    else p->yield = (uint8_t)(p->yield - APP_FARM_PEST_EAT);
    if (p->yield == 0) wither_plot(p);
}

static void tick_wither(app_farm_plot_t *p, uint32_t dt)
{
    if (p->stage != APP_FARM_ST_RIPE) return;
    if (p->grow_left == 0) p->grow_left = ripe_sec(p);
    if (dt >= p->grow_left) {
        wither_plot(p);
        return;
    }
    p->grow_left = (uint16_t)(p->grow_left - dt);
}

static void tick_plot(app_farm_t *f, app_farm_plot_t *p, uint32_t dt)
{
    uint32_t left, n, rem, gdt, used, rest, span;

    if (p->stage == APP_FARM_ST_EMPTY || p->stage == APP_FARM_ST_DEAD) return;
    rest = 0;
    if (!p->dry && p->stage != APP_FARM_ST_RIPE) {
        gdt = grow_dt(f, p, dt);
        left = p->grow_left;
        if (gdt >= left) {
            used = p->weed ? left * 2u : left;
            if (used > dt) used = dt;
            make_ripe(p);
            rest = dt - used;
        } else {
            p->grow_left = (uint16_t)(left - gdt);
            if (p->stage == APP_FARM_ST_SEED &&
                p->grow_left * 2u < grow_sec_of(p)) {
                p->stage = APP_FARM_ST_GROW;
            }
        }
    } else if (p->stage == APP_FARM_ST_RIPE) {
        rest = dt;
    }
    tick_wither(p, rest);
    if (p->stage == APP_FARM_ST_EMPTY || p->stage == APP_FARM_ST_DEAD) return;
    span = APP_FARM_HAZ_SEC - APP_FARM_HAZ_JIT +
           roll(f, APP_FARM_HAZ_JIT * 2u + 1u);
    if (span == 0) span = 1;
    n = dt / span;
    rem = dt % span;
    while (n--) {
        spawn_haz(f, p);
        pest_eat(p);
        if (p->stage == APP_FARM_ST_DEAD) return;
    }
    if (rem && roll(f, span) < rem) {
        spawn_haz(f, p);
        pest_eat(p);
    }
}

void app_farm_advance(app_farm_t *f, uint32_t now_sec)
{
    uint32_t dt;
    int i, n;
    const uint32_t wall = 1700000000u;

    if (!f) return;
    if (f->last_sec < wall && now_sec >= wall) {
        f->last_sec = now_sec;
        return;
    }
    if (now_sec < f->last_sec) {
        f->last_sec = now_sec;
        return;
    }
    dt = now_sec - f->last_sec;
    if (dt > 8u * 3600u) dt = 8u * 3600u;
    f->last_sec = now_sec;
    if (dt == 0) return;
    n = app_farm_plot_n((int)f->level);
    for (i = 0; i < n; i++) tick_plot(f, &f->plots[i], dt);
}

static app_farm_res_t plot_ok(const app_farm_t *f, int plot)
{
    if (!f) return APP_FARM_NONE;
    if (plot < 0 || plot >= APP_FARM_PLOT_N) return APP_FARM_LOCKED;
    if (plot >= app_farm_plot_n((int)f->level)) return APP_FARM_LOCKED;
    return APP_FARM_OK;
}

app_farm_res_t app_farm_plant(app_farm_t *f, int plot, int crop)
{
    app_farm_res_t e;
    const app_farm_crop_info_t *c;
    uint8_t gen;

    e = plot_ok(f, plot);
    if (e != APP_FARM_OK) return e;
    c = app_farm_crop(crop);
    if (!c) return APP_FARM_LOCKED_CROP;
    if (!app_farm_crop_open(f, crop)) return APP_FARM_LOCKED_CROP;
    if (f->seeds[crop] == 0) return APP_FARM_NO_SEED;
    if (f->plots[plot].stage != APP_FARM_ST_EMPTY) return APP_FARM_NEED_EMPTY;
    f->seeds[crop]--;
    gen = f->plots[plot].gen;
    if (gen == 255) gen = 0;
    gen++;
    clear_plot(&f->plots[plot]);
    f->plots[plot].crop = (uint8_t)crop;
    f->plots[plot].stage = APP_FARM_ST_SEED;
    f->plots[plot].dry = 1;
    f->plots[plot].yield = 100;
    f->plots[plot].gen = gen;
    f->plots[plot].grow_left = c->grow_sec;
    f->pick = (uint8_t)crop;
    app_farm_add_xp(f, 2);
    return APP_FARM_OK;
}

app_farm_res_t app_farm_water(app_farm_t *f, int plot)
{
    app_farm_res_t e = plot_ok(f, plot);
    app_farm_plot_t *p;

    if (e != APP_FARM_OK) return e;
    p = &f->plots[plot];
    if (p->stage == APP_FARM_ST_EMPTY || p->stage == APP_FARM_ST_DEAD) {
        return APP_FARM_NEED_CROP;
    }
    if (!p->dry) return APP_FARM_NEED_DRY;
    p->dry = 0;
    app_farm_add_xp(f, 1);
    return APP_FARM_OK;
}

app_farm_res_t app_farm_weed(app_farm_t *f, int plot)
{
    app_farm_res_t e = plot_ok(f, plot);

    if (e != APP_FARM_OK) return e;
    if (f->plots[plot].stage == APP_FARM_ST_EMPTY ||
        f->plots[plot].stage == APP_FARM_ST_DEAD) {
        return APP_FARM_NEED_CROP;
    }
    if (!f->plots[plot].weed) return APP_FARM_NEED_WEED;
    f->plots[plot].weed = 0;
    app_farm_add_xp(f, 2);
    return APP_FARM_OK;
}

app_farm_res_t app_farm_pest(app_farm_t *f, int plot)
{
    app_farm_res_t e = plot_ok(f, plot);

    if (e != APP_FARM_OK) return e;
    if (f->plots[plot].stage == APP_FARM_ST_EMPTY ||
        f->plots[plot].stage == APP_FARM_ST_DEAD) {
        return APP_FARM_NEED_CROP;
    }
    if (!f->plots[plot].pest) return APP_FARM_NEED_PEST;
    f->plots[plot].pest = 0;
    app_farm_add_xp(f, 2);
    return APP_FARM_OK;
}

app_farm_res_t app_farm_cut(app_farm_t *f, int plot, uint16_t *coins)
{
    app_farm_res_t e = plot_ok(f, plot);
    const app_farm_crop_info_t *c;
    app_farm_plot_t *p;
    uint16_t got;
    uint8_t gen;

    if (coins) *coins = 0;
    if (e != APP_FARM_OK) return e;
    p = &f->plots[plot];
    if (p->stage == APP_FARM_ST_DEAD) {
        gen = p->gen;
        clear_plot(p);
        p->gen = gen;
        return APP_FARM_OK;
    }
    if (p->stage != APP_FARM_ST_RIPE) return APP_FARM_NEED_RIPE;
    if (p->weed || p->pest) return APP_FARM_HAS_HAZ;
    c = app_farm_crop((int)p->crop);
    if (!c) return APP_FARM_NEED_RIPE;
    got = harvest_got(p);
    f->coins += got;
    app_farm_add_xp(f, (int)c->xp);
    gen = p->gen;
    clear_plot(p);
    p->gen = gen;
    if (coins) *coins = got;
    return APP_FARM_OK;
}

bool app_farm_can_tool(const app_farm_t *f, int plot, int tool)
{
    const app_farm_plot_t *p;

    if (plot_ok(f, plot) != APP_FARM_OK) return false;
    p = &f->plots[plot];
    if (tool == APP_FARM_TOOL_PLANT) {
        if (p->stage != APP_FARM_ST_EMPTY) return false;
        if (!app_farm_crop_open(f, (int)f->pick)) return false;
        return f->seeds[f->pick] > 0;
    }
    if (tool == APP_FARM_TOOL_WATER) {
        return p->stage != APP_FARM_ST_EMPTY && p->stage != APP_FARM_ST_DEAD &&
               p->dry;
    }
    if (tool == APP_FARM_TOOL_WEED) {
        return p->stage != APP_FARM_ST_EMPTY && p->stage != APP_FARM_ST_DEAD &&
               p->weed;
    }
    if (tool == APP_FARM_TOOL_PEST) {
        return p->stage != APP_FARM_ST_EMPTY && p->stage != APP_FARM_ST_DEAD &&
               p->pest;
    }
    if (tool == APP_FARM_TOOL_CUT) {
        if (p->stage == APP_FARM_ST_DEAD) return true;
        if (p->stage != APP_FARM_ST_RIPE) return false;
        return !p->weed && !p->pest;
    }
    return false;
}

int app_farm_care_need(const app_farm_t *f)
{
    int i, n;
    int water = 0, weed = 0, pest = 0, cut = 0;

    if (!f) return -1;
    n = app_farm_plot_n((int)f->level);
    for (i = 0; i < n; i++) {
        const app_farm_plot_t *p = &f->plots[i];

        if (p->stage == APP_FARM_ST_EMPTY || p->stage == APP_FARM_ST_DEAD) {
            continue;
        }
        if (p->dry) water = 1;
        if (p->weed) weed = 1;
        if (p->pest) pest = 1;
        if (p->stage == APP_FARM_ST_RIPE && !p->weed && !p->pest) cut = 1;
    }
    if (cut) return APP_FARM_TOOL_CUT;
    if (pest) return APP_FARM_TOOL_PEST;
    if (weed) return APP_FARM_TOOL_WEED;
    if (water) return APP_FARM_TOOL_WATER;
    return -1;
}

int app_farm_next_plot(const app_farm_t *f, int tool, int from, int dir)
{
    int n, i, p;

    if (!f) return -1;
    n = app_farm_plot_n((int)f->level);
    if (n <= 0) return -1;
    if (dir == 0) {
        for (i = 0; i < n; i++) {
            if (app_farm_can_tool(f, i, tool)) return i;
        }
        return -1;
    }
    if (dir > 0) dir = 1;
    else dir = -1;
    if (from < 0) from = (dir > 0) ? -1 : n;
    for (i = 1; i <= n; i++) {
        p = from + dir * i;
        if (p < 0) p += n;
        if (p >= n) p -= n;
        if (app_farm_can_tool(f, p, tool)) return p;
    }
    return -1;
}

int app_farm_next_steal(const app_farm_view_t *v, int from, int dir)
{
    return app_farm_next_guest(v, APP_FARM_ACT_STEAL, from, dir);
}

bool app_farm_guest_can(const app_farm_view_t *v, int plot, int act)
{
    const app_farm_plot_t *p;

    if (!v || plot < 0 || plot >= app_farm_plot_n((int)v->level)) return false;
    p = &v->plots[plot];
    if (act == APP_FARM_ACT_STEAL) return app_farm_can_steal(p);
    if (p->stage == APP_FARM_ST_EMPTY || p->stage == APP_FARM_ST_DEAD) {
        return false;
    }
    if (act == APP_FARM_ACT_WATER) return p->dry != 0;
    if (act == APP_FARM_ACT_WEED) return p->weed != 0;
    if (act == APP_FARM_ACT_PEST) return p->pest != 0;
    return false;
}

int app_farm_next_guest(const app_farm_view_t *v, int act, int from, int dir)
{
    int n, i, p;

    if (!v) return -1;
    n = app_farm_plot_n((int)v->level);
    if (n <= 0) return -1;
    if (dir == 0) {
        for (i = 0; i < n; i++) {
            if (app_farm_guest_can(v, i, act)) return i;
        }
        return -1;
    }
    if (dir > 0) dir = 1;
    else dir = -1;
    if (from < 0) from = (dir > 0) ? -1 : n;
    for (i = 1; i <= n; i++) {
        p = from + dir * i;
        if (p < 0) p += n;
        if (p >= n) p -= n;
        if (app_farm_guest_can(v, p, act)) return p;
    }
    return -1;
}

app_farm_res_t app_farm_buy(app_farm_t *f, int crop, int n)
{
    const app_farm_crop_info_t *c;
    uint32_t cost;

    c = app_farm_crop(crop);
    if (!f || !c || n <= 0) return APP_FARM_LOCKED_CROP;
    if (!app_farm_crop_open(f, crop)) return APP_FARM_LOCKED_CROP;
    cost = (uint32_t)c->seed_cost * (uint32_t)n;
    if (f->coins < cost) return APP_FARM_NO_COIN;
    if ((int)f->seeds[crop] + n > APP_FARM_SEED_MAX) n = APP_FARM_SEED_MAX - (int)f->seeds[crop];
    if (n <= 0) return APP_FARM_BUSY;
    f->coins -= (uint32_t)c->seed_cost * (uint32_t)n;
    f->seeds[crop] = (uint16_t)(f->seeds[crop] + n);
    return APP_FARM_OK;
}

bool app_farm_can_steal(const app_farm_plot_t *p)
{
    if (!p) return false;
    return p->stage == APP_FARM_ST_RIPE && steal_take(p->yield) > 0;
}

app_farm_res_t app_farm_steal(app_farm_t *me, app_farm_view_t *you, int plot,
                              uint16_t *coins)
{
    const app_farm_crop_info_t *c;
    app_farm_plot_t *p;
    uint8_t take;
    uint16_t got;

    if (coins) *coins = 0;
    if (!me || !you) return APP_FARM_NONE;
    if (you->id && you->id == me->id) return APP_FARM_SELF;
    if (plot < 0 || plot >= app_farm_plot_n((int)you->level)) return APP_FARM_LOCKED;
    p = &you->plots[plot];
    if (!app_farm_can_steal(p)) return APP_FARM_NEED_RIPE;
    c = app_farm_crop((int)p->crop);
    if (!c) return APP_FARM_NEED_RIPE;
    take = steal_take(p->yield);
    got = (uint16_t)((uint32_t)c->harvest * take / 100u);
    if (got < 1) got = 1;
    me->coins += got;
    app_farm_add_xp(me, 3);
    p->yield = (uint8_t)(p->yield - take);
    p->stolen = 1;
    if (coins) *coins = got;
    return APP_FARM_OK;
}

void app_farm_mark_stolen(app_farm_t *f, int plot)
{
    uint8_t gen;

    if (!f || plot < 0 || plot >= APP_FARM_PLOT_N) return;
    gen = f->plots[plot].gen;
    clear_plot(&f->plots[plot]);
    f->plots[plot].gen = gen;
    f->plots[plot].stolen = 1;
}

static int merge_plot(app_farm_plot_t *loc, const app_farm_plot_t *rem)
{
    uint8_t gen;
    int hit = 0;

    if (rem->stage == APP_FARM_ST_EMPTY && rem->stolen) {
        if (loc->stage == APP_FARM_ST_EMPTY) return 0;
        if (loc->gen && rem->gen && loc->gen > rem->gen) return 0;
        if (!loc->gen && !rem->gen && loc->stage != APP_FARM_ST_RIPE) return 0;
        gen = loc->gen ? loc->gen : rem->gen;
        clear_plot(loc);
        loc->gen = gen;
        loc->stolen = 1;
        return 1;
    }
    if (loc->stage == APP_FARM_ST_EMPTY) return 0;
    if (loc->gen && rem->gen && loc->gen != rem->gen) return 0;
    if (rem->stage == APP_FARM_ST_DEAD) {
        gen = loc->gen;
        *loc = *rem;
        loc->gen = gen;
        return 0;
    }
    if (rem->stage == APP_FARM_ST_EMPTY) return 0;
    if (rem->yield && rem->yield < loc->yield) {
        loc->yield = rem->yield;
        loc->stolen = 1;
        hit = 1;
    }
    if (loc->dry && !rem->dry) loc->dry = 0;
    if (loc->weed && !rem->weed) loc->weed = 0;
    if (loc->pest && !rem->pest) loc->pest = 0;
    return hit;
}

int app_farm_apply_remote(app_farm_t *local, const app_farm_view_t *remote)
{
    int i, n, hit = 0;

    if (!local || !remote) return 0;
    n = app_farm_plot_n((int)local->level);
    for (i = 0; i < n; i++) hit += merge_plot(&local->plots[i], &remote->plots[i]);
    return hit;
}

bool app_farm_friend_has(const app_farm_t *f, uint32_t id)
{
    int i;

    if (!f || id == 0) return false;
    for (i = 0; i < f->friend_n && i < APP_FARM_FRIEND_N; i++) {
        if (f->friends[i] == id) return true;
    }
    return false;
}

app_farm_res_t app_farm_friend_add(app_farm_t *f, uint32_t id)
{
    if (!f || id < 100000u || id > 999999u) return APP_FARM_NONE;
    if (id == f->id) return APP_FARM_SELF;
    if (app_farm_friend_has(f, id)) return APP_FARM_BUSY;
    if (f->friend_n >= APP_FARM_FRIEND_N) return APP_FARM_BUSY;
    f->friends[f->friend_n++] = id;
    return APP_FARM_OK;
}

bool app_farm_friend_del(app_farm_t *f, uint32_t id)
{
    int i, j;

    if (!f) return false;
    for (i = 0; i < f->friend_n; i++) {
        if (f->friends[i] != id) continue;
        for (j = i; j + 1 < f->friend_n; j++) f->friends[j] = f->friends[j + 1];
        f->friend_n--;
        f->friends[f->friend_n] = 0;
        return true;
    }
    return false;
}

static void skip_ws(const char **p)
{
    const char *s = *p;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    *p = s;
}

static const char *after_key(const char *json, const char *key)
{
    char pat[24];
    const char *p;
    int n;

    if (!json || !key) return NULL;
    n = snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (n < 0 || n >= (int)sizeof(pat)) return NULL;
    p = strstr(json, pat);
    if (!p) return NULL;
    p += (size_t)n;
    skip_ws(&p);
    if (*p != ':') return NULL;
    p++;
    skip_ws(&p);
    return p;
}

static bool copy_quoted(const char *p, char *out, size_t n)
{
    size_t i = 0;

    if (!p || *p != '"' || !out || n == 0) return false;
    p++;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) c = *p++;
        if (i + 1 < n) out[i++] = c;
    }
    if (*p != '"') return false;
    out[i] = 0;
    return true;
}

uint32_t app_farm_json_u32(const char *json, const char *key, uint32_t def)
{
    const char *p;
    unsigned long v;
    char *end;

    p = after_key(json, key);
    if (!p || *p < '0' || *p > '9') return def;
    v = strtoul(p, &end, 10);
    if (end == p) return def;
    return (uint32_t)v;
}

bool app_farm_json_str(const char *json, const char *key, char *out, size_t n)
{
    const char *p = after_key(json, key);

    return copy_quoted(p, out, n);
}

bool app_farm_json_ok(const char *json)
{
    const char *p = after_key(json, "ok");

    if (!p) return false;
    return p[0] == 't' || p[0] == '1';
}

static bool read_plots(const char *json, app_farm_plot_t *plots)
{
    const char *p, *arr;
    int i;

    memset(plots, 0, sizeof(app_farm_plot_t) * APP_FARM_PLOT_N);
    arr = after_key(json, "plots");
    if (!arr || *arr != '[') return true;
    p = arr + 1;
    for (i = 0; i < APP_FARM_PLOT_N; i++) {
        const char *end;

        skip_ws(&p);
        if (*p == ']') break;
        if (*p != '{') return false;
        end = strchr(p, '}');
        if (!end) return false;
        plots[i].crop = (uint8_t)app_farm_json_u32(p, "c", 0);
        plots[i].stage = (uint8_t)app_farm_json_u32(p, "s", 0);
        plots[i].dry = (uint8_t)app_farm_json_u32(p, "d", 0);
        plots[i].weed = (uint8_t)app_farm_json_u32(p, "w", 0);
        plots[i].pest = (uint8_t)app_farm_json_u32(p, "p", 0);
        plots[i].stolen = (uint8_t)app_farm_json_u32(p, "x", 0);
        plots[i].yield = (uint8_t)app_farm_json_u32(p, "y", 100);
        if (plots[i].yield > 100) plots[i].yield = 100;
        plots[i].grow_left = (uint16_t)app_farm_json_u32(p, "g", 0);
        plots[i].gen = (uint8_t)app_farm_json_u32(p, "n", 0);
        p = end + 1;
        skip_ws(&p);
        if (*p == ',') p++;
    }
    return true;
}

static bool read_u16_arr(const char *json, const char *key, uint16_t *out, int max)
{
    const char *p;
    int i;

    memset(out, 0, sizeof(uint16_t) * (size_t)max);
    p = after_key(json, key);
    if (!p || *p != '[') return true;
    p++;
    for (i = 0; i < max; i++) {
        char *end;

        skip_ws(&p);
        if (*p == ']') break;
        out[i] = (uint16_t)strtoul(p, &end, 10);
        if (end == p) return false;
        p = end;
        skip_ws(&p);
        if (*p == ',') p++;
    }
    return true;
}

bool app_farm_json_read_view(const char *json, app_farm_view_t *out)
{
    if (!json || !out) return false;
    memset(out, 0, sizeof(*out));
    out->id = app_farm_json_u32(json, "id", 0);
    out->level = (uint8_t)app_farm_json_u32(json, "level", 1);
    if (out->level < 1) out->level = 1;
    out->coins = app_farm_json_u32(json, "coins", 0);
    app_farm_json_str(json, "name", out->name, sizeof(out->name));
    return read_plots(json, out->plots);
}

bool app_farm_json_read_self(const char *json, app_farm_t *f)
{
    app_farm_view_t v;
    uint16_t seeds[APP_FARM_CROP_N];
    uint32_t friends[APP_FARM_FRIEND_N];
    const char *p;
    int i;

    if (!f || !app_farm_json_read_view(json, &v)) return false;
    if (v.id) f->id = v.id;
    if (v.level) f->level = v.level;
    f->coins = v.coins;
    f->xp = (uint16_t)app_farm_json_u32(json, "xp", f->xp);
    if (v.name[0]) app_farm_set_name(f, v.name);
    memcpy(f->plots, v.plots, sizeof(f->plots));
    if (read_u16_arr(json, "seeds", seeds, APP_FARM_CROP_N)) {
        memcpy(f->seeds, seeds, sizeof(f->seeds));
    }
    memset(friends, 0, sizeof(friends));
    p = after_key(json, "friends");
    f->friend_n = 0;
    if (p && *p == '[') {
        p++;
        for (i = 0; i < APP_FARM_FRIEND_N; i++) {
            char *end;
            uint32_t id;

            skip_ws(&p);
            if (*p == ']') break;
            id = (uint32_t)strtoul(p, &end, 10);
            if (end == p) break;
            if (id >= 100000u) f->friends[f->friend_n++] = id;
            p = end;
            skip_ws(&p);
            if (*p == ',') p++;
        }
    }
    app_farm_json_str(json, "token", f->token, sizeof(f->token));
    return true;
}

int app_farm_json_read_peers(const char *json, const char *key,
                             app_farm_peer_t *out, int max)
{
    const char *p;
    int n = 0;

    if (!json || !out || max <= 0) return 0;
    p = after_key(json, key ? key : "list");
    if (!p || *p != '[') return 0;
    p++;
    while (n < max) {
        const char *end;

        skip_ws(&p);
        if (*p == ']' || *p == 0) break;
        if (*p != '{') break;
        end = strchr(p, '}');
        if (!end) break;
        memset(&out[n], 0, sizeof(out[n]));
        out[n].id = app_farm_json_u32(p, "id", 0);
        out[n].level = (uint8_t)app_farm_json_u32(p, "level", 1);
        out[n].coins = app_farm_json_u32(p, "coins", 0);
        app_farm_json_str(p, "name", out[n].name, sizeof(out[n].name));
        if (out[n].id) n++;
        p = end + 1;
        skip_ws(&p);
        if (*p == ',') p++;
    }
    return n;
}

int app_farm_json_read_mail(const char *json, const char *key,
                            app_farm_mail_t *out, int max)
{
    const char *p;
    int n = 0;

    if (!json || !out || max <= 0) return 0;
    p = after_key(json, key ? key : "list");
    if (!p || *p != '[') return 0;
    p++;
    while (n < max) {
        const char *end;

        skip_ws(&p);
        if (*p == ']' || *p == 0) break;
        if (*p != '{') break;
        end = strchr(p, '}');
        if (!end) break;
        memset(&out[n], 0, sizeof(out[n]));
        out[n].from = app_farm_json_u32(p, "from", 0);
        out[n].kind = (uint8_t)app_farm_json_u32(p, "kind", 0);
        out[n].got = (uint16_t)app_farm_json_u32(p, "got", 0);
        app_farm_json_str(p, "name", out[n].name, sizeof(out[n].name));
        if (out[n].from || out[n].kind) n++;
        p = end + 1;
        skip_ws(&p);
        if (*p == ',') p++;
    }
    return n;
}

int app_farm_json_write(const app_farm_t *f, char *out, size_t n)
{
    char buf[1400];
    int used = 0, i, pn, r;

    if (!f || !out || n == 0) return -1;
    r = snprintf(buf, sizeof(buf),
                 "{\"id\":%lu,\"name\":\"%s\",\"level\":%u,\"xp\":%u,"
                 "\"coins\":%lu,\"pick\":%u,\"seeds\":[",
                 (unsigned long)f->id, f->name, (unsigned)f->level,
                 (unsigned)f->xp, (unsigned long)f->coins, (unsigned)f->pick);
    if (r < 0 || r >= (int)sizeof(buf)) return -1;
    used = r;
    for (i = 0; i < APP_FARM_CROP_N; i++) {
        r = snprintf(buf + used, sizeof(buf) - (size_t)used, "%s%u",
                     i ? "," : "", (unsigned)f->seeds[i]);
        if (r < 0 || used + r >= (int)sizeof(buf)) return -1;
        used += r;
    }
    r = snprintf(buf + used, sizeof(buf) - (size_t)used, "],\"plots\":[");
    if (r < 0 || used + r >= (int)sizeof(buf)) return -1;
    used += r;
    pn = APP_FARM_PLOT_N;
    for (i = 0; i < pn; i++) {
        const app_farm_plot_t *p = &f->plots[i];

        r = snprintf(buf + used, sizeof(buf) - (size_t)used,
                     "%s{\"c\":%u,\"s\":%u,\"d\":%u,\"w\":%u,\"p\":%u,"
                     "\"x\":%u,\"y\":%u,\"g\":%u,\"n\":%u}",
                     i ? "," : "", (unsigned)p->crop, (unsigned)p->stage,
                     (unsigned)p->dry, (unsigned)p->weed, (unsigned)p->pest,
                     (unsigned)p->stolen, (unsigned)p->yield,
                     (unsigned)p->grow_left, (unsigned)p->gen);
        if (r < 0 || used + r >= (int)sizeof(buf)) return -1;
        used += r;
    }
    r = snprintf(buf + used, sizeof(buf) - (size_t)used, "],\"friends\":[");
    if (r < 0 || used + r >= (int)sizeof(buf)) return -1;
    used += r;
    for (i = 0; i < f->friend_n; i++) {
        r = snprintf(buf + used, sizeof(buf) - (size_t)used, "%s%lu",
                     i ? "," : "", (unsigned long)f->friends[i]);
        if (r < 0 || used + r >= (int)sizeof(buf)) return -1;
        used += r;
    }
    r = snprintf(buf + used, sizeof(buf) - (size_t)used, "]}");
    if (r < 0 || used + r >= (int)sizeof(buf)) return -1;
    used += r;
    if ((size_t)used + 1 > n) return -1;
    memcpy(out, buf, (size_t)used + 1);
    return used;
}
