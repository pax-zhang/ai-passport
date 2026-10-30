#pragma once
#include "mascot_common.h"

// Grok — xAI 黑白极简石碑风
#define GRO_BODY  RGB565(38, 38, 42)
#define GRO_EDGE  RGB565(72, 72, 80)
#define GRO_EYE   0xFFFF
#define GRO_ALERT RGB565(255, 61, 0)

static void drawGrokBody(float dy, float scale = 1.0f) {
  float cx = 7.5f, cy = 9.0f;
  float w = 7.5f * scale, h = 8.0f * scale;
  float x = cx - w / 2.0f;
  float y = cy - h / 2.0f;
  gfx->fillRoundRect(sx(x), sy(y, dy), sw(w), sh(h), sw(1.0f * scale), GRO_BODY);
  gfx->fillRect(sx(x + 0.6f), sy(y + 0.5f, dy), sw(w - 1.2f), sh(0.9f * scale), GRO_EDGE);
  gfx->fillRect(sx(x + 0.6f), sy(y + h - 1.2f, dy), sw(w - 1.2f), sh(0.7f * scale), GRO_EDGE);
}

void grokSleep(float t) {
  useViewport(15.0f, 12.0f, 4.0f);
  float fl = sinf(fmodf(t, 4.0f) / 4.0f * 2.0f * PI) * 0.8f;
  drawShadow(6.0f + fabsf(fl) * 0.3f, 0.0f, 15.0f, 4.5f, 6.0f);
  drawGrokBody(fl, 0.9f);
  // 白色横线眼（休眠态）
  float blinkPhase = fmodf(t, 4.0f);
  bool blink = (blinkPhase > 3.5f && blinkPhase < 3.7f);
  if (!blink) {
    gfx->fillRect(sx(5.0f), sy(8.6f, fl), sw(1.4f), sh(0.5f), GRO_EYE);
    gfx->fillRect(sx(8.6f), sy(8.6f, fl), sw(1.4f), sh(0.5f), GRO_EYE);
  }
  drawZParticles(t);
}

void grokWork(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float bounce = sinf(t * 2.0f * PI / 0.4f) * 1.0f;
  drawShadow(7.0f, bounce, 16.0f, 4.0f, 7.0f);
  drawKeyboard(t, GRO_EDGE, GRO_BODY, GRO_EYE);
  drawGrokBody(bounce, 1.0f);
  float blinkPhase = fmodf(t, 2.5f);
  bool blink = (blinkPhase > 2.2f && blinkPhase < 2.35f);
  if (!blink) {
    // X 形双眼
    gfx->fillRect(sx(5.0f), sy(8.2f, bounce), sw(1.6f), sh(0.5f), GRO_EYE);
    gfx->fillRect(sx(5.5f), sy(8.8f, bounce), sw(0.5f), sh(1.6f), GRO_EYE);
    gfx->fillRect(sx(8.4f), sy(8.2f, bounce), sw(1.6f), sh(0.5f), GRO_EYE);
    gfx->fillRect(sx(8.9f), sy(8.8f, bounce), sw(0.5f), sh(1.6f), GRO_EYE);
  }
}

void grokAlert(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float pct = fmodf(t, 3.5f) / 3.5f;
  float jumpY = lerpKF(kfJumpSoft, 18, pct);
  float bangOp = lerpKF(kfBangOpCommon, 6, pct);
  float bangSc = lerpKF(kfBangScCommon, 6, pct);
  float pulse = (pct > 0.03f && pct < 0.55f) ? 1.0f + sinf(pct * 20.0f) * 0.15f : 1.0f;
  float shakeX = (pct > 0.15f && pct < 0.55f) ? sinf(pct * 80.0f) * 0.6f : 0.0f;
  drawShadow(7.0f, jumpY, 16.0f, 4.0f, 7.0f);
  setViewportShiftX(shakeX);
  drawGrokBody(jumpY, pulse);
  gfx->fillRect(sx(5.0f), sy(8.2f, jumpY), sw(1.6f), sh(0.6f), GRO_EYE);
  gfx->fillRect(sx(8.4f), sy(8.2f, jumpY), sw(1.6f), sh(0.6f), GRO_EYE);
  setViewportShiftX(0.0f);
  drawBang(bangOp, bangSc, jumpY, jumpY, GRO_ALERT);
}

void grokQuestion(float t) {
  _questionMode = true;
  grokAlert(t);
  _questionMode = false;
}
