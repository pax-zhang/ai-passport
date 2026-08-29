#include "app_farm_net.h"

#include "bsp_wifi.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "farm_net";

#define HTTP_BUF  1024
#define JSON_MAX  2048
#define URL_MAX   160
#define BODY_MAX  1400

typedef enum {
    JOB_NONE = 0,
    JOB_REG,
    JOB_SYNC,
    JOB_PULL,
    JOB_RANDOM,
    JOB_VISIT,
    JOB_STEAL,
    JOB_FRIENDS,
    JOB_INBOX,
    JOB_ADD,
    JOB_REPLY,
    JOB_REMOVE,
    JOB_RANK,
    JOB_HELP
} job_t;

static TaskHandle_t s_task;
static volatile job_t s_job;
static volatile app_farm_net_st_t s_st;
static char s_host[APP_FARM_HOST_MAX + 1];
static char s_tok[APP_FARM_TOK_MAX + 1];
static char s_mac[18];
static uint32_t s_id;
static uint32_t s_target;
static int s_plot;
static int s_act;
static char s_body[BODY_MAX];
static char s_json[JSON_MAX];
static char s_detail[32];
static app_farm_view_t s_view;
static app_farm_peer_t s_peers[APP_FARM_NET_PEER_MAX];
static app_farm_mail_t s_mail[APP_FARM_NET_PEER_MAX];
static int s_peer_n, s_mail_n;
static bool s_already, s_linked;
static uint16_t s_coins;
static bool s_have_tok;
static bool s_have_self;
static app_farm_t s_self;
static uint8_t s_left[APP_FARM_ACT_N] = {
    APP_FARM_STEAL_DAY, APP_FARM_HELP_DAY, APP_FARM_HELP_DAY, APP_FARM_HELP_DAY
};

static void set_fail(const char *why)
{
    snprintf(s_detail, sizeof(s_detail), "%s", why ? why : "fail");
    s_st = APP_FARM_NET_FAIL;
    ESP_LOGW(TAG, "%s", s_detail);
}

static bool wifi_ok(void)
{
    return bsp_wifi_enabled() && bsp_wifi_state() == BSP_WIFI_CONNECTED;
}

static bool host_ok(void)
{
    return s_host[0] != 0;
}

static void make_url(char *out, size_t n, const char *path)
{
    const char *h = s_host;

    if (strncmp(h, "http://", 7) == 0 || strncmp(h, "https://", 8) == 0) {
        snprintf(out, n, "%s%s", h, path);
        return;
    }
    snprintf(out, n, "http://%s%s", h, path);
}

static int parse_url(const char *url, char *host, size_t hn, int *port)
{
    const char *p = url;
    int def = 80, n = 0;

    if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        def = 443;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }
    while (*p && *p != ':' && *p != '/' && n + 1 < (int)hn) host[n++] = *p++;
    host[n] = 0;
    if (*p == ':') {
        p++;
        def = 0;
        while (*p >= '0' && *p <= '9') def = def * 10 + (*p++ - '0');
    }
    *port = def;
    return (*p == '/') ? (int)(p - url) : -1;
}

