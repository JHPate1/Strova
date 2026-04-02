/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/SplashScreen.h
   Module:      Ui
   Purpose:     Splash screen state and entry points.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>

class SplashScreen {
public:
    ~SplashScreen();

    void start(Uint32 nowMs);
    void handleEvent(const SDL_Event& e);
    void update(Uint32 nowMs);

    bool finished() const { return done; }

    void draw(SDL_Renderer* r, int w, int h, TTF_Font* font);

    void setStatusText(const std::string& primary, const std::string& secondary = std::string());
    const std::string& getStatusText() const { return statusPrimary; }

private:
    Uint32 startMs = 0;
    bool done = false;
    float progress = 0.0f; 
    Uint32 durationMs = 5000;

    SDL_Texture* backgroundTex = nullptr;
    int backgroundW = 0;
    int backgroundH = 0;
    bool backgroundTriedLoad = false;

    SDL_Texture* logoTex = nullptr;
    int logoW = 0;
    int logoH = 0;
    bool logoTriedLoad = false;

    std::string statusPrimary = "Starting Strova...";
    std::string statusSecondary = "Preparing workspace";
    std::vector<std::string> rotatingSteps;

    void tryLoadBackground(SDL_Renderer* r);
    void tryLoadLogo(SDL_Renderer* r);
};
