#include "app_meow.h"
#include "app_logic.h"

#include <string.h>

_Static_assert(offsetof(app_meow_t, ailment) == APP_MEOW_VER2_SIZE, "v2 blob size");
_Static_assert(offsetof(app_meow_t, ver) == 12, "ver offset");
_Static_assert(offsetof(app_meow_t, name) == 97, "v9 name offset");
_Static_assert(APP_MEOW_G_N == 16, "goods count");

typedef struct {
    uint8_t cat;
    uint8_t use;
    int8_t hunger;
    int8_t happy;
    int8_t health;
    int8_t weight;
    int8_t dirt;
    uint8_t poop;
    uint8_t dur;
    uint8_t ailment;
    uint8_t flags;
    uint8_t loot_w;
    uint8_t tier; /* 0 常见 .. 3 稀有 */
    uint8_t xp;
} spec_t;

/* hun hap hp wt dirt poop dur ail flags loot tier xp */
static const spec_t CAT[APP_MEOW_G_N] = {
    { APP_MEOW_CAT_FOOD, APP_MEOW_USE_MEAL,  32,  4,  0, 3,  6, 1, 1, 0, APP_MEOW_GF_JUNK,    10, 2, 4 },
    { APP_MEOW_CAT_FOOD, APP_MEOW_USE_MEAL,  22,  2,  0, 1,  0, 1, 1, 0, 0,                  12, 0, 4 },
    { APP_MEOW_CAT_FOOD, APP_MEOW_USE_MEAL,  14,  8,  4, 0, -2, 0, 1, 0, APP_MEOW_GF_HEALTHY,  9, 1, 5 },
    { APP_MEOW_CAT_FOOD, APP_MEOW_USE_MEAL,  12, 10,  2, 0,  0, 0, 1, 0, APP_MEOW_GF_HEALTHY, 10, 1, 5 },
    { APP_MEOW_CAT_FOOD, APP_MEOW_USE_DRINK,  8,  0,  2, 0, -4, 0, 3, 0, APP_MEOW_GF_HEALTHY, 12, 0, 3 },
    { APP_MEOW_CAT_FOOD, APP_MEOW_USE_DRINK, 10, 12,  0, 2,  2, 0, 2, 0, APP_MEOW_GF_JUNK,     8, 2, 4 },
    { APP_MEOW_CAT_MED,  APP_MEOW_USE_MED,    0,  0, 18, 0,  0, 0, 2, APP_MEOW_AI_WORM,  0,  4, 3, 6 },
    { APP_MEOW_CAT_MED,  APP_MEOW_USE_MED,    8,  0, 18, 0,  0, 0, 2, APP_MEOW_AI_TUMMY, 0,  5, 1, 5 },
    { APP_MEOW_CAT_MED,  APP_MEOW_USE_MED,    0,  4, 18, 0,  0, 0, 2, APP_MEOW_AI_COLD,  0,  5, 1, 5 },
    { APP_MEOW_CAT_MED,  APP_MEOW_USE_MED,    0,  8, 18, 0,  0, 0, 3, APP_MEOW_AI_COUGH, 0,  5, 2, 5 },
    { APP_MEOW_CAT_MED,  APP_MEOW_USE_MED,    0,  6, 14, 0,  0, 0, 4, 0, APP_MEOW_GF_HEALTHY,  4, 2, 5 },
    { APP_MEOW_CAT_GEAR, APP_MEOW_USE_WASH,   0,  8,  0, 0,-40, 0, 4, 0, 0,                   7, 1, 4 },
    { APP_MEOW_CAT_GEAR, APP_MEOW_USE_SWEEP,  0,  6,  0, 0,  0, 0, 3, 0, 0,                   8, 0, 3 },
    { APP_MEOW_CAT_GEAR, APP_MEOW_USE_SWEEP,  0,  0,  0, 0,-18, 0, 4, 0, 0,                   9, 0, 3 },
    { APP_MEOW_CAT_GEAR, APP_MEOW_USE_DIG,    0,  0,  0, 0,  8, 0, 8, 0, APP_MEOW_GF_SHOVEL,   2, 3, 4 },
    { APP_MEOW_CAT_GEAR, APP_MEOW_USE_WASH,   0, 10,  0, 0,-32, 0, 4, 0, 0,                   6, 2, 4 },
};

/* v4 44 种 -> 现用 16 种。同类合并。 */
static const int8_t V4_MAP[APP_MEOW_VER4_N] = {
    APP_MEOW_G_BURGER, APP_MEOW_G_BURGER, APP_MEOW_G_ONIGIRI, APP_MEOW_G_SALAD,
    APP_MEOW_G_ONIGIRI, APP_MEOW_G_BURGER, APP_MEOW_G_BURGER, APP_MEOW_G_ONIGIRI,
    APP_MEOW_G_ONIGIRI, APP_MEOW_G_ONIGIRI, APP_MEOW_G_SALAD, APP_MEOW_G_BURGER,
    APP_MEOW_G_ONIGIRI, APP_MEOW_G_FRUIT, APP_MEOW_G_FRUIT, APP_MEOW_G_BURGER,
    APP_MEOW_G_WATER, APP_MEOW_G_COLA, APP_MEOW_G_WATER, APP_MEOW_G_COLA,
    APP_MEOW_G_COLA, APP_MEOW_G_WATER, APP_MEOW_G_WATER, APP_MEOW_G_WATER,
    APP_MEOW_G_WORM, APP_MEOW_G_STOMACH, APP_MEOW_G_COLD, APP_MEOW_G_COUGH,
    APP_MEOW_G_VITAMIN, APP_MEOW_G_VITAMIN, APP_MEOW_G_VITAMIN, APP_MEOW_G_COLD,
    APP_MEOW_G_SOAP, APP_MEOW_G_SOAP, APP_MEOW_G_TOWEL, APP_MEOW_G_SOAP,
    APP_MEOW_G_SOAP, APP_MEOW_G_TOWEL, APP_MEOW_G_TRASH, APP_MEOW_G_TISSUE,
    APP_MEOW_G_SHOVEL, APP_MEOW_G_TOWEL, APP_MEOW_G_TOWEL, APP_MEOW_G_TOWEL
};

static int s_last_got = -1;
static int s_last_souv = -1;
static uint8_t s_last_prize[APP_MEOW_G_N];
static int s_rot_feed = -1;
static int s_rot_bath = -1;
static int s_rot_heal = -1;

static void rot_reset(void)
{
    s_rot_feed = -1;
    s_rot_bath = -1;
    s_rot_heal = -1;
}

static int *rot_slot(app_meow_act_t act)
{
    if (act == APP_MEOW_FEED) return &s_rot_feed;
    if (act == APP_MEOW_BATH) return &s_rot_bath;
    if (act == APP_MEOW_HEAL) return &s_rot_heal;
    return NULL;
}

static const spec_t *spec(int id)
{
    if (id < 0 || id >= APP_MEOW_G_N) return NULL;
    return &CAT[id];
}

static void mark_sick(app_meow_t *p, uint8_t ai);

static void bump(uint8_t *v, int d, int lo, int hi)
{
    int n = (int)*v + d;
    if (n < lo) n = lo;
    if (n > hi) n = hi;
    *v = (uint8_t)n;
}

static void clamp(app_meow_t *p)
{
    if (p->hunger > APP_MEOW_STAT_MAX) p->hunger = APP_MEOW_STAT_MAX;
    if (p->happy > APP_MEOW_STAT_MAX) p->happy = APP_MEOW_STAT_MAX;
    if (p->health > APP_MEOW_STAT_MAX) p->health = APP_MEOW_STAT_MAX;
    if (p->poop > APP_MEOW_POOP_MAX) p->poop = APP_MEOW_POOP_MAX;
    if (p->dirt > APP_MEOW_DIRT_MAX) p->dirt = APP_MEOW_DIRT_MAX;
    if (p->ailment > APP_MEOW_AI_COUGH) p->ailment = 0;
    if (p->weight < 1) p->weight = 1;
    if (p->weight > 99) p->weight = 99;
    if (p->level > APP_MEOW_LV_MAX) p->level = APP_MEOW_LV_MAX;
    if (p->stage == APP_MEOW_EGG || p->stage == APP_MEOW_DEAD) p->level = 0;
    for (int i = 0; i < APP_MEOW_G_N; i++) {
        if (p->inv_n[i] > APP_MEOW_INV_MAX) p->inv_n[i] = APP_MEOW_INV_MAX;
        if (p->inv_n[i] == 0) {
            p->inv_d[i] = 0;
        } else if (p->inv_d[i] == 0 || p->inv_d[i] > CAT[i].dur) {
            p->inv_d[i] = CAT[i].dur;
        }
    }
    if (p->trip_st > APP_MEOW_TRIP_BACK) p->trip_st = APP_MEOW_TRIP_IDLE;
    if (p->trip_pack > APP_MEOW_TRIP_PACK_MAX) p->trip_pack = APP_MEOW_TRIP_PACK_MAX;
    if (p->trip_gain > (uint16_t)(APP_MEOW_TRIP_PACK_MAX * 64)) {
        p->trip_gain = (uint16_t)(APP_MEOW_TRIP_PACK_MAX * 64);
    }
    p->found &= (uint16_t)((1u << APP_MEOW_SOUV_N) - 1u);
    p->named = p->named ? 1 : 0;
    p->name[APP_MEOW_NAME_MAX] = 0;
}

