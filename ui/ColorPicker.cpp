/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/ColorPicker.cpp
   Module:      Ui
   Purpose:     Color picker interaction, layout, and drawing.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "ColorPicker.h"
#include "../core/DrawingEngine.h"
#include "../core/Gradient.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>   

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float ColorPicker::clamp01(float x) { return std::max(0.0f, std::min(1.0f, x)); }
float ColorPicker::clampf(float v, float a, float b) { return (v < a) ? a : (v > b ? b : v); }
int   ColorPicker::clampi(int v, int a, int b) { return (v < a) ? a : (v > b ? b : v); }

SDL_Color ColorPicker::hsvToRgb(float h, float s, float v) {
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;

    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r1 = 0, g1 = 0, b1 = 0;
    if (h < 60) { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }

    SDL_Color out;
    out.r = (Uint8)std::round((r1 + m) * 255.0f);
    out.g = (Uint8)std::round((g1 + m) * 255.0f);
    out.b = (Uint8)std::round((b1 + m) * 255.0f);
    out.a = 255;
    return out;
}

void ColorPicker::rgbToHsv(SDL_Color c, float& h, float& s, float& v) {
    float r = c.r / 255.0f;
    float g = c.g / 255.0f;
    float b = c.b / 255.0f;

    float mx = std::max(r, std::max(g, b));
    float mn = std::min(r, std::min(g, b));
    float d = mx - mn;

    v = mx;
    s = (mx == 0) ? 0.0f : (d / mx);

    if (d == 0) h = 0;
    else if (mx == r) h = 60.0f * std::fmod(((g - b) / d), 6.0f);
    else if (mx == g) h = 60.0f * (((b - r) / d) + 2.0f);
    else h = 60.0f * (((r - g) / d) + 4.0f);

    if (h < 0) h += 360.0f;
}

SDL_Color ColorPicker::gradientSample(float t) const {
    t = clamp01(t);
    int hi = 1;
    while (hi < kGradientStopCount && gradientStopPos[hi] < t) ++hi;
    if (hi >= kGradientStopCount) return gradientStopColor[kGradientStopCount - 1];
    int lo = std::max(0, hi - 1);

    float a = gradientStopPos[lo];
    float b = gradientStopPos[hi];
    float f = (b > a) ? (t - a) / (b - a) : 0.0f;

    SDL_Color c0 = gradientStopColor[lo];
    SDL_Color c1 = gradientStopColor[hi];
    SDL_Color out{};
    out.r = (Uint8)std::lround(c0.r * (1.0f - f) + c1.r * f);
    out.g = (Uint8)std::lround(c0.g * (1.0f - f) + c1.g * f);
    out.b = (Uint8)std::lround(c0.b * (1.0f - f) + c1.b * f);
    out.a = 255;
    return out;
}

void ColorPicker::syncTextFieldsFromColor() {
    SDL_Color baseRGB = hsvToRgb(H, S, V);

    char hex[16];
    std::snprintf(hex, sizeof(hex), "%02X%02X%02X", baseRGB.r, baseRGB.g, baseRGB.b);
    if (activeField != EditField::Hex) hexInput = hex;
    if (activeField != EditField::R)   rInput = std::to_string((int)baseRGB.r);
    if (activeField != EditField::G)   gInput = std::to_string((int)baseRGB.g);
    if (activeField != EditField::B)   bInput = std::to_string((int)baseRGB.b);

    rgbInput = rInput + "," + gInput + "," + bInput;
}

void ColorPicker::setColor(SDL_Color c) {
    rgba = c;
    opacity = clamp01(c.a / 255.0f);
    rgbToHsv(SDL_Color{ c.r,c.g,c.b,255 }, H, S, V);
    syncTextFieldsFromColor();
}

void ColorPicker::updateSelectedGradientStopColor() {
    if (gradientMode == GradientMode::Solid) return;
    selectedGradientStop = clampi(selectedGradientStop, 0, kGradientStopCount - 1);
    SDL_Color rgb = hsvToRgb(H, S, V);
    rgb.a = 255;
    gradientStopColor[selectedGradientStop] = rgb;
}

void ColorPicker::syncGradientToEngine() {
    if (!engine) return;
    engine->setGradientConfig(buildGradientConfig());
}

