#include "ToolOptionsPanel.h"

#include "../core/Theme.h"
#include "BrushPreviewCache.h"
#include "TextRenderer.h"
#include "UiPrimitives.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace
{
    static std::string lowerCopy(const std::string& s)
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return out;
    }

    struct FilterLayout
    {
        SDL_Rect category{};
        SDL_Rect source{};
        SDL_Rect search{};
        int totalHeight = 0;
    };

    static FilterLayout buildFilterLayout(const SDL_Rect& root, int y)
    {
        FilterLayout out{};
        const int padX = 10;
        const int gap = 6;
        const int h = 30;
        const int usableW = std::max(40, root.w - padX * 2);
        const int x = root.x + padX;

        if (usableW >= 330)
        {
            const int colW = std::max(40, (usableW - gap * 2) / 3);
            out.category = SDL_Rect{ x, y, colW, h };
            out.source = SDL_Rect{ out.category.x + out.category.w + gap, y, colW, h };
            out.search = SDL_Rect{ out.source.x + out.source.w + gap, y, std::max(40, root.x + root.w - (out.source.x + out.source.w + gap) - padX), h };
            out.totalHeight = h;
            return out;
        }

        if (usableW >= 230)
        {
            const int halfW = std::max(40, (usableW - gap) / 2);
            out.category = SDL_Rect{ x, y, halfW, h };
            out.source = SDL_Rect{ out.category.x + out.category.w + gap, y, std::max(40, root.x + root.w - padX - (out.category.x + out.category.w + gap)), h };
            out.search = SDL_Rect{ x, y + h + gap, usableW, h };
            out.totalHeight = h * 2 + gap;
            return out;
        }

        out.category = SDL_Rect{ x, y, usableW, h };
        out.source = SDL_Rect{ x, y + h + gap, usableW, h };
        out.search = SDL_Rect{ x, y + (h + gap) * 2, usableW, h };
        out.totalHeight = h * 3 + gap * 2;
        return out;
    }

    static std::vector<SDL_Rect> buildFooterButtons(const SDL_Rect& root, int y)
    {
        std::vector<SDL_Rect> out;
        const int padX = 10;
        const int gap = 8;
        const int buttonH = 28;
        const int usableW = std::max(40, root.w - padX * 2);
        const int x = root.x + padX;

        if (usableW >= 300)
        {
            const int colW = std::max(40, (usableW - gap * 3) / 4);
            out.push_back(SDL_Rect{ x, y, colW, buttonH });
            out.push_back(SDL_Rect{ x + colW + gap, y, colW, buttonH });
            out.push_back(SDL_Rect{ x + (colW + gap) * 2, y, colW, buttonH });
            out.push_back(SDL_Rect{ x + (colW + gap) * 3, y, std::max(40, root.x + root.w - padX - (x + (colW + gap) * 3)), buttonH });
            return out;
        }

        const int colW = std::max(40, (usableW - gap) / 2);
        out.push_back(SDL_Rect{ x, y, colW, buttonH });
        out.push_back(SDL_Rect{ x + colW + gap, y, std::max(40, root.x + root.w - padX - (x + colW + gap)), buttonH });
        out.push_back(SDL_Rect{ x, y + buttonH + gap, colW, buttonH });
        out.push_back(SDL_Rect{ x + colW + gap, y + buttonH + gap, std::max(40, root.x + root.w - padX - (x + colW + gap)), buttonH });
        return out;
    }


    static SDL_Rect insetRect(const SDL_Rect& rc, int inset)
    {
        SDL_Rect out = rc;
        out.x += inset;
        out.y += inset;
        out.w = std::max(0, out.w - inset * 2);
        out.h = std::max(0, out.h - inset * 2);
        return out;
    }

    static void drawSmallGear(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
    {
        if (!r || rc.w <= 6 || rc.h <= 6)
            return;
        const int cx = rc.x + rc.w / 2;
        const int cy = rc.y + rc.h / 2;
        const int radius = std::max(2, std::min(rc.w, rc.h) / 5);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_Rect center{ cx - radius, cy - radius, radius * 2, radius * 2 };
        SDL_RenderDrawRect(r, &center);
        const int spoke = std::max(2, radius + 2);
        SDL_RenderDrawLine(r, cx, cy - spoke, cx, cy - radius - 1);
        SDL_RenderDrawLine(r, cx, cy + radius + 1, cx, cy + spoke);
        SDL_RenderDrawLine(r, cx - spoke, cy, cx - radius - 1, cy);
        SDL_RenderDrawLine(r, cx + radius + 1, cy, cx + spoke, cy);
        SDL_RenderDrawLine(r, cx - spoke + 1, cy - spoke + 1, cx - radius - 1, cy - radius - 1);
        SDL_RenderDrawLine(r, cx + radius + 1, cy + radius + 1, cx + spoke - 1, cy + spoke - 1);
        SDL_RenderDrawLine(r, cx - spoke + 1, cy + spoke - 1, cx - radius - 1, cy + radius + 1);
        SDL_RenderDrawLine(r, cx + radius + 1, cy - radius - 1, cx + spoke - 1, cy - spoke + 1);
    }

    static std::string brushCardMeta(const strova::brush::BrushPackage& pkg)
    {
        return std::string(pkg.builtIn ? "Built-in" : "Installed") + " • " + pkg.manifest.category;
    }
}

