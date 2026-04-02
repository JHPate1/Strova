/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/ToolTip.h
   Module:      Ui
   Purpose:     Tooltip state and public helpers.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>

class Tooltip {
public:
    Tooltip() = default;
    ~Tooltip();

    void setFont(TTF_Font* f);

    void beginFrame();

    void show(const std::string& text, int mouseX, int mouseY);

    void draw(SDL_Renderer* r, int windowW, int windowH);

    void shutdown();

private:
    void rebuildTexture(SDL_Renderer* r);

private:
    TTF_Font* font = nullptr;

    SDL_Texture* textTex = nullptr;
    int textW = 0;
    int textH = 0;

    std::string currentText;
    std::string pendingText;

    bool visible = false;
    bool dirty = false;

    int mouseX = 0;
    int mouseY = 0;

    int padding = 8;
    int offsetX = 14;
    int offsetY = 14;

    SDL_Color textColor{ 235, 235, 235, 255 };
    SDL_Color bgColor{ 15,  15,  15,  235 };
    SDL_Color borderColor{ 50, 50, 50, 255 };
};
