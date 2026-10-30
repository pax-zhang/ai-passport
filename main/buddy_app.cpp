#include "buddy.h"
#include "buddy_ble.h"
#include "buddy_protocol.h"
#include "buddy_gfx.hpp"

extern "C" {
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "bsp_pm.h"
#include "esp_rom_sys.h"
#include "hal/usb_serial_jtag_ll.h"
}

#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mascot_common.h"
#include "mascot_clawd.h"
#include "mascot_dex.h"
#include "mascot_gemini.h"
#include "mascot_cursor.h"
#include "mascot_copilot.h"
#include "mascot_trae.h"
#include "mascot_qoder.h"
#include "mascot_droid.h"
#include "mascot_buddy.h"
#include "mascot_stepfun.h"
#include "mascot_opencode.h"
#include "mascot_qwen.h"
#include "mascot_antigrav.h"
#include "mascot_workbuddy.h"
#include "mascot_hermes.h"
#include "mascot_kimi.h"
#include "mascot_grok.h"
#include "mascot_cline.h"
#include "mascot_pi.h"
#include "mascot_openclaw.h"
#include "mascot_kiro.h"
#include "mascot_minimax.h"
#include "mascot_aiwork.h"
#include "mascot_mimo.h"

static const char *TAG = "buddy";

#define NVS_NS "buddy"
#define FPS_ACTIVE 25
#define FPS_SLEEP 10
#define AUTO_CYCLE_MS 8000UL
#define BLE_TIMEOUT_MS 60000UL
#define BL_IDLE_TIMEOUT_MS 30000UL
#define PAIR_CONFIRM_TIMEOUT_MS 30000UL
#define PAIR_REJECT_DELAY_MS 500UL
#define LONG_PRESS_MS 600
#define SUPER_LONG_MS 3000
#define BLIT_ROWS 20
#define CANVAS_RESERVE 28672
#define ANIM_DURATION_MS 2000
#define CELEBRATE_MS 8000
#define ACK_MS 1600
#define MAX_TOOL_HISTORY 10
#define TASK_SWITCH_GRACE_US 1000000LL
#define LOG_INTERVAL_MS 2000UL
#define CODEISLAND_QR_SIZE 33
#define CODEISLAND_QR_SCALE 4

int LCD_W_ = BSP_LCD_W;
int LCD_H_ = BSP_LCD_H;
static GFXcanvas16 s_canvas;
GFXcanvas16 *gfx = &s_canvas;
bool globalBored = false;
float globalBoredEyeOffsetX = 0.0f;

enum AppMode { MODE_ONBOARD, MODE_DEMO, MODE_AGENT, MODE_PAIR_CONFIRM };
enum TransientAnim { ANIM_NONE, ANIM_CELEBRATE, ANIM_FRUSTRATED };
enum AckKind { ACK_NONE, ACK_ALLOW, ACK_DENY, ACK_OPEN, ACK_SKIP, ACK_PAIR, ACK_DONE };

struct Mascot {
    void (*sleep)(float t);
    void (*work)(float t);
    void (*alert)(float t);
    void (*question)(float t);
    const char *name;
};

static const Mascot s_mascots[BUDDY_MASCOT_COUNT] = {
    { clawdSleep, clawdWork, clawdAlert, clawdQuestion, "Claude" },
    { dexSleep, dexWork, dexAlert, dexQuestion, "Codex" },
    { geminiSleep, geminiWork, geminiAlert, geminiQuestion, "Gemini" },
    { cursorSleep, cursorWork, cursorAlert, cursorQuestion, "Cursor" },
    { copilotSleep, copilotWork, copilotAlert, copilotQuestion, "Copilot" },
    { traeSleep, traeWork, traeAlert, traeQuestion, "Trae" },
    { qoderSleep, qoderWork, qoderAlert, qoderQuestion, "Qoder" },
    { droidSleep, droidWork, droidAlert, droidQuestion, "Factory" },
    { buddySleep, buddyWork, buddyAlert, buddyQuestion, "CodeBuddy" },
    { stepfunSleep, stepfunWork, stepfunAlert, stepfunQuestion, "StepFun" },
    { opencodeSleep, opencodeWork, opencodeAlert, opencodeQuestion, "OpenCode" },
    { qwenSleep, qwenWork, qwenAlert, qwenQuestion, "Qwen" },
    { antigravSleep, antigravWork, antigravAlert, antigravQuestion, "AntiGravity" },
    { workbuddySleep, workbuddyWork, workbuddyAlert, workbuddyQuestion, "WorkBuddy" },
    { hermesSleep, hermesWork, hermesAlert, hermesQuestion, "Hermes" },
    { kimiSleep, kimiWork, kimiAlert, kimiQuestion, "Kimi" },
    { grokSleep, grokWork, grokAlert, grokQuestion, "Grok" },
    { clineSleep, clineWork, clineAlert, clineQuestion, "Cline" },
    { piSleep, piWork, piAlert, piQuestion, "Pi" },
    { openclawSleep, openclawWork, openclawAlert, openclawQuestion, "OpenClaw" },
    { kiroSleep, kiroWork, kiroAlert, kiroQuestion, "Kiro" },
    { minimaxSleep, minimaxWork, minimaxAlert, minimaxQuestion, "MiniMax" },
    { aiworkSleep, aiworkWork, aiworkAlert, aiworkQuestion, "AIWork" },
    { mimoSleep, mimoWork, mimoAlert, mimoQuestion, "MiMo" },
};

static const char CODEISLAND_QR[CODEISLAND_QR_SIZE][CODEISLAND_QR_SIZE + 1] = {
    "000000000000000000000000000000000",
    "000000000000000000000000000000000",
    "001111111011101101000110111111100",
    "001000001010010100000010100000100",
    "001011101001110000001110101110100",
    "001011101011100011010010101110100",
    "001011101001001011100100101110100",
    "001000001001110100110100100000100",
    "001111111010101010101010111111100",
    "000000000011111001111010000000000",
    "001011011101110101111010100101100",
    "000110100100110110110110111000100",
    "000100111011111100110001010011000",
    "001111000001000000101011101000100",
    "000001011111010011011010000110000",
    "001010110011111011100100100011100",
    "001000101010000100111110100011100",
    "000000010000001011100111111001000",
    "000011101101011011011110001101000",
    "000001000111010011000000010111000",
    "001010001110111111111010111010000",
    "000011000101110111011000111010000",
    "000111011001111110011011111110000",
    "000000000010001000111010001111100",
    "001111111011001010101110101101000",
    "001000001011011110101010001100100",
    "001011101001000000010011111010000",
    "001011101010001100110101011100100",
    "001011101011010100001010010010100",
    "001000001000010111101011010101000",
    "001111111010101100001111100001000",
    "000000000000000000000000000000000",
    "000000000000000000000000000000000",
};

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile AppMode s_mode = MODE_ONBOARD;
static volatile uint8_t s_src;
static volatile uint8_t s_status;
static volatile uint8_t s_bright = 70;
static volatile uint8_t s_orient;
static volatile bool s_orient_dirty;
static volatile bool s_nvs_dirty;
static volatile bool s_paired;
static volatile bool s_auth;
static volatile bool s_reject_pending;
static volatile int64_t s_pair_t0;
static volatile int64_t s_reject_t0;
static volatile int64_t s_last_ble;
static volatile int64_t s_write_t0;
static volatile uint32_t s_write_interval = 5000;
static volatile TransientAnim s_anim = ANIM_NONE;
static volatile int64_t s_anim_t0;
static volatile uint8_t s_subagents;
static volatile uint8_t s_hour = 255;
static uint8_t s_host[BUDDY_HOST_ID_LEN];
static uint8_t s_pending[BUDDY_HOST_ID_LEN];
static char s_tool[BUDDY_TOOL_NAME_MAX + 1];
static char s_workspace[BUDDY_WORKSPACE_MAX + 1];
static char s_model[BUDDY_MODEL_NAME_MAX + 1];
static uint8_t s_act_sess, s_tot_sess, s_dur_min;
static uint16_t s_tool_calls;
static uint8_t s_heatmap[24];
static uint8_t s_heat_slot;
static bool s_heat_ready;
static struct {
    char name[12];
    bool ok;
} s_hist[MAX_TOOL_HISTORY];
static uint8_t s_hist_n;

