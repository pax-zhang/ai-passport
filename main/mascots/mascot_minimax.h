#pragma once
#include "mascot_common.h"

// MiniMax — 珊瑚红渐变方块（错位双三角眼）
#define MMX_BODY  RGB565(244, 84, 84)
#define MMX_DARK  RGB565(206, 52, 62)
#define MMX_LIGHT RGB565(255, 132, 120)
#define MMX_EYE   0xFFFF
#define MMX_ALERT RGB565(255, 61, 0)
#define MMX_KB_BASE RGB565(46, 61, 87)
#define MMX_KB_KEY  RGB565(97, 128, 163)
#define MMX_KB_HI   0xFFFF

static void drawMinimaxBody(float dy, float scale = 1.0f) {
  float cx = 7.5f, cy = 9.0f;
  float w = 8.5f * scale, h = 7.5f * scale;
  float x = cx - w / 2.0f;
  float y = cy - h / 2.0f;
  gfx->fillRoundRect(sx(x), sy(y, dy), sw(w), sh(h), sw(1.5f * scale), MMX_BODY);
  gfx->fillRoundRect(sx(x + 0.7f), sy(y + 0.5f, dy), sw(w - 1.4f), sh(1.2f * scale),
                     sw(0.8f), MMX_LIGHT);
  gfx->fillRoundRect(sx(x + 0.7f), sy(y + h - 1.4f, dy), sw(w - 1.4f), sh(0.9f * scale),
                     sw(0.6f), MMX_DARK);
}

void minimaxSleep(float t) {
  useViewport(15.0f, 12.0f, 4.0f);
  float fl = sinf(fmodf(t, 4.0f) / 4.0f * 2.0f * PI) * 0.8f;
  drawShadow(6.0f + fabsf(fl) * 0.3f, 0.0f, 15.0f, 4.5f, 6.0f);
  drawMinimaxBody(fl, 0.9f);
  float blinkPhase = fmodf(t, 4.0f);
  float eyeH = fmaxf(0.3f, 1.8f * ((blinkPhase > 3.5f && blinkPhase < 3.7f) ? 0.15f : 0.5f));
  gfx->fillRect(sx(5.1f), sy(8.3f + (1.8f - eyeH) / 2.0f, fl), sw(1.2f), sh(eyeH), MMX_EYE);
  gfx->fillRect(sx(8.7f), sy(8.3f + (1.8f - eyeH) / 2.0f, fl), sw(1.2f), sh(eyeH), MMX_EYE);
  drawZParticles(t);
}

void minimaxWork(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float bounce = sinf(t * 2.0f * PI / 0.4f) * 1.0f;
  drawShadow(7.0f, bounce, 16.0f, 4.0f, 7.0f);
  drawKeyboard(t, MMX_KB_BASE, MMX_KB_KEY, MMX_KB_HI);
  drawMinimaxBody(bounce, 1.0f);
  float blinkPhase = fmodf(t, 2.5f);
  float eyeH = (blinkPhase > 2.2f && blinkPhase < 2.35f) ? 0.1f : 1.8f;
  gfx->fillRect(sx(5.1f), sy(8.3f + (1.8f - eyeH) / 2.0f, bounce), sw(1.2f), sh(eyeH), MMX_EYE);
  gfx->fillRect(sx(8.7f), sy(8.3f + (1.8f - eyeH) / 2.0f, bounce), sw(1.2f), sh(eyeH), MMX_EYE);
}

void minimaxAlert(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float pct = fmodf(t, 3.5f) / 3.5f;
  float jumpY = lerpKF(kfJumpSoft, 18, pct);
  float bangOp = lerpKF(kfBangOpCommon, 6, pct);
  float bangSc = lerpKF(kfBangScCommon, 6, pct);
  float pulse = (pct > 0.03f && pct < 0.55f) ? 1.0f + sinf(pct * 20.0f) * 0.15f : 1.0f;
  float shakeX = (pct > 0.15f && pct < 0.55f) ? sinf(pct * 80.0f) * 0.6f : 0.0f;
  drawShadow(7.0f, jumpY, 16.0f, 4.0f, 7.0f);
  setViewportShiftX(shakeX);
  drawMinimaxBody(jumpY, pulse);
  gfx->fillRect(sx(5.1f), sy(8.3f, jumpY), sw(1.2f), sh(2.1f), MMX_EYE);
  gfx->fillRect(sx(8.7f), sy(8.3f, jumpY), sw(1.2f), sh(2.1f), MMX_EYE);
  setViewportShiftX(0.0f);
  drawBang(bangOp, bangSc, jumpY, jumpY, MMX_ALERT);
}

void minimaxQuestion(float t) {
  _questionMode = true;
  minimaxAlert(t);
  _questionMode = false;
}
