#pragma once
#include "mascot_common.h"

// Cline — 琥珀色工程师机器人（宽面罩）
#define CLN_BODY  RGB565(255, 140, 50)
#define CLN_DARK  RGB565(214, 105, 22)
#define CLN_VISOR RGB565(40, 44, 56)
#define CLN_EYE   RGB565(120, 230, 255)
#define CLN_ALERT RGB565(255, 61, 0)
#define CLN_KB_BASE RGB565(46, 61, 87)
#define CLN_KB_KEY  RGB565(97, 128, 163)
#define CLN_KB_HI   0xFFFF

static void drawClineBody(float dy, float scale = 1.0f) {
  float cx = 7.5f, cy = 9.0f;
  float w = 9.5f * scale, h = 7.5f * scale;
  float x = cx - w / 2.0f;
  float y = cy - h / 2.0f;
  gfx->fillRoundRect(sx(x), sy(y, dy), sw(w), sh(h), sw(2.0f * scale), CLN_BODY);
  // 宽面罩
  gfx->fillRoundRect(sx(x + 1.0f), sy(y + 1.6f, dy), sw(w - 2.0f), sh(2.6f * scale),
                     sw(1.0f), CLN_VISOR);
  // 底部深色带
  gfx->fillRoundRect(sx(x + 1.0f), sy(y + h - 1.5f, dy), sw(w - 2.0f), sh(0.9f * scale),
                     sw(0.5f), CLN_DARK);
}

static void drawClineEyes(float y, float dy, float h) {
  gfx->fillRect(sx(5.2f), sy(y + (2.0f - h) / 2.0f, dy), sw(1.2f), sh(h), CLN_EYE);
  gfx->fillRect(sx(8.6f), sy(y + (2.0f - h) / 2.0f, dy), sw(1.2f), sh(h), CLN_EYE);
}

void clineSleep(float t) {
  useViewport(15.0f, 12.0f, 4.0f);
  float fl = sinf(fmodf(t, 4.0f) / 4.0f * 2.0f * PI) * 0.8f;
  drawShadow(6.0f + fabsf(fl) * 0.3f, 0.0f, 15.0f, 4.5f, 6.0f);
  drawClineBody(fl, 0.9f);
  float blinkPhase = fmodf(t, 4.0f);
  float eyeH = fmaxf(0.3f, 2.0f * ((blinkPhase > 3.5f && blinkPhase < 3.7f) ? 0.15f : 0.55f));
  drawClineEyes(7.4f, fl, eyeH);
  drawZParticles(t);
}

void clineWork(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float bounce = sinf(t * 2.0f * PI / 0.4f) * 1.0f;
  drawShadow(7.0f, bounce, 16.0f, 4.0f, 7.0f);
  drawKeyboard(t, CLN_KB_BASE, CLN_KB_KEY, CLN_KB_HI);
  drawClineBody(bounce, 1.0f);
  float blinkPhase = fmodf(t, 2.5f);
  float eyeH = (blinkPhase > 2.2f && blinkPhase < 2.35f) ? 0.1f : 2.0f;
  drawClineEyes(7.4f, bounce, eyeH);
}

void clineAlert(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float pct = fmodf(t, 3.5f) / 3.5f;
  float jumpY = lerpKF(kfJumpSoft, 18, pct);
  float bangOp = lerpKF(kfBangOpCommon, 6, pct);
  float bangSc = lerpKF(kfBangScCommon, 6, pct);
  float pulse = (pct > 0.03f && pct < 0.55f) ? 1.0f + sinf(pct * 20.0f) * 0.15f : 1.0f;
  float shakeX = (pct > 0.15f && pct < 0.55f) ? sinf(pct * 80.0f) * 0.6f : 0.0f;
  drawShadow(7.0f, jumpY, 16.0f, 4.0f, 7.0f);
  setViewportShiftX(shakeX);
  drawClineBody(jumpY, pulse);
  drawClineEyes(7.4f, jumpY, 2.4f);
  setViewportShiftX(0.0f);
  drawBang(bangOp, bangSc, jumpY, jumpY, CLN_ALERT);
}

void clineQuestion(float t) {
  _questionMode = true;
  clineAlert(t);
  _questionMode = false;
}