struct TaskSlot {
    uint8_t seen;
    uint8_t status;
    char tool[BUDDY_TOOL_NAME_MAX + 1];
    char workspace[BUDDY_WORKSPACE_MAX + 1];
    char model[BUDDY_MODEL_NAME_MAX + 1];
};
static TaskSlot s_tasks[BUDDY_MASCOT_COUNT];
static volatile uint8_t s_want_src = 255;
static volatile int64_t s_want_t0;

static volatile int s_btn;
static volatile bool s_ok_down;
static volatile bool s_long_press;
static volatile int64_t s_ok_t0;
static bool s_super_fired;

static uint8_t s_mascot_idx;
static buddy_scene_t s_demo_scene = BUDDY_SCENE_SLEEP;
static int64_t s_scene_t0;
static int64_t s_interact_t0;
static volatile bool s_asleep;
static volatile bool s_wake_req;
static bool s_wake_skip;
static volatile AckKind s_ack;
static volatile int64_t s_ack_t0;
static uint8_t s_cur_bl = 70;
static uint32_t s_frames, s_loop_n;
static int64_t s_fps_t0, s_log_t0;
static float s_fps;
static uint16_t *s_blit[2];
static int s_blit_rows;
static int s_blit_i;

static int line_h(uint8_t size)
{
    return 16 * (int)(size ? size : 1);
}

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = 70;
    if (nvs_get_u8(h, "bright", &v) == ESP_OK) s_bright = buddy_clamp_brightness(v);
    v = 0;
    if (nvs_get_u8(h, "orient", &v) == ESP_OK) s_orient = buddy_clamp_orientation(v);
    v = 0;
    if (nvs_get_u8(h, "src", &v) == ESP_OK && v < BUDDY_MASCOT_COUNT) {
        s_src = v;
        s_tasks[v].seen = 1;
    }
    uint8_t ps = 0;
    size_t n = sizeof(s_host);
    if (nvs_get_u8(h, "ps", &ps) == ESP_OK && ps &&
        nvs_get_blob(h, "ph", s_host, &n) == ESP_OK && n == sizeof(s_host)) {
        s_paired = true;
    }
    nvs_close(h);
}

static void nvs_save_pair(bool paired)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (paired) {
        nvs_set_blob(h, "ph", s_host, sizeof(s_host));
        nvs_set_u8(h, "ps", 1);
    } else {
        nvs_erase_key(h, "ph");
        nvs_set_u8(h, "ps", 0);
    }
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_save_cfg(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "bright", s_bright);
    nvs_set_u8(h, "orient", s_orient);
    nvs_set_u8(h, "src", s_src);
    nvs_commit(h);
    nvs_close(h);
}

static void blit(void);

static void apply_orient(uint8_t o)
{
    esp_lcd_panel_handle_t p = bsp_display_panel();
    if (!p) return;
    bool flip = o == 1;
    esp_lcd_panel_mirror(p, flip, flip);
    if (s_canvas.getBuffer()) {
        s_canvas.fillScreen(0);
        blit();
    }
}

static void blit(void)
{
    esp_lcd_panel_handle_t p = bsp_display_panel();
    if (!p || !s_canvas.getBuffer()) return;
    int w = s_canvas.width();
    int h = s_canvas.height();
    int x0 = (BSP_LCD_W - w) / 2;
    int y0 = (BSP_LCD_H - h) / 2;
    const uint16_t *buf = s_canvas.getBuffer();
    int rows = (s_blit[0] && s_blit_rows > 0) ? s_blit_rows : 1;
    uint16_t stack_line[BSP_LCD_W];
    for (int y = 0; y < h; y += rows) {
        int n = rows;
        if (y + n > h) n = h - y;
        uint16_t *dst = s_blit[0] ? s_blit[s_blit_i] : stack_line;
        if (s_blit[0]) s_blit_i ^= 1;
        for (int r = 0; r < n; r++) {
            const uint16_t *src = buf + (y + r) * w;
            uint16_t *out = dst + r * w;
            for (int x = 0; x < w; x++) out[x] = __builtin_bswap16(src[x]);
        }
        esp_lcd_panel_draw_bitmap(p, x0, y0 + y, x0 + w, y0 + y + n, dst);
    }
}

static void tasks_clear(void)
{
    memset(s_tasks, 0, sizeof(s_tasks));
    s_want_src = 255;
}

static void task_store_meta(void)
{
    uint8_t src = s_src;
    if (src >= BUDDY_MASCOT_COUNT) return;
    s_tasks[src].seen = 1;
    memcpy(s_tasks[src].workspace, s_workspace, sizeof(s_tasks[src].workspace));
    memcpy(s_tasks[src].model, s_model, sizeof(s_tasks[src].model));
}

static void task_apply(uint8_t src)
{
    if (src >= BUDDY_MASCOT_COUNT) return;
    s_src = src;
    s_status = s_tasks[src].status;
    memcpy(s_tool, s_tasks[src].tool, sizeof(s_tool));
    memcpy(s_workspace, s_tasks[src].workspace, sizeof(s_workspace));
    memcpy(s_model, s_tasks[src].model, sizeof(s_model));
    s_nvs_dirty = true;
}

static uint8_t task_step(int dir)
{
    uint8_t ids[BUDDY_MASCOT_COUNT];
    uint8_t n = 0;
    uint8_t cur = s_src;
    for (uint8_t i = 0; i < BUDDY_MASCOT_COUNT; i++) {
        if (s_tasks[i].seen) ids[n++] = i;
    }
    if (n == 0) return cur;
    if (n == 1) return ids[0];
    int pos = 0;
    for (uint8_t i = 0; i < n; i++) {
        if (ids[i] == cur) {
            pos = i;
            break;
        }
    }
    return ids[(pos + dir + n) % n];
}

static void draw_centered(const char *text, int y, uint8_t size, uint16_t color)
{
    gfx->setTextColor(color);
    gfx->setTextSize(size);
    int max_w = LCD_W_ - 4;
    int tw = gfx->text_width(text);
    if (tw > max_w) tw = max_w;
    gfx->setCursor((LCD_W_ - tw) / 2, y);
    gfx->print_fit(text, max_w, (uint32_t)(now_us() / 1000));
}

