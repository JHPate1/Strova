#include "BrushResourceManager.h"

#include "../app/App.h"
#include "../core/Theme.h"
#include "../core/BrushSystem.h"
#include "../platform/FileDialog.h"
#include "../ui/BrushPreviewCache.h"
#include "../ui/TextRenderer.h"
#include "../ui/UiPrimitives.h"

#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace
{
    static bool inRect(const SDL_Rect& rc, int x, int y)
    {
        return x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
    }

    static void fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderFillRect(r, &rc);
    }

    static void drawRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDrawRect(r, &rc);
    }

    static void drawText(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y, SDL_Color col)
    {
        strova::ui_text::drawText(r, font, text, x, y, col);
    }

    static void drawButton(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const std::string& label, bool active = false)
    {
        strova::uix::drawButton(r, font, rc, label, false, active, false);
    }

    static std::string lowerCopy(const std::string& s)
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return out;
    }

#ifdef _WIN32
    static void openFolderForPath(const std::filesystem::path& path)
    {
        std::wstring wp = path.wstring();
        ShellExecuteW(nullptr, L"open", wp.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
    }
#endif
}

void BrushResourceManager::open(const std::string& brushId)
{
    visible = true;
    selectedBrushId = brushId;
    searchFocused = false;
    scrollOffset = 0;
}

void BrushResourceManager::close()
{
    visible = false;
    searchFocused = false;
}

bool BrushResourceManager::brushMatches(const std::string& id, const std::string& name, bool builtIn, const std::string& category) const
{
    if (sourceFilter == SourceFilter::BuiltIn && !builtIn) return false;
    if (sourceFilter == SourceFilter::Installed && builtIn) return false;
    if (searchText.empty()) return true;
    const std::string needle = lowerCopy(searchText);
    const std::string hay = lowerCopy(id + " " + name + " " + category);
    return hay.find(needle) != std::string::npos;
}