static int http_do(const char *method, const char *path, const char *body,
                   char *out, size_t n)
{
    esp_http_client_config_t cfg;
    esp_http_client_handle_t cli;
    char url[URL_MAX], host[48], ip[16];
    int status = -1, wlen, port, poff, got = 0, r;
    bool tls;

    make_url(url, sizeof(url), path);
    poff = parse_url(url, host, sizeof(host), &port);
    tls = strncmp(url, "https://", 8) == 0;
    bsp_wifi_ip(ip, sizeof(ip));
    memset(&cfg, 0, sizeof(cfg));
    if (out) out[0] = 0;
    cfg.host = host;
    cfg.port = port;
    cfg.path = poff >= 0 ? url + poff : "/";
    cfg.timeout_ms = 8000;
    cfg.buffer_size = HTTP_BUF;
    cfg.buffer_size_tx = 512;
    cfg.keep_alive_enable = false;
    cfg.disable_auto_redirect = true;
    if (tls) {
        cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    } else {
        cfg.transport_type = HTTP_TRANSPORT_OVER_TCP;
    }
    cli = esp_http_client_init(&cfg);
    if (!cli) {
        ESP_LOGW(TAG, "init fail %s %s:%d ip=%s", method, host, port, ip);
        return -1;
    }
    esp_http_client_set_method(cli, strcmp(method, "PUT") == 0
                               ? HTTP_METHOD_PUT : (strcmp(method, "POST") == 0
                               ? HTTP_METHOD_POST : HTTP_METHOD_GET));
    esp_http_client_set_header(cli, "User-Agent", "FoloToy-AI-Passport");
    esp_http_client_set_header(cli, "Accept", "application/json");
    esp_http_client_set_header(cli, "Connection", "close");
    if (s_tok[0]) {
        char auth[48];
        snprintf(auth, sizeof(auth), "Bearer %s", s_tok);
        esp_http_client_set_header(cli, "Authorization", auth);
    }
    wlen = body ? (int)strlen(body) : 0;
    if (wlen > 0) {
        esp_http_client_set_header(cli, "Content-Type", "application/json");
    }
    if (esp_http_client_open(cli, wlen) != ESP_OK) {
        ESP_LOGW(TAG, "open fail %s %s:%d ip=%s", method, host, port, ip);
        goto out_cli;
    }
    if (wlen > 0 && esp_http_client_write(cli, body, wlen) < 0) {
        ESP_LOGW(TAG, "write fail %s %s:%d", method, host, port);
        goto out_close;
    }
    if (esp_http_client_fetch_headers(cli) < 0) {
        ESP_LOGW(TAG, "hdr fail %s %s:%d ip=%s", method, host, port, ip);
        goto out_close;
    }
    status = esp_http_client_get_status_code(cli);
    while (out && got + 1 < (int)n) {
        r = esp_http_client_read(cli, out + got, (int)n - 1 - got);
        if (r < 0) {
            status = -1;
            break;
        }
        if (r == 0) break;
        got += r;
    }
    if (out) out[got] = 0;
    ESP_LOGI(TAG, "%s %s:%d%s ip=%s -> %d n=%d %.80s", method, host, port,
             poff >= 0 ? url + poff : "/", ip, status, got, out ? out : "");
out_close:
    esp_http_client_close(cli);
out_cli:
    esp_http_client_cleanup(cli);
    return status;
}

static void take_quota(const char *json)
{
    uint32_t v;

    v = app_farm_json_u32(json, "qs", ~0u);
    if (v != ~0u) s_left[0] = (uint8_t)v;
    v = app_farm_json_u32(json, "qw", ~0u);
    if (v != ~0u) s_left[1] = (uint8_t)v;
    v = app_farm_json_u32(json, "qg", ~0u);
    if (v != ~0u) s_left[2] = (uint8_t)v;
    v = app_farm_json_u32(json, "qp", ~0u);
    if (v != ~0u) s_left[3] = (uint8_t)v;
}

static void take_quota_drop(const char *json, int act)
{
    uint8_t prev;

    prev = (act >= 0 && act < APP_FARM_ACT_N) ? s_left[act] : 0;
    take_quota(json);
    if (act < 0 || act >= APP_FARM_ACT_N || prev == 0) return;
    if (s_left[act] >= prev) s_left[act] = (uint8_t)(prev - 1);
}

static void take_auth(const char *json)
{
    char tok[APP_FARM_TOK_MAX + 1];
    uint32_t id;

    if (app_farm_json_str(json, "token", tok, sizeof(tok)) && tok[0]) {
        strncpy(s_tok, tok, sizeof(s_tok) - 1);
        s_tok[sizeof(s_tok) - 1] = 0;
        s_have_tok = true;
    }
    id = app_farm_json_u32(json, "id", 0);
    if (id) s_id = id;
}