static void draw_qr(int x, int y, uint8_t scale)
{
    int px = CODEISLAND_QR_SIZE * scale;
    gfx->fillRect(x - 4, y - 4, px + 8, px + 8, RGB565(245, 245, 245));
    for (int r = 0; r < CODEISLAND_QR_SIZE; r++) {
        for (int c = 0; c < CODEISLAND_QR_SIZE; c++) {
            if (CODEISLAND_QR[r][c] == '1') {
                gfx->fillRect(x + c * scale, y + r * scale, scale, scale, RGB565(10, 10, 14));
            }
        }
    }
}

static void draw_tool_label(int base_y)
{
    char tool[sizeof(s_tool)];
    uint8_t st;
    portENTER_CRITICAL(&s_mux);
    memcpy(tool, s_tool, sizeof(tool));
    st = s_status;
    portEXIT_CRITICAL(&s_mux);
    if (!tool[0] || st < 1 || st > 4) return;
    ToolIcon icon = classifyTool(tool);
    uint8_t size = 1;
    gfx->setTextSize(size);
    int icon_w = icon != ICON_NONE ? 10 : 0;
    int max_w = LCD_W_ - 8 - icon_w;
    int tw = gfx->text_width(tool);
    if (tw > max_w) tw = max_w;
    int x = (LCD_W_ - tw - icon_w) / 2;
    if (icon != ICON_NONE) {
        drawToolIcon(icon, x, base_y + 1, RGB565(80, 180, 220));
        x += 10;
    }
    gfx->setTextColor(RGB565(120, 120, 130));
    gfx->setCursor(x, base_y);
    gfx->print_fit(tool, max_w, (uint32_t)(now_us() / 1000));
}

static int draw_name(uint8_t idx)
{
    if (idx >= BUDDY_MASCOT_COUNT) return 4;
    char model[sizeof(s_model)];
    portENTER_CRITICAL(&s_mux);
    memcpy(model, s_model, sizeof(model));
    portEXIT_CRITICAL(&s_mux);
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565(160, 160, 170));
    gfx->setCursor(4, 4);
    gfx->print_fit(s_mascots[idx].name, LCD_W_ - 8, (uint32_t)(now_us() / 1000));
    int y = 4 + line_h(1);
    if (model[0]) {
        gfx->setTextColor(RGB565(120, 120, 130));
        gfx->setCursor(4, y);
        gfx->print_fit(model, LCD_W_ - 8, (uint32_t)(now_us() / 1000));
        y += line_h(1);
    }
    return y;
}

static bool draw_workspace(int y)
{
    char ws[sizeof(s_workspace)];
    portENTER_CRITICAL(&s_mux);
    memcpy(ws, s_workspace, sizeof(ws));
    portEXIT_CRITICAL(&s_mux);
    if (!ws[0]) return false;
    const char *p = ws;
    for (const char *q = ws; *q; q++) {
        if (*q == '/' || *q == '\\') p = q + 1;
    }
    if (!p[0]) p = ws;
    draw_centered(p, y, 1, RGB565(80, 180, 220));
    return true;
}

static void draw_bottom_hints(const char *a, uint16_t ca, const char *b, uint16_t cb)
{
    int y = LCD_H_ - line_h(1) * 2;
    draw_centered(a, y, 1, ca);
    draw_centered(b, y + line_h(1), 1, cb);
}

static void draw_hints(uint8_t st)
{
    if (st == 3) {
        draw_bottom_hints("短按允许", RGB565(50, 200, 50),
                          "长按拒绝", RGB565(200, 60, 60));
    } else if (st == 4) {
        draw_bottom_hints("短按打开", RGB565(80, 180, 255),
                          "长按跳过", RGB565(150, 150, 160));
    }
}

static bool draw_stats(int y)
{
    uint8_t a, t, d;
    uint16_t c;
    portENTER_CRITICAL(&s_mux);
    a = s_act_sess;
    t = s_tot_sess;
    c = s_tool_calls;
    d = s_dur_min;
    portEXIT_CRITICAL(&s_mux);
    if (!a && !t && !c) return false;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d场 %d次 %d分", a, t, c, d);
    draw_centered(buf, y, 1, RGB565(100, 100, 120));
    return true;
}

static void draw_task_strip(void)
{
    uint8_t st[BUDDY_MASCOT_COUNT];
    uint8_t n = 0;
    uint8_t cur = 0;
    uint8_t act;
    uint8_t src;
    portENTER_CRITICAL(&s_mux);
    src = s_src;
    act = s_act_sess;
    for (uint8_t i = 0; i < BUDDY_MASCOT_COUNT; i++) {
        if (!s_tasks[i].seen) continue;
        if (i == src) cur = n;
        st[n] = s_tasks[i].status;
        n++;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!n && !act) return;
    uint8_t show = n;
    if (act > show) show = act;
    if (show > 8) show = 8;
    if (!show) return;
    int gap = 14;
    int x0 = LCD_W_ - 10 - ((int)show - 1) * gap;
    int y = 12;
    for (uint8_t i = 0; i < show; i++) {
        uint16_t col = RGB565(70, 70, 80);
        if (i < n) {
            if (st[i] == 3) col = RGB565(220, 70, 70);
            else if (st[i] == 4) col = RGB565(80, 170, 255);
            else if (st[i] >= 1) col = RGB565(80, 200, 180);
            else col = RGB565(120, 120, 130);
        }
        int x = x0 + i * gap;
        if (i == cur && i < n) {
            gfx->fillCircle(x, y, 7, RGB565(235, 235, 245));
            gfx->fillCircle(x, y, 5, col);
        } else {
            gfx->fillCircle(x, y, 3, col);
        }
    }
}

static void draw_timeline(int y)
{
    uint8_t n;
    char names[MAX_TOOL_HISTORY][12];
    bool ok[MAX_TOOL_HISTORY];
    portENTER_CRITICAL(&s_mux);
    n = s_hist_n;
    for (uint8_t i = 0; i < MAX_TOOL_HISTORY; i++) {
        memcpy(names[i], s_hist[i].name, sizeof(names[i]));
        ok[i] = s_hist[i].ok;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!n) return;
    gfx->setTextSize(1);
    gfx->fillRect(6, y + 3, 4, 4, ok[n - 1] ? RGB565(50, 200, 50) : RGB565(200, 60, 60));
    gfx->setTextColor(RGB565(120, 120, 140));
    gfx->setCursor(14, y);
    gfx->print_fit(names[n - 1], LCD_W_ - 20, (uint32_t)(now_us() / 1000));
}

static void draw_heatmap(void)
{
    uint8_t hm[24];
    uint8_t slot;
    portENTER_CRITICAL(&s_mux);
    memcpy(hm, s_heatmap, sizeof(hm));
    slot = s_heat_slot;
    portEXIT_CRITICAL(&s_mux);
    bool any = false;
    for (int i = 0; i < 24; i++) if (hm[i]) { any = true; break; }
    if (!any) return;
    int bar_y = LCD_H_ - 8;
    int slot_w = (LCD_W_ - 4) / 24;
    for (int i = 0; i < 24; i++) {
        uint8_t val = hm[(slot + 1 + i) % 24];
        float k = val / 255.0f;
        gfx->fillRect(2 + i * slot_w, bar_y, slot_w - 1, 6,
                      RGB565((uint8_t)(20 + k * 30), (uint8_t)(40 + k * 200), (uint8_t)(80 + k * 50)));
    }
}