bool ToolOptionsPanel::in(const SDL_Rect& rc, int x, int y) const
{
    return x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
}

void ToolOptionsPanel::layout(const SDL_Rect& area)
{
    root = area;
}

void ToolOptionsPanel::drawBox(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color fill, SDL_Color border)
{
    if (!r || rc.w <= 0 || rc.h <= 0)
        return;
    strova::uix::fillRect(r, rc, fill);
    strova::uix::drawRect(r, rc, border);
}

void ToolOptionsPanel::drawText(SDL_Renderer* r, TTF_Font* font, const std::string& s, int x, int y, SDL_Color c)
{
    strova::ui_text::drawText(r, font, s, x, y, c);
}

void ToolOptionsPanel::drawTextFit(SDL_Renderer* r, TTF_Font* font, const std::string& s, const SDL_Rect& rc, SDL_Color c)
{
    strova::ui_text::drawTextLeftMiddle(r, font, s, rc, c);
}

void ToolOptionsPanel::drawSlider(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const char* label, float v01)
{
    drawBox(r, rc, COL_BG_PANEL2, COL_BORDER_SOFT);
    strova::ui_text::drawTextLeftMiddle(r, font, label ? std::string(label) : std::string(), SDL_Rect{ rc.x + 8, rc.y + 2, 104, rc.h - 4 }, COL_TEXT_DIM);
    SDL_Rect track{ rc.x + 112, rc.y + rc.h / 2 - 4, std::max(16, rc.w - 152), 8 };
    drawBox(r, track, SDL_Color{ 44, 52, 66, 255 }, SDL_Color{ 44, 52, 66, 255 });
    int fillW = (int)std::lround((float)track.w * std::clamp(v01, 0.0f, 1.0f));
    SDL_Rect fill{ track.x, track.y, fillW, track.h };
    drawBox(r, fill, COL_ACCENT, COL_ACCENT);
    strova::ui_text::drawTextCentered(r, font, std::to_string((int)std::lround(v01 * 100.0f)) + "%", SDL_Rect{ rc.x + rc.w - 42, rc.y + 2, 34, rc.h - 4 }, COL_TEXT_MAIN);
}

bool ToolOptionsPanel::sliderEvent(const SDL_Event& e, int mx, int my, const SDL_Rect& rc, float& v01)
{
    if ((e.type != SDL_MOUSEBUTTONDOWN && e.type != SDL_MOUSEMOTION) || (e.type == SDL_MOUSEBUTTONDOWN && e.button.button != SDL_BUTTON_LEFT))
        return false;
    Uint32 buttons = SDL_GetMouseState(nullptr, nullptr);
    if (e.type == SDL_MOUSEMOTION && (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) == 0)
        return false;
    SDL_Rect track{ rc.x + 112, rc.y + rc.h / 2 - 4, std::max(16, rc.w - 152), 8 };
    SDL_Rect hit{ track.x, rc.y, track.w, rc.h };
    if (!in(hit, mx, my)) return false;
    v01 = std::clamp((float)(mx - track.x) / (float)std::max(1, track.w), 0.0f, 1.0f);
    return true;
}

