#include "app_notif_store.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static app_notif_rec_t mk(uint32_t uid, const char *app_id, const char *title,
                          uint8_t cat, uint8_t alert)
{
    app_notif_rec_t r;
    memset(&r, 0, sizeof(r));
    r.uid = uid;
    snprintf(r.app_id, sizeof(r.app_id), "%s", app_id);
    snprintf(r.title, sizeof(r.title), "%s", title);
    r.category = cat;
    r.alert = alert;
    r.unread = 1;
    return r;
}

static void test_push_order(void)
{
    app_notif_store_t s;
    app_notif_store_init(&s);
    assert(app_notif_store_count(&s) == 0);
    assert(app_notif_store_at(&s, 0) == NULL);

    for (int i = 1; i <= 3; i++) {
        app_notif_rec_t r = mk((uint32_t)i, "com.a", "t", 1, APP_ALERT_POPUP);
        assert(app_notif_store_push(&s, &r));
    }
    assert(app_notif_store_count(&s) == 3);
    // 0 号是最新
    assert(app_notif_store_at(&s, 0)->uid == 3);
    assert(app_notif_store_at(&s, 2)->uid == 1);
    assert(app_notif_store_at(&s, 3) == NULL);
}

static void test_ring_overflow(void)
{
    app_notif_store_t s;
    app_notif_store_init(&s);
    for (int i = 1; i <= APP_NOTIF_STORE_N + 4; i++) {
        app_notif_rec_t r = mk((uint32_t)i, "com.a", "t", 1, APP_ALERT_SILENT);
        app_notif_store_push(&s, &r);
    }
    assert(app_notif_store_count(&s) == APP_NOTIF_STORE_N);
    assert(app_notif_store_at(&s, 0)->uid == APP_NOTIF_STORE_N + 4);
    assert(app_notif_store_at(&s, APP_NOTIF_STORE_N - 1)->uid == 5);
}

static void test_modified_in_place(void)
{
    app_notif_store_t s;
    app_notif_store_init(&s);
    app_notif_rec_t a = mk(10, "com.a", "old", 1, APP_ALERT_POPUP);
    app_notif_rec_t b = mk(11, "com.b", "b", 2, APP_ALERT_POPUP);
    app_notif_store_push(&s, &a);
    app_notif_store_push(&s, &b);

    // 同 uid 原地更新,不改变次序也不多占一格
    app_notif_rec_t upd = mk(10, "com.a", "new", 1, APP_ALERT_URGENT);
    assert(app_notif_store_push(&s, &upd));
    assert(app_notif_store_count(&s) == 2);
    assert(app_notif_store_at(&s, 1)->uid == 10);
    assert(strcmp(app_notif_store_at(&s, 1)->title, "new") == 0);
    assert(app_notif_store_at(&s, 1)->alert == APP_ALERT_URGENT);

    // uid 为 0 时无法对齐,一律当新条目
    app_notif_rec_t z1 = mk(0, "com.c", "z1", 3, APP_ALERT_SILENT);
    app_notif_rec_t z2 = mk(0, "com.c", "z2", 3, APP_ALERT_SILENT);
    app_notif_store_push(&s, &z1);
    app_notif_store_push(&s, &z2);
    assert(app_notif_store_count(&s) == 4);
    assert(app_notif_store_find_uid(&s, 0) == -1);
}

static void test_remove(void)
{
    app_notif_store_t s;
    app_notif_store_init(&s);
    for (int i = 1; i <= 4; i++) {
        app_notif_rec_t r = mk((uint32_t)i, "com.a", "t", 1, APP_ALERT_POPUP);
        app_notif_store_push(&s, &r);
    }
    assert(app_notif_store_remove_uid(&s, 2));
    assert(app_notif_store_count(&s) == 3);
    assert(app_notif_store_find_uid(&s, 2) == -1);
    assert(app_notif_store_at(&s, 0)->uid == 4);
    assert(app_notif_store_at(&s, 1)->uid == 3);
    assert(app_notif_store_at(&s, 2)->uid == 1);

    assert(!app_notif_store_remove_uid(&s, 99));
    assert(!app_notif_store_remove(&s, 3));
    assert(app_notif_store_remove(&s, 0));
    assert(app_notif_store_at(&s, 0)->uid == 3);
}

