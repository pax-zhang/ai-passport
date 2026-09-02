#include "app_notif_rule.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum {
    F_ANY = 0,
    F_TITLE,
    F_SUB,
    F_MSG,
    F_APP,
    F_NAME,
    F_CAT,
};

typedef struct {
    const char *p;
} parser_t;

static char ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
    return c;
}

static bool has_wild(const char *s)
{
    for (; *s; s++) {
        if (*s == '*' || *s == '?') return true;
    }
    return false;
}

static bool has_non_ascii(const char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s >= 0x80) return true;
    }
    return false;
}

static bool contains_ci(const char *hay, const char *needle)
{
    if (!hay || !needle || !needle[0]) return false;
    if (has_non_ascii(needle)) return strstr(hay, needle) != NULL;

    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] && ascii_lower(p[i]) == ascii_lower(needle[i])) i++;
        if (i == nlen) return true;
    }
    return false;
}

static bool equal_ci(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (ascii_lower(*a) != ascii_lower(*b)) return false;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static bool wild_match_ci(const char *pat, const char *hay)
{
    if (!pat || !hay) return false;
    if (!pat[0]) return hay[0] == 0;

    if (*pat == '*') {
        if (!pat[1]) return true;
        for (const char *h = hay; ; h++) {
            if (wild_match_ci(pat + 1, h)) return true;
            if (!*h) break;
        }
        return false;
    }
    if (!*hay) return false;
    if (*pat == '?') return wild_match_ci(pat + 1, hay + 1);
    if (ascii_lower(*pat) != ascii_lower(*hay)) return false;
    return wild_match_ci(pat + 1, hay + 1);
}

static bool pat_match(const char *pat, const char *hay)
{
    if (!pat || !pat[0]) return false;
    if (!hay) hay = "";
    if (pat[0] == '=') return equal_ci(pat + 1, hay);
    if (has_wild(pat)) return wild_match_ci(pat, hay);
    return contains_ci(hay, pat);
}

static bool field_id(const char *s, size_t n, uint8_t *out)
{
    if (!s || n == 0 || !out) return false;
    char tmp[12];
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, s, n);
    tmp[n] = 0;
    for (char *c = tmp; *c; c++) *c = (char)ascii_lower(*c);

    if (!strcmp(tmp, "any")) { *out = F_ANY; return true; }
    if (!strcmp(tmp, "title") || !strcmp(tmp, "t")) { *out = F_TITLE; return true; }
    if (!strcmp(tmp, "sub") || !strcmp(tmp, "subtitle")) { *out = F_SUB; return true; }
    if (!strcmp(tmp, "msg") || !strcmp(tmp, "message")) { *out = F_MSG; return true; }
    if (!strcmp(tmp, "app") || !strcmp(tmp, "appid")) { *out = F_APP; return true; }
    if (!strcmp(tmp, "name") || !strcmp(tmp, "appname")) { *out = F_NAME; return true; }
    if (!strcmp(tmp, "cat") || !strcmp(tmp, "category")) { *out = F_CAT; return true; }
    return false;
}

static bool cat_match(uint8_t cat, const char *pat)
{
    if (!pat || !pat[0]) return false;
    if (has_wild(pat)) {
        char num[8];
        snprintf(num, sizeof(num), "%u", (unsigned)cat);
        return pat_match(pat, num);
    }
    const char *p = pat;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        unsigned v = 0;
        int n = 0;
        if (sscanf(p, "%u%n", &v, &n) != 1 || n <= 0) return false;
        if (v == (unsigned)cat) return true;
        p += n;
    }
    return false;
}