void ToolOptionsPanel::drawStepper(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const char* label, float v, float step, float minv, float maxv)
{
    (void)step; (void)minv; (void)maxv;
    drawBox(r, rc, COL_BG_PANEL2, COL_BORDER_SOFT);
    strova::ui_text::drawTextLeftMiddle(r, font, label ? std::string(label) : std::string(), SDL_Rect{ rc.x + 8, rc.y + 2, std::max(40, rc.w - 96), rc.h - 4 }, COL_TEXT_DIM);
    strova::ui_text::drawTextCentered(r, font, std::to_string((int)std::lround(v)), SDL_Rect{ rc.x + rc.w - 90, rc.y + 2, 28, rc.h - 4 }, COL_TEXT_MAIN);
    SDL_Rect minusR{ rc.x + rc.w - 56, rc.y + 6, 20, 20 };
    SDL_Rect plusR{ rc.x + rc.w - 28, rc.y + 6, 20, 20 };
    drawBox(r, minusR, COL_BTN_IDLE, COL_BORDER_SOFT);
    drawBox(r, plusR, COL_BTN_IDLE, COL_BORDER_SOFT);
    strova::ui_text::drawTextCentered(r, font, "-", minusR, COL_TEXT_DIM);
    strova::ui_text::drawTextCentered(r, font, "+", plusR, COL_TEXT_DIM);
}

bool ToolOptionsPanel::stepperEvent(const SDL_Event& e, int mx, int my, const SDL_Rect& rc, float& v, float step, float minv, float maxv)
{
    if (e.type != SDL_MOUSEBUTTONDOWN || e.button.button != SDL_BUTTON_LEFT) return false;
    SDL_Rect minusR{ rc.x + rc.w - 56, rc.y + 6, 20, 20 };
    SDL_Rect plusR{ rc.x + rc.w - 28, rc.y + 6, 20, 20 };
    if (in(minusR, mx, my)) { v = std::max(minv, v - step); return true; }
    if (in(plusR, mx, my)) { v = std::min(maxv, v + step); return true; }
    return false;
}

bool ToolOptionsPanel::brushVisible(const strova::brush::BrushPackage& pkg) const
{
    if (sourceFilter == SourceFilter::BuiltIn && !pkg.builtIn) return false;
    if (sourceFilter == SourceFilter::Installed && pkg.builtIn) return false;
    if (categoryFilter != "All" && pkg.manifest.category != categoryFilter) return false;
    if (searchText.empty()) return true;
    std::string hay = lowerCopy(pkg.manifest.name + " " + pkg.manifest.id + " " + pkg.manifest.category + " " + pkg.manifest.tags);
    return hay.find(lowerCopy(searchText)) != std::string::npos;
}

std::vector<std::string> ToolOptionsPanel::availableCategories() const
{
    std::vector<std::string> out{ "All" };
    if (auto* mgr = strova::brush::globalManager())
    {
        for (const auto& pkg : mgr->all())
            if (std::find(out.begin(), out.end(), pkg.manifest.category) == out.end())
                out.push_back(pkg.manifest.category);
    }
    return out;
}

void ToolOptionsPanel::rebuildFilteredBrushes()
{
    filteredBrushIds.clear();
    if (auto* mgr = strova::brush::globalManager())
        for (const auto& pkg : mgr->all())
            if (brushVisible(pkg))
                filteredBrushIds.push_back(pkg.manifest.id);
}

const strova::brush::BrushPackage* ToolOptionsPanel::selectedBrushPackage(const ToolSettings& settings) const
{
    if (auto* mgr = strova::brush::globalManager())
    {
        if (!settings.brushId.empty())
            if (const auto* pkg = mgr->findById(settings.brushId))
                return pkg;
        return mgr->selected();
    }
    return nullptr;
}

void ToolOptionsPanel::drawBrushPreview(SDL_Renderer* r, const SDL_Rect& rc, const strova::brush::BrushPackage* pkg)
{
    drawBox(r, rc, SDL_Color{ 15, 20, 30, 255 }, COL_BORDER_SOFT);
    if (!pkg) return;
    SDL_Texture* tex = strova::ui_brush_preview::getPreviewTexture(r, *pkg);
    if (!tex) return;

    bool fallbackToStamp = false;
    const strova::brush::BrushStamp* src = strova::ui_brush_preview::previewSource(*pkg, fallbackToStamp);
    if (!src) return;

    SDL_Rect dst = rc;
    const float sx = (float)(rc.w - 10) / (float)std::max(1, src->width);
    const float sy = (float)(rc.h - 10) / (float)std::max(1, src->height);
    const float fit = std::max(0.01f, std::min(sx, sy));
    dst.w = std::max(1, (int)std::lround((float)src->width * fit));
    dst.h = std::max(1, (int)std::lround((float)src->height * fit));
    dst.x = rc.x + (rc.w - dst.w) / 2;
    dst.y = rc.y + (rc.h - dst.h) / 2;
    SDL_RenderCopy(r, tex, nullptr, &dst);
}

