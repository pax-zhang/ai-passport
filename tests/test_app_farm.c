#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_farm.h"

static app_farm_t f;

static void reset(void)
{
    app_farm_reset(&f, 1000, 123456, 0x11);
    assert(app_farm_valid(&f));
    assert(f.level == 1);
    assert(f.coins == APP_FARM_START_COIN);
    assert(f.seeds[0] == APP_FARM_START_SEED);
    assert(strcmp(f.host, APP_FARM_HOST_DEFAULT) == 0);
}

int main(void)
{
    app_farm_t z;
    app_farm_view_t v;
    uint16_t coins = 0;
    char json[1600];
    uint8_t mac[6];
    int i;

    memset(&z, 0, sizeof(z));
    assert(!app_farm_valid(&z));

    reset();
    assert(app_farm_plot_n(1) == 6);
    assert(app_farm_plot_n(3) == 9);
    assert(app_farm_plot_n(6) == 12);
    assert(app_farm_xp_need(1) == 35);
    assert(app_farm_crop(0)->seed_cost == 10);
    assert(app_farm_crop(0)->grow_sec == 1800);
    assert(app_farm_crop(0)->wither_sec == 1200);
    assert(app_farm_crop_open(&f, 0));
    assert(!app_farm_crop_open(&f, 2));

    assert(app_farm_mac_parse("AA:BB:CC:DD:EE:FF", mac));
    assert(mac[0] == 0xAA && mac[5] == 0xFF);
    assert(app_farm_id_from_mac(mac) >= 100000);
    assert(app_farm_id_from_mac(mac) <= 999999);
    {
        uint8_t m2[6];
        char txt[20];
        app_farm_mac_fmt(mac, txt, sizeof(txt));
        assert(strcmp(txt, "AA:BB:CC:DD:EE:FF") == 0);
        assert(app_farm_mac_parse("aa-bb-cc-dd-ee-ff", m2));
        assert(memcmp(mac, m2, 6) == 0);
        assert(app_farm_id_from_mac(mac) == app_farm_id_from_mac(m2));
    }
    assert(!app_farm_mac_parse("AABBCCDDEEFF", mac));

    assert(app_farm_can_tool(&f, 0, APP_FARM_TOOL_PLANT));
    assert(!app_farm_can_tool(&f, 0, APP_FARM_TOOL_WATER));
    assert(!app_farm_can_tool(&f, 0, APP_FARM_TOOL_WEED));
    assert(app_farm_next_plot(&f, APP_FARM_TOOL_PLANT, -1, 0) == 0);
    assert(app_farm_next_plot(&f, APP_FARM_TOOL_WEED, -1, 0) < 0);
    assert(app_farm_next_plot(&f, APP_FARM_TOOL_WATER, -1, 0) < 0);

    assert(app_farm_plant(&f, 0, 0) == APP_FARM_OK);
    assert(f.seeds[0] == APP_FARM_START_SEED - 1);
    assert(f.plots[0].stage == APP_FARM_ST_SEED);
    assert(f.plots[0].dry == 1);
    assert(app_farm_plot_eta_sec(&f.plots[0]) == 1800);
    assert(app_farm_plant(&f, 0, 0) == APP_FARM_NEED_EMPTY);
    assert(app_farm_plant(&f, 9, 0) == APP_FARM_LOCKED);
    assert(app_farm_cut(&f, 0, &coins) == APP_FARM_NEED_RIPE);

    assert(app_farm_can_tool(&f, 0, APP_FARM_TOOL_WATER));
    assert(!app_farm_can_tool(&f, 0, APP_FARM_TOOL_PLANT));
    assert(app_farm_next_plot(&f, APP_FARM_TOOL_PLANT, -1, 0) == 1);
    assert(app_farm_water(&f, 0) == APP_FARM_OK);
    assert(f.plots[0].dry == 0);
    assert(app_farm_plot_eta_sec(&f.plots[0]) == 1800);
    assert(app_farm_water(&f, 0) == APP_FARM_NEED_DRY);
    assert(!app_farm_can_tool(&f, 0, APP_FARM_TOOL_WATER));

    f.plots[0].weed = 1;
    assert(app_farm_plot_eta_sec(&f.plots[0]) == 3600);
    f.plots[0].weed = 0;

    app_farm_advance(&f, 1000 + 900);
    f.plots[0].weed = 0;
    f.plots[0].pest = 0;
    assert(f.plots[0].stage == APP_FARM_ST_SEED);
    assert(app_farm_plot_eta_sec(&f.plots[0]) == 900);
    app_farm_advance(&f, 1000 + 901);
    f.plots[0].weed = 0;
    f.plots[0].pest = 0;
    assert(f.plots[0].stage == APP_FARM_ST_GROW);
    app_farm_advance(&f, 1000 + 1800);
    f.plots[0].weed = 0;
    f.plots[0].pest = 0;
    f.plots[0].yield = 100;
    assert(f.plots[0].stage == APP_FARM_ST_RIPE);
    assert(f.plots[0].grow_left == 1200);
    assert(app_farm_plot_eta_sec(&f.plots[0]) == 0);

    f.plots[0].weed = 1;
    assert(app_farm_can_tool(&f, 0, APP_FARM_TOOL_WEED));
    assert(!app_farm_can_tool(&f, 0, APP_FARM_TOOL_CUT));
    assert(app_farm_cut(&f, 0, &coins) == APP_FARM_HAS_HAZ);
    assert(app_farm_next_plot(&f, APP_FARM_TOOL_WEED, 2, 1) == 0);
    assert(app_farm_weed(&f, 0) == APP_FARM_OK);
    assert(app_farm_weed(&f, 0) == APP_FARM_NEED_WEED);
    assert(app_farm_cut(&f, 0, &coins) == APP_FARM_OK);
    assert(coins == 18);
    assert(f.coins == APP_FARM_START_COIN + 18);
    assert(f.plots[0].stage == APP_FARM_ST_EMPTY);

    assert(app_farm_buy(&f, 2, 1) == APP_FARM_LOCKED_CROP);
    assert(app_farm_buy(&f, 0, 1) == APP_FARM_OK);
    assert(f.seeds[0] == APP_FARM_START_SEED);
    assert(f.coins == APP_FARM_START_COIN + 18 - 10);

    f.level = 12;
    f.coins = 200;
    assert(app_farm_crop_open(&f, 5));
    assert(app_farm_buy(&f, 5, 1) == APP_FARM_OK);

    reset();
    f.level = 99;
    f.xp = 10;
    assert(app_farm_add_xp(&f, 50) == 0);
    assert(f.level == 99);

    reset();
    f.xp = 34;
    assert(app_farm_add_xp(&f, 2) == 1);
    assert(f.level == 2);
    assert(f.xp == 1);

    reset();
    app_farm_plant(&f, 1, 0);
    app_farm_water(&f, 1);
    f.plots[1].stage = APP_FARM_ST_RIPE;
    f.plots[1].dry = 0;
    f.plots[1].weed = 0;
    f.plots[1].pest = 0;
    memset(&v, 0, sizeof(v));
    v.id = 654321;
    v.level = 1;
    v.plots[1] = f.plots[1];
    assert(app_farm_can_steal(&v.plots[1]));
    assert(app_farm_next_steal(&v, -1, 0) == 1);
    assert(app_farm_next_steal(&v, 1, 1) == 1);
    assert(app_farm_steal(&f, &v, 1, &coins) == APP_FARM_OK);
    assert(coins == 10);
    assert(v.plots[1].stage == APP_FARM_ST_RIPE);
    assert(v.plots[1].yield == 40);
    assert(v.plots[1].stolen == 1);
    assert(app_farm_next_steal(&v, -1, 0) == 1);
    v.id = f.id;
    v.plots[0] = f.plots[1];
    v.plots[0].stage = APP_FARM_ST_RIPE;
    assert(app_farm_steal(&f, &v, 0, &coins) == APP_FARM_SELF);

    reset();
    f.plots[2].stage = APP_FARM_ST_RIPE;
    f.plots[2].crop = 0;
    memset(&v, 0, sizeof(v));
    v.level = 1;
    v.plots[2].stage = APP_FARM_ST_EMPTY;
    v.plots[2].stolen = 1;
    assert(app_farm_apply_remote(&f, &v) == 1);
    assert(f.plots[2].stage == APP_FARM_ST_EMPTY);
    assert(f.plots[2].stolen == 1);

    reset();
    assert(app_farm_friend_add(&f, f.id) == APP_FARM_SELF);
    assert(app_farm_friend_add(&f, 111111) == APP_FARM_OK);
    assert(app_farm_friend_has(&f, 111111));
    assert(app_farm_friend_add(&f, 111111) == APP_FARM_BUSY);
    assert(app_farm_friend_add(&f, 99) == APP_FARM_NONE);
    assert(app_farm_friend_del(&f, 111111));
    assert(!app_farm_friend_has(&f, 111111));

    reset();
    app_farm_set_name(&f, "  Patch  ");
    assert(strcmp(app_farm_name(&f), "Patch") == 0);
    app_farm_plant(&f, 0, 0);
    app_farm_friend_add(&f, 222222);
    assert(app_farm_json_write(&f, json, sizeof(json)) > 0);
    assert(strstr(json, "\"id\":123456") != NULL);
    assert(strstr(json, "\"name\":\"Patch\"") != NULL);
    {
        app_farm_t g;
        app_farm_reset(&g, 2000, 1, 1);
        assert(app_farm_json_read_self(json, &g));
        assert(g.id == 123456);
        assert(strcmp(g.name, "Patch") == 0);
        assert(g.plots[0].stage == APP_FARM_ST_SEED);
        assert(g.friend_n == 1);
        assert(g.friends[0] == 222222);
        assert(app_farm_json_read_view(json, &v));
        assert(v.id == 123456);
        assert(v.plots[0].dry == 1);
    }
    {
        const char *list =
            "{\"ok\":true,\"list\":[{\"id\":111111,\"name\":\"A\",\"level\":2,"
            "\"coins\":9},{\"id\":222222,\"name\":\"B\",\"level\":4,\"coins\":3}]}";
        app_farm_peer_t peers[4];
        assert(app_farm_json_ok(list));
        assert(app_farm_json_read_peers(list, "list", peers, 4) == 2);
        assert(peers[0].id == 111111);
        assert(peers[1].level == 4);
        assert(strcmp(peers[0].name, "A") == 0);
    }

    reset();
    {
        char raw[sizeof(f)];
        memcpy(raw, &f, sizeof(f));
        assert(app_farm_import(&z, raw, sizeof(f)));
        assert(z.id == 123456);
        assert(strcmp(z.host, APP_FARM_HOST_DEFAULT) == 0);
    }

    reset();
    {
        char raw[sizeof(f)];
        f.host[0] = 0;
        memcpy(raw, &f, sizeof(f));
        assert(app_farm_import(&z, raw, sizeof(f)));
        assert(strcmp(z.host, APP_FARM_HOST_DEFAULT) == 0);
    }

    reset();
    app_farm_set_name(&f, "Keep");
    strncpy(f.host, "10.0.0.1:3000", sizeof(f.host) - 1);
    strncpy(f.token, "abcd", sizeof(f.token) - 1);
    f.id = 555555;
    app_farm_wipe(&f, 3000);
    assert(f.level == 1);
    assert(f.coins == APP_FARM_START_COIN);
    assert(f.id == 555555);
    assert(strcmp(f.host, "10.0.0.1:3000") == 0);
    assert(strcmp(f.token, "abcd") == 0);
    assert(f.name[0] == 0);

    reset();
    assert(app_farm_plant(&f, 0, 0) == APP_FARM_OK);
    /* 不浇水不应生长 */
    app_farm_advance(&f, 1000 + 200);
    assert(f.plots[0].stage == APP_FARM_ST_SEED);
    assert(f.plots[0].dry == 1);

    reset();
    for (i = 0; i < APP_FARM_START_SEED; i++) {
        assert(app_farm_plant(&f, i, 0) == APP_FARM_OK);
    }
    assert(app_farm_plant(&f, 4, 0) == APP_FARM_NO_SEED);
    assert(app_farm_next_seed(&f, 0) < 0);

    puts("ok");
    return 0;
}
