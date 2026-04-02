/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/IconButton.cpp
   Module:      Ui
   Purpose:     Icon button drawing and interaction handling.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "IconButton.h"

bool IconButton::hitTest(int mx, int my) const {
    SDL_Point p{ mx, my };
    return SDL_PointInRect(&p, &rect);
}

bool IconButton::handleEvent(const SDL_Event& e, int mx, int my) {
    if (!enabled) { pressed = false; return false; }

    bool inside = hitTest(mx, my);

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (inside) pressed = true;
        return false;
    }

    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        bool clicked = pressed && inside;
        pressed = false;
        return clicked;
    }

    return false;
}

void IconButton::draw(SDL_Renderer* r, bool hovered) const {
    if (!enabled) {
        SDL_SetRenderDrawColor(r, 35, 35, 35, 255);
    }
    else if (pressed) {
        SDL_SetRenderDrawColor(r, 28, 28, 28, 255);
    }
    else if (hovered) {
        SDL_SetRenderDrawColor(r, 60, 60, 60, 255);
    }
    else {
        SDL_SetRenderDrawColor(r, 45, 45, 45, 255);
    }
    SDL_RenderFillRect(r, &rect);

    

    if (texture) {
        int pad = 6;
        SDL_Rect dst{
            rect.x + pad, rect.y + pad,
            rect.w - pad * 2, rect.h - pad * 2
        };

        Uint8 oldA = 255;
        SDL_GetTextureAlphaMod(texture, &oldA);

        if (!enabled) SDL_SetTextureAlphaMod(texture, 80);
        else          SDL_SetTextureAlphaMod(texture, 255);

        SDL_RenderCopy(r, texture, nullptr, &dst);

        SDL_SetTextureAlphaMod(texture, oldA);
    }
}