static void run_job(job_t job)
{
    int st;
    char path[48];

    s_have_self = false;
    s_coins = 0;
    s_detail[0] = 0;
    if (!wifi_ok()) {
        set_fail("wifi");
        return;
    }
    if (!host_ok()) {
        set_fail("host");
        return;
    }

    if (job == JOB_REG) {
        snprintf(s_body, sizeof(s_body), "{\"mac\":\"%s\",\"id\":%lu}",
                 s_mac, (unsigned long)s_id);
        st = http_do("POST", "/api/register", s_body, s_json, sizeof(s_json));
        if (st != 200 || !app_farm_json_ok(s_json)) {
            snprintf(s_detail, sizeof(s_detail), "reg %d", st);
            s_st = APP_FARM_NET_FAIL;
            ESP_LOGW(TAG, "%s %s", s_detail, s_json);
            return;
        }
        take_auth(s_json);
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_SYNC) {
        st = http_do("PUT", "/api/farm", s_body, s_json, sizeof(s_json));
        if (st != 200) {
            snprintf(s_detail, sizeof(s_detail), "sync %d", st);
            s_st = APP_FARM_NET_FAIL;
            ESP_LOGW(TAG, "%s", s_detail);
            return;
        }
        if (strstr(s_json, "\"plots\"")) {
            app_farm_json_read_view(s_json, &s_view);
            if (app_farm_json_read_self(s_json, &s_self)) s_have_self = true;
        }
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_PULL) {
        st = http_do("GET", "/api/farm", NULL, s_json, sizeof(s_json));
        if (st != 200) {
            snprintf(s_detail, sizeof(s_detail), "pull %d", st);
            s_st = APP_FARM_NET_FAIL;
            ESP_LOGW(TAG, "%s", s_detail);
            return;
        }
        app_farm_json_read_view(s_json, &s_view);
        if (app_farm_json_read_self(s_json, &s_self)) s_have_self = true;
        st = http_do("GET", "/api/inbox", NULL, s_json, sizeof(s_json));
        if (st == 200) {
            s_mail_n = app_farm_json_read_mail(s_json, "list", s_mail,
                                              APP_FARM_NET_PEER_MAX);
        } else {
            s_mail_n = 0;
        }
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_RANDOM) {
        st = http_do("GET", "/api/farm/random", NULL, s_json, sizeof(s_json));
        if (st != 200) {
            set_fail("none");
            return;
        }
        if (!app_farm_json_read_view(s_json, &s_view) || s_view.id == 0) {
            set_fail("none");
            return;
        }
        take_quota(s_json);
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_VISIT) {
        snprintf(path, sizeof(path), "/api/farm/%lu", (unsigned long)s_target);
        st = http_do("GET", path, NULL, s_json, sizeof(s_json));
        if (st != 200 || !app_farm_json_read_view(s_json, &s_view)) {
            set_fail("visit");
            return;
        }
        take_quota(s_json);
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_STEAL) {
        snprintf(s_body, sizeof(s_body), "{\"targetId\":%lu,\"plot\":%d}",
                 (unsigned long)s_target, s_plot);
        st = http_do("POST", "/api/steal", s_body, s_json, sizeof(s_json));
        if (st != 200) {
            char err[12] = { 0 };
            app_farm_json_str(s_json, "err", err, sizeof(err));
            if (strcmp(err, "limit") == 0) set_fail("limit");
            else if (strcmp(err, "cool") == 0) set_fail("cool");
            else if (st == 409) set_fail("ripe");
            else set_fail("steal");
            return;
        }
        s_coins = (uint16_t)app_farm_json_u32(s_json, "got", 0);
        if (strstr(s_json, "\"plots\"")) app_farm_json_read_view(s_json, &s_view);
        take_quota_drop(s_json, APP_FARM_ACT_STEAL);
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_HELP) {
        snprintf(s_body, sizeof(s_body), "{\"targetId\":%lu,\"plot\":%d,\"act\":%d}",
                 (unsigned long)s_target, s_plot, s_act);
        st = http_do("POST", "/api/help", s_body, s_json, sizeof(s_json));
        if (st != 200) {
            char err[12] = { 0 };
            app_farm_json_str(s_json, "err", err, sizeof(err));
            if (strcmp(err, "limit") == 0) set_fail("limit");
            else set_fail("help");
            return;
        }
        if (strstr(s_json, "\"plots\"")) app_farm_json_read_view(s_json, &s_view);
        take_quota_drop(s_json, s_act);
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_FRIENDS) {
        st = http_do("GET", "/api/friends", NULL, s_json, sizeof(s_json));
        if (st != 200) {
            set_fail("friends");
            return;
        }
        s_peer_n = app_farm_json_read_peers(s_json, "list", s_peers,
                                            APP_FARM_NET_PEER_MAX);
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_INBOX) {
        st = http_do("GET", "/api/inbox", NULL, s_json, sizeof(s_json));
        if (st != 200) {
            set_fail("inbox");
            return;
        }
        s_mail_n = app_farm_json_read_mail(s_json, "list", s_mail,
                                          APP_FARM_NET_PEER_MAX);
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_RANK) {
        st = http_do("GET", "/api/rank", NULL, s_json, sizeof(s_json));
        if (st != 200) {
            set_fail("rank");
            return;
        }
        s_peer_n = app_farm_json_read_peers(s_json, "list", s_peers,
                                            APP_FARM_NET_PEER_MAX);
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_ADD) {
        snprintf(s_body, sizeof(s_body), "{\"id\":%lu}", (unsigned long)s_target);
        st = http_do("POST", "/api/friends", s_body, s_json, sizeof(s_json));
        if (st != 200 || !app_farm_json_ok(s_json)) {
            set_fail("add");
            return;
        }
        s_already = strstr(s_json, "\"already\":true") != NULL;
        s_linked = strstr(s_json, "\"linked\":true") != NULL;
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_REPLY) {
        snprintf(s_body, sizeof(s_body), "{\"id\":%lu,\"accept\":%s}",
                 (unsigned long)s_target, s_act ? "true" : "false");
        st = http_do("POST", "/api/friends/reply", s_body, s_json, sizeof(s_json));
        if (st != 200 || !app_farm_json_ok(s_json)) {
            set_fail("reply");
            return;
        }
        s_st = APP_FARM_NET_OK;
        return;
    }

    if (job == JOB_REMOVE) {
        snprintf(s_body, sizeof(s_body), "{\"id\":%lu}", (unsigned long)s_target);
        st = http_do("POST", "/api/friends/remove", s_body, s_json, sizeof(s_json));
        if (st != 200) {
            set_fail("del");
            return;
        }
        s_st = APP_FARM_NET_OK;
        return;
    }

    set_fail("job");
}