static void draw_status_bar(void)
{
    uint16_t col = buddy_ble_connected() ? RGB565(50, 230, 50) : RGB565(100, 100, 100);
    gfx->fillRect(LCD_W_ / 2 - 3, 0, 6, 3, col);
    if (s_mode == MODE_DEMO) {
        const char *d = "演示";
        gfx->setTextColor(RGB565(60, 60, 70));
        gfx->setTextSize(1);
        gfx->setCursor(LCD_W_ - 4 - gfx->text_width(d), 4);
        gfx->print(d);
    }
}

static void set_ack(AckKind kind, int64_t t)
{
    s_ack = kind;
    s_ack_t0 = t;
}

static bool ack_active(void)
{
    if (s_ack == ACK_NONE) return false;
    if ((now_us() - s_ack_t0) > (int64_t)ACK_MS * 1000) {
        s_ack = ACK_NONE;
        return false;
    }
    return true;
}

static void draw_ack(void)
{
    if (!ack_active()) return;
    const char *txt = "";
    uint16_t col = RGB565(50, 230, 50);
    switch (s_ack) {
    case ACK_ALLOW: txt = "已允许"; break;
    case ACK_DENY: txt = "已拒绝"; col = RGB565(200, 60, 60); break;
    case ACK_OPEN: txt = "已打开"; break;
    case ACK_SKIP: txt = "已跳过"; col = RGB565(150, 150, 160); break;
    case ACK_PAIR: txt = "已配对"; break;
    case ACK_DONE: txt = "已确认"; break;
    default: return;
    }
    draw_centered(txt, LCD_H_ - line_h(1), 1, col);
}

static void draw_pair(float t)
{
    draw_centered("配对?", 16, 2, RGB565(235, 235, 245));
    char hex[18];
    snprintf(hex, sizeof(hex), "%02X%02X%02X-%02X%02X%02X",
             s_pending[0], s_pending[1], s_pending[2],
             s_pending[3], s_pending[4], s_pending[5]);
    draw_centered(hex, 40, 1, RGB565(120, 200, 255));
    float pulse = (sinf(t * 2.5f) + 1.0f) * 0.5f;
    uint8_t g = (uint8_t)(140 + pulse * 60);
    int64_t elapsed = now_us() - s_pair_t0;
    unsigned rem = 0;
    if (elapsed < (int64_t)PAIR_CONFIRM_TIMEOUT_MS * 1000) {
        rem = (unsigned)((PAIR_CONFIRM_TIMEOUT_MS * 1000 - elapsed) / 1000000);
    }
    char tb[8];
    snprintf(tb, sizeof(tb), "%u秒", rem);
    draw_centered(tb, 58, 1, RGB565(100, 100, 120));
    draw_bottom_hints("短按同意", RGB565(50, g, 50),
                      "长按拒绝", RGB565(180, 80, 80));
}

static void draw_onboard(float t)
{
    int lh1 = line_h(1);
    draw_centered("Buddy", 8, 2, RGB565(235, 235, 245));
    draw_centered(buddy_ble_name(), 28, 1, RGB565(120, 200, 255));
    uint8_t scale = CODEISLAND_QR_SCALE;
    int qr = CODEISLAND_QR_SIZE * scale;
    int qr_y = 44;
    int foot = lh1 * 5 + 8;
    while (scale > 2 && qr_y + qr + foot > LCD_H_) {
        scale--;
        qr = CODEISLAND_QR_SIZE * scale;
    }
    draw_qr((LCD_W_ - qr) / 2, qr_y, scale);
    int y = qr_y + qr + 8;
    draw_centered("打开 CodeIsland", y, 1, RGB565(170, 170, 190));
    draw_centered("设置 > Buddy", y + lh1, 1, RGB565(120, 200, 255));
    draw_centered("用蓝牙连接", y + lh1 * 2, 1, RGB565(130, 130, 150));
    y += lh1 * 3 + 4;
    if (buddy_ble_connected()) {
        draw_centered(s_paired ? "已配对" : "已连接", y, 1, RGB565(50, 230, 50));
    } else {
        float pulse = (sinf(t * 3.0f) + 1.0f) * 0.5f;
        uint8_t g = (uint8_t)(80 + pulse * 80);
        draw_centered("等待连接", y, 1, RGB565(g, g, (uint8_t)(g + 30)));
    }
    draw_centered(s_paired ? "长按解绑" : "长按演示", LCD_H_ - lh1, 1, RGB565(60, 60, 80));
}

static void draw_celebrate(float t, uint8_t idx, int64_t start)
{
    float elapsed = (now_us() - start) / 1000000.0f;
    s_mascots[idx].work(t);
    for (int i = 0; i < 5; i++) {
        float px = 3.0f + i * 2.5f + sinf(elapsed * 3 + i) * 1.5f;
        float py = 4.0f - elapsed * 3.0f + i * 0.5f;
        float op = fmaxf(0, 1.0f - elapsed * 0.5f);
        gfx->fillRect(sx(px), sy(py), sw(1), sh(1), dim565(RGB565(255, 220, 50), op));
    }
    draw_bottom_hints("完成", dim565(RGB565(50, 230, 50), fmaxf(0.4f, 1.0f - elapsed * 0.2f)),
                      "短按确认", RGB565(160, 160, 170));
}

static void draw_frustrated(float t, uint8_t idx, int64_t start)
{
    float elapsed = (now_us() - start) / 1000000.0f;
    float shake = sinf(elapsed * 30.0f) * (1.0f - elapsed * 0.5f) * 2.0f;
    setViewportShiftX(shake);
    s_mascots[idx].work(t);
    setViewportShiftX(0);
    draw_centered("失败", LCD_H_ - line_h(1), 1, dim565(RGB565(255, 60, 60), fmaxf(0, 1.0f - elapsed * 0.5f)));
}

