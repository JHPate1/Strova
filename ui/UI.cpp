/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/UI.cpp
   Module:      Ui
   Purpose:     Basic UI widget drawing used by lightweight controls.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */

#include "UI.h"

#include <algorithm>
#include <cmath>

void drawRoundedRect(SDL_Renderer* renderer, SDL_Rect rect, int radius, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    SDL_Rect inner{rect.x + radius, rect.y + radius, rect.w - 2 * radius, rect.h - 2 * radius};
    SDL_RenderFillRect(renderer, &inner);

    SDL_Rect top{rect.x + radius, rect.y, rect.w - 2 * radius, radius};
    SDL_Rect bottom{rect.x + radius, rect.y + rect.h - radius, rect.w - 2 * radius, radius};
    SDL_RenderFillRect(renderer, &top);
    SDL_RenderFillRect(renderer, &bottom);

    SDL_Rect left{rect.x, rect.y + radius, radius, rect.h - 2 * radius};
    SDL_Rect right{rect.x + rect.w - radius, rect.y + radius, radius, rect.h - 2 * radius};
    SDL_RenderFillRect(renderer, &left);
    SDL_RenderFillRect(renderer, &right);

    const auto drawCircle = [&](int cx, int cy)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            for (int y = -radius; y <= radius; ++y)
            {
                if (x * x + y * y <= radius * radius)
                {
                    SDL_RenderDrawPoint(renderer, cx + x, cy + y);
                }
            }
        }
    };

    drawCircle(rect.x + radius, rect.y + radius);
    drawCircle(rect.x + rect.w - radius, rect.y + radius);
    drawCircle(rect.x + radius, rect.y + rect.h - radius);
    drawCircle(rect.x + rect.w - radius, rect.y + rect.h - radius);
}

Button::Button(int x, int y, int w, int h, const std::string& text, SDL_Color col, std::function<void()> cb)
    : label(text), onClick(std::move(cb))
{
    rect = {x, y, w, h};
    color = col;
    hoverColor = {
        static_cast<Uint8>(std::min(255, col.r + 30)),
        static_cast<Uint8>(std::min(255, col.g + 30)),
        static_cast<Uint8>(std::min(255, col.b + 30)),
        col.a,
    };
    clickColor = {
        static_cast<Uint8>(std::max(0, col.r - 30)),
        static_cast<Uint8>(std::max(0, col.g - 30)),
        static_cast<Uint8>(std::max(0, col.b - 30)),
        col.a,
    };
}

void Button::draw(SDL_Renderer* renderer)
{
    SDL_Color drawColor = color;
    if (clicked)
    {
        drawColor = clickColor;
    }
    else if (hovered)
    {
        drawColor = hoverColor;
    }

    drawRoundedRect(renderer, rect, rect.h / 2, drawColor);
}

void Button::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_MOUSEMOTION)
    {
        const int mx = event.motion.x;
        const int my = event.motion.y;
        hovered = mx >= rect.x && mx <= rect.x + rect.w && my >= rect.y && my <= rect.y + rect.h;
    }

    if (event.type == SDL_MOUSEBUTTONDOWN && hovered)
    {
        clicked = true;
    }

    if (event.type == SDL_MOUSEBUTTONUP)
    {
        if (clicked && hovered && onClick)
        {
            onClick();
        }
        clicked = false;
    }
}

Slider::Slider(int x, int y, int w, int h, int minV, int maxV, int startV, std::function<void(int)> cb)
    : minValue(minV), maxValue(maxV), value(startV), onChange(std::move(cb))
{
    barRect = {x, y, w, h / 3};
    thumbRect = {x + (w * (value - minV)) / (maxV - minV) - h / 2, y - h / 3, h, h};
}

void Slider::draw(SDL_Renderer* renderer)
{
    drawRoundedRect(renderer, barRect, barRect.h / 2, {180, 180, 180, 255});
    drawRoundedRect(renderer, thumbRect, thumbRect.w / 2, {100, 100, 255, 255});
}

void Slider::handleEvent(const SDL_Event& event)
{
    int mx = 0;
    int my = 0;

    if (event.type == SDL_MOUSEMOTION)
    {
        mx = event.motion.x;
        my = event.motion.y;
    }
    else if (event.type == SDL_MOUSEBUTTONDOWN)
    {
        mx = event.button.x;
        my = event.button.y;
        if (mx >= thumbRect.x && mx <= thumbRect.x + thumbRect.w &&
            my >= thumbRect.y && my <= thumbRect.y + thumbRect.h)
        {
            dragging = true;
        }
    }
    else if (event.type == SDL_MOUSEBUTTONUP)
    {
        dragging = false;
    }

    if (!dragging)
    {
        return;
    }

    mx = std::max(barRect.x, std::min(barRect.x + barRect.w, mx));
    value = minValue + (maxValue - minValue) * (mx - barRect.x) / barRect.w;
    thumbRect.x = mx - thumbRect.w / 2;

    if (onChange)
    {
        onChange(value);
    }
}