static void put(app_meow_t *p, int id, int n)
{
    if (id < 0 || id >= APP_MEOW_G_N || n <= 0) return;
    if (n > APP_MEOW_INV_MAX) n = APP_MEOW_INV_MAX;
    p->inv_n[id] = (uint16_t)n;
    p->inv_d[id] = CAT[id].dur;
}

static void hatch_kit(app_meow_t *p)
{
    memset(p->inv_n, 0, sizeof(p->inv_n));
    memset(p->inv_d, 0, sizeof(p->inv_d));
    put(p, APP_MEOW_G_ONIGIRI, 2);
    put(p, APP_MEOW_G_FRUIT, 1);
    put(p, APP_MEOW_G_WATER, 2);
    put(p, APP_MEOW_G_SOAP, 1);
    put(p, APP_MEOW_G_TRASH, 1);
    put(p, APP_MEOW_G_TISSUE, 1);
    put(p, APP_MEOW_G_STOMACH, 1);
    put(p, APP_MEOW_G_COLD, 1);
}

static void migrate_v3(app_meow_t *p, const uint8_t old[5])
{
    int any = 0;
    for (int i = 0; i < 5; i++) {
        if (old[i]) any = 1;
    }
    if (!any) return;
    put(p, APP_MEOW_G_BURGER, old[0]);
    put(p, APP_MEOW_G_WATER, old[1]);
    put(p, APP_MEOW_G_SOAP, old[2]);
    put(p, APP_MEOW_G_TRASH, old[3]);
    put(p, APP_MEOW_G_COLD, old[4]);
}

static void migrate_v4(app_meow_t *p, const uint8_t *old_n, const uint8_t *old_d)
{
    uint16_t n[APP_MEOW_G_N] = { 0 };
    uint8_t d[APP_MEOW_G_N] = { 0 };

    memset(p->inv_n, 0, sizeof(p->inv_n));
    memset(p->inv_d, 0, sizeof(p->inv_d));
    for (int i = 0; i < APP_MEOW_VER4_N; i++) {
        int dst = V4_MAP[i];
        if (dst < 0 || dst >= APP_MEOW_G_N || !old_n[i]) continue;
        int add = (int)old_n[i];
        if ((int)n[dst] + add > APP_MEOW_INV_MAX) add = APP_MEOW_INV_MAX - (int)n[dst];
        if (add <= 0) continue;
        if (n[dst] == 0) d[dst] = old_d[i];
        n[dst] = (uint16_t)(n[dst] + add);
    }
    for (int i = 0; i < APP_MEOW_G_N; i++) {
        if (!n[i]) continue;
        p->inv_n[i] = n[i];
        p->inv_d[i] = d[i] ? d[i] : CAT[i].dur;
    }
}

static uint8_t rnd(app_meow_t *p)
{
    uint8_t x = p->rng ? p->rng : 0xA5;
    x ^= (uint8_t)(x << 3);
    x ^= (uint8_t)(x >> 5);
    x ^= (uint8_t)(x << 1);
    p->rng = x ? x : 0x5A;
    return p->rng;
}

int app_meow_care_score(const app_meow_t *p)
{
    if (!p) return 0;
    int s = (int)p->care_good - (int)p->care_miss;
    s += (int)p->diet_good / 2;
    s -= (int)p->diet_junk / 3;
    if (p->weight > 35) s -= (int)(p->weight - 35) / 8;
    if (p->dirt >= 75) s--;
    return s;
}

int app_meow_good_tier(int good)
{
    const spec_t *s = spec(good);
    return s ? (int)s->tier : -1;
}

int app_meow_level(const app_meow_t *p)
{
    if (!p) return 0;
    if (p->stage == APP_MEOW_EGG || p->stage == APP_MEOW_DEAD) return 0;
    return p->level ? (int)p->level : 1;
}

int app_meow_xp(const app_meow_t *p)
{
    return p ? (int)p->xp : 0;
}

int app_meow_xp_need(int level)
{
    int n;

    if (level < 1) level = 1;
    if (level > (int)APP_MEOW_LV_MAX) level = (int)APP_MEOW_LV_MAX;
    n = 20 + (level - 1) * 8;
    if (n > 200) n = 200;
    return n;
}

int app_meow_xp_pct(const app_meow_t *p)
{
    int need;

    if (!p || p->stage == APP_MEOW_EGG) {
        int hatch = p ? (int)p->hatch_min * 100 / (int)APP_MEOW_HATCH_SEC : 0;
        if (hatch > 100) hatch = 100;
        if (hatch < 0) hatch = 0;
        return hatch;
    }
    if (p->stage == APP_MEOW_DEAD) return 0;
    need = app_meow_xp_need(app_meow_level(p));
    if (need <= 0) return 0;
    return (int)p->xp * 100 / need;
}

int app_meow_clean(const app_meow_t *p)
{
    int v;

    if (!p) return 0;
    v = (int)APP_MEOW_STAT_MAX - (int)p->dirt - (int)p->poop * 15;
    if (v < 0) v = 0;
    if (v > (int)APP_MEOW_STAT_MAX) v = (int)APP_MEOW_STAT_MAX;
    return v;
}

static int live_ix(const app_meow_t *p)
{
    if (p->stage == APP_MEOW_CHILD) return 1;
    if (p->stage == APP_MEOW_TEEN) return 2;
    if (p->stage == APP_MEOW_ADULT) return 3;
    return 0;
}

static int stage_lv_cap(const app_meow_t *p)
{
    if (p->stage == APP_MEOW_BABY) return 5;
    if (p->stage == APP_MEOW_CHILD) return 12;
    if (p->stage == APP_MEOW_TEEN) return 18;
    return (int)APP_MEOW_LV_MAX;
}

int app_meow_hunger_every(const app_meow_t *p)
{
    static const uint8_t ev[4] = { 18, 16, 14, 12 };
    int e;
    int lv;

    if (!p) return (int)APP_MEOW_HUNGER_EVERY;
    e = (int)ev[live_ix(p)];
    lv = app_meow_level(p);
    e -= lv / 8;
    if (e < 8) e = 8;
    return e;
}

int app_meow_dirt_every(const app_meow_t *p)
{
    static const uint8_t ev[4] = { 22, 20, 18, 16 };
    int e;
    int lv;

    if (!p) return (int)APP_MEOW_DIRT_EVERY;
    e = (int)ev[live_ix(p)];
    lv = app_meow_level(p);
    e -= lv / 8;
    if (e < 10) e = 10;
    return e;
}

static int hunger_drop(const app_meow_t *p)
{
    static const uint8_t d[4] = { 10, 12, 14, 16 };
    int n = (int)d[live_ix(p)] + app_meow_level(p) / 10;
    if (n > 24) n = 24;
    return n;
}

static int dirt_drop(const app_meow_t *p)
{
    static const uint8_t d[4] = { 8, 10, 12, 14 };
    int n = (int)d[live_ix(p)] + app_meow_level(p) / 10;
    if (n > 22) n = 22;
    return n;
}

static void fill_stage(app_meow_t *p)
{
    static const uint8_t hun[4] = { 80, 90, 95, 100 };
    static const uint8_t hap[4] = { 70, 80, 85, 90 };
    static const uint8_t hp[4] = { 90, 95, 100, 100 };
    static const uint8_t drt[4] = { 10, 15, 15, 10 };
    int i = live_ix(p);

    p->hunger = hun[i];
    p->happy = hap[i];
    p->health = hp[i];
    p->dirt = drt[i];
    p->poop = 0;
    p->sick = 0;
    p->ailment = 0;
    p->hunger_acc = 0;
    p->dirt_acc = 0;
    p->happy_acc = 0;
    if (p->level < 1) p->level = 1;
}

static void add_xp(app_meow_t *p, int n)
{
    int cap;

    if (!p || n <= 0) return;
    if (p->stage < APP_MEOW_BABY || p->stage > APP_MEOW_ADULT) return;
    if (p->level < 1) p->level = 1;
    cap = stage_lv_cap(p);
    while (n > 0 && (int)p->level < cap) {
        int need = app_meow_xp_need((int)p->level);
        int room = need - (int)p->xp;
        if (n < room) {
            p->xp = (uint16_t)((int)p->xp + n);
            return;
        }
        n -= room;
        p->xp = 0;
        p->level++;
        bump(&p->health, 4, 0, APP_MEOW_STAT_MAX);
        bump(&p->happy, 4, 0, APP_MEOW_STAT_MAX);
    }
    if ((int)p->level >= cap) p->xp = 0;
}

static void stage_gift(app_meow_t *p)
{
    if (p->stage == APP_MEOW_CHILD) {
        app_meow_give(p, APP_MEOW_G_FRUIT, 1);
        app_meow_give(p, APP_MEOW_G_WATER, 1);
    } else if (p->stage == APP_MEOW_TEEN) {
        app_meow_give(p, APP_MEOW_G_VITAMIN, 1);
        app_meow_give(p, APP_MEOW_G_SOAP, 1);
    } else if (p->stage == APP_MEOW_ADULT) {
        app_meow_give(p, APP_MEOW_G_VITAMIN, 1);
        app_meow_give(p, APP_MEOW_G_TOWEL, 1);
    }
}

