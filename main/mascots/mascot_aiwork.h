#pragma once
#include "mascot_common.h"

// AIWork — 翠绿效率小助手（进度条胸口）
#define AIW_BODY  RGB565(52, 188, 108)
#define AIW_DARK  RGB565(28, 142, 74)
#define AIW_LIGHT RGB565(112, 232, 160)
#define AIW_EYE   0xFFFF
#define AIW_BAR   RGB565(255, 214, 64)
#define AIW_ALERT RGB565(255, 61, 0)
#define AIW_KB_BASE RGB565(46, 61, 87)
#define AIW_KB_KEY  RGB565(97, 128, 163)
#define AIW_KB_HI   0xFFFF

static void drawAiworkBody(float dy, float scale = 1.0f, float progress = -1.0f) {
  float cx = 7.5f, cy = 9.0f;
  float w = 9.0f * scale, h = 7.5f * scale;
  float x = cx - w / 2.0f;
  float y = cy - h / 2.0f;
  gfx->fillRoundRect(sx(x), sy(y, dy), sw(w), sh(h), sw(2.0f * scale), AIW_BODY);
  gfx->fillRoundRect(sx(x + 0.8f), sy(y + 0.5f, dy), sw(w - 1.6f), sh(1.1f * scale),
                     sw(0.8f), AIW_LIGHT);
  // 胸口进度条
  gfx->fillRect(sx(x + 1.4f), sy(y + h - 2.0f, dy), sw(w - 2.8f), sh(0.9f * scale), AIW_DARK);
  if (progress >= 0.0f) {
    float pw = (w - 3.2f) * progress;
    if (pw > 0.2f)
      gfx->fillRect(sx(x + 1.6f), sy(y + h - 1.9f, dy), sw(pw), sh(0.7f * scale), AIW_BAR);
  }
}

void aiworkSleep(float t) {
  useViewport(15.0f, 12.0f, 4.0f);
  float fl = sinf(fmodf(t, 4.0f) / 4.0f * 2.0f * PI) * 0.8f;
  drawShadow(6.0f + fabsf(fl) * 0.3f, 0.0f, 15.0f, 4.5f, 6.0f);
  drawAiworkBody(fl, 0.9f, 0.25f);
  float blinkPhase = fmodf(t, 4.0f);
  float eyeH = fmaxf(0.3f, 1.7f * ((blinkPhase > 3.5f && blinkPhase < 3.7f) ? 0.15f : 0.5f));
  gfx->fillRect(sx(5.0f), sy(7.9f + (1.7f - eyeH) / 2.0f, fl), sw(1.3f), sh(eyeH), AIW_EYE);
  gfx->fillRect(sx(8.7f), sy(7.9f + (1.7f - eyeH) / 2.0f, fl), sw(1.3f), sh(eyeH), AIW_EYE);
  drawZParticles(t);
}

void aiworkWork(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float bounce = sinf(t * 2.0f * PI / 0.4f) * 1.0f;
  drawShadow(7.0f, bounce, 16.0f, 4.0f, 7.0f);
  drawKeyboard(t, AIW_KB_BASE, AIW_KB_KEY, AIW_KB_HI);
  float progress = 0.5f + 0.5f * sinf(t * 2.0f * PI / 1.2f);
  drawAiworkBody(bounce, 1.0f, progress);
  float blinkPhase = fmodf(t, 2.5f);
  float eyeH = (blinkPhase > 2.2f && blinkPhase < 2.35f) ? 0.1f : 1.7f;
  gfx->fillRect(sx(5.0f), sy(7.9f + (1.7f - eyeH) / 2.0f, bounce), sw(1.3f), sh(eyeH), AIW_EYE);
  gfx->fillRect(sx(8.7f), sy(7.9f + (1.7f - eyeH) / 2.0f, bounce), sw(1.3f), sh(eyeH), AIW_EYE);
}

void aiworkAlert(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float pct = fmodf(t, 3.5f) / 3.5f;
  float jumpY = lerpKF(kfJumpSoft, 18, pct);
  float bangOp = lerpKF(kfBangOpCommon, 6, pct);
  float bangSc = lerpKF(kfBangScCommon, 6, pct);
  float pulse = (pct > 0.03f && pct < 0.55f) ? 1.0f + sinf(pct * 20.0f) * 0.15f : 1.0f;
  float shakeX = (pct > 0.15f && pct < 0.55f) ? sinf(pct * 80.0f) * 0.6f : 0.0f;
  drawShadow(7.0f, jumpY, 16.0f, 4.0f, 7.0f);
  setViewportShiftX(shakeX);
  drawAiworkBody(jumpY, pulse, 1.0f);
  gfx->fillRect(sx(5.0f), sy(7.9f, jumpY), sw(1.3f), sh(2.0f), AIW_EYE);
  gfx->fillRect(sx(8.7f), sy(7.9f, jumpY), sw(1.3f), sh(2.0f), AIW_EYE);
  setViewportShiftX(0.0f);
  drawBang(bangOp, bangSc, jumpY, jumpY, AIW_ALERT);
}

void aiworkQuestion(float t) {
  _questionMode = true;
  aiworkAlert(t);
  _questionMode = false;
}