static void on_write(const uint8_t *data, size_t len)
{
    buddy_frame_t f;
    if (!buddy_parse_frame(data, len, &f)) return;
    int64_t t = now_us();

    if (f.kind == BUDDY_KIND_PAIR_REQ) {
        portENTER_CRITICAL(&s_mux);
        bool paired = s_paired;
        bool match = paired && buddy_host_id_eq(s_host, f.host_id);
        bool confirming = s_mode == MODE_PAIR_CONFIRM;
        if (match) {
            s_auth = true;
            s_mode = MODE_AGENT;
            s_last_ble = t;
            s_interact_t0 = t;
            s_wake_req = true;
            portEXIT_CRITICAL(&s_mux);
            buddy_ble_notify(BUDDY_PAIR_ACCEPTED);
            ESP_LOGI(TAG, "pair accepted known host");
        } else if (!paired && !confirming) {
            memcpy(s_pending, f.host_id, BUDDY_HOST_ID_LEN);
            s_pair_t0 = t;
            s_mode = MODE_PAIR_CONFIRM;
            s_interact_t0 = t;
            s_wake_req = true;
            portEXIT_CRITICAL(&s_mux);
            buddy_ble_notify(BUDDY_PAIR_PENDING);
            ESP_LOGI(TAG, "pair pending");
        } else if (!paired && confirming) {
            if (!buddy_host_id_eq(s_pending, f.host_id)) {
                memcpy(s_pending, f.host_id, BUDDY_HOST_ID_LEN);
            }
            s_interact_t0 = t;
            s_wake_req = true;
            portEXIT_CRITICAL(&s_mux);
            buddy_ble_notify(BUDDY_PAIR_PENDING);
        } else {
            portEXIT_CRITICAL(&s_mux);
            buddy_ble_notify(BUDDY_PAIR_REJECTED);
            s_reject_pending = true;
            s_reject_t0 = t;
            ESP_LOGI(TAG, "pair rejected");
        }
        return;
    }

    if (f.kind == BUDDY_KIND_UNPAIR) {
        portENTER_CRITICAL(&s_mux);
        bool ok = s_paired && buddy_host_id_eq(s_host, f.host_id);
        if (ok) {
            memset(s_host, 0, sizeof(s_host));
            s_paired = false;
            s_auth = false;
            s_mode = MODE_ONBOARD;
            s_status = 0;
            s_tool[0] = 0;
            s_workspace[0] = 0;
            s_model[0] = 0;
            tasks_clear();
        }
        portEXIT_CRITICAL(&s_mux);
        if (ok) nvs_save_pair(false);
        return;
    }

    if (!s_auth && !s_paired) return;

    switch (f.kind) {
    case BUDDY_KIND_BRIGHTNESS:
        s_bright = f.brightness;
        s_nvs_dirty = true;
        s_interact_t0 = t;
        s_wake_req = true;
        break;
    case BUDDY_KIND_ORIENTATION:
        if (s_orient != f.orientation) {
            s_orient = f.orientation;
            s_orient_dirty = true;
            s_nvs_dirty = true;
        }
        s_interact_t0 = t;
        s_wake_req = true;
        break;
    case BUDDY_KIND_WORKSPACE:
        portENTER_CRITICAL(&s_mux);
        memcpy(s_workspace, f.workspace, sizeof(s_workspace));
        task_store_meta();
        s_last_ble = t;
        portEXIT_CRITICAL(&s_mux);
        break;
    case BUDDY_KIND_MODEL:
        portENTER_CRITICAL(&s_mux);
    memcpy(s_model, f.model, sizeof(s_model));
        task_store_meta();
        s_last_ble = t;
        portEXIT_CRITICAL(&s_mux);
        break;
    case BUDDY_KIND_STATS:
        portENTER_CRITICAL(&s_mux);
        if (!s_heat_ready || s_hour == 255 || f.tool_calls < s_tool_calls) {
            s_heat_ready = true;
        } else if (f.tool_calls > s_tool_calls) {
            uint16_t dlt = f.tool_calls - s_tool_calls;
            uint16_t nv = s_heatmap[s_heat_slot] + dlt;
            s_heatmap[s_heat_slot] = nv > 255 ? 255 : (uint8_t)nv;
        }
        s_act_sess = f.active_sessions;
        s_tot_sess = f.total_sessions;
        s_tool_calls = f.tool_calls;
        s_dur_min = f.duration_min;
        s_last_ble = t;
        portEXIT_CRITICAL(&s_mux);
        break;
    case BUDDY_KIND_SUBAGENT:
        s_subagents = f.subagent_count;
        s_last_ble = t;
        break;
    case BUDDY_KIND_EVENT:
        s_last_ble = t;
        if (f.event_id == 1) {
            s_anim = ANIM_CELEBRATE;
            s_anim_t0 = t;
            s_interact_t0 = t;
            s_wake_req = true;
        } else if (f.event_id == 2) {
            s_anim = ANIM_FRUSTRATED;
            s_anim_t0 = t;
            s_interact_t0 = t;
            s_wake_req = true;
        }
        break;
    case BUDDY_KIND_TIME_HINT:
        portENTER_CRITICAL(&s_mux);
        if (s_hour != 255 && f.hour != s_hour) {
            s_heat_slot = f.hour % 24;
            s_heatmap[s_heat_slot] = 0;
        } else if (s_hour == 255) {
            s_heat_slot = f.hour % 24;
        }
        s_hour = f.hour;
        s_last_ble = t;
        portEXIT_CRITICAL(&s_mux);
        break;
    case BUDDY_KIND_MSG_PREVIEW:
        s_last_ble = t;
        break;
    case BUDDY_KIND_TOOL_HIST_CLEAR:
        portENTER_CRITICAL(&s_mux);
        s_hist_n = 0;
        s_last_ble = t;
        portEXIT_CRITICAL(&s_mux);
        break;
    case BUDDY_KIND_TOOL_HIST:
        portENTER_CRITICAL(&s_mux);
        if (f.hist_index == 0) s_hist_n = 0;
        if (s_hist_n < MAX_TOOL_HISTORY) {
            memset(s_hist[s_hist_n].name, 0, sizeof(s_hist[0].name));
            memcpy(s_hist[s_hist_n].name, f.hist_name, f.hist_name_len);
            s_hist[s_hist_n].ok = f.hist_success;
            s_hist_n++;
        }
        s_last_ble = t;
        portEXIT_CRITICAL(&s_mux);
        break;
    case BUDDY_KIND_AGENT:
        portENTER_CRITICAL(&s_mux);
        if (s_want_src != 255 && f.source_id != s_want_src) {
            if ((t - s_want_t0) < TASK_SWITCH_GRACE_US) {
                if (f.source_id < BUDDY_MASCOT_COUNT) {
                    s_tasks[f.source_id].seen = 1;
                    s_tasks[f.source_id].status = f.status_id;
                    memcpy(s_tasks[f.source_id].tool, f.tool, sizeof(s_tasks[f.source_id].tool));
                }
                s_last_ble = t;
                portEXIT_CRITICAL(&s_mux);
                break;
            }
            s_want_src = 255;
        }
        if (f.source_id == s_want_src) s_want_src = 255;
        if (f.source_id < BUDDY_MASCOT_COUNT && s_src != f.source_id) {
            s_src = f.source_id;
            s_nvs_dirty = true;
            s_wake_req = true;
        }
        s_status = f.status_id;
        memcpy(s_tool, f.tool, sizeof(s_tool));
        if (f.source_id < BUDDY_MASCOT_COUNT) {
            s_tasks[f.source_id].seen = 1;
            s_tasks[f.source_id].status = f.status_id;
            memcpy(s_tasks[f.source_id].tool, f.tool, sizeof(s_tasks[f.source_id].tool));
        }
        s_last_ble = t;
        s_mode = MODE_AGENT;
        if (s_write_t0) s_write_interval = (uint32_t)((t - s_write_t0) / 1000);
        s_write_t0 = t;
        s_interact_t0 = t;
        if (f.status_id >= 3) s_wake_req = true;
        portEXIT_CRITICAL(&s_mux);
        break;
    default:
        break;
    }
}

static uint8_t bl_pct(uint8_t base255)
{
    uint16_t v = (uint16_t)base255 * s_bright / 70;
    if (v > 255) v = 255;
    if (v < 1) v = 1;
    return (uint8_t)((v * 100 + 127) / 255);
}

static bool keep_awake(void)
{
    if (s_mode == MODE_PAIR_CONFIRM) return true;
    if (s_mode != MODE_AGENT) return false;
    buddy_scene_t sc = buddy_status_to_scene(s_status);
    return sc == BUDDY_SCENE_ALERT || sc == BUDDY_SCENE_QUESTION ||
           s_anim != ANIM_NONE || s_ack != ACK_NONE;
}