GradientConfig ColorPicker::buildGradientConfig() const
{
    GradientConfig cfg{};
    cfg.enabled = (gradientMode != GradientMode::Solid);

    switch (gradientMode)
    {
    case GradientMode::Linear: cfg.mode = 1; break;
    case GradientMode::Radial: cfg.mode = 2; break;
    case GradientMode::Solid:
    default:                  cfg.mode = 0; break;
    }

    for (int i = 0; i < kGradientStopCount; ++i)
    {
        cfg.stopPos[(size_t)i] = gradientStopPos[i];
        SDL_Color c = gradientStopColor[i];
        c.a = 255;
        cfg.stopColor[(size_t)i] = c;
    }

    return cfg;
}
void ColorPicker::pushToEngine() {
    if (!engine) return;

    SDL_Color rgb = hsvToRgb(H, S, V);
    rgb.a = (Uint8)std::round(clamp01(opacity) * 255.0f);
    rgba = rgb;

    updateSelectedGradientStopColor();
    syncGradientToEngine();
    engine->setColor(rgb);
    syncTextFieldsFromColor();
}

void ColorPicker::beginFieldEditing(EditField field) {
    activeField = field;
    editingField = field;
    replaceFieldOnType = true;
    if (activeField != EditField::None) SDL_StartTextInput();
}

void ColorPicker::stopFieldEditing(bool apply) {
    if (apply) {
        if (activeField == EditField::Hex) applyHexInput();
        if (activeField == EditField::R || activeField == EditField::G || activeField == EditField::B)
            applyRgbInput(activeField);
    }

    activeField = EditField::None;
    editingField = EditField::None;
    replaceFieldOnType = false;
    SDL_StopTextInput();
    syncTextFieldsFromColor();
}

bool ColorPicker::isEditingText() const {
    return activeField != EditField::None;
}

void ColorPicker::layout(const SDL_Rect& panelArea) {
    panelRect = panelArea;

    const int pad = 10;
    const int rowH = 44;
    const int toggleH = 30;
    const int rgbH = 30;
    const int hexH = 34;
    const int hueH = 14;
    const int gradExtra = (gradientMode == GradientMode::Solid) ? 0 : 36;

    const int innerX = panelRect.x + pad;
    const int innerW = std::max(0, panelRect.w - pad * 2);

    purpleBarRect = { innerX, panelRect.y + pad, innerW, 4 };

    topRowRect = { innerX, purpleBarRect.y + purpleBarRect.h + 8, innerW, rowH };
    swatchRect = { topRowRect.x, topRowRect.y + 4, 36, 36 };

    opacityLabelRect = { swatchRect.x + swatchRect.w + 10, topRowRect.y + 2, 120, 18 };
    opacityPctRect = { topRowRect.x + topRowRect.w - 56, topRowRect.y + 2, 56, 18 };

    int trackX = swatchRect.x + swatchRect.w + 10;
    int trackW = std::max(30, topRowRect.x + topRowRect.w - trackX - 6);
    opacityTrackRect = { trackX, topRowRect.y + 24, trackW, 12 };

    int y = topRowRect.y + topRowRect.h + 8;

    gradientRowRect = { innerX, y, innerW, toggleH };
    gradientToggleRect = gradientRowRect;

    int gap = 8;
    int btnW = std::max(44, (innerW - gap * 2) / 3);
    gradientSolidRect = { innerX, y, btnW, toggleH };
    gradientLinearRect = { gradientSolidRect.x + btnW + gap, y, btnW, toggleH };
    gradientRadialRect = { gradientLinearRect.x + btnW + gap, y, std::max(44, innerW - (btnW * 2 + gap * 2)), toggleH };

    y += toggleH + 8;
    int remaining = std::max(0, panelRect.y + panelRect.h - y - pad);

    int reserve = rgbH + 8 + hexH + 8 + gradExtra + hueH + 10;
    int wheelH = remaining - reserve;
    if (wheelH < 48) wheelH = std::max(24, remaining - (rgbH + hexH + hueH + gradExtra + 12));
    wheelH = std::max(24, std::min(wheelH, remaining));

    wheelBlockRect = { innerX, y, innerW, wheelH };

    int wheelSize = std::max(60, std::min(wheelBlockRect.h - 8, wheelBlockRect.w - 56));
    wheelRect = { wheelBlockRect.x + 4, wheelBlockRect.y + 4, wheelSize, wheelSize };

    int svSize = std::max(40, (int)std::round(wheelSize * 0.52f));
    svRect = {
        wheelRect.x + (wheelRect.w - svSize) / 2,
        wheelRect.y + (wheelRect.h - svSize) / 2,
        svSize, svSize
    };

    alphaRect = { wheelRect.x + wheelRect.w + 12, wheelRect.y + 8, 16, std::max(24, wheelRect.h - 16) };

    y = wheelBlockRect.y + wheelBlockRect.h + 8;

    rgbRowRect = { innerX, y, innerW, rgbH };
    int fieldGap = 8;
    int rgbW = std::max(36, (innerW - fieldGap * 2) / 3);
    rTextRect = { rgbRowRect.x, y + 4, rgbW, 22 };
    gTextRect = { rTextRect.x + rgbW + fieldGap, y + 4, rgbW, 22 };
    bTextRect = { gTextRect.x + rgbW + fieldGap, y + 4, std::max(36, innerW - (rgbW * 2 + fieldGap * 2)), 22 };

    y += rgbH + 8;
    int panelBottom = panelRect.y + panelRect.h - pad;

    int hexY = std::min(y, std::max(panelRect.y, panelBottom - (hexH + 8 + gradExtra + hueH)));
    hexRowRect = { innerX, hexY, innerW, hexH };
    colorInputRect = hexRowRect;

    hexTextRect = { hexRowRect.x + 34, hexRowRect.y + 6, std::max(40, innerW - 106), 22 };
    hexModeRect = { hexRowRect.x + innerW - 64, hexRowRect.y + 6, 54, 22 };
    modeDropdownRect = hexModeRect;

    if (gradientMode != GradientMode::Solid) {
        gradientPreviewRect = { innerX, hexRowRect.y + hexRowRect.h + 6, innerW, 12 };
        gradientStopsRect = { innerX, gradientPreviewRect.y + gradientPreviewRect.h + 4, innerW, 12 };
        int hueY = std::min(gradientStopsRect.y + gradientStopsRect.h + 6, panelBottom - hueH);
        hueBarRect = { innerX, hueY, innerW, hueH };
    }
    else {
        gradientPreviewRect = { 0,0,0,0 };
        gradientStopsRect = { 0,0,0,0 };
        int hueY = std::min(hexRowRect.y + hexRowRect.h + 8, panelBottom - hueH);
        hueBarRect = { innerX, hueY, innerW, hueH };
    }
}