static int can_evolve(const app_meow_t *p, uint8_t next)
{
    if (next == APP_MEOW_CHILD) {
        return p->age_min >= APP_MEOW_BABY_MIN && p->level >= APP_MEOW_BABY_LV;
    }
    if (next == APP_MEOW_TEEN) {
        return p->age_min >= APP_MEOW_CHILD_MIN && p->level >= APP_MEOW_CHILD_LV;
    }
    if (next == APP_MEOW_ADULT) {
        return p->age_min >= APP_MEOW_TEEN_MIN && p->level >= APP_MEOW_TEEN_LV;
    }
    return 0;
}

static void mood_regulate(app_meow_t *p)
{
    int tgt;
    int h;
    int d;

    tgt = ((int)p->hunger + app_meow_clean(p) + (int)p->health) / 3;
    if (p->sick) tgt -= 15;
    if (p->sleeping && !p->lights_off) tgt -= 10;
    if (tgt < 0) tgt = 0;
    if (tgt > (int)APP_MEOW_STAT_MAX) tgt = (int)APP_MEOW_STAT_MAX;
    h = (int)p->happy;
    d = tgt - h;
    if (d > 2) d = 2;
    if (d < -3) d = -3;
    bump(&p->happy, d, 0, APP_MEOW_STAT_MAX);
}

static int health_risk(const app_meow_t *p)
{
    int r = 2;

    if (p->hunger < 30) r += 6;
    if (p->hunger < 10) r += 8;
    if (p->dirt > 70) r += 6;
    if (p->dirt >= APP_MEOW_DIRT_MAX) r += 8;
    if (p->poop >= APP_MEOW_POOP_MAX) r += 18;
    if (p->happy < 30) r += 5;
    if (p->stage == APP_MEOW_BABY) r += 2;
    if (p->stage == APP_MEOW_ADULT && p->level >= 20) r += 2;
    if (r > 48) r = 48;
    return r;
}

static uint8_t pick_ailment(app_meow_t *p)
{
    if (p->poop >= APP_MEOW_POOP_MAX) return APP_MEOW_AI_WORM;
    if (p->hunger < 15) return APP_MEOW_AI_TUMMY;
    if (p->dirt > 70) return (rnd(p) & 1) ? APP_MEOW_AI_COLD : APP_MEOW_AI_COUGH;
    return (rnd(p) & 1) ? APP_MEOW_AI_COLD : APP_MEOW_AI_COUGH;
}

static void health_roll(app_meow_t *p)
{
    int ix = live_ix(p);

    if (p->sick) {
        if ((rnd(p) % 100u) < (uint8_t)(8 + ix)) {
            bump(&p->health, -(6 + ix * 2), 0, APP_MEOW_STAT_MAX);
        }
        return;
    }
    if ((int)(rnd(p) % 100u) < health_risk(p)) {
        mark_sick(p, pick_ailment(p));
        bump(&p->health, -(4 + ix), 0, APP_MEOW_STAT_MAX);
    }
}

static void scale_legacy_stats(app_meow_t *p)
{
    if (p->hunger <= 4) p->hunger = (uint8_t)(p->hunger * 25);
    if (p->happy <= 4) p->happy = (uint8_t)(p->happy * 25);
    if (p->health <= 4) p->health = (uint8_t)(p->health * 25);
    if (p->dirt <= 4) p->dirt = (uint8_t)(p->dirt * 25);
    if (p->level == 0 && p->stage >= APP_MEOW_BABY && p->stage <= APP_MEOW_ADULT) {
        int lv = 1 + (int)p->age_min / 30;
        int cap = stage_lv_cap(p);
        if (lv > cap) lv = cap;
        if (lv < 1) lv = 1;
        p->level = (uint8_t)lv;
        p->xp = 0;
    }
}

static uint8_t pick_teen(app_meow_t *p)
{
    uint8_t r = rnd(p);
    if ((r % 5) == 0) return (r & 1) ? APP_MEOW_SP_TEEN_A : APP_MEOW_SP_TEEN_B;
    return app_meow_care_score(p) >= 0 ? APP_MEOW_SP_TEEN_A : APP_MEOW_SP_TEEN_B;
}

static uint8_t pick_adult(app_meow_t *p)
{
    int s = app_meow_care_score(p);
    uint8_t r = rnd(p);
    int slot = 2;
    if (s >= 6) slot = 0;
    else if (s >= 2) slot = 1;
    else if (s >= -1) slot = 2;
    else if (s >= -4) slot = 3;
    else slot = 4;
    if (p->diet_good > (uint8_t)(p->diet_junk + 3) && slot > 0) slot--;
    if (p->diet_junk > (uint8_t)(p->diet_good + 4) && slot < 5) slot++;
    if ((r % 5) == 0) slot = (int)(rnd(p) % 6);
    else if ((r & 3) == 0) {
        if (r & 4) {
            if (slot < 5) slot++;
        } else if (slot > 0) {
            slot--;
        }
    }
    return (uint8_t)(APP_MEOW_SP_ADULT_0 + slot);
}

static void evolve(app_meow_t *p)
{
    if (p->stage == APP_MEOW_DEAD || p->stage == APP_MEOW_EGG) return;
    if (p->stage == APP_MEOW_BABY && can_evolve(p, APP_MEOW_CHILD)) {
        p->stage = APP_MEOW_CHILD;
        p->species = APP_MEOW_SP_CHILD;
        fill_stage(p);
        stage_gift(p);
        add_xp(p, 8);
        return;
    }
    if (p->stage == APP_MEOW_CHILD && can_evolve(p, APP_MEOW_TEEN)) {
        p->stage = APP_MEOW_TEEN;
        p->species = pick_teen(p);
        fill_stage(p);
        stage_gift(p);
        add_xp(p, 12);
        return;
    }
    if (p->stage == APP_MEOW_TEEN && can_evolve(p, APP_MEOW_ADULT)) {
        p->stage = APP_MEOW_ADULT;
        p->species = pick_adult(p);
        p->form = (app_meow_care_score(p) < 0) ? 1 : 0;
        fill_stage(p);
        stage_gift(p);
        add_xp(p, 16);
    }
}

static void mark_sick(app_meow_t *p, uint8_t ai)
{
    p->sick = 1;
    if (!p->ailment) p->ailment = ai;
}

static void hatch_now(app_meow_t *p)
{
    p->stage = APP_MEOW_BABY;
    p->species = APP_MEOW_SP_BABY;
    p->level = 1;
    p->xp = 0;
    p->age_min = 0;
    fill_stage(p);
    hatch_kit(p);
}

bool app_meow_asleep_at(int hour, int bed, int wake)
{
    return app_dnd_in_range(hour, bed, wake);
}

static void one_min(app_meow_t *p, int hour, int bed, int wake)
{
    if (p->stage == APP_MEOW_DEAD) return;

    if (p->trip_st == APP_MEOW_TRIP_AWAY) {
        if (p->age_min < 0xFFF0) p->age_min++;
        if (p->trip_left > 0) p->trip_left--;
        if (p->trip_left == 0) p->trip_st = APP_MEOW_TRIP_BACK;
        evolve(p);
        return;
    }

    if (hour >= 0) p->sleeping = app_meow_asleep_at(hour, bed, wake) ? 1 : 0;

    if (p->stage == APP_MEOW_EGG) {
        if (p->hatch_min >= APP_MEOW_HATCH_SEC) hatch_now(p);
        return;
    }

    if (p->age_min < 0xFFF0) p->age_min++;

    if (p->sleeping && !p->lights_off) {
        p->miss_light++;
        if (p->miss_light >= 12) {
            p->miss_light = 0;
            if (p->care_miss < 255) p->care_miss++;
            bump(&p->happy, -8, 0, APP_MEOW_STAT_MAX);
        }
    } else {
        p->miss_light = 0;
    }

    if (!p->sleeping) {
        p->hunger_acc++;
        if (p->hunger_acc >= (uint8_t)app_meow_hunger_every(p)) {
            p->hunger_acc = 0;
            if (p->hunger) {
                bump(&p->hunger, -hunger_drop(p), 0, APP_MEOW_STAT_MAX);
            } else {
                mark_sick(p, APP_MEOW_AI_TUMMY);
                if (p->care_miss < 255) p->care_miss++;
                bump(&p->health, -(10 + live_ix(p) * 2), 0, APP_MEOW_STAT_MAX);
            }
        }
        p->dirt_acc++;
        if (p->dirt_acc >= (uint8_t)app_meow_dirt_every(p)) {
            p->dirt_acc = 0;
            bump(&p->dirt, dirt_drop(p), 0, APP_MEOW_DIRT_MAX);
        }
        health_roll(p);
        if ((p->age_min % 8u) == 0) add_xp(p, 1);
    }
    mood_regulate(p);

    if (p->poop_in) {
        p->poop_in--;
        if (p->poop_in == 0 && p->poop < APP_MEOW_POOP_MAX) p->poop++;
    }
    if (p->poop >= APP_MEOW_POOP_MAX) mark_sick(p, APP_MEOW_AI_WORM);

    if (p->health == 0) {
        p->stage = APP_MEOW_DEAD;
        p->sleeping = 0;
        return;
    }
    evolve(p);
}