static void net_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run_job(s_job);
        s_job = JOB_NONE;
    }
}

static bool kick(job_t job)
{
    if (s_st == APP_FARM_NET_BUSY) return false;
    s_st = APP_FARM_NET_BUSY;
    s_job = job;
    if (s_task) xTaskNotifyGive(s_task);
    return true;
}

void app_farm_net_init(void)
{
    if (s_task) return;
    s_st = APP_FARM_NET_IDLE;
    xTaskCreate(net_task, "farm_net", 6144, NULL, 5, &s_task);
}

static void snap_auth(const app_farm_t *f)
{
    if (!f) return;
    strncpy(s_host, f->host, sizeof(s_host) - 1);
    s_host[sizeof(s_host) - 1] = 0;
    strncpy(s_tok, f->token, sizeof(s_tok) - 1);
    s_tok[sizeof(s_tok) - 1] = 0;
    s_id = f->id;
}

app_farm_net_st_t app_farm_net_state(void)
{
    return s_st;
}

bool app_farm_net_busy(void)
{
    return s_st == APP_FARM_NET_BUSY;
}

const char *app_farm_net_detail(void)
{
    return s_detail;
}

bool app_farm_net_register(const app_farm_t *f, const uint8_t mac[6])
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    app_farm_mac_fmt(mac, s_mac, sizeof(s_mac));
    s_have_tok = false;
    return kick(JOB_REG);
}