void ColorPicker::fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rc);
}

void ColorPicker::drawRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(r, &rc);
}

void ColorPicker::drawChecker(SDL_Renderer* r, const SDL_Rect& rc, int cell) {
    SDL_Color a{ 55,55,55,255 };
    SDL_Color b{ 85,85,85,255 };
    for (int y = 0; y < rc.h; y += cell) {
        for (int x = 0; x < rc.w; x += cell) {
            bool odd = (((x / cell) + (y / cell)) & 1) != 0;
            SDL_Rect c = { rc.x + x, rc.y + y, std::min(cell, rc.w - x), std::min(cell, rc.h - y) };
            fillRect(r, c, odd ? b : a);
        }
    }
}

void ColorPicker::drawText(SDL_Renderer* r, const char* text, int x, int y, SDL_Color c) {
    if (!font || !text) return;
    SDL_Surface* s = TTF_RenderUTF8_Blended(font, text, c);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    if (!t) { SDL_FreeSurface(s); return; }
    SDL_Rect dst{ x, y, s->w, s->h };
    SDL_FreeSurface(s);
    SDL_RenderCopy(r, t, nullptr, &dst);
    SDL_DestroyTexture(t);
}

void ColorPicker::drawText(SDL_Renderer* r, const char* text) {
    drawText(r, text, panelRect.x + 8, panelRect.y + 8, SDL_Color{ 220,220,220,255 });
}

void ColorPicker::drawText(SDL_Renderer* r, const char* text, int x) {
    drawText(r, text, x, panelRect.y + 8, SDL_Color{ 220,220,220,255 });
}

void ColorPicker::drawText(SDL_Renderer* r, const char* text, int x, int y) {
    drawText(r, text, x, y, SDL_Color{ 220,220,220,255 });
}