void app_meow_reset(app_meow_t *p, uint32_t now_sec, uint8_t rng)
{
    uint16_t found = 0;

    rot_reset();
    if (!p) return;
    if (p->magic == APP_MEOW_MAGIC) found = p->found;
    memset(p, 0, sizeof(*p));
    p->magic = APP_MEOW_MAGIC;
    p->ver = APP_MEOW_VER;
    p->last_sec = now_sec;
    p->stage = APP_MEOW_EGG;
    p->hunger = APP_MEOW_STAT_MAX;
    p->happy = APP_MEOW_STAT_MAX;
    p->health = APP_MEOW_STAT_MAX;
    p->weight = 5;
    p->rng = rng ? rng : 0xA5;
    p->found = found;
    p->level = 0;
    p->xp = 0;
}

void app_meow_wipe(app_meow_t *p, uint32_t now_sec, uint8_t rng)
{
    rot_reset();
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->magic = APP_MEOW_MAGIC;
    p->ver = APP_MEOW_VER;
    p->last_sec = now_sec;
    p->stage = APP_MEOW_DEAD;
    p->rng = rng ? rng : 0xA5;
}

bool app_meow_valid(const app_meow_t *p)
{
    if (!p || p->magic != APP_MEOW_MAGIC || p->ver != APP_MEOW_VER) return false;
    if (p->stage > APP_MEOW_DEAD) return false;
    if (p->hunger > APP_MEOW_STAT_MAX || p->happy > APP_MEOW_STAT_MAX) return false;
    if (p->health > APP_MEOW_STAT_MAX || p->poop > APP_MEOW_POOP_MAX) return false;
    if (p->dirt > APP_MEOW_DIRT_MAX || p->ailment > APP_MEOW_AI_COUGH) return false;
    if (p->level > APP_MEOW_LV_MAX) return false;
    if (p->form > 1 || p->species > APP_MEOW_SP_MAX) return false;
    if (p->trip_st > APP_MEOW_TRIP_BACK) return false;
    if (p->trip_pack > APP_MEOW_TRIP_PACK_MAX) return false;
    for (int i = 0; i < APP_MEOW_G_N; i++) {
        if (p->inv_n[i] > APP_MEOW_INV_MAX) return false;
    }
    return true;
}

uint16_t app_meow_inv(const app_meow_t *p, int good)
{
    if (!p || good < 0 || good >= APP_MEOW_G_N) return 0;
    return p->inv_n[good];
}

uint8_t app_meow_dur(const app_meow_t *p, int good)
{
    if (!p || good < 0 || good >= APP_MEOW_G_N) return 0;
    return p->inv_d[good];
}

int app_meow_give(app_meow_t *p, int good, int n)
{
    if (!app_meow_valid(p) || good < 0 || good >= APP_MEOW_G_N || n <= 0) {
        return 0;
    }
    int room = (int)APP_MEOW_INV_MAX - (int)p->inv_n[good];
    if (room <= 0) return 0;
    if (n > room) n = room;
    if (p->inv_n[good] == 0) p->inv_d[good] = CAT[good].dur;
    p->inv_n[good] = (uint16_t)(p->inv_n[good] + (uint16_t)n);
    return n;
}

int app_meow_good_cat(int good)
{
    const spec_t *s = spec(good);
    return s ? (int)s->cat : -1;
}

int app_meow_good_use(int good)
{
    const spec_t *s = spec(good);
    return s ? (int)s->use : -1;
}

int app_meow_good_dur_max(int good)
{
    const spec_t *s = spec(good);
    return s ? (int)s->dur : 0;
}

int app_meow_good_gain(int good)
{
    const spec_t *s = spec(good);
    int g;

    if (!s) return 0;
    g = (int)s->hunger + (int)s->happy + (int)s->health - (int)s->dirt;
    if (g < 0) g = 0;
    return g;
}

const char *app_meow_name(const app_meow_t *p)
{
    return (p && p->name[0]) ? p->name : "";
}

