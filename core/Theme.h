/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/Theme.h
   Module:      Core
   Purpose:     Shared theme tokens and sizing values used across the UI.

   Notes:
   - Expanded into a real token set so panel chrome, buttons, inputs, tabs,
     modals, scrollbars, and status surfaces can all read from one source.
   - Compatibility aliases are kept below so older files continue to build.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include "SDL.h"

namespace strova::theme
{
    inline constexpr SDL_Color bgApp{ 8, 11, 17, 255 };
    inline constexpr SDL_Color bgCanvasArea{ 12, 16, 23, 255 };
    inline constexpr SDL_Color bgWorkspace{ 10, 14, 20, 255 };

    inline constexpr SDL_Color panelBody{ 17, 22, 32, 255 };
    inline constexpr SDL_Color panelBodyAlt{ 22, 28, 40, 255 };
    inline constexpr SDL_Color panelRaised{ 26, 33, 47, 255 };
    inline constexpr SDL_Color panelFloating{ 18, 23, 33, 248 };
    inline constexpr SDL_Color panelHeader{ 31, 39, 56, 255 };
    inline constexpr SDL_Color panelHeaderActive{ 42, 53, 76, 255 };
    inline constexpr SDL_Color panelFooter{ 14, 18, 27, 255 };

    inline constexpr SDL_Color inputFill{ 18, 24, 36, 255 };
    inline constexpr SDL_Color inputFillFocused{ 22, 30, 46, 255 };
    inline constexpr SDL_Color cardFill{ 20, 26, 38, 255 };
    inline constexpr SDL_Color cardFillHover{ 25, 32, 47, 255 };
    inline constexpr SDL_Color cardFillSelected{ 46, 60, 88, 255 };
    inline constexpr SDL_Color timelineFill{ 15, 20, 30, 255 };

    inline constexpr SDL_Color borderSoft{ 108, 124, 152, 78 };
    inline constexpr SDL_Color borderStrong{ 132, 150, 182, 124 };
    inline constexpr SDL_Color borderFocus{ 112, 160, 255, 255 };
    inline constexpr SDL_Color borderMuted{ 62, 74, 94, 255 };

    inline constexpr SDL_Color textPrimary{ 234, 239, 247, 255 };
    inline constexpr SDL_Color textSecondary{ 162, 174, 195, 255 };
    inline constexpr SDL_Color textDisabled{ 110, 119, 136, 255 };
    inline constexpr SDL_Color textInverse{ 9, 12, 18, 255 };

    inline constexpr SDL_Color accent{ 106, 138, 255, 255 };
    inline constexpr SDL_Color accentHover{ 126, 154, 255, 255 };
    inline constexpr SDL_Color accentPressed{ 86, 116, 230, 255 };
    inline constexpr SDL_Color accentSelected{ 78, 102, 162, 255 };

    inline constexpr SDL_Color buttonIdle{ 26, 33, 47, 255 };
    inline constexpr SDL_Color buttonHover{ 33, 41, 58, 255 };
    inline constexpr SDL_Color buttonPressed{ 21, 27, 40, 255 };
    inline constexpr SDL_Color buttonActive{ 86, 116, 200, 255 };
    inline constexpr SDL_Color iconButtonIdle{ 21, 27, 40, 255 };

    inline constexpr SDL_Color success{ 104, 192, 124, 255 };
    inline constexpr SDL_Color warning{ 232, 190, 92, 255 };
    inline constexpr SDL_Color error{ 224, 102, 102, 255 };

    inline constexpr int spacing4 = 4;
    inline constexpr int spacing8 = 8;
    inline constexpr int spacing12 = 12;
    inline constexpr int spacing16 = 16;

    inline constexpr int radiusSm = 4;
    inline constexpr int radiusMd = 6;
    inline constexpr int radiusLg = 8;

    inline constexpr int headerHeight = 28;
    inline constexpr int buttonHeight = 28;
    inline constexpr int inputHeight = 30;
    inline constexpr int scrollbarWidth = 8;
    inline constexpr int splitterThickness = 6;
    inline constexpr int toolbarHeight = 32;
    inline constexpr int statusHeight = 24;
}

// Compatibility aliases used by the existing codebase.
inline constexpr SDL_Color COL_BG_MAIN = strova::theme::bgApp;
inline constexpr SDL_Color COL_BG_PANEL = strova::theme::panelBody;
inline constexpr SDL_Color COL_BG_PANEL2 = strova::theme::panelBodyAlt;
inline constexpr SDL_Color COL_CANVAS_AREA = strova::theme::bgCanvasArea;
inline constexpr SDL_Color COL_PAGE{ 255, 255, 255, 255 };
inline constexpr SDL_Color COL_BORDER_SOFT = strova::theme::borderSoft;
inline constexpr SDL_Color COL_BORDER_STRONG = strova::theme::borderStrong;
inline constexpr SDL_Color COL_TEXT_MAIN = strova::theme::textPrimary;
inline constexpr SDL_Color COL_TEXT_DIM = strova::theme::textSecondary;
inline constexpr SDL_Color COL_ACCENT = strova::theme::accent;
inline constexpr SDL_Color COL_ACCENT_DIM = strova::theme::accentSelected;
inline constexpr SDL_Color COL_BTN_IDLE = strova::theme::buttonIdle;
inline constexpr SDL_Color COL_BTN_HOVER = strova::theme::buttonHover;
inline constexpr SDL_Color COL_BTN_ACTIVE = strova::theme::buttonActive;