void ColorPicker::drawHueWheel(SDL_Renderer* r) {
    const float cx = wheelRect.x + wheelRect.w * 0.5f;
    const float cy = wheelRect.y + wheelRect.h * 0.5f;
    const float R = wheelRect.w * 0.5f;
    const float ring = std::max(10.0f, R * 0.18f);
    const float Rin = R - ring;

    for (int y = wheelRect.y; y < wheelRect.y + wheelRect.h; ++y) {
        for (int x = wheelRect.x; x < wheelRect.x + wheelRect.w; ++x) {
            float dx = (x + 0.5f) - cx;
            float dy = (y + 0.5f) - cy;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d < Rin || d > R) continue;

            float ang = std::atan2(dy, dx);
            float h = (ang * 180.0f / (float)M_PI);
            if (h < 0) h += 360.0f;

            SDL_Color col = hsvToRgb(h, 1.0f, 1.0f);
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
            SDL_RenderDrawPoint(r, x, y);
        }
    }

    float ang = (H * (float)M_PI / 180.0f);
    float mx = cx + std::cos(ang) * (R - ring * 0.5f);
    float my = cy + std::sin(ang) * (R - ring * 0.5f);

    SDL_Rect outer{ (int)mx - 6, (int)my - 6, 12, 12 };
    drawRect(r, outer, SDL_Color{ 0,0,0,255 });
    SDL_Rect inner{ (int)mx - 5, (int)my - 5, 10, 10 };
    drawRect(r, inner, SDL_Color{ 255,255,255,255 });
}

void ColorPicker::drawSVSquare(SDL_Renderer* r) {
    for (int y = 0; y < svRect.h; ++y) {
        float v = 1.0f - (float)y / (float)(svRect.h - 1);
        for (int x = 0; x < svRect.w; ++x) {
            float s = (float)x / (float)(svRect.w - 1);
            SDL_Color c = hsvToRgb(H, s, v);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
            SDL_RenderDrawPoint(r, svRect.x + x, svRect.y + y);
        }
    }
    drawRect(r, svRect, SDL_Color{ 90,90,90,255 });

    int selX = svRect.x + (int)std::round(S * (svRect.w - 1));
    int selY = svRect.y + (int)std::round((1.0f - V) * (svRect.h - 1));
    SDL_Rect o{ selX - 6, selY - 6, 12, 12 };
    drawRect(r, o, SDL_Color{ 0,0,0,255 });
    SDL_Rect i{ selX - 5, selY - 5, 10, 10 };
    drawRect(r, i, SDL_Color{ 255,255,255,255 });
}

void ColorPicker::drawAlphaStrip(SDL_Renderer* r) {
    drawChecker(r, alphaRect, 6);

    SDL_Color base = hsvToRgb(H, S, V);
    for (int y = 0; y < alphaRect.h; ++y) {
        float t = (float)y / (float)(alphaRect.h - 1);
        Uint8 a = (Uint8)std::round((1.0f - t) * 255.0f);
        SDL_SetRenderDrawColor(r, base.r, base.g, base.b, a);
        SDL_RenderDrawLine(r, alphaRect.x, alphaRect.y + y, alphaRect.x + alphaRect.w - 1, alphaRect.y + y);
    }

    drawRect(r, alphaRect, SDL_Color{ 90,90,90,255 });

    int ky = alphaRect.y + (int)std::round((1.0f - opacity) * (alphaRect.h - 1));
    SDL_Rect knob{ alphaRect.x - 2, ky - 2, alphaRect.w + 4, 4 };
    fillRect(r, knob, SDL_Color{ 230,230,230,255 });
    drawRect(r, knob, SDL_Color{ 0,0,0,255 });
}

void ColorPicker::drawHueBar(SDL_Renderer* r) {
    for (int x = 0; x < hueBarRect.w; ++x) {
        float h = (float)x / (float)(hueBarRect.w - 1) * 360.0f;
        SDL_Color c = hsvToRgb(h, 1.0f, 1.0f);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        SDL_RenderDrawLine(r, hueBarRect.x + x, hueBarRect.y, hueBarRect.x + x, hueBarRect.y + hueBarRect.h - 1);
    }
    drawRect(r, hueBarRect, SDL_Color{ 90,90,90,255 });

    int mx = hueBarRect.x + (int)std::round((H / 360.0f) * (hueBarRect.w - 1));
    SDL_Rect m{ mx - 2, hueBarRect.y - 2, 4, hueBarRect.h + 4 };
    drawRect(r, m, SDL_Color{ 0,0,0,255 });
    SDL_Rect m2{ mx - 1, hueBarRect.y - 1, 2, hueBarRect.h + 2 };
    drawRect(r, m2, SDL_Color{ 255,255,255,255 });
}