static int utf8_w(unsigned char c)
{
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

size_t app_meow_name_copy(char *dst, size_t dst_n, const char *src)
{
    const char *s = src ? src : "";
    size_t n, o = 0;
    int chars = 0;

    if (!dst || dst_n == 0) return 0;
    while (*s == ' ' || *s == '\t') s++;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (dst_n > APP_MEOW_NAME_MAX + 1) dst_n = APP_MEOW_NAME_MAX + 1;
    for (size_t i = 0; i < n && o + 1 < dst_n && chars < APP_MEOW_NAME_CHARS; ) {
        int w = utf8_w((unsigned char)s[i]);
        if (i + (size_t)w > n) break;
        if (o + (size_t)w >= dst_n) break;
        if (o + (size_t)w > APP_MEOW_NAME_MAX) break;
        memcpy(dst + o, s + i, (size_t)w);
        o += (size_t)w;
        i += (size_t)w;
        chars++;
    }
    dst[o] = 0;
    return o;
}

void app_meow_set_name(app_meow_t *p, const char *name)
{
    if (!p) return;
    app_meow_name_copy(p->name, sizeof(p->name), name);
}

int app_meow_owned_n(const app_meow_t *p, int cat)
{
    if (!p || cat < 0 || cat >= APP_MEOW_CAT_N) return 0;
    int n = 0;
    for (int i = 0; i < APP_MEOW_G_N; i++) {
        if (CAT[i].cat == (uint8_t)cat && p->inv_n[i]) n++;
    }
    return n;
}

int app_meow_owned_list(const app_meow_t *p, int cat, uint8_t *out, int max)
{
    if (!p || !out || max <= 0 || cat < 0 || cat >= APP_MEOW_CAT_N) return 0;
    int n = 0;
    for (int i = 0; i < APP_MEOW_G_N && n < max; i++) {
        if (CAT[i].cat == (uint8_t)cat && p->inv_n[i]) {
            out[n++] = (uint8_t)i;
        }
    }
    return n;
}

int app_meow_last_got(void)
{
    return s_last_got;
}

void app_meow_last_prizes(uint8_t out[APP_MEOW_G_N])
{
    if (!out) return;
    memcpy(out, s_last_prize, APP_MEOW_G_N);
}

int app_meow_play_tier(int score)
{
    int n;

    if (score < 0) score = 0;
    n = score / 100;
    if (n > 5) n = 5;
    return n;
}

int app_meow_play_swim_pct(int score)
{
    int t = app_meow_play_tier(score);

    if (t < 3) return 0;
    return 30 * (t - 2);
}

int app_meow_play_koi_pct(int score)
{
    return app_meow_play_swim_pct(score);
}

static int wear(app_meow_t *p, int id)
{
    if (id < 0 || id >= APP_MEOW_G_N || p->inv_n[id] == 0) return 0;
    if (p->inv_d[id] > 1) {
        p->inv_d[id]--;
        return 1;
    }
    p->inv_n[id]--;
    p->inv_d[id] = p->inv_n[id] ? CAT[id].dur : 0;
    return 1;
}

static int loot_w(int id, app_meow_loot_t src, int level)
{
    int w = (int)CAT[id].loot_w;
    int tier;
    int mul;
    if (w <= 0) return 0;
    if (level < 1) level = 1;
    tier = (int)CAT[id].tier;
    if (tier <= 0) mul = 24 - (level > 16 ? 16 : level);
    else if (tier == 1) mul = 6 + level;
    else if (tier == 2) mul = level > 1 ? level - 1 : 1;
    else mul = level > 6 ? level - 6 : 0;
    w = w * mul;
    if (w <= 0) return 0;
    uint8_t cat = CAT[id].cat;
    uint8_t use = CAT[id].use;
    if (src == APP_MEOW_LOOT_WALK) {
        if (cat == APP_MEOW_CAT_FOOD) w *= 2;
        if (CAT[id].flags & APP_MEOW_GF_SHOVEL) w = (w + 1) / 2;
    } else if (src == APP_MEOW_LOOT_VISIT) {
        if (cat == APP_MEOW_CAT_GEAR) w += 2;
    } else if (src == APP_MEOW_LOOT_WIN) {
        if (cat != APP_MEOW_CAT_FOOD) w *= 2;
    } else if (src == APP_MEOW_LOOT_PLAY) {
        if (use == APP_MEOW_USE_MEAL || use == APP_MEOW_USE_DRINK) w *= 2;
        else w = (w + 1) / 2;
    } else if (src == APP_MEOW_LOOT_TRIP) {
        if (cat == APP_MEOW_CAT_GEAR) w += 3;
        if (cat == APP_MEOW_CAT_MED) w += 1;
    }
    return w;
}

static int roll_loot(app_meow_t *p, app_meow_loot_t src)
{
    int lv = app_meow_level(p);
    int sum = 0;
    for (int i = 0; i < APP_MEOW_G_N; i++) {
        if (p->inv_n[i] >= APP_MEOW_INV_MAX) continue;
        sum += loot_w(i, src, lv);
    }
    if (sum <= 0) return -1;
    int roll = (int)rnd(p) % sum;
    for (int i = 0; i < APP_MEOW_G_N; i++) {
        if (p->inv_n[i] >= APP_MEOW_INV_MAX) continue;
        int w = loot_w(i, src, lv);
        if (roll < w) return i;
        roll -= w;
    }
    return -1;
}

int app_meow_loot(app_meow_t *p, app_meow_loot_t src)
{
    s_last_got = -1;
    if (!app_meow_valid(p)) return -1;
    if (p->stage < APP_MEOW_BABY || p->stage > APP_MEOW_ADULT) return -1;
    if (src == APP_MEOW_LOOT_PLAY && (rnd(p) % 4u) != 0) return -1;
    if (src == APP_MEOW_LOOT_DRAW && (rnd(p) & 1u)) return -1;
    int it = roll_loot(p, src);
    if (it < 0) return -1;
    if (app_meow_give(p, it, 1) <= 0) return -1;
    s_last_got = it;
    if (src == APP_MEOW_LOOT_WALK && p->inv_n[APP_MEOW_G_SHOVEL]) {
        wear(p, APP_MEOW_G_SHOVEL);
        int extra = roll_loot(p, src);
        if (extra >= 0) app_meow_give(p, extra, 1);
    }
    return it;
}

int app_meow_play_prize(app_meow_t *p, int score)
{
    int n;
    int got = 0;
    int last = -1;

    s_last_got = -1;
    memset(s_last_prize, 0, sizeof(s_last_prize));
    if (!app_meow_valid(p) || score <= 0) return 0;
    if (p->stage < APP_MEOW_BABY || p->stage > APP_MEOW_ADULT) return 0;
    n = score / 100;
    for (int i = 0; i < n; i++) {
        int it = roll_loot(p, APP_MEOW_LOOT_PLAY);
        if (it < 0) break;
        if (app_meow_give(p, it, 1) <= 0) break;
        last = it;
        s_last_prize[it]++;
        got++;
    }
    s_last_got = last;
    return got;
}

int app_meow_roll(app_meow_t *p, app_meow_loot_t src)
{
    if (!app_meow_valid(p)) return -1;
    return roll_loot(p, src);
}

int app_meow_run_prize(app_meow_t *p, const uint8_t got[APP_MEOW_G_N])
{
    int n = 0;
    int last = -1;

    s_last_got = -1;
    memset(s_last_prize, 0, sizeof(s_last_prize));
    if (!app_meow_valid(p) || !got) return 0;
    if (p->stage < APP_MEOW_BABY || p->stage > APP_MEOW_ADULT) return 0;
    for (int i = 0; i < APP_MEOW_G_N; i++) {
        int g;

        if (!got[i]) continue;
        g = app_meow_give(p, i, (int)got[i]);
        s_last_prize[i] = got[i];
        if (g > 0) {
            last = i;
            n += g;
        }
    }
    s_last_got = last;
    return n;
}

static int first_owned(const app_meow_t *p, uint8_t use)
{
    for (int i = 0; i < APP_MEOW_G_N; i++) {
        if (CAT[i].use == use && p->inv_n[i]) return i;
    }
    return -1;
}

static bool act_uses(app_meow_act_t act, uint8_t use)
{
    if (act == APP_MEOW_FEED) {
        return use == APP_MEOW_USE_MEAL || use == APP_MEOW_USE_DRINK;
    }
    if (act == APP_MEOW_DRINK) return use == APP_MEOW_USE_DRINK;
    if (act == APP_MEOW_BATH) {
        return use == APP_MEOW_USE_WASH || use == APP_MEOW_USE_SWEEP;
    }
    if (act == APP_MEOW_CLEAN) return use == APP_MEOW_USE_SWEEP;
    if (act == APP_MEOW_HEAL) return use == APP_MEOW_USE_MED;
    return false;
}

static bool good_needed(const app_meow_t *p, int good)
{
    const spec_t *s = spec(good);

    if (!s || !p->inv_n[good]) return false;
    if (s->use == APP_MEOW_USE_MEAL || s->use == APP_MEOW_USE_DRINK) {
        return p->hunger < APP_MEOW_STAT_MAX;
    }
    if (s->use == APP_MEOW_USE_MED) {
        return p->sick || p->health < APP_MEOW_STAT_MAX || s->happy > 0;
    }
    if (s->use == APP_MEOW_USE_WASH) {
        return p->poop || p->happy < APP_MEOW_STAT_MAX || p->dirt > 0;
    }
    if (s->use == APP_MEOW_USE_SWEEP) {
        if (good == APP_MEOW_G_TRASH) return p->poop != 0;
        return p->poop || p->dirt > 0;
    }
    return true;
}

/* 目录顺序里,after 之后下一份能用的;都用不成则退回第一份已有。 */
static int next_owned(const app_meow_t *p, app_meow_act_t act, int after)
{
    int first_any = -1;
    int first_need = -1;

    for (int i = 0; i < APP_MEOW_G_N; i++) {
        if (!p->inv_n[i] || !act_uses(act, CAT[i].use)) continue;
        if (first_any < 0) first_any = i;
        if (!good_needed(p, i)) continue;
        if (first_need < 0) first_need = i;
        if (i > after) return i;
    }
    if (first_need >= 0) return first_need;
    return first_any;
}

int app_meow_pick(const app_meow_t *p, app_meow_act_t act)
{
    int *rot;

    if (!p) return -1;
    rot = rot_slot(act);
    if (rot) return next_owned(p, act, *rot);
    if (act == APP_MEOW_DRINK) return first_owned(p, APP_MEOW_USE_DRINK);
    if (act == APP_MEOW_CLEAN) {
        if (p->inv_n[APP_MEOW_G_TRASH]) return APP_MEOW_G_TRASH;
        return first_owned(p, APP_MEOW_USE_SWEEP);
    }
    return -1;
}

static void note_diet(app_meow_t *p, uint8_t flags)
{
    if (flags & APP_MEOW_GF_HEALTHY) {
        if (p->diet_good < 255) p->diet_good++;
    }
    if (flags & APP_MEOW_GF_JUNK) {
        if (p->diet_junk < 255) p->diet_junk++;
    }
}

static void apply_stat(app_meow_t *p, const spec_t *s)
{
    bump(&p->hunger, s->hunger, 0, APP_MEOW_STAT_MAX);
    bump(&p->happy, s->happy, 0, APP_MEOW_STAT_MAX);
    bump(&p->health, s->health, 0, APP_MEOW_STAT_MAX);
    bump(&p->weight, s->weight, 1, 99);
    bump(&p->dirt, s->dirt, 0, APP_MEOW_DIRT_MAX);
    if (s->poop) p->poop_in = APP_MEOW_POOP_DELAY;
    note_diet(p, s->flags);
    if (p->care_good < 255) p->care_good++;
    add_xp(p, (int)s->xp);
}

app_meow_res_t app_meow_use(app_meow_t *p, int good)
{
    s_last_got = -1;
    if (!app_meow_valid(p)) return APP_MEOW_NONE;
    if (p->stage == APP_MEOW_DEAD) return APP_MEOW_GONE;
    if (p->stage == APP_MEOW_EGG) return APP_MEOW_EGG_WAIT;
    if (p->trip_st == APP_MEOW_TRIP_AWAY) return APP_MEOW_NONE;
    if (app_meow_rest_lock(p)) return APP_MEOW_SLEEP;
    const spec_t *s = spec(good);
    if (!s || p->inv_n[good] == 0) return APP_MEOW_EMPTY;

    if (s->use == APP_MEOW_USE_MEAL || s->use == APP_MEOW_USE_DRINK) {
        if (p->hunger >= APP_MEOW_STAT_MAX) return APP_MEOW_FULL;
        wear(p, good);
        apply_stat(p, s);
        return APP_MEOW_OK;
    }
    if (s->use == APP_MEOW_USE_MED) {
        int need = p->sick || p->health < APP_MEOW_STAT_MAX || s->happy > 0;
        if (!need) return APP_MEOW_NONE;
        uint8_t was = p->ailment;
        wear(p, good);
        p->sick = 0;
        p->ailment = 0;
        apply_stat(p, s);
        if (s->ailment && s->ailment == was) {
            bump(&p->health, 12, 0, APP_MEOW_STAT_MAX);
        }
        return APP_MEOW_OK;
    }
    if (s->use == APP_MEOW_USE_WASH) {
        if (!p->poop && p->happy >= APP_MEOW_STAT_MAX && p->dirt == 0) {
            return APP_MEOW_NONE;
        }
        wear(p, good);
        p->poop = 0;
        apply_stat(p, s);
        return APP_MEOW_OK;
    }
    if (s->use == APP_MEOW_USE_SWEEP) {
        if (good == APP_MEOW_G_TRASH) {
            if (!p->poop) return APP_MEOW_NONE;
        } else if (!p->poop && p->dirt == 0) {
            return APP_MEOW_NONE;
        }
        wear(p, good);
        p->poop = 0;
        apply_stat(p, s);
        return APP_MEOW_OK;
    }
    if (s->use == APP_MEOW_USE_GROOM) {
        if (p->happy >= APP_MEOW_STAT_MAX && p->dirt == 0 &&
            p->health >= APP_MEOW_STAT_MAX) {
            return APP_MEOW_NONE;
        }
        wear(p, good);
        apply_stat(p, s);
        return APP_MEOW_OK;
    }
    if (s->use == APP_MEOW_USE_DIG) {
        wear(p, good);
        apply_stat(p, s);
        bump(&p->hunger, -8, 0, APP_MEOW_STAT_MAX);
        int it = roll_loot(p, APP_MEOW_LOOT_WALK);
        if (it >= 0 && app_meow_give(p, it, 1) > 0) s_last_got = it;
        return APP_MEOW_OK;
    }
    return APP_MEOW_NONE;
}

bool app_meow_import(app_meow_t *p, const void *raw, size_t n)
{
    if (!p || !raw || n == 0) return false;
    const uint8_t *b = (const uint8_t *)raw;
    memset(p, 0, sizeof(*p));
    size_t head = n < APP_MEOW_VER2_SIZE ? n : APP_MEOW_VER2_SIZE;
    memcpy(p, b, head);
    if (p->magic != APP_MEOW_MAGIC) return false;
    {
        uint8_t src_ver = p->ver;

    if (p->ver == APP_MEOW_VER) {
        if (n > APP_MEOW_VER2_SIZE) {
            size_t rest = n - APP_MEOW_VER2_SIZE;
            size_t room = sizeof(*p) - APP_MEOW_VER2_SIZE;
            if (rest > room) rest = room;
            memcpy((uint8_t *)p + APP_MEOW_VER2_SIZE, b + APP_MEOW_VER2_SIZE, rest);
        }
        p->ver = APP_MEOW_VER;
    } else if (p->ver == APP_MEOW_VER9 || p->ver == APP_MEOW_VER8 ||
               p->ver == APP_MEOW_VER7 || p->ver == APP_MEOW_VER6) {
        /* v6–v9 的 name 只有 12 字节,后面紧跟 trip_gain。加长名字后不能整段 memcpy。 */
        size_t name_off = offsetof(app_meow_t, name);
        size_t old_gain_off = name_off + APP_MEOW_NAME_MAX_V9 + 1;
        if (n > APP_MEOW_VER2_SIZE && name_off > APP_MEOW_VER2_SIZE) {
            size_t mid = name_off - APP_MEOW_VER2_SIZE;
            if (n - APP_MEOW_VER2_SIZE < mid) mid = n - APP_MEOW_VER2_SIZE;
            memcpy((uint8_t *)p + APP_MEOW_VER2_SIZE, b + APP_MEOW_VER2_SIZE, mid);
        }
        memset(p->name, 0, sizeof(p->name));
        if (n > name_off) {
            size_t have = n - name_off;
            if (have > APP_MEOW_NAME_MAX_V9) have = APP_MEOW_NAME_MAX_V9;
            memcpy(p->name, b + name_off, have);
            p->name[APP_MEOW_NAME_MAX] = 0;
        }
        p->trip_gain = 0;
        if (n >= old_gain_off + sizeof(p->trip_gain)) {
            memcpy(&p->trip_gain, b + old_gain_off, sizeof(p->trip_gain));
        }
        p->ver = APP_MEOW_VER;
    } else if (p->ver == 5) {
        /* v5: 8 位数量,背包从 VER3 头开始。 */
        if (n > APP_MEOW_VER2_SIZE) {
            size_t mid = APP_MEOW_VER3_SIZE - APP_MEOW_VER2_SIZE;
            if (n - APP_MEOW_VER2_SIZE < mid) mid = n - APP_MEOW_VER2_SIZE;
            memcpy((uint8_t *)p + APP_MEOW_VER2_SIZE, b + APP_MEOW_VER2_SIZE, mid);
        }
        if (n > APP_MEOW_VER3_SIZE) {
            size_t have = n - APP_MEOW_VER3_SIZE;
            size_t nn = have < APP_MEOW_G_N ? have : (size_t)APP_MEOW_G_N;
            for (size_t i = 0; i < nn; i++) {
                p->inv_n[i] = b[APP_MEOW_VER3_SIZE + i];
            }
            if (have > APP_MEOW_G_N) {
                size_t dn = have - APP_MEOW_G_N;
                if (dn > APP_MEOW_G_N) dn = APP_MEOW_G_N;
                memcpy(p->inv_d, b + APP_MEOW_VER3_SIZE + APP_MEOW_G_N, dn);
            }
        }
        p->ver = APP_MEOW_VER;
    } else if (p->ver == 4) {
        size_t inv_off = APP_MEOW_VER3_SIZE;
        if (n > APP_MEOW_VER2_SIZE && inv_off > APP_MEOW_VER2_SIZE) {
            size_t mid = inv_off - APP_MEOW_VER2_SIZE;
            if (n - APP_MEOW_VER2_SIZE < mid) mid = n - APP_MEOW_VER2_SIZE;
            memcpy((uint8_t *)p + APP_MEOW_VER2_SIZE, b + APP_MEOW_VER2_SIZE, mid);
        }
        uint8_t old_n[APP_MEOW_VER4_N] = { 0 };
        uint8_t old_d[APP_MEOW_VER4_N] = { 0 };
        if (n > inv_off) {
            size_t have = n - inv_off;
            size_t nn = have < APP_MEOW_VER4_N ? have : (size_t)APP_MEOW_VER4_N;
            memcpy(old_n, b + inv_off, nn);
            if (have > APP_MEOW_VER4_N) {
                size_t dn = have - APP_MEOW_VER4_N;
                if (dn > APP_MEOW_VER4_N) dn = APP_MEOW_VER4_N;
                memcpy(old_d, b + inv_off + APP_MEOW_VER4_N, dn);
            }
        }
        p->ver = APP_MEOW_VER;
        migrate_v4(p, old_n, old_d);
    } else if (p->ver == 3) {
        uint8_t old[5] = { 0 };
        if (n > APP_MEOW_VER2_SIZE) {
            size_t k = n - APP_MEOW_VER2_SIZE;
            if (k > 5) k = 5;
            memcpy(old, b + APP_MEOW_VER2_SIZE, k);
        }
        p->ver = APP_MEOW_VER;
        migrate_v3(p, old);
    } else if (p->ver == 2) {
        p->ver = APP_MEOW_VER;
        if (p->stage != APP_MEOW_EGG && p->stage != APP_MEOW_DEAD) {
            hatch_kit(p);
        }
    } else {
        return false;
    }
    if (src_ver && src_ver < APP_MEOW_VER6) scale_legacy_stats(p);
    }
    clamp(p);
    rot_reset();
    return app_meow_valid(p);
}

static bool wall_clock(uint32_t sec)
{
    return sec >= APP_MEOW_WALL_SEC;
}

void app_meow_advance_night(app_meow_t *p, uint32_t now_sec, int hour,
                            int bed, int wake)
{
    if (!app_meow_valid(p)) return;
    /* 开机秒和墙上时钟不能混算,否则一次补满 8 小时会把宠物判离开。 */
    if (now_sec < p->last_sec ||
        wall_clock(now_sec) != wall_clock(p->last_sec)) {
        p->last_sec = now_sec;
        return;
    }
    uint32_t dt = now_sec - p->last_sec;
    if (dt > APP_MEOW_MAX_CATCHUP_SEC) dt = APP_MEOW_MAX_CATCHUP_SEC;

    if (p->stage == APP_MEOW_EGG) {
        uint32_t need = (p->hatch_min < APP_MEOW_HATCH_SEC)
            ? (APP_MEOW_HATCH_SEC - (uint32_t)p->hatch_min) : 0;
        uint32_t used = dt < need ? dt : need;
        if (p->hatch_min < 255) {
            uint32_t next = (uint32_t)p->hatch_min + used;
            p->hatch_min = next > 255 ? 255 : (uint8_t)next;
        }
        p->last_sec += used;
        dt -= used;
        if (p->hatch_min >= APP_MEOW_HATCH_SEC) hatch_now(p);
        if (p->stage == APP_MEOW_EGG) {
            clamp(p);
            return;
        }
    }

    uint32_t n = dt / APP_MEOW_SEC_PER_MIN;
    for (uint32_t i = 0; i < n; i++) one_min(p, hour, bed, wake);
    p->last_sec += n * APP_MEOW_SEC_PER_MIN;
    clamp(p);
}

void app_meow_advance(app_meow_t *p, uint32_t now_sec, int hour)
{
    app_meow_advance_night(p, now_sec, hour,
                           APP_MEOW_BED_HOUR, APP_MEOW_WAKE_HOUR);
}

bool app_meow_can(const app_meow_t *p, app_meow_act_t act)
{
    if (!app_meow_valid(p)) return false;
    if (p->trip_st == APP_MEOW_TRIP_AWAY) {
        return act == APP_MEOW_RESET || act == APP_MEOW_PLAY;
    }
    if (act == APP_MEOW_RESET || act == APP_MEOW_LIGHT) return true;
    if (p->stage == APP_MEOW_DEAD || p->stage == APP_MEOW_EGG) return false;
    if (act == APP_MEOW_BED || act == APP_MEOW_PLAY) return true;
    if (app_meow_rest_lock(p)) return false;
    return true;
}

bool app_meow_rest_lock(const app_meow_t *p)
{
    return app_meow_valid(p) && p->sleeping && p->lights_off;
}

bool app_meow_bed_call(const app_meow_t *p)
{
    if (!app_meow_valid(p) || p->stage == APP_MEOW_EGG ||
        p->stage == APP_MEOW_DEAD || p->trip_st == APP_MEOW_TRIP_AWAY) {
        return false;
    }
    if (!p->sleeping) return false;
    if (!p->lights_off) return true;
    return app_meow_alert_peak(p) >= APP_MEOW_ALERT_WARN;
}

app_meow_res_t app_meow_act(app_meow_t *p, app_meow_act_t act)
{
    if (!app_meow_valid(p)) return APP_MEOW_NONE;
    if (act == APP_MEOW_RESET) {
        app_meow_reset(p, p->last_sec, (uint8_t)(p->rng + 1));
        return APP_MEOW_OK;
    }
    if (p->trip_st == APP_MEOW_TRIP_AWAY) return APP_MEOW_NONE;
    if (act == APP_MEOW_LIGHT) {
        p->lights_off = p->lights_off ? 0 : 1;
        return APP_MEOW_OK;
    }
    if (act == APP_MEOW_BED) {
        if (p->stage == APP_MEOW_DEAD) return APP_MEOW_GONE;
        if (p->stage == APP_MEOW_EGG) return APP_MEOW_EGG_WAIT;
        if (p->lights_off && p->sleeping) return APP_MEOW_NONE;
        p->lights_off = 1;
        p->sleeping = 1;
        if (p->care_good < 255) p->care_good++;
        add_xp(p, 2);
        return APP_MEOW_OK;
    }
    if (p->stage == APP_MEOW_DEAD) return APP_MEOW_GONE;
    if (p->stage == APP_MEOW_EGG) return APP_MEOW_EGG_WAIT;
    if (app_meow_rest_lock(p)) return APP_MEOW_SLEEP;

    if (act == APP_MEOW_FEED || act == APP_MEOW_DRINK || act == APP_MEOW_CLEAN ||
        act == APP_MEOW_BATH || act == APP_MEOW_HEAL) {
        int id = app_meow_pick(p, act);
        app_meow_res_t r;
        int *rot;

        if (id < 0) return APP_MEOW_EMPTY;
        r = app_meow_use(p, id);
        rot = rot_slot(act);
        if (r == APP_MEOW_OK && rot) *rot = id;
        return r;
    }
    if (act == APP_MEOW_PET) {
        if (p->happy >= APP_MEOW_STAT_MAX) return APP_MEOW_NONE;
        bump(&p->happy, 12, 0, APP_MEOW_STAT_MAX);
        if (p->care_good < 255) p->care_good++;
        add_xp(p, 2);
        return APP_MEOW_OK;
    }
    if (act == APP_MEOW_WALK) {
        if (p->happy >= APP_MEOW_STAT_MAX && p->hunger == 0) return APP_MEOW_NONE;
        bump(&p->happy, 10, 0, APP_MEOW_STAT_MAX);
        bump(&p->hunger, -8, 0, APP_MEOW_STAT_MAX);
        if (p->care_good < 255) p->care_good++;
        add_xp(p, 4);
        return APP_MEOW_OK;
    }
    return APP_MEOW_NONE;
}

int app_meow_play_apply(app_meow_t *p, int win)
{
    if (!app_meow_can(p, APP_MEOW_PLAY)) return -1;
    if (win) {
        bump(&p->happy, 14, 0, APP_MEOW_STAT_MAX);
        bump(&p->hunger, -6, 0, APP_MEOW_STAT_MAX);
        if (p->care_good < 255) p->care_good++;
        add_xp(p, 6);
        return 1;
    }
    bump(&p->happy, -8, 0, APP_MEOW_STAT_MAX);
    if (p->care_miss < 255) p->care_miss++;
    add_xp(p, 2);
    return 0;
}

int app_meow_play(app_meow_t *p, int guess)
{
    if (!app_meow_can(p, APP_MEOW_PLAY)) return -1;
    int face = rnd(p) & 1;
    return app_meow_play_apply(p, (guess != 0) == (face != 0));
}

bool app_meow_can_link(const app_meow_t *p)
{
    if (!app_meow_valid(p)) return false;
    if (p->stage == APP_MEOW_EGG || p->stage == APP_MEOW_DEAD) return false;
    if (p->trip_st == APP_MEOW_TRIP_AWAY) return false;
    if (p->sleeping) return false;
    return true;
}

void app_meow_snap(const app_meow_t *p, app_meow_snap_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!p) return;
    out->stage = p->stage;
    out->hunger = p->hunger;
    out->happy = p->happy;
    out->health = p->health;
    out->sick = p->sick;
    out->form = p->form;
    out->species = p->species;
    out->weight = p->weight;
    out->rng = p->rng;
    out->age_min = p->age_min;
}