static void screen_wake(int64_t t)
{
    s_interact_t0 = t;
    s_wake_req = false;
    if (!s_asleep) return;
    s_asleep = false;
    bsp_pm_set_sleeping(false);
    bsp_button_sleep_gpio(false);
    bsp_display_sleep(false);
    s_cur_bl = 0;
}

static void screen_sleep(void)
{
    if (s_asleep) return;
    s_asleep = true;
    s_cur_bl = 0;
    bsp_display_backlight(0);
    bsp_display_sleep(true);
    bsp_button_sleep_gpio(true);
    bsp_pm_set_sleeping(true);
}

static void handle_btn(int btn, int64_t t)
{
    s_interact_t0 = t;
    if (s_mode == MODE_PAIR_CONFIRM) {
        if (btn == 1) {
            portENTER_CRITICAL(&s_mux);
            memcpy(s_host, s_pending, BUDDY_HOST_ID_LEN);
            s_paired = true;
            s_auth = true;
            s_mode = MODE_AGENT;
            s_last_ble = t;
            portEXIT_CRITICAL(&s_mux);
            nvs_save_pair(true);
            buddy_ble_notify(BUDDY_PAIR_ACCEPTED);
            s_interact_t0 = t;
            set_ack(ACK_PAIR, t);
            ESP_LOGI(TAG, "pair accepted");
        } else if (btn == 2) {
            buddy_ble_notify(BUDDY_PAIR_REJECTED);
            s_reject_pending = true;
            s_reject_t0 = t;
            s_mode = MODE_ONBOARD;
            ESP_LOGI(TAG, "pair rejected");
        }
        return;
    }

    if (s_mode == MODE_AGENT) {
        if (s_anim == ANIM_CELEBRATE && btn == 1) {
            s_anim = ANIM_NONE;
            set_ack(ACK_DONE, t);
            if (buddy_ble_connected()) buddy_ble_notify(s_src);
            return;
        }
        buddy_scene_t sc = buddy_status_to_scene(s_status);
        if (btn == 3 || btn == 4) {
            uint8_t next = task_step(btn == 4 ? 1 : -1);
            if (next != s_src) {
                portENTER_CRITICAL(&s_mux);
                task_apply(next);
                s_want_src = next;
                s_want_t0 = t;
                portEXIT_CRITICAL(&s_mux);
                if (buddy_ble_connected()) buddy_ble_notify(next);
            }
        } else if ((sc == BUDDY_SCENE_ALERT || sc == BUDDY_SCENE_QUESTION) && buddy_ble_connected()) {
            if (btn == 1) {
                buddy_ble_notify(s_status == 3 ? BUDDY_APPROVE : s_src);
                set_ack(s_status == 3 ? ACK_ALLOW : ACK_OPEN, t);
            } else if (btn == 2) {
                buddy_ble_notify(s_status == 3 ? BUDDY_DENY : BUDDY_SKIP);
                set_ack(s_status == 3 ? ACK_DENY : ACK_SKIP, t);
            }
        } else if (btn == 1 && buddy_ble_connected()) {
            buddy_ble_notify(s_src);
        }
        return;
    }

    if (btn == 1 || btn == 4) {
        s_interact_t0 = t;
        if (s_mode == MODE_DEMO) {
            s_mascot_idx = (uint8_t)((s_mascot_idx + 1) % BUDDY_MASCOT_COUNT);
            s_scene_t0 = t;
        }
    } else if (btn == 3) {
        s_interact_t0 = t;
        if (s_mode == MODE_DEMO) {
            s_mascot_idx = (uint8_t)((s_mascot_idx + BUDDY_MASCOT_COUNT - 1) % BUDDY_MASCOT_COUNT);
            s_scene_t0 = t;
        }
    } else if (btn == 2) {
        s_interact_t0 = t;
        s_mode = (s_mode == MODE_ONBOARD) ? MODE_DEMO : MODE_ONBOARD;
        s_scene_t0 = t;
    }
}