void BrushResourceManager::handleEvent(App& app, const SDL_Event& e)
{
    if (!visible) return;

    int w = 0, h = 0;
    SDL_GetWindowSize(app.windowHandle(), &w, &h);
    SDL_Rect modal{ w / 2 - 460, h / 2 - 300, 920, 600 };
    SDL_Rect closeR{ modal.x + modal.w - 34, modal.y + 12, 22, 24 };
    SDL_Rect searchR{ modal.x + 20, modal.y + 56, 260, 28 };
    SDL_Rect sourceR{ modal.x + 290, modal.y + 56, 140, 28 };
    SDL_Rect installR{ modal.x + modal.w - 420, modal.y + 56, 90, 28 };
    SDL_Rect reloadR{ modal.x + modal.w - 320, modal.y + 56, 90, 28 };
    SDL_Rect creatorR{ modal.x + modal.w - 220, modal.y + 56, 100, 28 };
    SDL_Rect removeR{ modal.x + modal.w - 110, modal.y + 56, 90, 28 };
    SDL_Rect listR{ modal.x + 20, modal.y + 98, 380, modal.h - 118 };
    SDL_Rect detailR{ modal.x + 412, modal.y + 98, modal.w - 432, modal.h - 118 };

    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
    {
        close();
        return;
    }

    if (searchFocused)
    {
        if (e.type == SDL_TEXTINPUT)
        {
            searchText.append(e.text.text);
            return;
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_BACKSPACE && !searchText.empty())
        {
            searchText.pop_back();
            return;
        }
    }

    if (e.type == SDL_MOUSEWHEEL)
    {
        if (inRect(listR, mx, my) || inRect(detailR, mx, my))
        {
            scrollOffset -= e.wheel.y * 44;
            if (scrollOffset < 0) scrollOffset = 0;
            return;
        }
    }

    if (e.type != SDL_MOUSEBUTTONDOWN || e.button.button != SDL_BUTTON_LEFT) return;

    if (inRect(closeR, mx, my)) { close(); return; }
    if (inRect(searchR, mx, my)) { searchFocused = true; return; }
    searchFocused = false;

    if (inRect(sourceR, mx, my))
    {
        int v = (int)sourceFilter + 1;
        if (v > (int)SourceFilter::Installed) v = 0;
        sourceFilter = (SourceFilter)v;
        return;
    }
    if (inRect(installR, mx, my))
    {
        std::string path;
        if (platform::pickOpenBrushOrProject(path))
        {
            std::string installed;
            std::string err;
            if (!app.brushManager().installPackageFile(path, installed, err))
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Brush Install", err.c_str(), app.windowHandle());
        }
        return;
    }
    if (inRect(reloadR, mx, my))
    {
        app.brushManager().refresh();
        return;
    }
    if (inRect(creatorR, mx, my))
    {
        close();
        app.openBrushCreatorWorkspace("", true);
        return;
    }
    if (inRect(removeR, mx, my))
    {
        const auto* pkg = app.brushManager().findById(selectedBrushId);
        if (pkg && !pkg->builtIn && !pkg->sourcePath.empty())
        {
            std::error_code ec;
            std::filesystem::remove(pkg->sourcePath, ec);
            app.brushManager().refresh();
        }
        return;
    }

    const auto& all = app.brushManager().all();
    int rowY = listR.y + 10 - scrollOffset;
    for (const auto& pkg : all)
    {
        if (!brushMatches(pkg.manifest.id, pkg.manifest.name, pkg.builtIn, pkg.manifest.category))
            continue;
        SDL_Rect rr{ listR.x + 8, rowY, listR.w - 16, 68 };
        if (inRect(rr, mx, my))
        {
            selectedBrushId = pkg.manifest.id;
            auto settings = app.toolBank.get(ToolType::Brush);
            settings.brushId = pkg.manifest.id;
            settings.brushDisplayName = pkg.manifest.name;
            settings.brushVersion = pkg.manifest.version;
            settings.brushSupportsUserColor = pkg.manifest.color.supportsUserColor;
            settings.brushSupportsGradient = pkg.manifest.color.supportsGradient;
            settings.spacing = pkg.manifest.params.spacing;
            settings.flow = pkg.manifest.params.flow;
            app.replaceToolSettingsCommand(ToolType::Brush, settings);
            app.brushManager().select(pkg.manifest.id);
            app.getEngine().setBrushSelection(pkg.manifest.id, pkg.manifest.version, pkg.manifest.name);
            return;
        }
        rowY += 76;
    }

#ifdef _WIN32
    const auto* pkg = app.brushManager().findById(selectedBrushId);
    if (pkg && inRect(detailR, mx, my) && !pkg->sourcePath.empty())
    {
        SDL_Rect openFolderR{ detailR.x + detailR.w - 112, detailR.y + detailR.h - 40, 100, 28 };
        if (inRect(openFolderR, mx, my))
        {
            openFolderForPath(std::filesystem::path(pkg->sourcePath).parent_path());
            return;
        }
    }
#endif
}