int app_meow_power(const app_meow_snap_t *s)
{
    if (!s || s->stage < APP_MEOW_BABY || s->stage > APP_MEOW_ADULT) return 0;
    return (int)s->stage * 20 + (int)s->health / 5 + (int)s->happy / 8 +
           (int)s->hunger / 10 - (s->sick ? 15 : 0) - (s->form ? 8 : 0);
}

app_meow_res_t app_meow_visit(app_meow_t *p)
{
    if (!app_meow_can_link(p)) {
        if (!app_meow_valid(p) || p->stage == APP_MEOW_DEAD) return APP_MEOW_GONE;
        if (p->stage == APP_MEOW_EGG) return APP_MEOW_EGG_WAIT;
        if (p->sleeping) return APP_MEOW_SLEEP;
        return APP_MEOW_NONE;
    }
    bump(&p->happy, 10, 0, APP_MEOW_STAT_MAX);
    if (p->care_good < 255) p->care_good++;
    add_xp(p, 4);
    return APP_MEOW_OK;
}

int app_meow_fight(app_meow_t *me, const app_meow_snap_t *you)
{
    if (!app_meow_can_link(me) || !you) return -2;
    if (you->stage < APP_MEOW_BABY || you->stage > APP_MEOW_ADULT) return -2;
    app_meow_snap_t mine;
    app_meow_snap(me, &mine);
    int a = app_meow_power(&mine);
    int b = app_meow_power(you);
    int r = 0;
    if (a > b) r = 1;
    else if (a < b) r = -1;
    else if (mine.rng > you->rng) r = 1;
    else if (mine.rng < you->rng) r = -1;
    if (r > 0) {
        bump(&me->happy, 12, 0, APP_MEOW_STAT_MAX);
        if (me->care_good < 255) me->care_good++;
        add_xp(me, 8);
    } else if (r < 0) {
        bump(&me->happy, -10, 0, APP_MEOW_STAT_MAX);
        if (me->care_miss < 255) me->care_miss++;
        add_xp(me, 2);
    }
    return r;
}

