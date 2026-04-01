/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/UIButton.h
   Module:      Ui
   Purpose:     Small button helper used by parts of the UI.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */

#pragma once

#include <SDL.h>

class UIButton
{
public:
    void setRect(const SDL_Rect& newRect) { rect = newRect; }
    void setTexture(SDL_Texture* texture) { tex = texture; }
    void setEnabled(bool isEnabled) { enabled = isEnabled; }

    bool hitTest(int mx, int my) const
    {
        return enabled &&
            mx >= rect.x && mx < rect.x + rect.w &&
            my >= rect.y && my < rect.y + rect.h;
    }

    bool handleEvent(SDL_Event& event, int mx, int my)
    {
        if (!enabled)
        {
            return false;
        }

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT && hitTest(mx, my))
        {
            pressed = true;
            return true;
        }

        if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
        {
            pressed = false;
        }

        return false;
    }

    void draw(SDL_Renderer* renderer, bool hover)
    {
        const SDL_Color bg =
            !enabled ? SDL_Color{40, 40, 50, 255} :
            pressed ? SDL_Color{70, 70, 110, 255} :
            hover ? SDL_Color{55, 55, 85, 255} :
                      SDL_Color{45, 45, 70, 255};

        SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 255);
        SDL_RenderFillRect(renderer, &rect);

        SDL_SetRenderDrawColor(renderer, 90, 90, 120, 120);
        SDL_RenderDrawRect(renderer, &rect);

        if (!tex)
        {
            return;
        }

        SDL_Rect dst = rect;
        dst.x += 6;
        dst.y += 6;
        dst.w -= 12;
        dst.h -= 12;
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
    }

private:
    SDL_Rect rect{};
    SDL_Texture* tex = nullptr;
    bool enabled = true;
    bool pressed = false;
};