void ToolOptionsPanel::drawBrushBrowser(SDL_Renderer* r, TTF_Font* font, const ToolSettings& settings, int& y)
{
    rebuildFilteredBrushes();

    const int titleH = std::max(18, strova::ui_text::measureTextHeight(font));
    drawText(r, font, "Brush Browser", root.x + 10, y, COL_TEXT_MAIN);
    strova::ui_text::drawTextLeftMiddle(r, font, std::to_string((int)filteredBrushIds.size()) + " brushes",
        SDL_Rect{ root.x + root.w - 108, y - 2, 98, titleH + 4 }, COL_TEXT_DIM);
    y += titleH + 10;

    const int panelMaxScroll = std::max(0, contentHeight - root.h);
    const int contentRightInset = panelMaxScroll > 0 ? (strova::theme::scrollbarWidth + 8) : 0;
    SDL_Rect browserBounds{ root.x + 3, root.y + 3, std::max(20, root.w - 6 - contentRightInset), std::max(20, root.h - 6) };

    SDL_Rect oldClip{};
    const SDL_bool hadClip = SDL_RenderIsClipEnabled(r);
    SDL_RenderGetClipRect(r, &oldClip);
    SDL_Rect browserClip = insetRect(browserBounds, 1);
    SDL_RenderSetClipRect(r, &browserClip);

    const FilterLayout filters = buildFilterLayout(SDL_Rect{ root.x, root.y, std::max(40, root.w - contentRightInset), root.h }, y);
    drawBox(r, filters.category, COL_BG_PANEL2, COL_BORDER_SOFT);
    drawBox(r, filters.source, COL_BG_PANEL2, COL_BORDER_SOFT);
    drawBox(r, filters.search, COL_BG_PANEL2, searchFocused ? COL_ACCENT : COL_BORDER_SOFT);

    const char* sourceLabel = sourceFilter == SourceFilter::All ? "Source: All" : (sourceFilter == SourceFilter::BuiltIn ? "Source: Built-in" : "Source: Installed");
    drawTextFit(r, font, "Category: " + categoryFilter, SDL_Rect{ filters.category.x + 8, filters.category.y + 1, filters.category.w - 16, filters.category.h - 2 }, COL_TEXT_DIM);
    drawTextFit(r, font, sourceLabel, SDL_Rect{ filters.source.x + 8, filters.source.y + 1, filters.source.w - 16, filters.source.h - 2 }, COL_TEXT_DIM);
    drawTextFit(r, font, searchText.empty() ? "Search brushes" : searchText, SDL_Rect{ filters.search.x + 8, filters.search.y + 1, filters.search.w - 16, filters.search.h - 2 }, searchText.empty() ? COL_TEXT_DIM : COL_TEXT_MAIN);
    y += filters.totalHeight + 10;

    const int cardH = 76;
    const int cardGap = 8;
    if (filteredBrushIds.empty())
    {
        SDL_Rect emptyR{ browserBounds.x + 2, y, browserBounds.w - 4, 72 };
        drawBox(r, emptyR, COL_BG_PANEL2, COL_BORDER_SOFT);
        strova::ui_text::drawTextCentered(r, font, "No brushes match the current filters.", emptyR, COL_TEXT_DIM);
        y += emptyR.h + 10;
    }
    else if (auto* mgr = strova::brush::globalManager())
    {
        for (const auto& id : filteredBrushIds)
        {
            const auto* pkg = mgr->findById(id);
            if (!pkg) continue;

            SDL_Rect cardR{ browserBounds.x + 2, y, browserBounds.w - 4, cardH };
            if (cardR.y + cardR.h < browserBounds.y || cardR.y > browserBounds.y + browserBounds.h) { y += cardH + cardGap; continue; }
            const bool active = (id == settings.brushId);
            drawBox(r, cardR, active ? SDL_Color{ 86, 112, 156, 220 } : COL_BG_PANEL2, active ? SDL_Color{ 212, 228, 255, 255 } : COL_BORDER_SOFT);

            SDL_Rect cardOldClip{};
            const SDL_bool cardHadClip = SDL_RenderIsClipEnabled(r);
            SDL_RenderGetClipRect(r, &cardOldClip);
            SDL_Rect cardClip = insetRect(cardR, 1);
            SDL_RenderSetClipRect(r, &cardClip);

            SDL_Rect previewR{ cardR.x + 10, cardR.y + 10, 56, 56 };
            drawBrushPreview(r, previewR, pkg);

            const int textW = std::max(28, cardR.w - previewR.w - 60);
            SDL_Rect textR{ previewR.x + previewR.w + 10, cardR.y + 8, textW, 22 };
            SDL_Rect metaR{ textR.x, textR.y + 21, textR.w, 17 };
            SDL_Rect tagR{ textR.x, metaR.y + 17, textR.w, 17 };
            SDL_Color titleC = active ? SDL_Color{ 12, 14, 20, 255 } : COL_TEXT_MAIN;
            SDL_Color dimC = active ? SDL_Color{ 18, 24, 32, 255 } : COL_TEXT_DIM;
            strova::ui_text::drawTextEllipsized(r, font, pkg->manifest.name, textR, titleC);
            strova::ui_text::drawTextEllipsized(r, font, brushCardMeta(*pkg), metaR, dimC);
            const std::string tagLine = pkg->manifest.tags.empty() ? (pkg->manifest.id) : pkg->manifest.tags;
            strova::ui_text::drawTextEllipsized(r, font, tagLine, tagR, dimC);

            SDL_Rect manageR{ cardR.x + cardR.w - 28, cardR.y + cardR.h - 28, 20, 20 };
            drawBox(r, manageR, active ? SDL_Color{ 226, 232, 240, 255 } : COL_BTN_IDLE, active ? SDL_Color{ 50, 64, 82, 255 } : COL_BORDER_SOFT);
            drawSmallGear(r, manageR, active ? SDL_Color{ 18, 22, 28, 255 } : COL_TEXT_DIM);

            if (cardHadClip) SDL_RenderSetClipRect(r, &cardOldClip); else SDL_RenderSetClipRect(r, nullptr);
            y += cardH + cardGap;
        }
    }

    std::vector<SDL_Rect> footerBtns = buildFooterButtons(SDL_Rect{ root.x, root.y, std::max(40, root.w - contentRightInset), root.h }, y);
    static const char* labels[4] = { "Manage", "Creator", "Install", "Reload" };
    for (size_t i = 0; i < footerBtns.size() && i < 4; ++i)
    {
        drawBox(r, footerBtns[i], COL_BTN_IDLE, COL_BORDER_SOFT);
        strova::ui_text::drawTextCentered(r, font, labels[i], insetRect(footerBtns[i], 2), COL_TEXT_DIM);
    }
    y += footerBtns.size() > 2 ? 64 : 28;
    y += 14;

    if (hadClip) SDL_RenderSetClipRect(r, &oldClip); else SDL_RenderSetClipRect(r, nullptr);
}

