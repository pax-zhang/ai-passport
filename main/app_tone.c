#include "app_tone.h"

#include "app_prefs.h"
#include "bsp_audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include "app_tone_meow.inc"

#define SAMPLE_RATE 16000
#define CHUNK       256
/* codec open + I2S write needs the same 4 KB stack as demo_audio */
#define STACK_BYTES 4096

typedef struct {
    int16_t hz;
    int16_t ms;
} tone_job_t;

static QueueHandle_t s_q;
static TaskHandle_t s_task;
static int16_t s_pcm[CHUNK];
static bool s_held;

static void beep(int hz, int ms, int amp)
{
    int period = hz > 0 ? SAMPLE_RATE / hz : 2;
    int total = SAMPLE_RATE * ms / 1000;
    int phase = 0;
    while (total > 0) {
        int n = total < CHUNK ? total : CHUNK;
        for (int i = 0; i < n; i++) {
            s_pcm[i] = (int16_t)((phase < period / 2) ? amp : -amp);
            if (++phase >= period) phase = 0;
        }
        bsp_audio_write(s_pcm, (size_t)n * sizeof(int16_t));
        total -= n;
    }
}

/* 预渲染 16 kHz 8-bit「miao」共振峰采样,避免 C3 无 FPU 时实时滑音。 */
static void meow(void)
{
    int off = 0;
    int ntot = (int)sizeof(s_meow);

    while (off < ntot) {
        int n = ntot - off;
        if (n > CHUNK) n = CHUNK;
        for (int i = 0; i < n; i++)
            s_pcm[i] = (int16_t)(s_meow[off + i] * 56);
        bsp_audio_write(s_pcm, (size_t)n * sizeof(int16_t));
        off += n;
    }
}

static void silence(int ms)
{
    int total = SAMPLE_RATE * ms / 1000;
    while (total > 0) {
        int n = total < CHUNK ? total : CHUNK;
        for (int i = 0; i < n; i++) s_pcm[i] = 0;
        bsp_audio_write(s_pcm, (size_t)n * sizeof(int16_t));
        total -= n;
    }
}

static void play_id(int id)
{
    if (id == APP_TONE_OFF) return;
    if (app_prefs()->muted) return;
    if (bsp_audio_set_format(SAMPLE_RATE, 16, 1) != ESP_OK) return;
    bsp_audio_set_volume(app_prefs()->volume);
    switch (id) {
    case APP_TONE_BEEP:
        beep(880, 120, 5000);
        break;
    case APP_TONE_DOUBLE:
        beep(880, 90, 5000);
        silence(60);
        beep(880, 90, 5000);
        break;
    case APP_TONE_CHIME:
        beep(660, 100, 4500);
        beep(880, 140, 4500);
        break;
    case APP_TONE_TRIPLE:
        for (int i = 0; i < 3; i++) {
            beep(1200, 70, 6000);
            silence(50);
        }
        break;
    case APP_TONE_ALARM:
        beep(440, 160, 7000);
        beep(880, 160, 7000);
        beep(440, 200, 7000);
        break;
    case APP_TONE_MEOW:
        meow();
        break;
    default:
        break;
    }
    if (!s_held) bsp_audio_standby();
}

static void play_note(int hz, int ms)
{
    if (hz < 1 || ms < 1) return;
    if (app_prefs()->muted) return;
    if (bsp_audio_set_format(SAMPLE_RATE, 16, 1) != ESP_OK) return;
    bsp_audio_set_volume(app_prefs()->volume);
    beep(hz, ms, 5000);
    if (!s_held) bsp_audio_standby();
}

static void tone_task(void *arg)
{
    (void)arg;
    for (;;) {
        tone_job_t j;
        if (xQueueReceive(s_q, &j, portMAX_DELAY) != pdTRUE) continue;
        if (j.hz > 0) {
            play_note(j.hz, j.ms);
        } else if (j.hz == 0) {
            play_id(j.ms);
        } else if (j.hz == -1) {
            if (app_prefs()->muted) continue;
            if (bsp_audio_set_format(SAMPLE_RATE, 16, 1) == ESP_OK) {
                bsp_audio_set_volume(app_prefs()->volume);
                s_held = true;
            }
        } else if (j.hz == -2) {
            s_held = false;
            bsp_audio_standby();
        }
    }
}

void app_tone_start(void)
{
    if (s_q) return;
    s_q = xQueueCreate(8, sizeof(tone_job_t));
    if (!s_q) return;
    xTaskCreate(tone_task, "app_tone", STACK_BYTES, NULL, 4, &s_task);
}

void app_tone_play(int id)
{
    tone_job_t j = { 0, (int16_t)id };
    if (!s_q || id == APP_TONE_OFF || app_prefs()->muted) return;
    xQueueSend(s_q, &j, 0);
}

void app_tone_note(int hz, int ms)
{
    tone_job_t j;
    if (!s_q || hz < 1 || app_prefs()->muted) return;
    if (ms < 20) ms = 20;
    if (ms > 160) ms = 160;
    j.hz = (int16_t)hz;
    j.ms = (int16_t)ms;
    xQueueSend(s_q, &j, 0);
}

void app_tone_gate(bool on)
{
    tone_job_t j = { on ? -1 : -2, 0 };
    if (!s_q) return;
    if (on && app_prefs()->muted) return;
    xQueueSend(s_q, &j, 0);
}
