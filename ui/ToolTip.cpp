/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/ToolTip.cpp
   Module:      Ui
   Purpose:     Tooltip timing and drawing helpers.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "Tooltip.h"
#include <algorithm>
#include "Rounded.h"

Tooltip::~Tooltip() {
    shutdown();
}

void Tooltip::shutdown() {
    if (textTex) {
        SDL_DestroyTexture(textTex);
        textTex = nullptr;
    }
    textW = textH = 0;
    currentText.clear();
    pendingText.clear();
    visible = false;
    dirty = false;
}

void Tooltip::setFont(TTF_Font* f) {
    font = f;
    dirty = true;
}

void Tooltip::beginFrame() {
    visible = false;
    pendingText.clear();
}

void Tooltip::show(const std::string& text, int mx, int my) {
    if (!font) return;

    visible = true;
    mouseX = mx;
    mouseY = my;

    pendingText = text;

    if (pendingText != currentText) {
        dirty = true;
    }
}

void Tooltip::rebuildTexture(SDL_Renderer* r) {
    if (!font) return;

    if (textTex) {
        SDL_DestroyTexture(textTex);
        textTex = nullptr;
    }
    textW = textH = 0;

    currentText = pendingText;

    if (currentText.empty()) return;

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, currentText.c_str(), textColor);
    if (!surf) {
        currentText.clear();
        return;
    }

    textW = surf->w;
    textH = surf->h;

    textTex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);

    if (!textTex) {
        textW = textH = 0;
        currentText.clear();
        return;
    }

    SDL_SetTextureBlendMode(textTex, SDL_BLENDMODE_BLEND);
}

void Tooltip::draw(SDL_Renderer* r, int windowW, int windowH) {
    if (!visible) return;
    if (!font) return;

    if (dirty) {
        rebuildTexture(r);
        dirty = false;
    }

    if (!textTex || textW <= 0 || textH <= 0) return;

    SDL_Rect box;
    box.w = textW + padding * 2;
    box.h = textH + padding * 2;

    box.x = mouseX + offsetX;
    box.y = mouseY + offsetY;

    if (box.x + box.w > windowW) box.x = std::max(0, windowW - box.w);
    if (box.y + box.h > windowH) box.y = std::max(0, windowH - box.h);

    SDL_FRect bubble{ (float)box.x, (float)box.y, (float)box.w, (float)box.h };
    ui::fillRoundedRect(r, bubble, 8.0f, bgColor);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
    SDL_RenderDrawRect(r, &box);

    SDL_Rect dst;
    dst.x = box.x + padding;
    dst.y = box.y + padding;
    dst.w = textW;
    dst.h = textH;

    SDL_RenderCopy(r, textTex, nullptr, &dst);
}