void ToolOptionsPanel::draw(SDL_Renderer* r, TTF_Font* font, ToolType activeTool, const ToolSettings& settings)
{
    if (!r) return;
    drawBox(r, root, COL_BG_PANEL, COL_BORDER_SOFT);

    const SDL_Rect contentClip = insetRect(root, 3);
    SDL_Rect oldClip{};
    const SDL_bool hadClip = SDL_RenderIsClipEnabled(r);
    SDL_RenderGetClipRect(r, &oldClip);
    SDL_RenderSetClipRect(r, &contentClip);

    int y = root.y + 10 - contentScroll;
    drawText(r, font, "Tool Options", root.x + 10, y, COL_TEXT_MAIN);
    y += std::max(18, strova::ui_text::measureTextHeight(font)) + 12;
    const int pad = 8;
    const int h = 44;

    if (activeTool == ToolType::Brush)
        drawBrushBrowser(r, font, settings, y);

    SDL_Rect sizeRc{ root.x + 10, y, root.w - 20, h };
    drawStepper(r, font, sizeRc, "Size", settings.size, 1.0f, 1.0f, 200.0f);
    y += h + pad;

    SDL_Rect opRc{ root.x + 10, y, root.w - 20, h };
    drawSlider(r, font, opRc, "Opacity", settings.opacity);
    y += h + pad;

    SDL_Rect stabRc{ root.x + 10, y, root.w - 20, h };
    drawSlider(r, font, stabRc, "Stabilizer", settings.stabilizer);
    y += h + pad;

    if (activeTool == ToolType::Brush)
    {
        SDL_Rect flowRc{ root.x + 10, y, root.w - 20, h };
        drawSlider(r, font, flowRc, "Flow", settings.flow);
        y += h + pad;
        SDL_Rect spacingRc{ root.x + 10, y, root.w - 20, h };
        drawSlider(r, font, spacingRc, "Spacing", settings.spacing);
        y += h + pad;
    }
    else if (activeTool == ToolType::Calligraphy)
    {
        SDL_Rect aRc{ root.x + 10, y, root.w - 20, h };
        drawStepper(r, font, aRc, "Angle", settings.angleDeg, 5.0f, 0.0f, 180.0f);
        y += h + pad;
        SDL_Rect aspRc{ root.x + 10, y, root.w - 20, h };
        drawSlider(r, font, aspRc, "Aspect", settings.aspect);
        y += h + pad;
    }
    else if (activeTool == ToolType::Airbrush)
    {
        SDL_Rect rRc{ root.x + 10, y, root.w - 20, h };
        drawStepper(r, font, rRc, "Radius", settings.airRadius, 2.0f, 2.0f, 120.0f);
        y += h + pad;
        SDL_Rect dRc{ root.x + 10, y, root.w - 20, h };
        drawSlider(r, font, dRc, "Density", settings.airDensity);
        y += h + pad;
    }
    else if (activeTool == ToolType::Eraser || activeTool == ToolType::SoftEraser)
    {
        SDL_Rect eRc{ root.x + 10, y, root.w - 20, h };
        drawSlider(r, font, eRc, "Strength", settings.eraserStrength);
        y += h + pad;
    }
    else if (activeTool == ToolType::Smudge)
    {
        SDL_Rect smRc{ root.x + 10, y, root.w - 20, h };
        drawSlider(r, font, smRc, "Strength", settings.smudgeStrength);
        y += h + pad;
    }
    else if (activeTool == ToolType::Blur)
    {
        SDL_Rect brRc{ root.x + 10, y, root.w - 20, h };
        drawStepper(r, font, brRc, "Radius", settings.blurRadius, 1.0f, 1.0f, 50.0f);
        y += h + pad;
    }
    else if (activeTool == ToolType::Fill)
    {
        SDL_Rect ftRc{ root.x + 10, y, root.w - 20, h };
        drawStepper(r, font, ftRc, "Tolerance", (float)settings.fillTolerance, 1.0f, 0.0f, 255.0f);
        y += h + pad;
    }

    contentHeight = std::max(root.h, y - root.y + 10 + contentScroll);
    const int maxScroll = std::max(0, contentHeight - root.h);
    contentScroll = std::clamp(contentScroll, 0, maxScroll);

    if (maxScroll > 0)
    {
        const int trackH = std::max(24, contentClip.h - 12);
        SDL_Rect track{ contentClip.x + contentClip.w - strova::theme::scrollbarWidth - 2, contentClip.y + 6, strova::theme::scrollbarWidth, trackH };
        strova::uix::drawScrollbar(r, track, contentScroll, root.h, contentHeight);
    }

    if (hadClip) SDL_RenderSetClipRect(r, &oldClip); else SDL_RenderSetClipRect(r, nullptr);
    strova::uix::drawRect(r, root, COL_BORDER_SOFT);
}