static void field_text(const app_notif_ctx_t *ctx, uint8_t field,
                       char *out, size_t n)
{
    out[0] = 0;
    if (!ctx || n == 0) return;
    switch (field) {
    case F_TITLE:
        if (ctx->title) strlcpy(out, ctx->title, n);
        return;
    case F_SUB:
        if (ctx->subtitle) strlcpy(out, ctx->subtitle, n);
        return;
    case F_MSG:
        if (ctx->message) strlcpy(out, ctx->message, n);
        return;
    case F_APP:
        if (ctx->app_id) strlcpy(out, ctx->app_id, n);
        return;
    case F_NAME:
        if (ctx->app_name) strlcpy(out, ctx->app_name, n);
        return;
    case F_ANY:
    default:
        snprintf(out, n, "%s %s %s %s %s",
                 ctx->title ? ctx->title : "",
                 ctx->subtitle ? ctx->subtitle : "",
                 ctx->message ? ctx->message : "",
                 ctx->app_name ? ctx->app_name : "",
                 ctx->app_id ? ctx->app_id : "");
        return;
    }
}

static bool match_field(const app_notif_ctx_t *ctx, uint8_t field, const char *pat)
{
    if (field == F_CAT) return cat_match(ctx ? ctx->category : 0, pat);
    char buf[APP_NOTIF_FIELD_MAX];
    field_text(ctx, field, buf, sizeof(buf));
    return pat_match(pat, buf);
}

static void skip_ws(parser_t *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t') ps->p++;
}

static bool read_pattern(parser_t *ps, char *out, size_t n)
{
    skip_ws(ps);
    if (!*ps->p || n == 0) {
        out[0] = 0;
        return false;
    }
    if (*ps->p == '"') {
        ps->p++;
        size_t o = 0;
        while (*ps->p && *ps->p != '"') {
            if (o + 1 < n) out[o++] = *ps->p;
            ps->p++;
        }
        if (*ps->p == '"') ps->p++;
        out[o] = 0;
        return o > 0;
    }
    size_t o = 0;
    while (*ps->p && *ps->p != '&' && *ps->p != '|') {
        if (o + 1 < n) out[o++] = *ps->p;
        ps->p++;
    }
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '\t')) o--;
    out[o] = 0;
    return o > 0;
}

static bool parse_term(parser_t *ps, const app_notif_ctx_t *ctx);
static bool parse_and(parser_t *ps, const app_notif_ctx_t *ctx);
static bool parse_or(parser_t *ps, const app_notif_ctx_t *ctx);

static bool parse_term(parser_t *ps, const app_notif_ctx_t *ctx)
{
    skip_ws(ps);
    bool neg = false;
    if (*ps->p == '!') {
        neg = true;
        ps->p++;
        skip_ws(ps);
    }

    uint8_t field = F_ANY;
    const char *start = ps->p;
    const char *colon = strchr(ps->p, ':');
    if (colon) {
        const char *amp = strchr(ps->p, '&');
        const char *bar = strchr(ps->p, '|');
        if ((!amp || colon < amp) && (!bar || colon < bar) &&
            field_id(ps->p, (size_t)(colon - ps->p), &field)) {
            ps->p = colon + 1;
        }
    } else {
        (void)start;
    }

    char pat[APP_KW_LEN + 1];
    if (!read_pattern(ps, pat, sizeof(pat))) {
        if (field == F_ANY) return !neg;
        bool hit = false;
        if (field != F_CAT) {
            char buf[APP_NOTIF_FIELD_MAX];
            field_text(ctx, field, buf, sizeof(buf));
            const char *p = buf;
            while (*p == ' ' || *p == '\t') p++;
            hit = *p == 0;
        }
        return neg ? !hit : hit;
    }

    bool hit = match_field(ctx, field, pat);
    return neg ? !hit : hit;
}

static bool parse_and(parser_t *ps, const app_notif_ctx_t *ctx)
{
    bool v = parse_term(ps, ctx);
    skip_ws(ps);
    while (*ps->p == '&') {
        ps->p++;
        bool rhs = parse_term(ps, ctx);
        v = v && rhs;
        skip_ws(ps);
    }
    return v;
}

static bool parse_or(parser_t *ps, const app_notif_ctx_t *ctx)
{
    bool v = parse_and(ps, ctx);
    skip_ws(ps);
    while (*ps->p == '|') {
        ps->p++;
        bool rhs = parse_and(ps, ctx);
        v = v || rhs;
        skip_ws(ps);
    }
    return v;
}

