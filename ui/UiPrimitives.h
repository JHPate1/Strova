#pragma once

#include "../core/Theme.h"
#include "TextRenderer.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <string>

namespace strova::uix
{
    inline bool pointInRect(const SDL_Rect& rc, int x, int y)
    {
        return rc.w > 0 && rc.h > 0 && x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
    }

    inline void fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
    {
        if (!r || rc.w <= 0 || rc.h <= 0) return;
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderFillRect(r, &rc);
    }

    inline void drawRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
    {
        if (!r || rc.w <= 0 || rc.h <= 0) return;
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDrawRect(r, &rc);
    }

    inline void drawShadow(SDL_Renderer* r, const SDL_Rect& rc, int layers = 6)
    {
        if (!r || rc.w <= 0 || rc.h <= 0) return;
        for (int i = 1; i <= layers; ++i)
        {
            SDL_Rect s{ rc.x + i, rc.y + i, rc.w, rc.h };
            drawRect(r, s, SDL_Color{ 0, 0, 0, (Uint8)std::max(8, 34 - i * 4) });
        }
    }

    inline void drawPanelFrame(SDL_Renderer* r, const SDL_Rect& rc, bool floating, bool active = false)
    {
        if (floating) drawShadow(r, rc);
        fillRect(r, rc, floating ? theme::panelFloating : theme::panelBody);
        drawRect(r, rc, active ? theme::borderStrong : theme::borderSoft);
    }

    inline void drawPanelHeader(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const std::string& title, bool active)
    {
        fillRect(r, rc, active ? theme::panelHeaderActive : theme::panelHeader);
        drawRect(r, rc, active ? theme::borderStrong : theme::borderMuted);
        const SDL_Rect titleRc{ rc.x + theme::spacing8, rc.y + 2, std::max(0, rc.w - 90), std::max(0, rc.h - 4) };
        ui_text::drawTextEllipsized(r, font, title, titleRc, theme::textPrimary);
    }

    inline void drawButton(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const std::string& label,
                           bool hovered = false, bool active = false, bool disabled = false)
    {
        SDL_Color fill = active ? theme::buttonActive : (hovered ? theme::buttonHover : theme::buttonIdle);
        SDL_Color border = active ? theme::borderFocus : theme::borderSoft;
        SDL_Color text = active ? theme::textInverse : theme::textSecondary;
        if (disabled)
        {
            fill = theme::iconButtonIdle;
            border = theme::borderMuted;
            text = theme::textDisabled;
        }
        fillRect(r, rc, fill);
        drawRect(r, rc, border);
        const SDL_Rect textRc{ rc.x + theme::spacing8, rc.y + 1, std::max(0, rc.w - theme::spacing16), std::max(0, rc.h - 2) };
        ui_text::drawTextCentered(r, font, label, textRc, text);
    }

    inline void drawInput(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const std::string& text, bool focused)
    {
        fillRect(r, rc, focused ? theme::inputFillFocused : theme::inputFill);
        drawRect(r, rc, focused ? theme::borderFocus : theme::borderSoft);
        const SDL_Rect textRc{ rc.x + theme::spacing8, rc.y + 1, std::max(0, rc.w - theme::spacing16), std::max(0, rc.h - 2) };
        ui_text::drawTextEllipsized(r, font, text, textRc, text.empty() ? theme::textDisabled : theme::textPrimary);
    }

    inline void drawCard(SDL_Renderer* r, const SDL_Rect& rc, bool hovered = false, bool selected = false)
    {
        fillRect(r, rc, selected ? theme::cardFillSelected : (hovered ? theme::cardFillHover : theme::cardFill));
        drawRect(r, rc, selected ? theme::borderFocus : theme::borderSoft);
    }

    inline void drawScrollbar(SDL_Renderer* r, const SDL_Rect& track, int contentOffset, int viewportH, int contentH)
    {
        if (!r || track.w <= 0 || track.h <= 0 || viewportH <= 0 || contentH <= viewportH) return;
        fillRect(r, track, theme::panelFooter);
        const float ratio = (float)viewportH / (float)std::max(viewportH, contentH);
        const int thumbH = std::max(18, (int)(track.h * ratio));
        const int maxScroll = std::max(1, contentH - viewportH);
        const int travel = std::max(1, track.h - thumbH);
        const int thumbY = track.y + (int)((float)travel * ((float)contentOffset / (float)maxScroll));
        SDL_Rect thumb{ track.x, thumbY, track.w, thumbH };
        fillRect(r, thumb, theme::borderStrong);
    }

    inline void drawModal(SDL_Renderer* r, const SDL_Rect& overlay, const SDL_Rect& modal)
    {
        fillRect(r, overlay, SDL_Color{ 0, 0, 0, 178 });
        drawPanelFrame(r, modal, true, true);
    }
}