bool ToolOptionsPanel::handleEvent(const SDL_Event& e, int mx, int my, ToolType activeTool, const ToolSettings& currentSettings, ToolSettings& outSettings)
{
    if (!in(root, mx, my)) return false;
    outSettings = currentSettings;
    pendingAction = PanelAction::None;
    pendingBrushId.clear();

    const int maxScroll = std::max(0, contentHeight - root.h);
    if (e.type == SDL_MOUSEWHEEL)
    {
        contentScroll -= e.wheel.y * 32;
        contentScroll = std::clamp(contentScroll, 0, maxScroll);
        return true;
    }

    int y = root.y + 10 - contentScroll;
    y += 30;
    const int pad = 8;
    const int h = 44;

    if (activeTool == ToolType::Brush)
    {
        rebuildFilteredBrushes();
        y += 18 + 10;
        const int panelMaxScroll = std::max(0, contentHeight - root.h);
    const int contentRightInset = panelMaxScroll > 0 ? (strova::theme::scrollbarWidth + 8) : 0;
    SDL_Rect browserBounds{ root.x + 3, root.y + 3, std::max(20, root.w - 6 - contentRightInset), std::max(20, root.h - 6) };

    const FilterLayout filters = buildFilterLayout(SDL_Rect{ root.x, root.y, std::max(40, root.w - contentRightInset), root.h }, y);

        if (searchFocused)
        {
            if (e.type == SDL_TEXTINPUT) { searchText += e.text.text; return true; }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_BACKSPACE && !searchText.empty()) { searchText.pop_back(); return true; }
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            if (in(filters.category, mx, my))
            {
                auto cats = availableCategories();
                auto it = std::find(cats.begin(), cats.end(), categoryFilter);
                size_t idx = (it == cats.end()) ? 0 : (size_t)std::distance(cats.begin(), it);
                idx = (idx + 1) % std::max<size_t>(1, cats.size());
                categoryFilter = cats[idx];
                return true;
            }
            if (in(filters.source, mx, my))
            {
                int v = (int)sourceFilter + 1;
                if (v > (int)SourceFilter::Installed) v = 0;
                sourceFilter = (SourceFilter)v;
                return true;
            }
            if (in(filters.search, mx, my))
            {
                searchFocused = true;
                return true;
            }
            searchFocused = false;
        }

        y += filters.totalHeight + 10;
        const int cardH = 76;
        const int cardGap = 8;

        if (filteredBrushIds.empty())
        {
            y += 72 + 10;
        }
        else if (auto* mgr = strova::brush::globalManager())
        {
            for (const auto& id : filteredBrushIds)
            {
                const auto* pkg = mgr->findById(id);
                if (!pkg) continue;
                SDL_Rect cardR{ browserBounds.x + 2, y, browserBounds.w - 4, cardH };
            if (cardR.y + cardR.h < browserBounds.y || cardR.y > browserBounds.y + browserBounds.h) { y += cardH + cardGap; continue; }
                SDL_Rect manageR{ cardR.x + cardR.w - 28, cardR.y + cardR.h - 28, 20, 20 };
                if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                {
                    if (in(manageR, mx, my))
                    {
                        pendingAction = PanelAction::ManageBrushes;
                        pendingBrushId = pkg->manifest.id;
                        return true;
                    }
                    if (in(cardR, mx, my))
                    {
                        outSettings.brushId = pkg->manifest.id;
                        outSettings.brushDisplayName = pkg->manifest.name;
                        outSettings.brushVersion = pkg->manifest.version;
                        outSettings.brushSupportsUserColor = pkg->manifest.color.supportsUserColor;
                        outSettings.brushSupportsGradient = pkg->manifest.color.supportsGradient;
                        outSettings.spacing = pkg->manifest.params.spacing;
                        outSettings.flow = pkg->manifest.params.flow;
                        outSettings.clamp();
                        pendingBrushId = pkg->manifest.id;
                        return true;
                    }
                }
                y += cardH + cardGap;
            }
        }

        std::vector<SDL_Rect> footerBtns = buildFooterButtons(SDL_Rect{ root.x, root.y, std::max(40, root.w - contentRightInset), root.h }, y);
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            if (footerBtns.size() > 0 && in(footerBtns[0], mx, my)) { pendingAction = PanelAction::ManageBrushes; pendingBrushId = outSettings.brushId; return true; }
            if (footerBtns.size() > 1 && in(footerBtns[1], mx, my)) { pendingAction = PanelAction::OpenBrushCreator; return true; }
            if (footerBtns.size() > 2 && in(footerBtns[2], mx, my)) { pendingAction = PanelAction::InstallBrush; return true; }
            if (footerBtns.size() > 3 && in(footerBtns[3], mx, my)) { pendingAction = PanelAction::RefreshBrushes; return true; }
        }
        y += footerBtns.size() > 2 ? 64 : 28;
        y += 14;
    }

    SDL_Rect sizeRc{ root.x + 10, y, root.w - 20, h };
    if (stepperEvent(e, mx, my, sizeRc, outSettings.size, 1.0f, 1.0f, 200.0f)) { outSettings.clamp(); return true; }
    y += h + pad;
    SDL_Rect opRc{ root.x + 10, y, root.w - 20, h };
    if (sliderEvent(e, mx, my, opRc, outSettings.opacity)) { outSettings.clamp(); return true; }
    y += h + pad;
    SDL_Rect stabRc{ root.x + 10, y, root.w - 20, h };
    if (sliderEvent(e, mx, my, stabRc, outSettings.stabilizer)) { outSettings.clamp(); return true; }
    y += h + pad;

    if (activeTool == ToolType::Brush)
    {
        SDL_Rect flowRc{ root.x + 10, y, root.w - 20, h };
        if (sliderEvent(e, mx, my, flowRc, outSettings.flow)) { outSettings.clamp(); return true; }
        y += h + pad;
        SDL_Rect spacingRc{ root.x + 10, y, root.w - 20, h };
        if (sliderEvent(e, mx, my, spacingRc, outSettings.spacing)) { outSettings.clamp(); return true; }
        y += h + pad;
    }
    else if (activeTool == ToolType::Calligraphy)
    {
        SDL_Rect aRc{ root.x + 10, y, root.w - 20, h };
        if (stepperEvent(e, mx, my, aRc, outSettings.angleDeg, 5.0f, 0.0f, 180.0f)) { outSettings.clamp(); return true; }
        y += h + pad;
        SDL_Rect aspRc{ root.x + 10, y, root.w - 20, h };
        if (sliderEvent(e, mx, my, aspRc, outSettings.aspect)) { outSettings.clamp(); return true; }
        y += h + pad;
    }
    else if (activeTool == ToolType::Airbrush)
    {
        SDL_Rect rRc{ root.x + 10, y, root.w - 20, h };
        if (stepperEvent(e, mx, my, rRc, outSettings.airRadius, 2.0f, 2.0f, 120.0f)) { outSettings.clamp(); return true; }
        y += h + pad;
        SDL_Rect dRc{ root.x + 10, y, root.w - 20, h };
        if (sliderEvent(e, mx, my, dRc, outSettings.airDensity)) { outSettings.clamp(); return true; }
        y += h + pad;
    }
    else if (activeTool == ToolType::Eraser || activeTool == ToolType::SoftEraser)
    {
        SDL_Rect eRc{ root.x + 10, y, root.w - 20, h };
        if (sliderEvent(e, mx, my, eRc, outSettings.eraserStrength)) { outSettings.clamp(); return true; }
        y += h + pad;
    }
    else if (activeTool == ToolType::Smudge)
    {
        SDL_Rect smRc{ root.x + 10, y, root.w - 20, h };
        if (sliderEvent(e, mx, my, smRc, outSettings.smudgeStrength)) { outSettings.clamp(); return true; }
        y += h + pad;
    }
    else if (activeTool == ToolType::Blur)
    {
        SDL_Rect brRc{ root.x + 10, y, root.w - 20, h };
        if (stepperEvent(e, mx, my, brRc, outSettings.blurRadius, 1.0f, 1.0f, 50.0f)) { outSettings.clamp(); return true; }
        y += h + pad;
    }
    else if (activeTool == ToolType::Fill)
    {
        float tol = (float)outSettings.fillTolerance;
        SDL_Rect ftRc{ root.x + 10, y, root.w - 20, h };
        if (stepperEvent(e, mx, my, ftRc, tol, 1.0f, 0.0f, 255.0f)) { outSettings.fillTolerance = (int)std::lround(tol); outSettings.clamp(); return true; }
        y += h + pad;
    }

    return true;
}

bool ToolOptionsPanel::consumeAction(PanelAction& outAction, std::string& outBrushId)
{
    if (pendingAction == PanelAction::None) return false;
    outAction = pendingAction;
    outBrushId = pendingBrushId;
    pendingAction = PanelAction::None;
    pendingBrushId.clear();
    return true;
}