static bool eval_expr(const char *expr, const app_notif_ctx_t *ctx)
{
    if (!expr || !expr[0]) return false;
    parser_t ps = { .p = expr };
    return parse_or(&ps, ctx);
}

app_alert_t app_notif_decide(const app_notif_ctx_t *ctx, const app_kw_t *rules,
                             int rule_n, bool dnd, app_alert_t def)
{
    app_alert_t out = def;
    if (ctx && rules && rule_n > 0) {
        for (int i = 0; i < rule_n; i++) {
            if (!rules[i].text[0]) continue;
            if (!eval_expr(rules[i].text, ctx)) continue;
            out = rules[i].prio > APP_ALERT_DROP ? APP_ALERT_URGENT
                                                   : (app_alert_t)rules[i].prio;
            break;
        }
    }
    if (dnd && out == APP_ALERT_POPUP) out = APP_ALERT_SILENT;
    return out;
}

void app_notif_rules_upgrade(app_kw_t *rules, int rule_n, uint8_t *def)
{
    if (rules) {
        for (int i = 0; i < rule_n; i++) {
            rules[i].prio = rules[i].prio ? APP_ALERT_URGENT : APP_ALERT_POPUP;
        }
    }
    if (def) *def = APP_ALERT_SILENT;
}

void app_rule_short(const char *expr, char *out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = 0;
    if (!expr || !expr[0]) return;
    size_t len = strlen(expr);
    if (len + 1 <= n) {
        memcpy(out, expr, len + 1);
        return;
    }
    size_t keep = n > 4 ? n - 4 : 0;
    if (keep > 0) memcpy(out, expr, keep);
    if (keep + 3 < n) {
        out[keep++] = '.';
        out[keep++] = '.';
        out[keep++] = '.';
    }
    out[keep] = 0;
}

static const char *const s_fkey[] = {
    "any", "title", "sub", "msg", "app", "name", "cat",
};

void app_rule_term(char *out, size_t n, int field, int op, const char *val)
{
    if (!out || n == 0) return;
    out[0] = 0;
    if (field < 0 || field > 6) field = 0;
    const char *f = s_fkey[field];
    if (op == APP_RULE_OP_EMPTY) {
        snprintf(out, n, "%s:", f);
        return;
    }
    char v[APP_KW_LEN + 1];
    v[0] = 0;
    if (val) {
        size_t o = 0;
        for (const char *p = val; *p && o + 1 < sizeof(v); p++) {
            if (*p == '&' || *p == '|') continue;
            v[o++] = *p;
        }
        v[o] = 0;
    }
    const char *neg = (op == APP_RULE_OP_NOT) ? "!" : "";
    if (op == APP_RULE_OP_HEAD) {
        snprintf(out, n, "%s%s:%s%s", neg, f, v, strchr(v, '*') ? "" : "*");
        return;
    }
    if (op == APP_RULE_OP_TAIL) {
        snprintf(out, n, "%s%s:%s%s", neg, f, v[0] == '*' ? "" : "*", v);
        return;
    }
    if (op == APP_RULE_OP_MATCH) {
        snprintf(out, n, "%s:=%.64s", f, v);
        return;
    }
    snprintf(out, n, "%s%s:%s", neg, f, v);
}

void app_rule_append(char *expr, size_t n, int join, const char *term, bool first)
{
    if (!expr || n == 0 || !term || !term[0]) return;
    if (first || !expr[0]) {
        size_t term_n = strlen(term);
        if (term_n < n) memcpy(expr, term, term_n + 1);
        return;
    }
    const char *t = term;
    const char *j = "&";
    if (join == APP_RULE_J_OR) j = "|";
    else if (join == APP_RULE_J_ANDNOT) {
        j = "&!";
        if (t[0] == '!') t++;
    } else if (join == APP_RULE_J_ORNOT) {
        j = "|!";
        if (t[0] == '!') t++;
    }
    size_t expr_n = strlen(expr);
    size_t join_n = strlen(j);
    size_t term_n = strlen(t);
    if (expr_n + join_n + term_n >= n) return;
    memcpy(expr + expr_n, j, join_n);
    memcpy(expr + expr_n + join_n, t, term_n + 1);
}
