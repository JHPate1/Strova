/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/SplashScreen.cpp
   Module:      Ui
   Purpose:     Splash screen drawing and timing helpers.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "SplashScreen.h"
#include "../platform/AppPaths.h"
#include <algorithm>
#include <cmath>
#include <SDL_image.h>

static float clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static void fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rc);
}

static SDL_Texture* makeText(SDL_Renderer* r, TTF_Font* font, const std::string& txt, SDL_Color col, int wrap = 0)
{
    if (!r || !font || txt.empty()) return nullptr;
    SDL_Surface* surf = wrap > 0 ? TTF_RenderUTF8_Blended_Wrapped(font, txt.c_str(), col, wrap)
                                 : TTF_RenderUTF8_Blended(font, txt.c_str(), col);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_FreeSurface(surf);
    return tex;
}

SplashScreen::~SplashScreen()
{
    if (backgroundTex)
    {
        SDL_DestroyTexture(backgroundTex);
        backgroundTex = nullptr;
    }
    if (logoTex)
    {
        SDL_DestroyTexture(logoTex);
        logoTex = nullptr;
    }
}

void SplashScreen::start(Uint32 nowMs)
{
    startMs = nowMs;
    done = false;
    progress = 0.0f;
    rotatingSteps = {
        "Loading configuration",
        "Scanning projects",
        "Caching brush resources",
        "Preparing workspace",
        "Checking update channel"
    };
    statusPrimary = "Starting Strova...";
    statusSecondary = "Preparing workspace";
}

void SplashScreen::setStatusText(const std::string& primary, const std::string& secondary)
{
    if (!primary.empty()) statusPrimary = primary;
    if (!secondary.empty()) statusSecondary = secondary;
}

void SplashScreen::handleEvent(const SDL_Event& e)
{
    (void)e;
}

void SplashScreen::update(Uint32 nowMs)
{
    if (done) return;

    const Uint32 elapsed = (nowMs >= startMs) ? (nowMs - startMs) : 0u;
    const float t = (durationMs > 0) ? (float)elapsed / (float)durationMs : 1.0f;
    progress = clamp01(t);

    if (!rotatingSteps.empty())
    {
        const std::size_t idx = (std::min<std::size_t>)((std::size_t)(progress * (float)rotatingSteps.size()), rotatingSteps.size() - 1);
        statusSecondary = rotatingSteps[idx];
    }

    if (progress >= 1.0f)
        done = true;
}

void SplashScreen::tryLoadBackground(SDL_Renderer* r)
{
    if (!r || backgroundTex || backgroundTriedLoad) return;
    backgroundTriedLoad = true;

    const std::string candidates[] = {
        strova::paths::resolveAssetPath("splash_background.png").string(),
        strova::paths::resolveAssetPath("splash.png").string(),
        strova::paths::resolveAssetPath("splash_bg.png").string()
    };

    for (const std::string& path : candidates)
    {
        SDL_Surface* surf = IMG_Load(path.c_str());
        if (!surf) continue;

        backgroundTex = SDL_CreateTextureFromSurface(r, surf);
        if (backgroundTex)
        {
            SDL_SetTextureBlendMode(backgroundTex, SDL_BLENDMODE_BLEND);
            backgroundW = surf->w;
            backgroundH = surf->h;
        }
        SDL_FreeSurface(surf);
        if (backgroundTex) break;
    }
}

void SplashScreen::tryLoadLogo(SDL_Renderer* r)
{
    if (!r || logoTex || logoTriedLoad) return;
    logoTriedLoad = true;

    const std::string candidates[] = {
        strova::paths::resolveAssetPath("strova.png").string(),
        strova::paths::resolveAssetPath("Strova.png").string(),
        strova::paths::resolveAssetPath("strova_logo.png").string(),
        strova::paths::resolveAssetPath("logo.png").string()
    };

    for (const std::string& path : candidates)
    {
        SDL_Surface* surf = IMG_Load(path.c_str());
        if (!surf) continue;
        logoTex = SDL_CreateTextureFromSurface(r, surf);
        if (logoTex)
        {
            SDL_SetTextureBlendMode(logoTex, SDL_BLENDMODE_BLEND);
            logoW = surf->w;
            logoH = surf->h;
        }
        SDL_FreeSurface(surf);
        if (logoTex) break;
    }
}

