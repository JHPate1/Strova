/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/UI.h
   Module:      Ui
   Purpose:     Simple UI element declarations used by shared widgets.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */

#pragma once

#include <SDL.h>

#include <functional>
#include <string>
#include <vector>

void drawRoundedRect(SDL_Renderer* renderer, SDL_Rect rect, int radius, SDL_Color color);

class UIElement
{
public:
    virtual ~UIElement() = default;

    virtual void draw(SDL_Renderer* renderer) = 0;
    virtual void handleEvent(const SDL_Event& event) = 0;
};

class Button : public UIElement
{
public:
    SDL_Rect rect{};
    SDL_Color color{};
    SDL_Color hoverColor{};
    SDL_Color clickColor{};
    std::string label;
    bool hovered = false;
    bool clicked = false;
    std::function<void()> onClick;

    Button(int x, int y, int w, int h, const std::string& text, SDL_Color col, std::function<void()> cb);

    void draw(SDL_Renderer* renderer) override;
    void handleEvent(const SDL_Event& event) override;
};

class Slider : public UIElement
{
public:
    SDL_Rect barRect{};
    SDL_Rect thumbRect{};
    int minValue = 0;
    int maxValue = 0;
    int value = 0;
    std::function<void(int)> onChange;
    bool dragging = false;

    Slider(int x, int y, int w, int h, int minV, int maxV, int startV, std::function<void(int)> cb);

    void draw(SDL_Renderer* renderer) override;
    void handleEvent(const SDL_Event& event) override;
};
