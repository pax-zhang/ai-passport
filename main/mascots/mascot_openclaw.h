#pragma once
#include "mascot_common.h"

// OpenClaw — 红色小螃蟹（开钳）
#define OCL_BODY  RGB565(232, 62, 44)
#define OCL_DARK  RGB565(188, 40, 26)
#define OCL_LIGHT RGB565(255, 108, 88)
#define OCL_EYE   0xFFFF
#define OCL_ALERT RGB565(255, 200, 0)
#define OCL_KB_BASE RGB565(46, 61, 87)
#define OCL_KB_KEY  RGB565(97, 128, 163)
#define OCL_KB_HI   0xFFFF

static void drawOpenClawBody(float dy, float scale = 1.0f) {
  float cx = 7.5f, cy = 9.5f;
  float w = 9.0f * scale, h = 6.5f * scale;
  float x = cx - w / 2.0f;
  float y = cy - h / 2.0f;
  gfx->fillRoundRect(sx(x), sy(y, dy), sw(w), sh(h), sw(3.0f * scale), OCL_BODY);
  gfx->fillRoundRect(sx(x + 0.8f), sy(y + 0.5f, dy), sw(w - 1.6f), sh(1.1f * scale),
                     sw(0.8f), OCL_LIGHT);
  // 两侧小钳
  float cy2 = y + h * 0.45f;
  gfx->fillCircle(sx(x - 0.8f * scale), sy(cy2, dy), sw(1.3f * scale), OCL_DARK);
  gfx->fillCircle(sx(x + w + 0.8f * scale), sy(cy2, dy), sw(1.3f * scale), OCL_DARK);
}

void openclawSleep(float t) {
  useViewport(15.0f, 12.0f, 4.0f);
  float fl = sinf(fmodf(t, 4.0f) / 4.0f * 2.0f * PI) * 0.8f;
  drawShadow(6.5f + fabsf(fl) * 0.3f, 0.0f, 15.0f, 4.2f, 6.6f);
  drawOpenClawBody(fl, 0.9f);
  float blinkPhase = fmodf(t, 4.0f);
  float eyeH = fmaxf(0.3f, 1.7f * ((blinkPhase > 3.5f && blinkPhase < 3.7f) ? 0.15f : 0.5f));
  gfx->fillRect(sx(5.1f), sy(8.4f + (1.7f - eyeH) / 2.0f, fl), sw(1.2f), sh(eyeH), OCL_EYE);
  gfx->fillRect(sx(8.7f), sy(8.4f + (1.7f - eyeH) / 2.0f, fl), sw(1.2f), sh(eyeH), OCL_EYE);
  drawZParticles(t);
}

void openclawWork(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float bounce = sinf(t * 2.0f * PI / 0.4f) * 1.0f;
  drawShadow(7.0f, bounce, 16.0f, 4.0f, 7.0f);
  drawKeyboard(t, OCL_KB_BASE, OCL_KB_KEY, OCL_KB_HI);
  drawOpenClawBody(bounce, 1.0f);
  float blinkPhase = fmodf(t, 2.5f);
  float eyeH = (blinkPhase > 2.2f && blinkPhase < 2.35f) ? 0.1f : 1.7f;
  gfx->fillRect(sx(5.1f), sy(8.4f + (1.7f - eyeH) / 2.0f, bounce), sw(1.2f), sh(eyeH), OCL_EYE);
  gfx->fillRect(sx(8.7f), sy(8.4f + (1.7f - eyeH) / 2.0f, bounce), sw(1.2f), sh(eyeH), OCL_EYE);
}

void openclawAlert(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float pct = fmodf(t, 3.5f) / 3.5f;
  float jumpY = lerpKF(kfJumpSoft, 18, pct);
  float bangOp = lerpKF(kfBangOpCommon, 6, pct);
  float bangSc = lerpKF(kfBangScCommon, 6, pct);
  float pulse = (pct > 0.03f && pct < 0.55f) ? 1.0f + sinf(pct * 20.0f) * 0.15f : 1.0f;
  float shakeX = (pct > 0.15f && pct < 0.55f) ? sinf(pct * 80.0f) * 0.6f : 0.0f;
  drawShadow(7.0f, jumpY, 16.0f, 4.0f, 7.0f);
  setViewportShiftX(shakeX);
  drawOpenClawBody(jumpY, pulse);
  gfx->fillRect(sx(5.1f), sy(8.4f, jumpY), sw(1.2f), sh(2.1f), OCL_EYE);
  gfx->fillRect(sx(8.7f), sy(8.4f, jumpY), sw(1.2f), sh(2.1f), OCL_EYE);
  setViewportShiftX(0.0f);
  drawBang(bangOp, bangSc, jumpY, jumpY, OCL_ALERT);
}

void openclawQuestion(float t) {
  _questionMode = true;
  openclawAlert(t);
  _questionMode = false;
}
