#pragma once
#include "mascot_common.h"

// Pi — 紫色圆滚滚的小水滴（π）
#define PI_BODY   RGB565(158, 96, 255)
#define PI_DARK   RGB565(122, 62, 224)
#define PI_LIGHT  RGB565(190, 142, 255)
#define PI_EYE    0xFFFF
#define PI_ALERT  RGB565(255, 61, 0)

static void drawPiBody(float dy, float scale = 1.0f) {
  float cx = 7.5f, cy = 9.5f;
  float r = 4.2f * scale;
  float x = cx - r, y = cy - r;
  gfx->fillCircle(sx(cx), sy(cy, dy), sw(r), PI_BODY);
  gfx->fillCircle(sx(cx - 1.2f * scale), sy(cy - 1.2f * scale, dy), sw(r * 0.35f), PI_LIGHT);
  // 底部投影色带
  gfx->fillRect(sx(cx - r + 0.8f), sy(cy + r * 0.55f, dy), sw(r * 2.0f - 1.6f),
                sh(r * 0.45f), PI_DARK);
}

void piSleep(float t) {
  useViewport(15.0f, 12.0f, 4.0f);
  float fl = sinf(fmodf(t, 4.0f) / 4.0f * 2.0f * PI) * 0.8f;
  drawShadow(6.0f + fabsf(fl) * 0.3f, 0.0f, 15.0f, 4.5f, 6.0f);
  drawPiBody(fl, 0.9f);
  float blinkPhase = fmodf(t, 4.0f);
  // 休眠：弯月闭眼
  bool blink = (blinkPhase > 3.5f && blinkPhase < 3.7f);
  if (blink) {
    gfx->fillRect(sx(5.0f), sy(8.8f, fl), sw(1.3f), sh(0.4f), PI_EYE);
    gfx->fillRect(sx(8.7f), sy(8.8f, fl), sw(1.3f), sh(0.4f), PI_EYE);
  } else {
    gfx->fillRect(sx(5.0f), sy(8.0f, fl), sw(1.3f), sh(1.6f), PI_EYE);
    gfx->fillRect(sx(8.7f), sy(8.0f, fl), sw(1.3f), sh(1.6f), PI_EYE);
  }
  drawZParticles(t);
}

void piWork(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float bounce = sinf(t * 2.0f * PI / 0.35f) * 1.2f;
  drawShadow(7.0f, bounce, 16.0f, 4.0f, 7.0f);
  drawKeyboard(t, PI_DARK, PI_BODY, PI_LIGHT);
  drawPiBody(bounce, 1.0f);
  float blinkPhase = fmodf(t, 2.5f);
  float eyeH = (blinkPhase > 2.2f && blinkPhase < 2.35f) ? 0.1f : 1.6f;
  gfx->fillRect(sx(5.0f), sy(8.0f + (1.6f - eyeH) / 2.0f, bounce), sw(1.3f), sh(eyeH), PI_EYE);
  gfx->fillRect(sx(8.7f), sy(8.0f + (1.6f - eyeH) / 2.0f, bounce), sw(1.3f), sh(eyeH), PI_EYE);
}

void piAlert(float t) {
  useViewport(16.0f, 14.0f, 3.0f);
  float pct = fmodf(t, 3.5f) / 3.5f;
  float jumpY = lerpKF(kfJumpSoft, 18, pct);
  float bangOp = lerpKF(kfBangOpCommon, 6, pct);
  float bangSc = lerpKF(kfBangScCommon, 6, pct);
  float pulse = (pct > 0.03f && pct < 0.55f) ? 1.0f + sinf(pct * 20.0f) * 0.15f : 1.0f;
  float shakeX = (pct > 0.15f && pct < 0.55f) ? sinf(pct * 80.0f) * 0.6f : 0.0f;
  drawShadow(7.0f, jumpY, 16.0f, 4.0f, 7.0f);
  setViewportShiftX(shakeX);
  drawPiBody(jumpY, pulse);
  gfx->fillRect(sx(5.0f), sy(8.0f, jumpY), sw(1.3f), sh(1.9f), PI_EYE);
  gfx->fillRect(sx(8.7f), sy(8.0f, jumpY), sw(1.3f), sh(1.9f), PI_EYE);
  setViewportShiftX(0.0f);
  drawBang(bangOp, bangSc, jumpY, jumpY, PI_ALERT);
}

void piQuestion(float t) {
  _questionMode = true;
  piAlert(t);
  _questionMode = false;
}
