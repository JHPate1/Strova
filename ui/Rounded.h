/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/Rounded.h
   Module:      Ui
   Purpose:     Rounded shape helpers used throughout the UI.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL.h>
#include <algorithm>
#include <cmath>

namespace ui {

    inline bool pointInFRect(float px, float py, const SDL_FRect& r)
    {
        return (px >= r.x && px <= (r.x + r.w) &&
            py >= r.y && py <= (r.y + r.h));
    }

    inline bool pointInFRect(int px, int py, const SDL_FRect& r)
    {
        return pointInFRect((float)px, (float)py, r);
    }

    inline void fillVerticalGradient(SDL_Renderer* r,
        const SDL_Rect& rect,
        SDL_Color top,
        SDL_Color bottom)
    {
        if (!r) return;
        if (rect.w <= 0 || rect.h <= 0) return;

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

        for (int y = 0; y < rect.h; ++y) {
            float t = (rect.h <= 1) ? 0.0f : (float)y / (float)(rect.h - 1);

            Uint8 rr = (Uint8)(top.r + (bottom.r - top.r) * t);
            Uint8 gg = (Uint8)(top.g + (bottom.g - top.g) * t);
            Uint8 bb = (Uint8)(top.b + (bottom.b - top.b) * t);
            Uint8 aa = (Uint8)(top.a + (bottom.a - top.a) * t);

            SDL_SetRenderDrawColor(r, rr, gg, bb, aa);
            SDL_RenderDrawLine(r,
                rect.x, rect.y + y,
                rect.x + rect.w - 1, rect.y + y
            );
        }
    }

    inline void fillRoundedRect(SDL_Renderer* r,
        const SDL_FRect& rect,
        float radius,
        SDL_Color color)
    {
        if (!r) return;
        if (rect.w <= 0 || rect.h <= 0) return;

        radius = std::max(0.0f, std::min(radius, std::min(rect.w, rect.h) * 0.5f));

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);

        float left = rect.x;
        float right = rect.x + rect.w;
        float top = rect.y;
        float bottom = rect.y + rect.h;
        float rad = radius;

        float cTLx = left + rad, cTLy = top + rad;
        float cTRx = right - rad, cTRy = top + rad;
        float cBLx = left + rad, cBLy = bottom - rad;
        float cBRx = right - rad, cBRy = bottom - rad;

        int y0 = (int)std::floor(top);
        int y1 = (int)std::ceil(bottom);

        for (int y = y0; y < y1; ++y) {
            float fy = (float)y + 0.5f;

            float sx = left;
            float ex = right;

            if (rad > 0.0f) {
                if (fy < top + rad) {
                    float dy = fy - cTLy;
                    float dx = std::sqrt(std::max(0.0f, rad * rad - dy * dy));
                    sx = std::max(sx, cTLx - dx);

                    dy = fy - cTRy;
                    dx = std::sqrt(std::max(0.0f, rad * rad - dy * dy));
                    ex = std::min(ex, cTRx + dx);
                }
                else if (fy > bottom - rad) {
                    float dy = fy - cBLy;
                    float dx = std::sqrt(std::max(0.0f, rad * rad - dy * dy));
                    sx = std::max(sx, cBLx - dx);

                    dy = fy - cBRy;
                    dx = std::sqrt(std::max(0.0f, rad * rad - dy * dy));
                    ex = std::min(ex, cBRx + dx);
                }
            }

            int ix0 = (int)std::ceil(sx);
            int ix1 = (int)std::floor(ex);

            if (ix1 >= ix0) {
                SDL_RenderDrawLine(r, ix0, y, ix1, y);
            }
        }
    }

}