void ColorPicker::drawGradientControls(SDL_Renderer* r) {
    if (gradientMode == GradientMode::Solid) return;
    if (gradientPreviewRect.w <= 0 || gradientStopsRect.w <= 0) return;

    drawText(r, "Gradient Stops", gradientPreviewRect.x, gradientPreviewRect.y - 16, SDL_Color{ 175,180,200,255 });

    for (int x = 0; x < gradientPreviewRect.w; ++x) {
        float t = (gradientPreviewRect.w > 1) ? (float)x / (float)(gradientPreviewRect.w - 1) : 0.0f;
        SDL_Color c = gradientSample(t);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
        SDL_RenderDrawLine(r, gradientPreviewRect.x + x, gradientPreviewRect.y,
            gradientPreviewRect.x + x, gradientPreviewRect.y + gradientPreviewRect.h - 1);
    }
    drawRect(r, gradientPreviewRect, SDL_Color{ 90,90,90,255 });

    fillRect(r, gradientStopsRect, SDL_Color{ 24,24,24,255 });
    drawRect(r, gradientStopsRect, SDL_Color{ 70,70,70,255 });

    for (int i = 0; i < kGradientStopCount; ++i) {
        int x = gradientStopsRect.x + (int)std::lround(gradientStopPos[i] * (gradientStopsRect.w - 1));
        SDL_Rect h{ x - 4, gradientStopsRect.y + 1, 8, gradientStopsRect.h - 2 };
        SDL_Color col = gradientStopColor[i];
        fillRect(r, h, col);
        drawRect(r, h, (i == selectedGradientStop) ? SDL_Color{ 255,255,255,255 } : SDL_Color{ 25,25,25,255 });
    }
}

void ColorPicker::draw(SDL_Renderer* r) {
    if (!r) return;

    SDL_BlendMode oldBlend;
    SDL_GetRenderDrawBlendMode(r, &oldBlend);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    fillRect(r, panelRect, SDL_Color{ 18,18,18,255 });
    fillRect(r, purpleBarRect, SDL_Color{ 92,54,220,255 });

    SDL_Rect topBg = { topRowRect.x, topRowRect.y, topRowRect.w, topRowRect.h };
    fillRect(r, topBg, SDL_Color{ 24,24,24,255 });
    drawRect(r, topBg, SDL_Color{ 55,55,55,255 });

    drawChecker(r, swatchRect, 6);
    SDL_Rect swIn = swatchRect; swIn.x += 3; swIn.y += 3; swIn.w -= 6; swIn.h -= 6;

    SDL_Color baseRGB = hsvToRgb(H, S, V);
    SDL_Color swCol = baseRGB;
    swCol.a = (Uint8)std::round(opacity * 255.0f);

    fillRect(r, swIn, SDL_Color{ baseRGB.r, baseRGB.g, baseRGB.b, 255 });
    SDL_SetRenderDrawColor(r, baseRGB.r, baseRGB.g, baseRGB.b, swCol.a);
    SDL_RenderFillRect(r, &swIn);
    drawRect(r, swatchRect, SDL_Color{ 90,90,90,255 });

    drawText(r, "Opacity", opacityLabelRect.x, opacityLabelRect.y, SDL_Color{ 220,220,220,255 });

    char pct[32];
    std::snprintf(pct, sizeof(pct), "%d%%", (int)std::round(opacity * 100.0f));
    drawText(r, pct, opacityPctRect.x + 2, opacityPctRect.y, SDL_Color{ 220,220,220,255 });

    fillRect(r, opacityTrackRect, SDL_Color{ 40,40,40,255 });
    drawRect(r, opacityTrackRect, SDL_Color{ 65,65,65,255 });

    int kx = opacityTrackRect.x + (int)std::round(opacity * (opacityTrackRect.w - 1));
    opacityKnobRect = { kx - 6, opacityTrackRect.y - 4, 12, opacityTrackRect.h + 8 };
    fillRect(r, opacityKnobRect, SDL_Color{ 230,230,230,255 });
    drawRect(r, opacityKnobRect, SDL_Color{ 0,0,0,255 });

    fillRect(r, gradientRowRect, SDL_Color{ 24,24,24,255 });
    drawRect(r, gradientRowRect, SDL_Color{ 55,55,55,255 });

    gradientEnabled = (gradientMode != GradientMode::Solid);

    auto drawModeBtn = [&](const SDL_Rect& rc, const char* txt, bool active) {
        fillRect(r, rc, active ? SDL_Color{ 82,124,230,255 } : SDL_Color{ 30,30,30,255 });
        drawRect(r, rc, active ? SDL_Color{ 150,180,255,255 } : SDL_Color{ 70,70,70,255 });
        drawText(r, txt, rc.x + 8, rc.y + 5, SDL_Color{ 220,220,220,255 });
        };

    drawModeBtn(gradientSolidRect, "Solid", gradientMode == GradientMode::Solid);
    drawModeBtn(gradientLinearRect, "Linear", gradientMode == GradientMode::Linear);
    drawModeBtn(gradientRadialRect, "Radial", gradientMode == GradientMode::Radial);

    fillRect(r, wheelBlockRect, SDL_Color{ 20,20,20,255 });
    drawHueWheel(r);
    drawSVSquare(r);
    drawAlphaStrip(r);

    fillRect(r, rgbRowRect, SDL_Color{ 18,18,18,255 });
    drawRect(r, rgbRowRect, SDL_Color{ 55,55,55,255 });

    auto drawInputField = [&](const SDL_Rect& rc, const char* label, const std::string& value, bool active) {
        SDL_Color bg = active ? SDL_Color{ 22,26,44,255 } : SDL_Color{ 24,24,24,255 };
        SDL_Color bd = active ? SDL_Color{ 120,160,255,150 } : SDL_Color{ 70,70,70,255 };
        fillRect(r, rc, bg);
        drawRect(r, rc, bd);
        drawText(r, label, rc.x + 5, rc.y + 3, SDL_Color{ 160,168,190,255 });

        std::string shown = value;
        if (active) shown += "|";
        drawText(r, shown.c_str(), rc.x + 17, rc.y + 3, SDL_Color{ 230,235,245,255 });
        };

    drawInputField(rTextRect, "R", rInput, activeField == EditField::R);
    drawInputField(gTextRect, "G", gInput, activeField == EditField::G);
    drawInputField(bTextRect, "B", bInput, activeField == EditField::B);

    fillRect(r, hexRowRect, SDL_Color{ 18,18,18,255 });
    drawRect(r, hexRowRect, activeField == EditField::Hex ? SDL_Color{ 120,160,255,150 } : SDL_Color{ 55,55,55,255 });

    SDL_Rect hexInputRc{ hexTextRect.x - 4, hexTextRect.y - 2, hexTextRect.w + 8, 26 };
    fillRect(r, hexInputRc, activeField == EditField::Hex ? SDL_Color{ 20,24,40,255 } : SDL_Color{ 24,24,24,255 });
    drawRect(r, hexInputRc, activeField == EditField::Hex ? SDL_Color{ 120,160,255,150 } : SDL_Color{ 70,70,70,255 });

    drawText(r, "#", hexRowRect.x + 12, hexRowRect.y + 6, SDL_Color{ 220,220,220,255 });
    std::string shownHex = hexInput;
    if (activeField == EditField::Hex) shownHex += "|";
    drawText(r, shownHex.c_str(), hexTextRect.x, hexTextRect.y, SDL_Color{ 230,235,245,255 });

    valueMode = ValueMode::Hex;
    fillRect(r, hexModeRect, SDL_Color{ 30,30,30,255 });
    drawRect(r, hexModeRect, SDL_Color{ 70,70,70,255 });
    drawText(r, "HEX", hexModeRect.x + 10, hexModeRect.y + 2, SDL_Color{ 220,220,220,255 });

    drawGradientControls(r);
    drawHueBar(r);

    SDL_SetRenderDrawBlendMode(r, oldBlend);
}