bool app_farm_net_sync(const app_farm_t *f)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    if (app_farm_json_write(f, s_body, sizeof(s_body)) < 0) {
        snprintf(s_detail, sizeof(s_detail), "json");
        return false;
    }
    return kick(JOB_SYNC);
}

bool app_farm_net_pull(const app_farm_t *f)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    return kick(JOB_PULL);
}

bool app_farm_net_random(const app_farm_t *f)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    return kick(JOB_RANDOM);
}

bool app_farm_net_visit(const app_farm_t *f, uint32_t id)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    s_target = id;
    return kick(JOB_VISIT);
}

bool app_farm_net_steal(const app_farm_t *f, uint32_t target, int plot)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    s_target = target;
    s_plot = plot;
    return kick(JOB_STEAL);
}

bool app_farm_net_help(const app_farm_t *f, uint32_t target, int plot, int act)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    s_target = target;
    s_plot = plot;
    s_act = act;
    return kick(JOB_HELP);
}

bool app_farm_net_friends(const app_farm_t *f)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    return kick(JOB_FRIENDS);
}

bool app_farm_net_inbox(const app_farm_t *f)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    return kick(JOB_INBOX);
}

bool app_farm_net_add(const app_farm_t *f, uint32_t id)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    s_target = id;
    s_already = false;
    s_linked = false;
    return kick(JOB_ADD);
}

bool app_farm_net_reply(const app_farm_t *f, uint32_t id, bool accept)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    s_target = id;
    s_act = accept ? 1 : 0;
    return kick(JOB_REPLY);
}

bool app_farm_net_remove(const app_farm_t *f, uint32_t id)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    s_target = id;
    return kick(JOB_REMOVE);
}

bool app_farm_net_rank(const app_farm_t *f)
{
    if (app_farm_net_busy()) return false;
    snap_auth(f);
    return kick(JOB_RANK);
}

const app_farm_view_t *app_farm_net_view(void)
{
    return &s_view;
}

int app_farm_net_peers(app_farm_peer_t *out, int max)
{
    int n = s_peer_n;

    if (!out || max <= 0) return 0;
    if (n > max) n = max;
    if (n) memcpy(out, s_peers, sizeof(s_peers[0]) * (size_t)n);
    return n;
}

int app_farm_net_mail(app_farm_mail_t *out, int max)
{
    int n = s_mail_n;

    if (!out || max <= 0) return 0;
    if (n > max) n = max;
    if (n) memcpy(out, s_mail, sizeof(s_mail[0]) * (size_t)n);
    return n;
}

bool app_farm_net_already(void)
{
    return s_already;
}

bool app_farm_net_linked(void)
{
    return s_linked;
}

uint16_t app_farm_net_last_coins(void)
{
    return s_coins;
}

void app_farm_net_quota(uint8_t out[APP_FARM_ACT_N])
{
    if (out) memcpy(out, s_left, sizeof(s_left));
}

bool app_farm_net_take_token(char *out, size_t n)
{
    if (!s_have_tok || !out || n == 0) return false;
    strncpy(out, s_tok, n - 1);
    out[n - 1] = 0;
    s_have_tok = false;
    return true;
}

bool app_farm_net_take_self(app_farm_t *f)
{
    if (!s_have_self || !f) return false;
    f->level = s_self.level;
    f->xp = s_self.xp;
    f->coins = s_self.coins;
    memcpy(f->seeds, s_self.seeds, sizeof(f->seeds));
    memcpy(f->plots, s_self.plots, sizeof(f->plots));
    memcpy(f->friends, s_self.friends, sizeof(f->friends));
    f->friend_n = s_self.friend_n;
    if (s_self.name[0]) app_farm_set_name(f, s_self.name);
    s_have_self = false;
    return true;
}

void app_farm_net_clear(void)
{
    if (s_st != APP_FARM_NET_BUSY) s_st = APP_FARM_NET_IDLE;
}
