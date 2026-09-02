#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_logic.h"
#include "app_notif_rule.h"

static app_notif_item_t make_item(const char *title, const char *msg,
                                  app_alert_t alert)
{
    app_notif_item_t it;
    memset(&it, 0, sizeof(it));
    if (title) {
        size_t n = strlen(title);
        if (n >= sizeof(it.title)) n = sizeof(it.title) - 1;
        memcpy(it.title, title, n);
    }
    if (msg) {
        size_t n = strlen(msg);
        if (n >= sizeof(it.message)) n = sizeof(it.message) - 1;
        memcpy(it.message, msg, n);
    }
    it.alert = alert;
    return it;
}

int main(void)
{
    char t[16];
    app_time_format(8, 22, 1, 9, t, sizeof(t));
    assert(strcmp(t, "8/22 1:09") == 0);
    app_time_format(12, 3, 0, 5, t, sizeof(t));
    assert(strcmp(t, "12/3 0:05") == 0);

    assert(app_ancs_date_text("20140915T173018", t, sizeof(t)));
    assert(strcmp(t, "9/15 17:30") == 0);
    assert(app_ancs_date_text("20260822T010905", t, sizeof(t)));
    assert(strcmp(t, "8/22 1:09") == 0);
    assert(!app_ancs_date_text("", t, sizeof(t)));
    assert(!app_ancs_date_text("20140915", t, sizeof(t)));
    assert(!app_ancs_date_text("20141315T173018", t, sizeof(t)));
    assert(!app_ancs_date_text(NULL, t, sizeof(t)));

    assert(!app_notif_show_subtitle("Mom", ""));
    assert(!app_notif_show_subtitle("Mom", NULL));
    assert(!app_notif_show_subtitle("Mom", "Mom"));
    assert(app_notif_show_subtitle("Mom", "Family"));
    assert(app_notif_show_subtitle("", "Family"));

    app_notif_q_t q;
    app_notif_q_init(&q);
    assert(app_notif_q_count(&q) == 0);
    assert(app_notif_q_front(&q) == NULL);
    app_notif_item_t a = make_item("a", "one", APP_ALERT_POPUP);
    strcpy(a.subtitle, "sub");
    strcpy(a.app_name, "Mail");
    strcpy(a.date, "20140915T173018");
    assert(app_notif_q_push(&q, &a));
    app_notif_item_t b = make_item("b", "two", APP_ALERT_URGENT);
    assert(app_notif_q_push(&q, &b));
    assert(app_notif_q_count(&q) == 2);
    assert(strcmp(app_notif_q_front(&q)->title, "a") == 0);
    assert(strcmp(app_notif_q_front(&q)->subtitle, "sub") == 0);
    assert(strcmp(app_notif_q_front(&q)->app_name, "Mail") == 0);
    assert(strcmp(app_notif_q_front(&q)->date, "20140915T173018") == 0);
    app_notif_q_pop(&q);
    assert(strcmp(app_notif_q_front(&q)->title, "b") == 0);
    assert(app_notif_q_front(&q)->alert == APP_ALERT_URGENT);
    app_notif_q_pop(&q);
    assert(app_notif_q_count(&q) == 0);

    for (int i = 0; i < APP_NOTIF_Q + 2; i++) {
        char title[8];
        title[0] = (char)('A' + i);
        title[1] = 0;
        app_notif_item_t it = make_item(title, "m", APP_ALERT_POPUP);
        app_notif_q_push(&q, &it);
    }
    assert(app_notif_q_count(&q) == APP_NOTIF_Q);
    assert(app_notif_q_front(&q)->title[0] == 'C');

    uint8_t key[32];
    int kn = app_b32_decode("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", key, sizeof(key));
    assert(kn == 20);
    assert(memcmp(key, "12345678901234567890", 20) == 0);
    assert(app_hotp(key, 20, 0, 6) == 755224);
    assert(app_hotp(key, 20, 1, 6) == 287082);

    app_totp_acct_t acct;
    memset(&acct, 0, sizeof(acct));
    strcpy(acct.secret, "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ");
    acct.digits = 8;
    acct.period = 30;
    char code[12];
    int remain = -1;
    assert(app_totp_code(&acct, 59, code, sizeof(code), &remain));
    assert(strcmp(code, "94287082") == 0);
    assert(remain == 1);
    assert(app_totp_code(&acct, 1111111109, code, sizeof(code), &remain));
    assert(strcmp(code, "07081804") == 0);
    assert(app_totp_code(&acct, 1111111111, code, sizeof(code), NULL));
    assert(strcmp(code, "14050471") == 0);
    assert(app_totp_code(&acct, 1234567890, code, sizeof(code), NULL));
    assert(strcmp(code, "89005924") == 0);
    assert(app_totp_code(&acct, 2000000000, code, sizeof(code), NULL));
    assert(strcmp(code, "69279037") == 0);

    char pretty[16];
    app_totp_format_code("123456", pretty, sizeof(pretty));
    assert(strcmp(pretty, "123 456") == 0);
    app_totp_format_code("12345678", pretty, sizeof(pretty));
    assert(strcmp(pretty, "1234 5678") == 0);
    app_totp_mask("JBSWY3DPEHPK3PXP", pretty, sizeof(pretty));
    assert(strcmp(pretty, "****3PXP") == 0);

    memset(&acct, 0, sizeof(acct));
    assert(app_totp_ingest(" jbswy3dpehpk3pxp ", &acct, true));
    assert(strcmp(acct.secret, "JBSWY3DPEHPK3PXP") == 0);
    assert(acct.digits == 0 && acct.period == 0);

    memset(&acct, 0, sizeof(acct));
    assert(app_totp_ingest(
        "otpauth://totp/ACME:john%40ex.com?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ"
        "&issuer=ACME&digits=8&period=30&algorithm=SHA1",
        &acct, true));
    assert(strcmp(acct.issuer, "ACME") == 0);
    assert(strcmp(acct.label, "john@ex.com") == 0);
    assert(acct.digits == 8 && acct.period == 30);
    assert(app_totp_code(&acct, 59, code, sizeof(code), NULL));
    assert(strcmp(code, "94287082") == 0);

    memset(&acct, 0, sizeof(acct));
    strcpy(acct.issuer, "Keep");
    strcpy(acct.label, "me");
    assert(app_totp_ingest("otpauth://totp/Other?secret=JBSWY3DPEHPK3PXP",
                           &acct, false));
    assert(strcmp(acct.issuer, "Keep") == 0);
    assert(strcmp(acct.label, "me") == 0);

    memset(&acct, 0, sizeof(acct));
    assert(!app_totp_ingest(
        "otpauth://totp/X?secret=JBSWY3DPEHPK3PXP&algorithm=SHA256",
        &acct, true));
    assert(!app_totp_ingest("not-base32!!", &acct, false));

    char iss[24], lab[32];
    app_totp_split_name("GitHub:alice", iss, sizeof(iss), lab, sizeof(lab));
    assert(strcmp(iss, "GitHub") == 0);
    assert(strcmp(lab, "alice") == 0);
    app_totp_split_name("Google", iss, sizeof(iss), lab, sizeof(lab));
    assert(strcmp(iss, "Google") == 0);
    assert(lab[0] == 0);

    app_totp_list_t list;
    app_totp_list_init(&list);
    app_totp_acct_t a1;
    memset(&a1, 0, sizeof(a1));
    strcpy(a1.issuer, "GitHub");
    strcpy(a1.label, "bob");
    strcpy(a1.secret, "JBSWY3DPEHPK3PXP");
    assert(app_totp_list_add(&list, &a1));
    strcpy(a1.label, "alice");
    assert(app_totp_list_add(&list, &a1));
    strcpy(a1.issuer, "Google");
    strcpy(a1.label, "work");
    assert(app_totp_list_add(&list, &a1));
    for (int i = 0; i < 10; i++) {
        a1.label[0] = (char)('0' + i);
        a1.label[1] = 0;
        assert(app_totp_list_add(&list, &a1));
    }
    assert(list.n == 13);
    assert(strcmp(list.items[0].issuer, "GitHub") == 0);
    assert(strcmp(list.items[0].label, "alice") == 0);
    assert(strcmp(list.items[1].label, "bob") == 0);
    assert(app_totp_same_group(&list.items[0], &list.items[1]));
    assert(!app_totp_same_group(&list.items[0], &list.items[2]));
    strcpy(a1.issuer, "GitHub");
    strcpy(a1.label, "bob");
    assert(app_totp_list_find(&list, &a1) == 1);
    assert(app_totp_list_delete(&list, 0));
    assert(list.n == 12);
    app_totp_list_clear(&list);
    assert(list.n == 0);
    assert(list.items == NULL);

    app_dlog_t dlog;
    app_dlog_init(&dlog);
    assert(app_dlog_count(&dlog) == 0);
    char line[APP_DLOG_W];
    app_dlog_copy(&dlog, 0, line, sizeof(line));
    assert(line[0] == 0);

    app_dlog_feed(&dlog, "hello\n", 6);
    assert(app_dlog_count(&dlog) == 1);
    app_dlog_copy(&dlog, 0, line, sizeof(line));
    assert(strcmp(line, "hello") == 0);
    assert(!app_dlog_cont(&dlog, 0));

    app_dlog_feed(&dlog, "ab", 2);
    assert(app_dlog_count(&dlog) == 1);
    app_dlog_feed(&dlog, "c\n", 2);
    assert(app_dlog_count(&dlog) == 2);
    app_dlog_copy(&dlog, 1, line, sizeof(line));
    assert(strcmp(line, "abc") == 0);
    assert(!app_dlog_cont(&dlog, 1));

    const char *ansi = "\033[0;32mI (1) t: x\033[0m\n";
    app_dlog_feed(&dlog, ansi, (int)strlen(ansi));
    assert(app_dlog_count(&dlog) == 3);
    app_dlog_copy(&dlog, 2, line, sizeof(line));
    assert(strcmp(line, "I (1) t: x") == 0);

    char longl[APP_DLOG_W + 8];
    memset(longl, 'A', sizeof(longl) - 2);
    longl[sizeof(longl) - 2] = '\n';
    longl[sizeof(longl) - 1] = 0;
    int before = app_dlog_count(&dlog);
    app_dlog_feed(&dlog, longl, (int)strlen(longl));
    assert(app_dlog_count(&dlog) == before + 2);
    assert(!app_dlog_cont(&dlog, before));
    assert(app_dlog_cont(&dlog, before + 1));

    app_dlog_feed(&dlog, "tail", 4);
    app_dlog_flush(&dlog);
    app_dlog_copy(&dlog, app_dlog_count(&dlog) - 1, line, sizeof(line));
    assert(strcmp(line, "tail") == 0);
    assert(!app_dlog_cont(&dlog, app_dlog_count(&dlog) - 1));

    for (int i = 0; i < APP_DLOG_N + 5; i++) {
        char one[8];
        snprintf(one, sizeof(one), "%d\n", i);
        app_dlog_feed(&dlog, one, (int)strlen(one));
    }
    assert(app_dlog_count(&dlog) == APP_DLOG_N);
    app_dlog_copy(&dlog, APP_DLOG_N - 1, line, sizeof(line));
    char expect[16];
    snprintf(expect, sizeof(expect), "%d", APP_DLOG_N + 4);
    assert(strcmp(line, expect) == 0);

    app_dlog_clear(&dlog);
    assert(app_dlog_count(&dlog) == 0);

    assert(app_dnd_in_range(21, 21, 8));
    assert(app_dnd_in_range(7, 21, 8));
    assert(!app_dnd_in_range(8, 21, 8));
    assert(!app_dnd_in_range(20, 21, 8));
    assert(!app_dnd_in_range(21, 21, 21));
    assert(app_dnd_in_range(13, 13, 15));
    assert(!app_dnd_in_range(15, 13, 15));

    app_notif_ctx_t nctx = {
        .app_id = "com.apple.MobileSMS",
        .app_name = "Messages",
        .title = "Your code is 1234",
        .subtitle = "",
        .message = "",
        .category = 4,
    };
    app_kw_t rules[4];
    memset(rules, 0, sizeof(rules));
    // 无规则时落到默认档,不再是隐式静默
    assert(app_notif_decide(&nctx, rules, 0, false, APP_ALERT_POPUP) ==
           APP_ALERT_POPUP);
    assert(app_notif_decide(&nctx, rules, 0, false, APP_ALERT_SILENT) ==
           APP_ALERT_SILENT);
    assert(app_notif_decide(&nctx, rules, 0, false, APP_ALERT_DROP) ==
           APP_ALERT_DROP);
    assert(app_notif_decide(NULL, rules, 4, false, APP_ALERT_URGENT) ==
           APP_ALERT_URGENT);

    strcpy(rules[0].text, "any:code");
    rules[0].prio = APP_ALERT_POPUP;
    assert(app_notif_decide(&nctx, rules, 1, false, APP_ALERT_DROP) ==
           APP_ALERT_POPUP);
    rules[0].prio = APP_ALERT_DROP;
    assert(app_notif_decide(&nctx, rules, 1, false, APP_ALERT_POPUP) ==
           APP_ALERT_DROP);
    rules[0].prio = APP_ALERT_POPUP;

    strcpy(rules[0].text, "app:*MobileSMS&any:*code*");
    rules[0].prio = APP_ALERT_URGENT;
    assert(app_notif_decide(&nctx, rules, 1, false, APP_ALERT_SILENT) ==
           APP_ALERT_URGENT);

    strcpy(rules[0].text, "cat:6");
    assert(app_notif_decide(&nctx, rules, 1, false, APP_ALERT_SILENT) ==
           APP_ALERT_SILENT);

    strcpy(rules[0].text, "cat:4,6");
    assert(app_notif_decide(&nctx, rules, 1, false, APP_ALERT_SILENT) ==
           APP_ALERT_URGENT);

    strcpy(rules[0].text, "any:*code*|sub:*code*");
    assert(app_notif_decide(&nctx, rules, 1, false, APP_ALERT_SILENT) ==
           APP_ALERT_URGENT);

    strcpy(rules[0].text, "!app:*MobileSMS");
    assert(app_notif_decide(&nctx, rules, 1, false, APP_ALERT_SILENT) ==
           APP_ALERT_SILENT);

    strcpy(rules[0].text, "title:*123?&!msg:*spam*");
    assert(app_notif_decide(&nctx, rules, 1, false, APP_ALERT_SILENT) ==
           APP_ALERT_URGENT);

    nctx.message = "spam offer";
    assert(app_notif_decide(&nctx, rules, 1, false, APP_ALERT_SILENT) ==
           APP_ALERT_SILENT);
    nctx.message = "";

    // 首条命中优先:屏蔽规则排在放行规则前面才拦得住
    memset(rules, 0, sizeof(rules));
    strcpy(rules[0].text, "app:*MobileSMS");
    rules[0].prio = APP_ALERT_SILENT;
    strcpy(rules[1].text, "any:*code*");
    rules[1].prio = APP_ALERT_URGENT;
    assert(app_notif_decide(&nctx, rules, 2, false, APP_ALERT_POPUP) ==
           APP_ALERT_SILENT);

    app_kw_t swapped[2] = { rules[1], rules[0] };
    assert(app_notif_decide(&nctx, swapped, 2, false, APP_ALERT_POPUP) ==
           APP_ALERT_URGENT);

    // 空规则跳过,不影响后面的命中
    memset(rules[0].text, 0, sizeof(rules[0].text));
    assert(app_notif_decide(&nctx, rules, 2, false, APP_ALERT_POPUP) ==
           APP_ALERT_URGENT);

    // 勿扰:弹窗降级为静默仍进历史,强提醒不受影响
    assert(app_notif_decide(&nctx, rules, 2, true, APP_ALERT_POPUP) ==
           APP_ALERT_URGENT);
    memset(rules, 0, sizeof(rules));
    strcpy(rules[0].text, "any:*code*");
    rules[0].prio = APP_ALERT_POPUP;
    assert(app_notif_decide(&nctx, rules, 1, true, APP_ALERT_SILENT) ==
           APP_ALERT_SILENT);
    assert(app_notif_decide(&nctx, rules, 0, true, APP_ALERT_POPUP) ==
           APP_ALERT_SILENT);

    // 越界档位夹到最高档,防止旧数据把 prio 写飞
    rules[0].prio = 200;
    assert(app_notif_decide(&nctx, rules, 1, false, APP_ALERT_SILENT) ==
           APP_ALERT_URGENT);

    // 旧版本迁移:0/1 两档升成弹窗/强提醒,未命中默认档补成静默保留原行为
    app_kw_t old_rules[2];
    memset(old_rules, 0, sizeof(old_rules));
    strcpy(old_rules[0].text, "any:code");
    old_rules[0].prio = 0;
    strcpy(old_rules[1].text, "any:otp");
    old_rules[1].prio = 1;
    uint8_t def = APP_ALERT_POPUP;
    app_notif_rules_upgrade(old_rules, 2, &def);
    assert(old_rules[0].prio == APP_ALERT_POPUP);
    assert(old_rules[1].prio == APP_ALERT_URGENT);
    assert(def == APP_ALERT_SILENT);
    app_notif_rules_upgrade(NULL, 0, NULL);

    // 迁移后行为等价于老的「无命中即屏蔽」
    assert(app_notif_decide(&nctx, old_rules, 2, false, (app_alert_t)def) ==
           APP_ALERT_POPUP);
    nctx.title = "nothing here";
    assert(app_notif_decide(&nctx, old_rules, 2, false, (app_alert_t)def) ==
           APP_ALERT_SILENT);
    nctx.title = "Your code is 1234";

    app_notif_item_t call;
    memset(&call, 0, sizeof(call));
    call.category = APP_CAT_INCOMING;
    call.flags = APP_NOTIF_FLAG_POS | APP_NOTIF_FLAG_NEG;
    strcpy(call.pos_label, "Answer");
    strcpy(call.neg_label, "Decline");
    app_notif_act_t acts[APP_NOTIF_ACT_MAX];
    int an = app_notif_acts(&call, acts, APP_NOTIF_ACT_MAX);
    assert(an == 2);
    assert(acts[0].kind == APP_NOTIF_ACT_POS);
    assert(strcmp(acts[0].label, "Answer") == 0);
    assert(acts[1].kind == APP_NOTIF_ACT_NEG);
    assert(app_notif_act_default(acts, an, APP_CAT_INCOMING) == 0);

    app_notif_item_t sms;
    memset(&sms, 0, sizeof(sms));
    sms.category = 4;
    sms.flags = APP_NOTIF_FLAG_NEG;
    strcpy(sms.neg_label, "Clear");
    an = app_notif_acts(&sms, acts, APP_NOTIF_ACT_MAX);
    assert(an == 2);
    assert(acts[0].kind == APP_NOTIF_ACT_CLOSE);
    assert(acts[1].kind == APP_NOTIF_ACT_NEG);
    assert(app_notif_act_default(acts, an, 4) == 0);

    app_notif_item_t plain;
    memset(&plain, 0, sizeof(plain));
    an = app_notif_acts(&plain, acts, APP_NOTIF_ACT_MAX);
    assert(an == 1);
    assert(acts[0].kind == APP_NOTIF_ACT_CLOSE);
    assert(app_notif_act_default(acts, an, 0) == 0);

    app_notif_q_t aq;
    app_notif_q_init(&aq);
    app_notif_item_t xa = make_item("keep", "m", APP_ALERT_POPUP);
    xa.uid = 1;
    app_notif_item_t xb = make_item("drop", "m", APP_ALERT_POPUP);
    xb.uid = 2;
    app_notif_item_t xc = make_item("keep2", "m", APP_ALERT_POPUP);
    xc.uid = 3;
    app_notif_q_push(&aq, &xa);
    app_notif_q_push(&aq, &xb);
    app_notif_q_push(&aq, &xc);
    app_notif_q_drop_uid(&aq, 2);
    assert(app_notif_q_count(&aq) == 2);
    assert(strcmp(app_notif_q_front(&aq)->title, "keep") == 0);
    app_notif_q_pop(&aq);
    assert(strcmp(app_notif_q_front(&aq)->title, "keep2") == 0);

    return 0;
}
