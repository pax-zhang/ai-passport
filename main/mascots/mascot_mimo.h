#pragma once
#include "mascot_common.h"

// MiMo — 小米橙圆耳朵小机器人
#define MIMO_BODY  RGB565(255, 103, 31)
#define MIMO_DARK  RGB565(214, 76, 12)
#define MIMO_LIGHT RGB565(255, 152, 92)
#define MIMO_EYE   0xFFFF
#define MIMO_ALERT RGB565(255, 61, 0)
#define MIMO_KB_BASE RGB565(46, 61, 87)
#define MIMO_KB_KEY  RGB565(97, 128, 163)
#define MIMO_KB_HI   0xFFFF

static void drawMimoEars(float x, float y, float dy, float scale) {
  // 两只圆耳朵
  gfx->fillCircle(sx(x + 1.0f * scale), sy(y, dy), sw(1.3f * scale), MIMO_DARK);
  gfx->fillCircle(sx(x + 8.0f * scale), sy(y, dy), sw(1.3f * scale), MIMO_DARK);
}

static void drawMimoBody(float dy, float scale = 1.0f) {
  float cx = 7.5f, cy = 9.2f;
  float w = 8.0f * scale, h = 6.8f * scale;
  float x = cx - w / 2.0f;
  float y = cy - h / 2.0f;
  drawMimoEars(x, y + 0.3f * scale, dy, scale);
  gfx->fillRoundRect(sx(x), sy(y, dy), sw(w), sh(h), sw(2.2f * scale), MIMO_BODY);
  gfx->fillRoundRect(sx(x + 0.8f), sy(y + 0.5f, dy), sw(w - 1.6f), sh(1.1f * scale),
                     sw(0.8f), MIMO_LIGHT);
}

void mimoSleep(float t) {
  useViewport(15.0f, 12.0f, 4.0f);
  float fl = sinf(fmodf(t, 4.0f) / 4.0f * 2.0f * PI) * 0.8f;
  drawShadow(6.0f + fabsf(fl) * 0.3f, 0.0f, 15.0f, 4.5f, 6.0f);
  drawMimoBody(fl, 0.9f);
  float blinkPhase = fmodf(t, 4.0f);
  float eyeH = fmaxf(0.3f, 1.8f * ((blinkPhase > 3.5f && blinkPhase < 3.7f) ? 0.15f : 0.5f));
  gfx->fillRect(sx(5.2f), sy(8.4f + (1.8f - eyeH) / 2.0f, fl), sw(1.2f), sh(eyeH), MIMO_EYE);
  gfx->fillRect(sx(8.6f), sy(8.4f + (1.8f - eyeH) / 2.0f, fl), sw(1.2f), sh(eyeH), MIMO_EYE);
  drawZParticles(t);
}

void mimoWork(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float bounce = sinf(t * 2.0f * PI / 0.4f) * 1.0f;
  drawShadow(7.0f, bounce, 16.0f, 4.0f, 7.0f);
  drawKeyboard(t, MIMO_KB_BASE, MIMO_KB_KEY, MIMO_KB_HI);
  drawMimoBody(bounce, 1.0f);
  float blinkPhase = fmodf(t, 2.5f);
  float eyeH = (blinkPhase > 2.2f && blinkPhase < 2.35f) ? 0.1f : 1.8f;
  gfx->fillRect(sx(5.2f), sy(8.4f + (1.8f - eyeH) / 2.0f, bounce), sw(1.2f), sh(eyeH), MIMO_EYE);
  gfx->fillRect(sx(8.6f), sy(8.4f + (1.8f - eyeH) / 2.0f, bounce), sw(1.2f), sh(eyeH), MIMO_EYE);
}

void mimoAlert(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float pct = fmodf(t, 3.5f) / 3.5f;
  float jumpY = lerpKF(kfJumpSoft, 18, pct);
  float bangOp = lerpKF(kfBangOpCommon, 6, pct);
  float bangSc = lerpKF(kfBangScCommon, 6, pct);
  float pulse = (pct > 0.03f && pct < 0.55f) ? 1.0f + sinf(pct * 20.0f) * 0.15f : 1.0f;
  float shakeX = (pct > 0.15f && pct < 0.55f) ? sinf(pct * 80.0f) * 0.6f : 0.0f;
  drawShadow(7.0f, jumpY, 16.0f, 4.0f, 7.0f);
  setViewportShiftX(shakeX);
  drawMimoBody(jumpY, pulse);
  gfx->fillRect(sx(5.2f), sy(8.4f, jumpY), sw(1.2f), sh(2.1f), MIMO_EYE);
  gfx->fillRect(sx(8.6f), sy(8.4f, jumpY), sw(1.2f), sh(2.1f), MIMO_EYE);
  setViewportShiftX(0.0f);
  drawBang(bangOp, bangSc, jumpY, jumpY, MIMO_ALERT);
}

void mimoQuestion(float t) {
  _questionMode = true;
  mimoAlert(t);
  _questionMode = false;
}