static uint8_t lv_stat(uint8_t v)
{
    if (v > APP_MEOW_ALERT_PCT_WARN) return APP_MEOW_ALERT_OK;
    if (v > APP_MEOW_ALERT_PCT_HIT) return APP_MEOW_ALERT_WARN;
    if (v > APP_MEOW_ALERT_PCT_CRIT) return APP_MEOW_ALERT_HIT;
    return APP_MEOW_ALERT_CRIT;
}

uint8_t app_meow_danger_lv(const app_meow_t *p, int danger)
{
    if (!app_meow_valid(p) || p->stage == APP_MEOW_EGG ||
        p->stage == APP_MEOW_DEAD) {
        return APP_MEOW_ALERT_OK;
    }
    switch (danger) {
    case APP_MEOW_D_HUNGER:
        return lv_stat(p->hunger);
    case APP_MEOW_D_HAPPY:
        return lv_stat(p->happy);
    case APP_MEOW_D_HEALTH:
        return lv_stat(p->health);
    case APP_MEOW_D_SICK:
        if (!p->sick) return APP_MEOW_ALERT_OK;
        if (p->health <= APP_MEOW_ALERT_PCT_CRIT) return APP_MEOW_ALERT_CRIT;
        return APP_MEOW_ALERT_HIT;
    case APP_MEOW_D_POOP:
        if (p->poop >= APP_MEOW_POOP_MAX && p->sick) return APP_MEOW_ALERT_CRIT;
        if (p->poop >= APP_MEOW_POOP_MAX) return APP_MEOW_ALERT_HIT;
        if (p->poop) return APP_MEOW_ALERT_WARN;
        return APP_MEOW_ALERT_OK;
    case APP_MEOW_D_LIGHT:
        if (!p->sleeping || p->lights_off) return APP_MEOW_ALERT_OK;
        if (p->miss_light >= 11) return APP_MEOW_ALERT_CRIT;
        if (p->miss_light >= 8) return APP_MEOW_ALERT_HIT;
        if (p->miss_light >= 4) return APP_MEOW_ALERT_WARN;
        return APP_MEOW_ALERT_OK;
    default:
        return APP_MEOW_ALERT_OK;
    }
}

uint8_t app_meow_alert_peak(const app_meow_t *p)
{
    uint8_t peak = APP_MEOW_ALERT_OK;
    for (int d = 0; d < APP_MEOW_D_N; d++) {
        uint8_t lv = app_meow_danger_lv(p, d);
        if (lv > peak) peak = lv;
    }
    return peak;
}