static void loop_once(void)
{
    int64_t t = now_us();
    s_loop_n++;

    if (s_wake_req || keep_awake()) screen_wake(t);
    if (s_asleep) {
        if (s_mode == MODE_PAIR_CONFIRM &&
            (t - s_pair_t0) > (int64_t)PAIR_CONFIRM_TIMEOUT_MS * 1000) {
            buddy_ble_notify(BUDDY_PAIR_REJECTED);
            s_reject_pending = true;
            s_reject_t0 = t;
            s_mode = MODE_ONBOARD;
        }
        if (s_reject_pending && (t - s_reject_t0) > (int64_t)PAIR_REJECT_DELAY_MS * 1000) {
            s_reject_pending = false;
            if (buddy_ble_connected()) buddy_ble_disconnect();
        }
        if (s_mode == MODE_AGENT && (t - s_last_ble) > (int64_t)BLE_TIMEOUT_MS * 1000) {
            if (!s_paired) s_mode = MODE_ONBOARD;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }

    if (s_orient_dirty) {
        s_orient_dirty = false;
        apply_orient(s_orient);
    }

    bool sleepy = (s_mode == MODE_DEMO && s_demo_scene == BUDDY_SCENE_SLEEP) ||
                  (s_mode == MODE_AGENT && buddy_status_to_scene(s_status) == BUDDY_SCENE_SLEEP) ||
                  s_mode == MODE_ONBOARD || s_mode == MODE_PAIR_CONFIRM;
    uint32_t interval = sleepy ? (1000 / FPS_SLEEP) : (1000 / FPS_ACTIVE);
    static int64_t last_frame;
    if ((t - last_frame) < (int64_t)interval * 1000) {
        vTaskDelay(1);
        return;
    }
    last_frame = t;
    s_frames++;
    if ((t - s_fps_t0) >= 1000000) {
        s_fps = s_frames * 1000000.0f / (float)(t - s_fps_t0);
        s_frames = 0;
        s_fps_t0 = t;
    }

    float tf = t / 1000000.0f;

    if (s_ok_down && !s_long_press &&
        (t - s_ok_t0) >= (int64_t)LONG_PRESS_MS * 1000) {
        s_long_press = true;
        handle_btn(2, t);
    }

    if (s_ok_down && !s_super_fired &&
        (t - s_ok_t0) >= (int64_t)SUPER_LONG_MS * 1000 &&
        s_mode != MODE_PAIR_CONFIRM) {
        buddy_scene_t sc = buddy_status_to_scene(s_status);
        bool busy = s_mode == MODE_AGENT &&
                    (sc == BUDDY_SCENE_ALERT || sc == BUDDY_SCENE_QUESTION);
        if (!busy) {
            s_super_fired = true;
            if (s_paired) {
                portENTER_CRITICAL(&s_mux);
                memset(s_host, 0, sizeof(s_host));
                s_paired = false;
                s_auth = false;
                s_mode = MODE_ONBOARD;
                s_status = 0;
                s_tool[0] = 0;
                s_workspace[0] = 0;
                s_model[0] = 0;
                tasks_clear();
                portEXIT_CRITICAL(&s_mux);
                nvs_save_pair(false);
                if (buddy_ble_connected()) buddy_ble_disconnect();
                ESP_LOGI(TAG, "unpair hold");
            }
        }
    }

    int btn = s_btn;
    s_btn = 0;
    if (btn) handle_btn(btn, t);

    if (s_mode == MODE_PAIR_CONFIRM &&
        (t - s_pair_t0) > (int64_t)PAIR_CONFIRM_TIMEOUT_MS * 1000) {
        buddy_ble_notify(BUDDY_PAIR_REJECTED);
        s_reject_pending = true;
        s_reject_t0 = t;
        s_mode = MODE_ONBOARD;
    }
    if (s_reject_pending && (t - s_reject_t0) > (int64_t)PAIR_REJECT_DELAY_MS * 1000) {
        s_reject_pending = false;
        if (buddy_ble_connected()) buddy_ble_disconnect();
    }
    static bool ble_to;
    bool timed = s_mode == MODE_AGENT &&
                 (t - s_last_ble) > (int64_t)BLE_TIMEOUT_MS * 1000;
    if (timed) {
        if (!ble_to) {
            ble_to = true;
            if (!s_paired) s_mode = MODE_ONBOARD;
            ESP_LOGI(TAG, "ble timeout");
        }
    } else {
        ble_to = false;
    }

    if (s_wake_req || keep_awake()) screen_wake(t);
    if (!s_asleep && !keep_awake() &&
        (t - s_interact_t0) > (int64_t)BL_IDLE_TIMEOUT_MS * 1000) {
        screen_sleep();
    }
    if (s_asleep) {
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }

    uint8_t target;
    if (s_mode == MODE_PAIR_CONFIRM) {
        target = bl_pct(180);
    } else if (s_mode == MODE_AGENT && buddy_ble_connected()) {
        target = buddy_status_to_scene(s_status) == BUDDY_SCENE_SLEEP ? bl_pct(80) : bl_pct(180);
    } else if (s_mode == MODE_ONBOARD) {
        target = bl_pct(80);
    } else {
        target = s_demo_scene == BUDDY_SCENE_SLEEP ? bl_pct(80) : bl_pct(180);
    }
    if (s_hour != 255) {
        if (s_hour >= 22 || s_hour < 6) {
            uint8_t idle = bl_pct(40);
            if (target > idle) target = idle;
        } else if (s_hour >= 18) {
            uint8_t sl = bl_pct(80);
            if (target > sl) target = sl;
        }
    }
    if (s_cur_bl != target) {
        if (s_cur_bl < target) s_cur_bl = (uint8_t)(s_cur_bl + (s_cur_bl + 3 > target ? target - s_cur_bl : 3));
        else s_cur_bl = (uint8_t)(s_cur_bl - (s_cur_bl < target + 3 ? s_cur_bl - target : 3));
        bsp_display_backlight(s_cur_bl);
    }

    gfx->fillScreen(0);
    if (s_mode == MODE_PAIR_CONFIRM) {
        draw_pair(tf);
    } else if (s_mode == MODE_ONBOARD) {
        draw_onboard(tf);
    } else {
        uint8_t idx;
        buddy_scene_t scene;
        if (s_mode == MODE_DEMO) {
            idx = s_mascot_idx;
            if ((t - s_scene_t0) >= (int64_t)AUTO_CYCLE_MS * 1000) {
                s_demo_scene = (buddy_scene_t)((s_demo_scene + 1) % BUDDY_SCENE_COUNT);
                if (s_demo_scene == BUDDY_SCENE_SLEEP) {
                    s_mascot_idx = (uint8_t)((s_mascot_idx + 1) % BUDDY_MASCOT_COUNT);
                }
                s_scene_t0 = t;
                idx = s_mascot_idx;
            }
            scene = s_demo_scene;
        } else {
            idx = s_src < BUDDY_MASCOT_COUNT ? s_src : 0;
            scene = buddy_status_to_scene(s_status);
        }
        draw_status_bar();
        int name_y = draw_name(idx);
        if (s_mode == MODE_AGENT) draw_task_strip();

        bool transient = false;
        TransientAnim anim = s_anim;
        int64_t anim_t = s_anim_t0;
        int64_t anim_ms = (anim == ANIM_CELEBRATE) ? CELEBRATE_MS : ANIM_DURATION_MS;
        if (anim != ANIM_NONE && (t - anim_t) < anim_ms * 1000) {
            transient = true;
            if (anim == ANIM_CELEBRATE) draw_celebrate(tf, idx, anim_t);
            else if (anim == ANIM_FRUSTRATED) draw_frustrated(tf, idx, anim_t);
        } else {
            if (anim != ANIM_NONE) s_anim = ANIM_NONE;
            if (scene == BUDDY_SCENE_SLEEP) {
                globalBored = (t - s_last_ble) > 300000000LL;
                if (globalBored) {
                    float c = fmodf(tf, 8.0f);
                    if (c < 1.0f) globalBoredEyeOffsetX = -1.0f;
                    else if (c > 3.0f && c < 4.0f) globalBoredEyeOffsetX = 1.0f;
                    else globalBoredEyeOffsetX = 0;
                } else {
                    globalBoredEyeOffsetX = 0;
                }
            } else {
                globalBored = false;
                globalBoredEyeOffsetX = 0;
            }
            float scale = 1.0f;
            if (scene == BUDDY_SCENE_WORK) {
                if (s_write_interval < 500) scale = 2.0f;
                else if (s_write_interval < 2000) scale = 1.5f;
            }
            const Mascot &m = s_mascots[idx];
            switch (scene) {
            case BUDDY_SCENE_SLEEP: m.sleep(tf); break;
            case BUDDY_SCENE_WORK: m.work(tf * scale); break;
            case BUDDY_SCENE_ALERT: m.alert(tf); break;
            case BUDDY_SCENE_QUESTION: m.question(tf); break;
            default: break;
            }
            if (scene == BUDDY_SCENE_WORK && s_subagents) drawSubagentDots(s_subagents, 0);
        }

        if (s_mode == MODE_AGENT && !transient) {
            int lh1 = line_h(1);
            int bot = LCD_H_ - lh1;
            bool ack = ack_active();
            if (scene == BUDDY_SCENE_SLEEP) {
                draw_workspace(name_y);
                if (!draw_stats(bot)) {
                    draw_timeline(bot);
                    if (!ack) draw_heatmap();
                }
            } else if (scene == BUDDY_SCENE_ALERT || scene == BUDDY_SCENE_QUESTION) {
                int y = name_y;
                if (draw_workspace(y)) y += lh1;
                draw_tool_label(y);
                if (!ack) draw_hints(s_status);
            } else {
                draw_workspace(LCD_H_ - lh1 * 2);
                if (!ack) draw_tool_label(bot);
            }
        }
        draw_ack();
    }
    blit();

    static int64_t nvs_t0;
    if ((t - nvs_t0) > 5000000 && s_nvs_dirty) {
        s_nvs_dirty = false;
        nvs_t0 = t;
        nvs_save_cfg();
    }

    if ((t - s_log_t0) >= (int64_t)LOG_INTERVAL_MS * 1000) {
        s_log_t0 = t;
        const char *modes[] = { "ONBOARD", "DEMO", "AGENT", "PAIR" };
        ESP_LOGI(TAG, "up=%lus fps=%.1f heap=%u bl=%u%% mode=%s ble=%s paired=%s",
                 (unsigned long)(t / 1000000), s_fps,
                 (unsigned)esp_get_free_heap_size(), s_cur_bl,
                 modes[s_mode], buddy_ble_connected() ? "on" : "off",
                 s_paired ? "yes" : "no");
        if (s_mode == MODE_AGENT) {
            ESP_LOGI(TAG, "agent=%s(#%u) status=%u tool=%s",
                     s_src < BUDDY_MASCOT_COUNT ? s_mascots[s_src].name : "?",
                     s_src, s_status, s_tool);
        } else if (s_mode == MODE_DEMO) {
            ESP_LOGI(TAG, "mascot=%s scene=%u", s_mascots[s_mascot_idx].name, s_demo_scene);
        }
    }
}

static void buddy_task(void *arg)
{
    (void)arg;
    s_interact_t0 = now_us();
    s_scene_t0 = s_interact_t0;
    s_fps_t0 = s_interact_t0;
    s_log_t0 = s_interact_t0;
    for (;;) loop_once();
}

static void on_gpio_wake(void)
{
    s_wake_req = true;
    s_wake_skip = true;
}

void buddy_on_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (s_asleep) {
        if (ev == BSP_BTN_PRESS || ev == BSP_BTN_CLICK) {
            s_wake_req = true;
            s_wake_skip = true;
        }
        return;
    }
    if (s_wake_skip) {
        if (ev == BSP_BTN_CLICK || ev == BSP_BTN_RELEASE) s_wake_skip = false;
        return;
    }
    if (btn == BSP_BTN_OK) {
        if (ev == BSP_BTN_PRESS) {
            s_ok_down = true;
            s_ok_t0 = now_us();
            s_long_press = false;
            s_super_fired = false;
        } else if (ev == BSP_BTN_RELEASE) {
            s_ok_down = false;
        } else if (ev == BSP_BTN_LONG) {
            return;
        } else if (ev == BSP_BTN_CLICK && !s_long_press) {
            s_btn = 1;
        }
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP) s_btn = 3;
    if (btn == BSP_BTN_DOWN) s_btn = 4;
}

