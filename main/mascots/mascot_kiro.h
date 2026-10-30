#pragma once
#include "mascot_common.h"

// Kiro — 青色滑翔翼小使者（AWS Kiro 风）
#define KIRO_BODY  RGB565(0, 168, 180)
#define KIRO_DARK  RGB565(0, 122, 138)
#define KIRO_LIGHT RGB565(88, 214, 224)
#define KIRO_EYE   0xFFFF
#define KIRO_ALERT RGB565(255, 61, 0)
#define KIRO_KB_BASE RGB565(46, 61, 87)
#define KIRO_KB_KEY  RGB565(97, 128, 163)
#define KIRO_KB_HI   0xFFFF

static void drawKiroBody(float dy, float scale = 1.0f) {
  float cx = 7.5f, cy = 9.0f;
  float w = 9.0f * scale, h = 7.0f * scale;
  float x = cx - w / 2.0f;
  float y = cy - h / 2.0f;
  gfx->fillRoundRect(sx(x), sy(y, dy), sw(w), sh(h), sw(2.0f * scale), KIRO_BODY);
  gfx->fillRoundRect(sx(x + 0.8f), sy(y + 0.5f, dy), sw(w - 1.6f), sh(1.2f * scale),
                     sw(0.8f), KIRO_LIGHT);
  // 两侧滑翔翼
  fillRotatedRect(x - 2.2f * scale, y + 2.0f * scale, 2.2f * scale, 1.0f * scale,
                  x, y + 2.5f * scale, -25.0f, dy, KIRO_DARK);
  fillRotatedRect(x + w, y + 2.0f * scale, 2.2f * scale, 1.0f * scale,
                  x + w, y + 2.5f * scale, 25.0f, dy, KIRO_DARK);
}

void kiroSleep(float t) {
  useViewport(15.0f, 12.0f, 4.0f);
  float fl = sinf(fmodf(t, 4.0f) / 4.0f * 2.0f * PI) * 0.8f;
  drawShadow(6.0f + fabsf(fl) * 0.3f, 0.0f, 15.0f, 4.5f, 6.0f);
  drawKiroBody(fl, 0.9f);
  float blinkPhase = fmodf(t, 4.0f);
  float eyeH = fmaxf(0.3f, 1.7f * ((blinkPhase > 3.5f && blinkPhase < 3.7f) ? 0.15f : 0.5f));
  gfx->fillRect(sx(5.0f), sy(8.2f + (1.7f - eyeH) / 2.0f, fl), sw(1.3f), sh(eyeH), KIRO_EYE);
  gfx->fillRect(sx(8.7f), sy(8.2f + (1.7f - eyeH) / 2.0f, fl), sw(1.3f), sh(eyeH), KIRO_EYE);
  drawZParticles(t);
}

void kiroWork(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float bounce = sinf(t * 2.0f * PI / 0.4f) * 1.0f;
  drawShadow(7.0f, bounce, 16.0f, 4.0f, 7.0f);
  drawKeyboard(t, KIRO_KB_BASE, KIRO_KB_KEY, KIRO_KB_HI);
  drawKiroBody(bounce, 1.0f);
  float blinkPhase = fmodf(t, 2.5f);
  float eyeH = (blinkPhase > 2.2f && blinkPhase < 2.35f) ? 0.1f : 1.7f;
  gfx->fillRect(sx(5.0f), sy(8.2f + (1.7f - eyeH) / 2.0f, bounce), sw(1.3f), sh(eyeH), KIRO_EYE);
  gfx->fillRect(sx(8.7f), sy(8.2f + (1.7f - eyeH) / 2.0f, bounce), sw(1.3f), sh(eyeH), KIRO_EYE);
}

void kiroAlert(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float pct = fmodf(t, 3.5f) / 3.5f;
  float jumpY = lerpKF(kfJumpSoft, 18, pct);
  float bangOp = lerpKF(kfBangOpCommon, 6, pct);
  float bangSc = lerpKF(kfBangScCommon, 6, pct);
  float pulse = (pct > 0.03f && pct < 0.55f) ? 1.0f + sinf(pct * 20.0f) * 0.15f : 1.0f;
  float shakeX = (pct > 0.15f && pct < 0.55f) ? sinf(pct * 80.0f) * 0.6f : 0.0f;
  drawShadow(7.0f, jumpY, 16.0f, 4.0f, 7.0f);
  setViewportShiftX(shakeX);
  drawKiroBody(jumpY, pulse);
  gfx->fillRect(sx(5.0f), sy(8.2f, jumpY), sw(1.3f), sh(2.0f), KIRO_EYE);
  gfx->fillRect(sx(8.7f), sy(8.2f, jumpY), sw(1.3f), sh(2.0f), KIRO_EYE);
  setViewportShiftX(0.0f);
  drawBang(bangOp, bangSc, jumpY, jumpY, KIRO_ALERT);
}

void kiroQuestion(float t) {
  _questionMode = true;
  kiroAlert(t);
  _questionMode = false;
}
