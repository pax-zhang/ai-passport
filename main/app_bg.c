#include "app_bg.h"

#include "esp_log.h"
#include "esp_partition.h"

#include <string.h>

static const char *TAG = "app_bg";
static const char MAGIC_RGB[8] = { 'D', 'I', 'A', 'L', 'B', 'G', '1', '6' };
static const char MAGIC_GIF[8] = { 'D', 'I', 'A', 'L', 'B', 'G', 'I', 'F' };

#define GIF_PAL_OFF  12
#define GIF_FR_OFF   524
#define GIF_MAX_FR   48
#define BG_STRIP     24

typedef struct {
    uint32_t off;
    uint16_t delay;
    uint16_t pad;
} bg_fr_t;

static const esp_partition_t *s_part;
static const void *s_map;
static esp_partition_mmap_handle_t s_map_h;
static lv_image_dsc_t s_dsc;
static bool s_ok;
static bool s_anim;
static size_t s_blob;
static int s_nfr;
static size_t s_wr;
static bool s_writing;
static uint16_t s_strip[APP_BG_W * BG_STRIP];
static lv_image_dsc_t s_strip_dsc;
static const uint8_t *s_linep[APP_BG_H];
static int s_line_fr = -1;

static void unmap(void)
{
    if (s_map) {
        esp_partition_munmap(s_map_h);
        s_map = NULL;
    }
    s_ok = false;
    s_anim = false;
    s_blob = 0;
    s_nfr = 0;
    s_line_fr = -1;
}

static const uint8_t *blob(void)
{
    return (const uint8_t *)s_map;
}

static const uint8_t *skip_line(const uint8_t *p, const uint8_t *end)
{
    int x = 0;
    while (x < APP_BG_W) {
        if (p + 1 >= end) return NULL;
        uint8_t c = *p++;
        p++;
        if (!c) return NULL;
        x += c;
        if (x > APP_BG_W) return NULL;
    }
    return p;
}

static size_t frame_end(const uint8_t *p, size_t n, uint32_t off)
{
    if (off >= n) return 0;
    const uint8_t *q = p + off;
    const uint8_t *end = p + n;
    for (int y = 0; y < APP_BG_H; y++) {
        q = skip_line(q, end);
        if (!q) return 0;
    }
    return (size_t)(q - p);
}

static bool gif_ok(const uint8_t *p, size_t n)
{
    if (n < GIF_FR_OFF + sizeof(bg_fr_t)) return false;
    uint16_t nfr, pal_n;
    memcpy(&nfr, p + 8, 2);
    memcpy(&pal_n, p + 10, 2);
    if (nfr < 1 || nfr > GIF_MAX_FR || pal_n < 1 || pal_n > 256) return false;
    size_t tab = GIF_FR_OFF + (size_t)nfr * sizeof(bg_fr_t);
    if (n < tab) return false;
    size_t prev = tab;
    for (uint16_t i = 0; i < nfr; i++) {
        bg_fr_t fr;
        memcpy(&fr, p + GIF_FR_OFF + (size_t)i * sizeof(fr), sizeof(fr));
        if (fr.off < prev || fr.off >= n) return false;
        size_t e = frame_end(p, n, fr.off);
        if (e < fr.off) return false;
        prev = e;
    }
    return true;
}

static void cache_lines(int frame)
{
    if (s_line_fr == frame) return;
    s_line_fr = -1;
    if (!s_anim || !s_map || frame < 0 || frame >= s_nfr) return;
    const uint8_t *p = blob();
    bg_fr_t fr;
    memcpy(&fr, p + GIF_FR_OFF + (size_t)frame * sizeof(bg_fr_t), sizeof(fr));
    const uint8_t *q = p + fr.off;
    const uint8_t *end = p + s_blob;
    for (int y = 0; y < APP_BG_H; y++) {
        s_linep[y] = q;
        q = skip_line(q, end);
        if (!q) return;
    }
    s_line_fr = frame;
}

static void remap(void)
{
    unmap();
    if (!s_part) return;
    if (esp_partition_mmap(s_part, 0, s_part->size, ESP_PARTITION_MMAP_DATA,
                           &s_map, &s_map_h) != ESP_OK) {
        s_map = NULL;
        return;
    }
    const uint8_t *p = blob();
    if (memcmp(p, MAGIC_RGB, 8) == 0) {
        s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_dsc.header.w = APP_BG_W;
        s_dsc.header.h = APP_BG_H;
        s_dsc.header.stride = APP_BG_W * 2;
        s_dsc.data_size = APP_BG_SIZE;
        s_dsc.data = p + 8;
        s_blob = 8 + APP_BG_SIZE;
        s_ok = true;
        return;
    }
    if (memcmp(p, MAGIC_GIF, 8) == 0 && gif_ok(p, s_part->size)) {
        uint16_t nfr;
        memcpy(&nfr, p + 8, 2);
        bg_fr_t last;
        memcpy(&last, p + GIF_FR_OFF + (size_t)(nfr - 1) * sizeof(bg_fr_t),
               sizeof(last));
        s_blob = frame_end(p, s_part->size, last.off);
        s_nfr = nfr;
        s_anim = true;
        s_ok = true;
        return;
    }
    esp_partition_munmap(s_map_h);
    s_map = NULL;
}

void app_bg_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                      ESP_PARTITION_SUBTYPE_ANY, "bgimg");
    if (!s_part) {
        ESP_LOGW(TAG, "no bgimg");
        return;
    }
    remap();
}

bool app_bg_ok(void)
{
    return s_ok;
}

bool app_bg_anim(void)
{
    return s_ok && s_anim;
}

int app_bg_nframes(void)
{
    return s_anim ? s_nfr : (s_ok ? 1 : 0);
}