static void test_unread(void)
{
    app_notif_store_t s;
    app_notif_store_init(&s);
    for (int i = 1; i <= 3; i++) {
        app_notif_rec_t r = mk((uint32_t)i, "com.a", "t", 1, APP_ALERT_SILENT);
        app_notif_store_push(&s, &r);
    }
    assert(app_notif_store_unread(&s) == 3);
    assert(app_notif_store_mark_read(&s, 0));
    assert(app_notif_store_unread(&s) == 2);
    // 已读再标一次不算变更,免得白写 flash
    assert(!app_notif_store_mark_read(&s, 0));
    assert(app_notif_store_mark_read_uid(&s, 1));
    assert(app_notif_store_unread(&s) == 1);
    assert(!app_notif_store_mark_read_uid(&s, 999));
    app_notif_store_mark_all_read(&s);
    assert(app_notif_store_unread(&s) == 0);
    assert(app_notif_store_mark_unread(&s, 0));
    assert(app_notif_store_unread(&s) == 1);
    assert(!app_notif_store_mark_unread(&s, 0));
}

static void test_labels_and_groups(void)
{
    app_notif_store_t s;
    app_notif_store_init(&s);
    app_notif_rec_t a = mk(1, "com.apple.MobileSMS", "sms", 1, APP_ALERT_POPUP);
    app_notif_rec_t b = mk(2, "com.apple.MobileSMS", "sms2", 1, APP_ALERT_POPUP);
    app_notif_rec_t c = mk(3, "com.tencent.xin", "wx", 2, APP_ALERT_SILENT);
    snprintf(c.app_name, sizeof(c.app_name), "WeChat");
    app_notif_store_push(&s, &a);
    app_notif_store_push(&s, &b);
    app_notif_store_push(&s, &c);

    char buf[APP_NOTIF_APP_NAME + 1];
    app_notif_rec_label(&a, buf, sizeof(buf));
    assert(strcmp(buf, "MobileSMS") == 0);  // 无 app_name 时取包名末段
    app_notif_rec_label(&c, buf, sizeof(buf));
    assert(strcmp(buf, "WeChat") == 0);

    app_notif_group_t g[4];
    int n = app_notif_store_apps(&s, g, 4);
    assert(n == 2);
    assert(g[0].count == 1 && strcmp(g[0].app_id, "com.tencent.xin") == 0);
    assert(g[1].count == 2);

    n = app_notif_store_cats(&s, g, 4);
    assert(n == 2);

    int idx[4];
    n = app_notif_store_match_app(&s, "com.apple.MobileSMS", idx, 4);
    assert(n == 2 && idx[0] == 1 && idx[1] == 2);
    n = app_notif_store_match_cat(&s, 2, idx, 4);
    assert(n == 1 && idx[0] == 0);
}

static void test_serialize(void)
{
    app_notif_store_t s;
    app_notif_store_init(&s);
    for (int i = 1; i <= 5; i++) {
        app_notif_rec_t r = mk((uint32_t)i, "com.a", "t", (uint8_t)i,
                               APP_ALERT_POPUP);
        if (i % 2) r.unread = 0;
        app_notif_store_push(&s, &r);
    }

    uint8_t buf[4096];
    size_t need = app_notif_store_blob_size(&s);
    assert(need <= sizeof(buf));
    assert(app_notif_store_serialize(&s, buf, need - 1) == 0);
    size_t used = app_notif_store_serialize(&s, buf, sizeof(buf));
    assert(used == need);

    app_notif_store_t r2;
    app_notif_store_init(&r2);
    assert(app_notif_store_deserialize(&r2, buf, used));
    assert(app_notif_store_count(&r2) == app_notif_store_count(&s));
    assert(app_notif_store_unread(&r2) == app_notif_store_unread(&s));
    for (int i = 0; i < app_notif_store_count(&s); i++) {
        const app_notif_rec_t *x = app_notif_store_at(&s, i);
        const app_notif_rec_t *y = app_notif_store_at(&r2, i);
        assert(x->uid == y->uid);
        assert(x->category == y->category);
        assert(x->unread == y->unread);
    }

    // 坏数据不能污染现有历史
    uint8_t bad[8] = { 0 };
    assert(!app_notif_store_deserialize(&r2, bad, sizeof(bad)));
    assert(!app_notif_store_deserialize(&r2, buf, used - 1));
    assert(app_notif_store_count(&r2) == app_notif_store_count(&s));
}

int main(void)
{
    test_push_order();
    test_ring_overflow();
    test_modified_in_place();
    test_remove();
    test_unread();
    test_labels_and_groups();
    test_serialize();
    return 0;
}