void BrushResourceManager::render(App& app, SDL_Renderer* r, TTF_Font* font, int w, int h)
{
    if (!visible) return;

    SDL_Rect dim{ 0, 0, w, h };
    SDL_Rect modal{ w / 2 - 460, h / 2 - 300, 920, 600 };
    strova::uix::drawModal(r, dim, modal);
    SDL_Rect closeR{ modal.x + modal.w - 34, modal.y + 12, 22, 24 };
    SDL_Rect searchR{ modal.x + 20, modal.y + 56, 260, 28 };
    SDL_Rect sourceR{ modal.x + 290, modal.y + 56, 140, 28 };
    SDL_Rect installR{ modal.x + modal.w - 420, modal.y + 56, 90, 28 };
    SDL_Rect reloadR{ modal.x + modal.w - 320, modal.y + 56, 90, 28 };
    SDL_Rect creatorR{ modal.x + modal.w - 220, modal.y + 56, 100, 28 };
    SDL_Rect removeR{ modal.x + modal.w - 110, modal.y + 56, 90, 28 };
    SDL_Rect listR{ modal.x + 20, modal.y + 98, 380, modal.h - 118 };
    SDL_Rect detailR{ modal.x + 412, modal.y + 98, modal.w - 432, modal.h - 118 };

    fillRect(r, modal, SDL_Color{ 13, 17, 24, 248 });
    drawRect(r, modal, SDL_Color{ 112, 126, 150, 255 });
    fillRect(r, listR, COL_BG_PANEL);
    fillRect(r, detailR, COL_BG_PANEL);
    drawRect(r, listR, COL_BORDER_SOFT);
    drawRect(r, detailR, COL_BORDER_SOFT);

    drawText(r, font, "Manage Brushes", modal.x + 18, modal.y + 14, COL_TEXT_MAIN);
    drawButton(r, font, closeR, "X");
    drawButton(r, font, searchR, searchText.empty() ? "Search brushes" : searchText, searchFocused);
    const char* srcLabel = sourceFilter == SourceFilter::All ? "Source: All" : (sourceFilter == SourceFilter::BuiltIn ? "Built-in" : "Installed");
    drawButton(r, font, sourceR, srcLabel);
    drawButton(r, font, installR, "Install");
    drawButton(r, font, reloadR, "Reload");
    drawButton(r, font, creatorR, "Creator");
    drawButton(r, font, removeR, "Remove");

    SDL_Rect oldClip{};
    const SDL_bool hadClip = SDL_RenderIsClipEnabled(r);
    SDL_RenderGetClipRect(r, &oldClip);
    SDL_RenderSetClipRect(r, &listR);
    const auto& all = app.brushManager().all();
    int rowY = listR.y + 10 - scrollOffset;
    int matchCount = 0;
    for (const auto& pkg : all)
    {
        if (!brushMatches(pkg.manifest.id, pkg.manifest.name, pkg.builtIn, pkg.manifest.category))
            continue;
        ++matchCount;
        SDL_Rect rr{ listR.x + 8, rowY, listR.w - 16, 68 };
        if (rr.y + rr.h < listR.y || rr.y > listR.y + listR.h)
        {
            rowY += 76;
            continue;
        }
        const bool active = (pkg.manifest.id == selectedBrushId);
        fillRect(r, rr, active ? SDL_Color{ 86, 112, 156, 220 } : COL_BG_PANEL2);
        drawRect(r, rr, active ? SDL_Color{ 212, 228, 255, 255 } : COL_BORDER_SOFT);

        SDL_Rect previewR{ rr.x + 8, rr.y + 8, 52, 52 };
        fillRect(r, previewR, SDL_Color{ 15, 20, 30, 255 });
        drawRect(r, previewR, COL_BORDER_SOFT);
        if (SDL_Texture* tex = strova::ui_brush_preview::getPreviewTexture(r, pkg))
        {
            bool fallback = false;
            const strova::brush::BrushStamp* src = strova::ui_brush_preview::previewSource(pkg, fallback);
            if (src)
            {
                SDL_Rect dst = previewR;
                const float sx = (float)(previewR.w - 8) / (float)std::max(1, src->width);
                const float sy = (float)(previewR.h - 8) / (float)std::max(1, src->height);
                const float fit = std::max(0.01f, std::min(sx, sy));
                dst.w = std::max(1, (int)std::lround((float)src->width * fit));
                dst.h = std::max(1, (int)std::lround((float)src->height * fit));
                dst.x = previewR.x + (previewR.w - dst.w) / 2;
                dst.y = previewR.y + (previewR.h - dst.h) / 2;
                SDL_RenderCopy(r, tex, nullptr, &dst);
            }
        }

        SDL_Color titleC = active ? SDL_Color{ 12, 14, 20, 255 } : COL_TEXT_MAIN;
        SDL_Color dimC = active ? SDL_Color{ 18, 24, 32, 255 } : COL_TEXT_DIM;
        strova::ui_text::drawTextLeftMiddle(r, font, pkg.manifest.name, SDL_Rect{ rr.x + 70, rr.y + 8, rr.w - 78, 22 }, titleC);
        strova::ui_text::drawTextLeftMiddle(r, font, std::string(pkg.builtIn ? "Built-in" : "Installed") + " • " + pkg.manifest.category,
            SDL_Rect{ rr.x + 70, rr.y + 28, rr.w - 78, 18 }, dimC);
        strova::ui_text::drawTextLeftMiddle(r, font, pkg.manifest.tags.empty() ? pkg.manifest.id : pkg.manifest.tags,
            SDL_Rect{ rr.x + 70, rr.y + 46, rr.w - 78, 16 }, dimC);
        rowY += 76;
    }
    if (hadClip) SDL_RenderSetClipRect(r, &oldClip); else SDL_RenderSetClipRect(r, nullptr);

    drawText(r, font, std::to_string(matchCount) + " matches", listR.x + 8, listR.y - 22, COL_TEXT_DIM);

    const auto* pkg = app.brushManager().findById(selectedBrushId);
    if (pkg)
    {
        SDL_Rect previewR{ detailR.x + 12, detailR.y + 12, 112, 112 };
        fillRect(r, previewR, SDL_Color{ 15, 20, 30, 255 });
        drawRect(r, previewR, COL_BORDER_SOFT);
        if (SDL_Texture* tex = strova::ui_brush_preview::getPreviewTexture(r, *pkg))
        {
            bool fallback = false;
            const strova::brush::BrushStamp* src = strova::ui_brush_preview::previewSource(*pkg, fallback);
            if (src)
            {
                SDL_Rect dst = previewR;
                const float sx = (float)(previewR.w - 12) / (float)std::max(1, src->width);
                const float sy = (float)(previewR.h - 12) / (float)std::max(1, src->height);
                const float fit = std::max(0.01f, std::min(sx, sy));
                dst.w = std::max(1, (int)std::lround((float)src->width * fit));
                dst.h = std::max(1, (int)std::lround((float)src->height * fit));
                dst.x = previewR.x + (previewR.w - dst.w) / 2;
                dst.y = previewR.y + (previewR.h - dst.h) / 2;
                SDL_RenderCopy(r, tex, nullptr, &dst);
            }
        }

        drawText(r, font, pkg->manifest.name, detailR.x + 136, detailR.y + 14, COL_TEXT_MAIN);
        strova::ui_text::drawTextWrapped(r, font, "Id: " + pkg->manifest.id, SDL_Rect{ detailR.x + 136, detailR.y + 42, detailR.w - 148, 34 }, COL_TEXT_DIM, 2);
        strova::ui_text::drawTextLeftMiddle(r, font, "Type: " + std::string(strova::brush::brushTypeName(pkg->manifest.type)), SDL_Rect{ detailR.x + 136, detailR.y + 74, detailR.w - 148, 18 }, COL_TEXT_DIM);
        strova::ui_text::drawTextLeftMiddle(r, font, "Source: " + std::string(pkg->builtIn ? "Built-in" : "Installed"), SDL_Rect{ detailR.x + 136, detailR.y + 94, detailR.w - 148, 18 }, COL_TEXT_DIM);
        strova::ui_text::drawTextLeftMiddle(r, font, "Category: " + pkg->manifest.category, SDL_Rect{ detailR.x + 136, detailR.y + 114, detailR.w - 148, 18 }, COL_TEXT_DIM);

        const int bodyY = detailR.y + 144;
        drawText(r, font, "Tags", detailR.x + 12, bodyY, COL_TEXT_MAIN);
        strova::ui_text::drawTextWrapped(r, font, pkg->manifest.tags.empty() ? std::string("No tags") : pkg->manifest.tags,
            SDL_Rect{ detailR.x + 12, bodyY + 24, detailR.w - 24, 48 }, COL_TEXT_DIM, 2);

        drawText(r, font, "Description", detailR.x + 12, bodyY + 84, COL_TEXT_MAIN);
        strova::ui_text::drawTextWrapped(r, font, pkg->manifest.description.empty() ? std::string("No description provided.") : pkg->manifest.description,
            SDL_Rect{ detailR.x + 12, bodyY + 108, detailR.w - 24, 88 }, COL_TEXT_DIM, 4);

        drawText(r, font, "Package Path", detailR.x + 12, bodyY + 208, COL_TEXT_MAIN);
        strova::ui_text::drawTextWrapped(r, font, pkg->sourcePath.empty() ? std::string("n/a") : pkg->sourcePath,
            SDL_Rect{ detailR.x + 12, bodyY + 232, detailR.w - 24, 66 }, COL_TEXT_DIM, 3);

        strova::ui_text::drawTextWrapped(r, font, "Selecting a brush here applies it immediately to the Brush tool runtime and keeps the editor brush browser in sync.",
            SDL_Rect{ detailR.x + 12, detailR.y + detailR.h - 92, detailR.w - 24, 48 }, COL_TEXT_DIM, 2);
#ifdef _WIN32
        if (!pkg->sourcePath.empty())
            drawButton(r, font, SDL_Rect{ detailR.x + detailR.w - 112, detailR.y + detailR.h - 40, 100, 28 }, "Open Folder");
#endif
    }
}