uint32_t app_bg_delay_ms(int frame)
{
    if (!s_anim || !s_map || frame < 0 || frame >= s_nfr) return 100;
    bg_fr_t fr;
    memcpy(&fr, blob() + GIF_FR_OFF + (size_t)frame * sizeof(bg_fr_t), sizeof(fr));
    uint32_t ms = fr.delay;
    if (ms < 20) ms = 20;
    if (ms > 2000) ms = 2000;
    return ms;
}

const lv_image_dsc_t *app_bg_dsc(void)
{
    return (s_ok && !s_anim) ? &s_dsc : NULL;
}

const uint8_t *app_bg_blob(void)
{
    return s_ok ? blob() : NULL;
}

size_t app_bg_blob_len(void)
{
    return s_ok ? s_blob : 0;
}

size_t app_bg_max(void)
{
    return s_part ? s_part->size : 0;
}

static bool area_and(lv_area_t *out, const lv_area_t *a, const lv_area_t *b)
{
    out->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
    out->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
    out->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
    out->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
    return out->x1 <= out->x2 && out->y1 <= out->y2;
}

void app_bg_draw(lv_layer_t *layer, const lv_area_t *coords, int frame)
{
    if (!layer || !coords || !s_anim || !s_map) return;
    if (frame < 0 || frame >= s_nfr) frame = 0;
    lv_area_t clip;
    if (!area_and(&clip, &layer->buf_area, coords)) return;
    int y0 = (int)(clip.y1 - coords->y1);
    int y1 = (int)(clip.y2 - coords->y1);
    int x0 = (int)(clip.x1 - coords->x1);
    int x1 = (int)(clip.x2 - coords->x1);
    if (y0 < 0) y0 = 0;
    if (y1 >= APP_BG_H) y1 = APP_BG_H - 1;
    if (x0 < 0) x0 = 0;
    if (x1 >= APP_BG_W) x1 = APP_BG_W - 1;
    int h = y1 - y0 + 1;
    int w = x1 - x0 + 1;
    if (h <= 0 || w <= 0) return;
    cache_lines(frame);
    if (s_line_fr != frame) return;
    const uint16_t *pal = (const uint16_t *)(blob() + GIF_PAL_OFF);
    const uint8_t *end = blob() + s_blob;

    if (h <= BG_STRIP) {
        for (int row = 0; row < h; row++) {
            const uint8_t *p = s_linep[y0 + row];
            int x = 0;
            uint16_t *dst = &s_strip[row * w];
            while (x < APP_BG_W && p + 1 < end) {
                uint8_t c = *p++;
                uint8_t idx = *p++;
                if (!c) break;
                int x2 = x + c;
                if (x2 > APP_BG_W) x2 = APP_BG_W;
                uint16_t col = pal[idx];
                int a = x > x0 ? x : x0;
                int b = x2 - 1 < x1 ? x2 - 1 : x1;
                for (int i = a; i <= b; i++) dst[i - x0] = col;
                x = x2;
            }
        }
        memset(&s_strip_dsc, 0, sizeof(s_strip_dsc));
        s_strip_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        s_strip_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_strip_dsc.header.w = (uint32_t)w;
        s_strip_dsc.header.h = (uint32_t)h;
        s_strip_dsc.header.stride = (uint32_t)w * 2;
        s_strip_dsc.data_size = (uint32_t)w * (uint32_t)h * 2;
        s_strip_dsc.data = (const uint8_t *)s_strip;
        lv_draw_image_dsc_t id;
        lv_draw_image_dsc_init(&id);
        id.src = &s_strip_dsc;
        lv_area_t a = { coords->x1 + x0, coords->y1 + y0,
                        coords->x1 + x1, coords->y1 + y1 };
        lv_draw_image(layer, &id, &a);
        return;
    }

    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_opa = LV_OPA_COVER;
    for (int y = y0; y <= y1; y++) {
        const uint8_t *p = s_linep[y];
        int x = 0;
        while (x < APP_BG_W && p + 1 < end) {
            uint8_t c = *p++;
            uint8_t idx = *p++;
            if (!c) break;
            int x2 = x + c;
            if (x2 > APP_BG_W) x2 = APP_BG_W;
            int a = x > x0 ? x : x0;
            int b = x2 - 1 < x1 ? x2 - 1 : x1;
            if (a <= b) {
                uint16_t v = pal[idx];
                rd.bg_color = lv_color_make((uint8_t)(((v >> 11) & 31) << 3),
                                            (uint8_t)(((v >> 5) & 63) << 2),
                                            (uint8_t)((v & 31) << 3));
                lv_area_t ra = { coords->x1 + a, coords->y1 + y,
                                 coords->x1 + b, coords->y1 + y };
                lv_draw_rect(layer, &rd, &ra);
            }
            x = x2;
        }
    }
}

bool app_bg_begin(void)
{
    if (!s_part) return false;
    unmap();
    if (esp_partition_erase_range(s_part, 0, s_part->size) != ESP_OK) return false;
    s_wr = 0;
    s_writing = true;
    return true;
}

bool app_bg_feed(const void *p, size_t n)
{
    if (!s_writing || !s_part || !p) return false;
    if (s_wr + n > s_part->size) return false;
    if (esp_partition_write(s_part, s_wr, p, n) != ESP_OK) return false;
    s_wr += n;
    return true;
}

bool app_bg_finish(void)
{
    s_writing = false;
    if (s_wr < 8) {
        app_bg_clear();
        return false;
    }
    remap();
    return s_ok;
}

void app_bg_clear(void)
{
    if (!s_part) return;
    unmap();
    s_writing = false;
    s_wr = 0;
    esp_partition_erase_range(s_part, 0, s_part->size);
}