void ColorPicker::updateSVFromMouse(int mx, int my) {
    float s = (mx - svRect.x) / (float)(svRect.w - 1);
    float v = 1.0f - (my - svRect.y) / (float)(svRect.h - 1);
    S = clamp01(s);
    V = clamp01(v);
    pushToEngine();
}

void ColorPicker::updateHueFromMouse(int mx, int my) {
    const float cx = wheelRect.x + wheelRect.w * 0.5f;
    const float cy = wheelRect.y + wheelRect.h * 0.5f;
    float dx = (mx + 0.5f) - cx;
    float dy = (my + 0.5f) - cy;
    float ang = std::atan2(dy, dx);
    float h = ang * 180.0f / (float)M_PI;
    if (h < 0) h += 360.0f;
    H = h;
    pushToEngine();
}

void ColorPicker::updateAlphaFromMouse(int mx, int my) {
    (void)mx;
    float t = (my - alphaRect.y) / (float)(alphaRect.h - 1);
    opacity = clamp01(1.0f - t);
    pushToEngine();
}

void ColorPicker::updateOpacityFromMouse(int mx, int my) {
    (void)my;
    float t = (mx - opacityTrackRect.x) / (float)(opacityTrackRect.w - 1);
    opacity = clamp01(t);
    pushToEngine();
}