static uint8_t ack_get(const app_meow_t *p, int d)
{
    return (uint8_t)((p->alert_ack >> (d * 2)) & 3u);
}

static void ack_set(app_meow_t *p, int d, uint8_t lv)
{
    unsigned sh = (unsigned)d * 2u;
    p->alert_ack &= (uint16_t)~(3u << sh);
    p->alert_ack |= (uint16_t)((lv & 3u) << sh);
}

int app_meow_alert_poll(app_meow_t *p, int *danger, int *lv)
{
    int fire_d = -1, fire_lv = 0;
    int best_d = -1, best_lv = 0;
    if (!app_meow_valid(p)) return 0;
    for (int d = 0; d < APP_MEOW_D_N; d++) {
        uint8_t cur = app_meow_danger_lv(p, d);
        uint8_t ack = ack_get(p, d);
        if (cur < ack) ack_set(p, d, cur);
        if (cur > ack && (int)cur > fire_lv) {
            fire_d = d;
            fire_lv = (int)cur;
        }
        if ((int)cur > best_lv) {
            best_d = d;
            best_lv = (int)cur;
        }
    }
    if (fire_d >= 0) {
        ack_set(p, fire_d, (uint8_t)fire_lv);
        if (danger) *danger = fire_d;
        if (lv) *lv = fire_lv;
        return 1;
    }
    if (danger) *danger = best_d;
    if (lv) *lv = best_lv;
    return 0;
}

static int pack_sum(const uint8_t take[APP_MEOW_G_N])
{
    int n = 0;

    if (!take) return 0;
    for (int i = 0; i < APP_MEOW_G_N; i++) {
        if (CAT[i].cat != APP_MEOW_CAT_FOOD) continue;
        n += (int)take[i];
    }
    return n;
}

bool app_meow_trip_can(const app_meow_t *p)
{
    if (!app_meow_valid(p)) return false;
    if (p->stage < APP_MEOW_BABY || p->stage > APP_MEOW_ADULT) return false;
    if (p->trip_st != APP_MEOW_TRIP_IDLE) return false;
    if (p->sleeping) return false;
    return app_meow_owned_n(p, APP_MEOW_CAT_FOOD) > 0;
}

int app_meow_trip_take_gain(const uint8_t take[APP_MEOW_G_N])
{
    int g = 0;
    int left = (int)APP_MEOW_TRIP_PACK_MAX;

    if (!take) return 0;
    for (int i = 0; i < APP_MEOW_G_N && left > 0; i++) {
        int n;
        int d;

        if (CAT[i].cat != APP_MEOW_CAT_FOOD) continue;
        n = (int)take[i];
        if (n > left) n = left;
        if (n <= 0) continue;
        d = (int)CAT[i].dur;
        if (d < 1) d = 1;
        g += n * app_meow_good_gain(i) * d;
        left -= n;
    }
    return g;
}

int app_meow_trip_mins(int gain)
{
    int n;

    if (gain <= 0) return 0;
    n = (gain * (int)APP_MEOW_TRIP_MIN_PER + (int)APP_MEOW_TRIP_GAIN_REF / 2)
        / (int)APP_MEOW_TRIP_GAIN_REF;
    if (n < 1) n = 1;
    return n;
}

int app_meow_trip_sec(int gain)
{
    return app_meow_trip_mins(gain) * (int)APP_MEOW_SEC_PER_MIN;
}

int app_meow_trip_rewards(int gain)
{
    int n;

    if (gain <= 0) return 0;
    /* 饭团单次收益为 1 当量;至少比当量多 1 份,每 4 当量再 +1。 */
    n = (gain + (int)APP_MEOW_TRIP_GAIN_REF / 2) / (int)APP_MEOW_TRIP_GAIN_REF;
    if (n < 1) n = 1;
    return n + 1 + n / 4;
}

int app_meow_trip_souv_pct(int pack)
{
    int pct;

    if (pack < APP_MEOW_TRIP_SOUV_MIN) return 0;
    if (pack > APP_MEOW_TRIP_PACK_MAX) pack = APP_MEOW_TRIP_PACK_MAX;
    pct = (pack - (APP_MEOW_TRIP_SOUV_MIN - 1)) * 6;
    if (pct > 40) pct = 40;
    return pct;
}

int app_meow_trip_sec_left(const app_meow_t *p, uint32_t now_sec)
{
    uint32_t rem;

    if (!p || p->trip_st != APP_MEOW_TRIP_AWAY) return 0;
    rem = (uint32_t)p->trip_left * APP_MEOW_SEC_PER_MIN;
    if (now_sec > p->last_sec) {
        uint32_t used = now_sec - p->last_sec;
        if (used >= rem) return 0;
        rem -= used;
    }
    return (int)rem;
}

app_meow_res_t app_meow_trip_start(app_meow_t *p, const uint8_t take[APP_MEOW_G_N])
{
    int pack = pack_sum(take);

    s_last_got = -1;
    s_last_souv = -1;
    if (!app_meow_trip_can(p)) {
        if (!app_meow_valid(p)) return APP_MEOW_NONE;
        if (p->stage == APP_MEOW_EGG) return APP_MEOW_EGG_WAIT;
        if (p->stage == APP_MEOW_DEAD) return APP_MEOW_GONE;
        if (p->sleeping) return APP_MEOW_SLEEP;
        return APP_MEOW_NONE;
    }
    if (pack <= 0) return APP_MEOW_EMPTY;
    if (pack > APP_MEOW_TRIP_PACK_MAX) pack = APP_MEOW_TRIP_PACK_MAX;
    for (int i = 0; i < APP_MEOW_G_N; i++) {
        int n;

        if (CAT[i].cat != APP_MEOW_CAT_FOOD) continue;
        n = (int)take[i];
        if (n <= 0) continue;
        if ((int)p->inv_n[i] < n) return APP_MEOW_EMPTY;
    }
    {
        int left = pack;
        for (int i = 0; i < APP_MEOW_G_N && left > 0; i++) {
            int n;

            if (CAT[i].cat != APP_MEOW_CAT_FOOD) continue;
            n = (int)take[i];
            if (n > left) n = left;
            if (n <= 0) continue;
            p->inv_n[i] = (uint16_t)(p->inv_n[i] - (uint16_t)n);
            if (p->inv_n[i] == 0) p->inv_d[i] = 0;
            left -= n;
        }
        pack -= left;
    }
    if (pack <= 0) return APP_MEOW_EMPTY;
    p->trip_st = APP_MEOW_TRIP_AWAY;
    p->trip_pack = (uint8_t)pack;
    p->trip_gain = (uint16_t)app_meow_trip_take_gain(take);
    p->trip_left = (uint16_t)app_meow_trip_mins((int)p->trip_gain);
    p->sleeping = 0;
    return APP_MEOW_OK;
}

static int pick_souv(app_meow_t *p)
{
    int free_n = 0;
    int pick;
    int n = 0;

    for (int i = 0; i < APP_MEOW_SOUV_N; i++) {
        if ((p->found & (uint16_t)(1u << i)) == 0) free_n++;
    }
    if (free_n > 0) {
        pick = (int)(rnd(p) % (uint8_t)free_n);
        for (int i = 0; i < APP_MEOW_SOUV_N; i++) {
            if (p->found & (uint16_t)(1u << i)) continue;
            if (n == pick) return i;
            n++;
        }
    }
    return (int)(rnd(p) % (uint8_t)APP_MEOW_SOUV_N);
}

int app_meow_trip_claim(app_meow_t *p)
{
    int pack;
    int need;
    int got = 0;
    int last = -1;
    int pct;

    s_last_got = -1;
    s_last_souv = -1;
    memset(s_last_prize, 0, sizeof(s_last_prize));
    if (!app_meow_valid(p) || p->trip_st != APP_MEOW_TRIP_BACK) return 0;
    pack = (int)p->trip_pack;
    {
        int gain = (int)p->trip_gain;

        if (gain <= 0) gain = pack * (int)APP_MEOW_TRIP_GAIN_REF;
        need = app_meow_trip_rewards(gain);
    }
    p->trip_st = APP_MEOW_TRIP_IDLE;
    p->trip_pack = 0;
    p->trip_gain = 0;
    p->trip_left = 0;
    for (int i = 0; i < need; i++) {
        int it = roll_loot(p, APP_MEOW_LOOT_TRIP);
        if (it < 0) break;
        if (app_meow_give(p, it, 1) <= 0) break;
        last = it;
        s_last_prize[it]++;
        got++;
    }
    pct = app_meow_trip_souv_pct(pack);
    if (pct > 0 && (int)(rnd(p) % 100u) < pct) {
        int id = pick_souv(p);
        p->found |= (uint16_t)(1u << id);
        s_last_souv = id;
    }
    s_last_got = last;
    return got;
}

int app_meow_last_souv(void)
{
    return s_last_souv;
}

bool app_meow_souv_on(const app_meow_t *p, int id)
{
    if (!p || id < 0 || id >= APP_MEOW_SOUV_N) return false;
    return (p->found & (uint16_t)(1u << id)) != 0;
}
