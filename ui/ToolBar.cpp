/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/ToolBar.cpp
   Module:      Ui
   Purpose:     Toolbar drawing and interaction handling.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "../ui/ToolBar.h"
#include "../core/Theme.h"
#include "../core/ToolRegistry.h"
#include "../ui/Rounded.h"
#include <algorithm>
#include <string>
#include <vector>

ToolBar::ToolBar() {}

static void fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    ui::fillRoundedRect(r, SDL_FRect{(float)rc.x,(float)rc.y,(float)rc.w,(float)rc.h}, 10.0f, c);
}

static void drawRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(r, &rc);
}

static void drawText(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y, SDL_Color col)
{
    if (!r || !font || text.empty()) return;

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), col);
    if (!surf) return;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }

    SDL_Rect dst{ x, y, surf->w, surf->h };
    SDL_FreeSurface(surf);

    SDL_RenderCopy(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

bool ToolBar::pointInRect(int x, int y, const SDL_Rect& rc) const
{
    return x >= rc.x && x < (rc.x + rc.w) && y >= rc.y && y < (rc.y + rc.h);
}

const ToolBar::ToolBtn* ToolBar::buttons()
{
    static std::vector<ToolBtn> s_buttons;
    if (s_buttons.empty())
    {
        const auto& descriptors = strova::tools::descriptors();
        s_buttons.reserve(descriptors.size());
        for (const ToolDescriptor& desc : descriptors)
        {
            s_buttons.push_back(ToolBtn{ desc.type, desc.displayName, desc.toolbarGlyph });
        }
    }
    return s_buttons.empty() ? nullptr : s_buttons.data();
}

int ToolBar::buttonCount()
{
    return strova::tools::toolCount();
}

void ToolBar::handleWheel(const SDL_Event& e, int mx, int my, const SDL_Rect& toolsArea)
{
    if (e.type != SDL_MOUSEWHEEL) return;
    if (!pointInRect(mx, my, toolsArea)) return;

    
    scrollY -= e.wheel.y * 32;
    if (scrollY < 0) scrollY = 0;
}

void ToolBar::handleClick(int mx, int my, const SDL_Rect& toolsArea)
{
    if (!pointInRect(mx, my, toolsArea)) return;

    
    const int pad = 10;
    const int gap = 8;
    const int btn = 44;          
    const int cols = std::max(1, (toolsArea.w - pad * 2 + gap) / (btn + gap));

    
    int count = buttonCount();
    int rows = (count + cols - 1) / cols;
    int contentH = pad * 2 + rows * btn + std::max(0, rows - 1) * gap;

    int maxScroll = std::max(0, contentH - toolsArea.h);
    if (scrollY > maxScroll) scrollY = maxScroll;

    
    int lx = mx - toolsArea.x;
    int ly = my - toolsArea.y + scrollY;

    
    const ToolBtn* list = buttons();

    for (int i = 0; i < count; ++i)
    {
        int row = i / cols;
        int col = i % cols;

        SDL_Rect rc{
            pad + col * (btn + gap),
            pad + row * (btn + gap),
            btn, btn
        };

        if (lx >= rc.x && lx < rc.x + rc.w && ly >= rc.y && ly < rc.y + rc.h)
        {
            selected = list[i].tool;
            return;
        }
    }
}

void ToolBar::draw(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& toolsArea)
{
    if (!r) return;

    SDL_SetRenderDrawColor(r, COL_BG_PANEL.r, COL_BG_PANEL.g, COL_BG_PANEL.b, COL_BG_PANEL.a);
    SDL_RenderFillRect(r, &toolsArea);

    
    SDL_RenderSetClipRect(r, &toolsArea);

    const int pad = 10;
    const int gap = 8;
    const int btn = 44;
    const int cols = std::max(1, (toolsArea.w - pad * 2 + gap) / (btn + gap));

    int count = buttonCount();
    int rows = (count + cols - 1) / cols;
    int contentH = pad * 2 + rows * btn + std::max(0, rows - 1) * gap;

    int maxScroll = std::max(0, contentH - toolsArea.h);
    if (scrollY > maxScroll) scrollY = maxScroll;

    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    const ToolBtn* list = buttons();

    
    for (int i = 0; i < count; ++i)
    {
        int row = i / cols;
        int col = i % cols;

        SDL_Rect rc{
            toolsArea.x + pad + col * (btn + gap),
            toolsArea.y + pad + row * (btn + gap) - scrollY,
            btn, btn
        };

        
        if (rc.y + rc.h < toolsArea.y || rc.y > toolsArea.y + toolsArea.h) continue;

        bool hover = (mx >= rc.x && mx < rc.x + rc.w && my >= rc.y && my < rc.y + rc.h);
        bool active = (list[i].tool == selected);

        SDL_Color fill = active ? COL_BTN_ACTIVE : (hover ? COL_BTN_HOVER : COL_BTN_IDLE);
        fillRect(r, rc, fill);
        drawRect(r, rc, COL_BORDER_SOFT);

        
        SDL_Color tc = hover || active ? COL_TEXT_MAIN : COL_TEXT_DIM;
        std::string g = list[i].glyph;

        
        
        int tx = rc.x + 10;
        int ty = rc.y + 10;
        drawText(r, font, g, tx, ty, tc);
    }

    SDL_RenderSetClipRect(r, nullptr);

    
    SDL_SetRenderDrawColor(r, COL_BORDER_SOFT.r, COL_BORDER_SOFT.g, COL_BORDER_SOFT.b, COL_BORDER_SOFT.a);
    SDL_RenderDrawLine(r, toolsArea.x, toolsArea.y + toolsArea.h - 1, toolsArea.x + toolsArea.w, toolsArea.y + toolsArea.h - 1);
}