void ColorPicker::updateGradientStopFromMouse(int mx) {
    if (gradientStopsRect.w <= 1) return;
    float t = (float)(mx - gradientStopsRect.x) / (float)(gradientStopsRect.w - 1);
    t = clamp01(t);

    selectedGradientStop = clampi(selectedGradientStop, 0, kGradientStopCount - 1);
    const float minGap = 0.05f;
    float lo = (selectedGradientStop > 0) ? (gradientStopPos[selectedGradientStop - 1] + minGap) : 0.0f;
    float hi = (selectedGradientStop < kGradientStopCount - 1) ? (gradientStopPos[selectedGradientStop + 1] - minGap) : 1.0f;
    gradientStopPos[selectedGradientStop] = clampf(t, lo, hi);
}

bool ColorPicker::applyHexInput() {
    std::string clean;
    clean.reserve(6);
    for (char ch : hexInput) {
        if (std::isxdigit((unsigned char)ch))
            clean.push_back((char)std::toupper((unsigned char)ch));
    }

    if (clean.size() != 6) return false;

    int rv = 0, gv = 0, bv = 0;
    if (std::sscanf(clean.c_str(), "%02X%02X%02X", &rv, &gv, &bv) != 3) return false;

    SDL_Color c{ (Uint8)rv, (Uint8)gv, (Uint8)bv, (Uint8)std::round(opacity * 255.0f) };
    setColor(c);
    pushToEngine();
    return true;
}

bool ColorPicker::applyRgbInput(EditField field) {
    std::string* src = nullptr;
    if (field == EditField::R) src = &rInput;
    if (field == EditField::G) src = &gInput;
    if (field == EditField::B) src = &bInput;
    if (!src || src->empty()) return false;

    int val = 0;
    if (std::sscanf(src->c_str(), "%d", &val) != 1) return false;
    val = clampi(val, 0, 255);

    SDL_Color rgb = hsvToRgb(H, S, V);
    if (field == EditField::R) rgb.r = (Uint8)val;
    if (field == EditField::G) rgb.g = (Uint8)val;
    if (field == EditField::B) rgb.b = (Uint8)val;
    rgb.a = (Uint8)std::round(opacity * 255.0f);

    setColor(rgb);
    pushToEngine();
    return true;
}

bool ColorPicker::applyRgbInputs() {
    bool okR = applyRgbInput(EditField::R);
    bool okG = applyRgbInput(EditField::G);
    bool okB = applyRgbInput(EditField::B);
    return okR || okG || okB;
}

bool ColorPicker::handleTextInput(const char* text) {
    if (activeField == EditField::None || !text) return false;

    bool changed = false;

    if (replaceFieldOnType) {
        if (activeField == EditField::Hex) hexInput.clear();
        else if (activeField == EditField::R) rInput.clear();
        else if (activeField == EditField::G) gInput.clear();
        else if (activeField == EditField::B) bInput.clear();
        replaceFieldOnType = false;
    }

    for (const char* p = text; *p; ++p) {
        char ch = *p;

        if (activeField == EditField::Hex) {
            if (ch == '#') continue;
            if (!std::isxdigit((unsigned char)ch)) continue;
            if (hexInput.size() >= 6) continue;
            hexInput.push_back((char)std::toupper((unsigned char)ch));
            changed = true;
            continue;
        }

        if (!std::isdigit((unsigned char)ch)) continue;
        std::string* target = (activeField == EditField::R) ? &rInput : (activeField == EditField::G ? &gInput : &bInput);
        if (target->size() >= 3) continue;
        target->push_back(ch);
        changed = true;
    }

    if (changed) {
        if (activeField == EditField::Hex) applyHexInput();
        else applyRgbInput(activeField);
    }
    return changed;
}

bool ColorPicker::handleKeyDown(SDL_Keycode key) {
    if (activeField == EditField::None) return false;

    if (key == SDLK_BACKSPACE) {
        replaceFieldOnType = false;

        if (activeField == EditField::Hex) {
            if (!hexInput.empty()) {
                hexInput.pop_back();
                applyHexInput();
            }
        }
        else {
            std::string* target = (activeField == EditField::R) ? &rInput : (activeField == EditField::G ? &gInput : &bInput);
            if (!target->empty()) {
                target->pop_back();
                applyRgbInput(activeField);
            }
        }

        if (editingField == EditField::RGB && !rgbInput.empty()) {
            rgbInput.pop_back();
            applyRgbInputs();
        }
        return true;
    }

    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        stopFieldEditing(true);
        return true;
    }

    if (key == SDLK_ESCAPE) {
        stopFieldEditing(false);
        return true;
    }

    if (key == SDLK_TAB) {
        if (activeField == EditField::Hex) beginFieldEditing(EditField::R);
        else if (activeField == EditField::R) beginFieldEditing(EditField::G);
        else if (activeField == EditField::G) beginFieldEditing(EditField::B);
        else beginFieldEditing(EditField::Hex);
        return true;
    }

    return false;
}