static void on_link(bool connected)
{
    portENTER_CRITICAL(&s_mux);
    if (connected) {
        s_auth = s_paired;
        if (s_paired) s_mode = MODE_AGENT;
    } else {
        s_auth = false;
        if (s_mode == MODE_PAIR_CONFIRM) s_mode = MODE_ONBOARD;
    }
    portEXIT_CRITICAL(&s_mux);
}

static bool canvas_try(int w, int h, size_t reserve)
{
    size_t need = (size_t)w * (size_t)h * 2;
    size_t blk = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (blk < need + reserve) return false;
    return s_canvas.begin(w, h);
}

static void usb_write(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    int spins = 0;
    while (len) {
        if (!usb_serial_jtag_ll_txfifo_writable()) {
            if (++spins > 64) {
                vTaskDelay(1);
                spins = 0;
            } else {
                esp_rom_delay_us(50);
            }
            continue;
        }
        spins = 0;
        int n = usb_serial_jtag_ll_write_txfifo(p, len);
        usb_serial_jtag_ll_txfifo_flush();
        if (n <= 0) continue;
        p += (size_t)n;
        len -= (size_t)n;
    }
    usb_serial_jtag_ll_txfifo_flush();
}

static void screenshot_dump(void)
{
    const uint16_t *buf = s_canvas.getBuffer();
    int w = s_canvas.width();
    int h = s_canvas.height();
    if (!buf || w <= 0 || h <= 0) return;
    size_t need = (size_t)BSP_LCD_W * (size_t)BSP_LCD_H * 2;
    char hdr[64];
    int n = snprintf(hdr, sizeof(hdr), "FAP_SCREENSHOT_V1 %d %d RGB565LE %u\n",
                     BSP_LCD_W, BSP_LCD_H, (unsigned)need);
    if (n > 0) usb_write(hdr, (size_t)n);
    int x0 = (BSP_LCD_W - w) / 2;
    int y0 = (BSP_LCD_H - h) / 2;
    uint16_t zrow[BSP_LCD_W];
    memset(zrow, 0, sizeof(zrow));
    for (int y = 0; y < BSP_LCD_H; y++) {
        if (y < y0 || y >= y0 + h) {
            usb_write(zrow, sizeof(zrow));
            continue;
        }
        if (x0 == 0 && w == BSP_LCD_W) {
            usb_write(buf + (y - y0) * w, (size_t)w * 2);
            continue;
        }
        uint16_t row[BSP_LCD_W];
        memset(row, 0, sizeof(row));
        memcpy(row + x0, buf + (y - y0) * w, (size_t)w * 2);
        usb_write(row, sizeof(row));
    }
}

static void screenshot_task(void *arg)
{
    (void)arg;
    char line[24];
    int n = 0;
    uint8_t c;
    for (;;) {
        if (!usb_serial_jtag_ll_rxfifo_data_available()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (usb_serial_jtag_ll_read_rxfifo(&c, 1) <= 0) continue;
        if (c == '\n' || c == '\r') {
            line[n] = 0;
            if (n && strcmp(line, "FAP_SCREENSHOT_V1") == 0) screenshot_dump();
            n = 0;
            continue;
        }
        if (n < (int)sizeof(line) - 1) line[n++] = (char)c;
        else n = 0;
    }
}

void buddy_start(void)
{
    nvs_load();
    if (s_paired) s_mode = MODE_AGENT;
    if (buddy_ble_init(on_write) != ESP_OK) {
        ESP_LOGE(TAG, "ble init failed");
    } else {
        buddy_ble_set_link_cb(on_link);
        for (int i = 0; i < 50 && !buddy_ble_ready(); i++) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    if (!canvas_try(BSP_LCD_W, BSP_LCD_H, CANVAS_RESERVE) &&
        !canvas_try(172, 320, CANVAS_RESERVE) &&
        !canvas_try(160, 200, 16 * 1024)) {
        ESP_LOGE(TAG, "canvas alloc failed");
        return;
    }
    LCD_W_ = s_canvas.width();
    LCD_H_ = s_canvas.height();

    s_blit_rows = BLIT_ROWS;
    size_t blit_n = (size_t)LCD_W_ * (size_t)s_blit_rows * 2;
    s_blit[0] = (uint16_t *)heap_caps_malloc(blit_n, MALLOC_CAP_DMA);
    s_blit[1] = (uint16_t *)heap_caps_malloc(blit_n, MALLOC_CAP_DMA);
    if (!s_blit[0] || !s_blit[1]) {
        heap_caps_free(s_blit[0]);
        heap_caps_free(s_blit[1]);
        s_blit[0] = s_blit[1] = nullptr;
        s_blit_rows = 8;
        blit_n = (size_t)LCD_W_ * (size_t)s_blit_rows * 2;
        s_blit[0] = (uint16_t *)heap_caps_malloc(blit_n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        s_blit[1] = (uint16_t *)heap_caps_malloc(blit_n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_blit[0] || !s_blit[1]) {
        heap_caps_free(s_blit[0]);
        heap_caps_free(s_blit[1]);
        s_blit[0] = s_blit[1] = nullptr;
        s_blit_rows = 0;
    }

    apply_orient(s_orient);
    bsp_display_backlight(s_bright);
    bsp_button_set_wake_cb(on_gpio_wake);
    xTaskCreate(screenshot_task, "fap_shot", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "Buddy %s canvas=%dx%d blit=%d heap=%u",
             buddy_ble_name(), LCD_W_, LCD_H_, s_blit_rows,
             (unsigned)esp_get_free_heap_size());
    xTaskCreate(buddy_task, "buddy", 12288, NULL, 5, NULL);
}