void SplashScreen::draw(SDL_Renderer* r, int w, int h, TTF_Font* font)
{
    if (!r) return;

    tryLoadBackground(r);
    tryLoadLogo(r);

    SDL_SetRenderDrawColor(r, 6, 8, 18, 255);
    SDL_RenderClear(r);

    if (backgroundTex && backgroundW > 0 && backgroundH > 0)
    {
        const float sx = (float)w / (float)backgroundW;
        const float sy = (float)h / (float)backgroundH;
        const float s = (std::max)(sx, sy);
        SDL_Rect bg;
        bg.w = (int)std::ceil((float)backgroundW * s);
        bg.h = (int)std::ceil((float)backgroundH * s);
        bg.x = (w - bg.w) / 2;
        bg.y = (h - bg.h) / 2;
        SDL_RenderCopy(r, backgroundTex, nullptr, &bg);

        SDL_Rect tint{ 0,0,w,h };
        fillRect(r, tint, SDL_Color{ 4, 8, 20, 165 });
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const float t = h > 1 ? (float)y / (float)(h - 1) : 0.0f;
            const Uint8 rr = (Uint8)(10 + 6 * t);
            const Uint8 gg = (Uint8)(16 + 14 * t);
            const Uint8 bb = (Uint8)(28 + 36 * t);
            SDL_SetRenderDrawColor(r, rr, gg, bb, 255);
            SDL_RenderDrawLine(r, 0, y, w, y);
        }
    }

    SDL_Rect panel{ 42, h - 148, (std::min)(620, w - 84), 108 };
    fillRect(r, panel, SDL_Color{ 7, 12, 22, 205 });
    SDL_SetRenderDrawColor(r, 110, 145, 255, 100);
    SDL_RenderDrawRect(r, &panel);

    SDL_Rect accent{ panel.x, panel.y, 4, panel.h };
    fillRect(r, accent, SDL_Color{ 88, 145, 255, 225 });

    if (logoTex && logoW > 0 && logoH > 0)
    {
        const int targetH = 82;
        float s = (float)targetH / (float)logoH;
        int targetW = (int)std::round((float)logoW * s);
        SDL_Rect dst{ panel.x + 26, panel.y - targetH - 16, targetW, targetH };
        SDL_RenderCopy(r, logoTex, nullptr, &dst);
    }

    SDL_Rect bar{ panel.x + 26, panel.y + panel.h - 24, panel.w - 52, 8 };
    fillRect(r, bar, SDL_Color{ 25, 32, 48, 220 });
    SDL_Rect fill = bar;
    fill.w = (int)((float)bar.w * clamp01(progress));
    fillRect(r, fill, SDL_Color{ 74, 126, 255, 255 });

    if (font)
    {
        SDL_Texture* titleTex = makeText(r, font, statusPrimary, SDL_Color{ 242, 246, 255, 255 });
        SDL_Texture* bodyTex = makeText(r, font, statusSecondary, SDL_Color{ 178, 190, 214, 255 });
        SDL_Texture* authorTex = makeText(r, font, "By Robin R. / Strova Project", SDL_Color{ 170, 180, 200, 215 });
        if (titleTex)
        {
            int tw = 0, th = 0;
            SDL_QueryTexture(titleTex, nullptr, nullptr, &tw, &th);
            SDL_Rect dst{ panel.x + 26, panel.y + 24, tw, th };
            SDL_RenderCopy(r, titleTex, nullptr, &dst);
            SDL_DestroyTexture(titleTex);
        }
        if (bodyTex)
        {
            int tw = 0, th = 0;
            SDL_QueryTexture(bodyTex, nullptr, nullptr, &tw, &th);
            SDL_Rect dst{ panel.x + 26, panel.y + 58, tw, th };
            SDL_RenderCopy(r, bodyTex, nullptr, &dst);
            SDL_DestroyTexture(bodyTex);
        }
        if (authorTex)
        {
            int tw = 0, th = 0;
            SDL_QueryTexture(authorTex, nullptr, nullptr, &tw, &th);
            SDL_Rect dst{ 18, h - th - 18, tw, th };
            SDL_RenderCopy(r, authorTex, nullptr, &dst);
            SDL_DestroyTexture(authorTex);
        }
    }
}