bool ColorPicker::handleMouseDown(int mx, int my) {
    SDL_Rect hexInputRc{ hexTextRect.x - 4, hexTextRect.y - 2, hexTextRect.w + 8, 26 };

    if (pointIn(hexInputRc, mx, my)) { beginFieldEditing(EditField::Hex); return true; }
    if (pointIn(rTextRect, mx, my)) { beginFieldEditing(EditField::R); return true; }
    if (pointIn(gTextRect, mx, my)) { beginFieldEditing(EditField::G); return true; }
    if (pointIn(bTextRect, mx, my)) { beginFieldEditing(EditField::B); return true; }

    if (pointIn(gradientSolidRect, mx, my)) { gradientMode = GradientMode::Solid;  syncGradientToEngine(); layout(panelRect); return true; }
    if (pointIn(gradientLinearRect, mx, my)) { gradientMode = GradientMode::Linear; syncGradientToEngine(); layout(panelRect); return true; }
    if (pointIn(gradientRadialRect, mx, my)) { gradientMode = GradientMode::Radial; syncGradientToEngine(); layout(panelRect); return true; }

    if (gradientMode != GradientMode::Solid && pointIn(gradientStopsRect, mx, my)) {
        int best = 0;
        int bestDist = 999999;
        for (int i = 0; i < kGradientStopCount; ++i) {
            int sx = gradientStopsRect.x + (int)std::lround(gradientStopPos[i] * (gradientStopsRect.w - 1));
            int d = std::abs(mx - sx);
            if (d < bestDist) { bestDist = d; best = i; }
        }
        selectedGradientStop = best;
        draggingGradientStop = true;
        updateGradientStopFromMouse(mx);
        updateSelectedGradientStopColor();
        syncGradientToEngine();
        return true;
    }

    if (activeField != EditField::None) stopFieldEditing(true);

    if (pointIn(opacityTrackRect, mx, my) || pointIn(opacityKnobRect, mx, my)) {
        draggingOpacity = true;
        updateOpacityFromMouse(mx, my);
        return true;
    }

    if (pointIn(svRect, mx, my)) {
        draggingSV = true;
        updateSVFromMouse(mx, my);
        return true;
    }

    if (pointIn(alphaRect, mx, my)) {
        draggingAlpha = true;
        updateAlphaFromMouse(mx, my);
        return true;
    }

    if (pointIn(hueBarRect, mx, my)) {
        float t = (mx - hueBarRect.x) / (float)(hueBarRect.w - 1);
        H = clamp01(t) * 360.0f;
        pushToEngine();
        return true;
    }

    
    {
        const float cx = wheelRect.x + wheelRect.w * 0.5f;
        const float cy = wheelRect.y + wheelRect.h * 0.5f;
        const float R = wheelRect.w * 0.5f;
        const float ring = std::max(10.0f, R * 0.18f);
        const float Rin = R - ring;

        float dx = (mx + 0.5f) - cx;
        float dy = (my + 0.5f) - cy;
        float d = std::sqrt(dx * dx + dy * dy);

        if (d >= Rin && d <= R) {
            draggingHue = true;
            updateHueFromMouse(mx, my);
            return true;
        }
    }

    return false;
}

bool ColorPicker::handleMouseUp(int, int) {
    bool was = draggingSV || draggingHue || draggingAlpha || draggingOpacity || draggingGradientStop;
    draggingSV = draggingHue = draggingAlpha = draggingOpacity = draggingGradientStop = false;
    return was;
}

bool ColorPicker::handleMouseMotion(int mx, int my) {
    if (draggingGradientStop) { updateGradientStopFromMouse(mx); syncGradientToEngine(); return true; }
    if (draggingOpacity) { updateOpacityFromMouse(mx, my); return true; }
    if (draggingSV) { updateSVFromMouse(mx, my); return true; }
    if (draggingAlpha) { updateAlphaFromMouse(mx, my); return true; }
    if (draggingHue) { updateHueFromMouse(mx, my); return true; }
    return false;
}


SDL_Color ColorPicker::sampleAtPosition(float x, float y) const {
    (void)x; (void)y; 

    SDL_Color c = hsvToRgb(H, S, V);
    c.a = (Uint8)std::round(clamp01(opacity) * 255.0f);
    return c;
}
