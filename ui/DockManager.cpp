/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/DockManager.cpp
   Module:      Ui
   Purpose:     Dockable panel layout, dragging, resizing, and persistence.

   Notes:
   - Windowless tool windows rendered in the main SDL framebuffer.
   - Persistence is dependency-free and tolerant of missing/corrupt fields.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "DockManager.h"
#include "TextRenderer.h"
#include "UiPrimitives.h"
#include "../core/Theme.h"
#include "../core/SerializationUtils.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace strova
{
    namespace
    {
        static bool pointInRect(const SDL_Rect& rc, int x, int y)
        {
            return rc.w > 0 && rc.h > 0 && x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
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

        static void drawText(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y, SDL_Color c)
        {
            strova::ui_text::drawText(r, font, text, x, y, c);
        }

        static int measureTextW(TTF_Font* font, const std::string& text)
        {
            return strova::ui_text::measureTextWidth(font, text);
        }

        static SDL_Cursor* cachedCursor(SDL_SystemCursor id)
        {
            static SDL_Cursor* cursors[SDL_NUM_SYSTEM_CURSORS]{};
            const int idx = (int)id;
            if (idx < 0 || idx >= SDL_NUM_SYSTEM_CURSORS) return nullptr;
            if (!cursors[idx]) cursors[idx] = SDL_CreateSystemCursor(id);
            return cursors[idx];
        }

        static void setCursorForResizeMask(int mask)
        {
            constexpr int kResizeLeft = 1;
            constexpr int kResizeRight = 2;
            constexpr int kResizeTop = 4;
            constexpr int kResizeBottom = 8;
            SDL_SystemCursor cursorId = SDL_SYSTEM_CURSOR_ARROW;
            const bool horiz = (mask & kResizeLeft) || (mask & kResizeRight);
            const bool vert = (mask & kResizeTop) || (mask & kResizeBottom);
            if (horiz && vert)
            {
                const bool nwse = ((mask & kResizeLeft) && (mask & kResizeTop)) ||
                                  ((mask & kResizeRight) && (mask & kResizeBottom));
                cursorId = nwse ? SDL_SYSTEM_CURSOR_SIZENWSE : SDL_SYSTEM_CURSOR_SIZENESW;
            }
            else if (horiz) cursorId = SDL_SYSTEM_CURSOR_SIZEWE;
            else if (vert) cursorId = SDL_SYSTEM_CURSOR_SIZENS;
            if (SDL_Cursor* cursor = cachedCursor(cursorId))
                SDL_SetCursor(cursor);
        }

        static void drawHeaderButton(SDL_Renderer* r, TTF_Font* /*font*/, const SDL_Rect& rc, const char* label, bool hover, bool disabled = false)
        {
            SDL_Color fill = hover ? SDL_Color{ 66, 74, 94, 235 } : SDL_Color{ 44, 48, 58, 235 };
            SDL_Color border = SDL_Color{ 92, 100, 122, 220 };
            SDL_Color ink = SDL_Color{ 224, 228, 236, 255 };
            if (disabled)
            {
                fill = SDL_Color{ 34, 38, 48, 200 };
                border = SDL_Color{ 70, 76, 92, 180 };
                ink = SDL_Color{ 120, 128, 144, 200 };
            }
            fillRect(r, rc, fill);
            drawRect(r, rc, border);

            if (!label || rc.w <= 0 || rc.h <= 0) return;

            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, ink.r, ink.g, ink.b, ink.a);

            const int pad = std::max(3, rc.w / 4);
            const int x0 = rc.x + pad;
            const int x1 = rc.x + rc.w - pad - 1;
            const int y0 = rc.y + pad;
            const int y1 = rc.y + rc.h - pad - 1;
            const int cy = rc.y + rc.h / 2;

            if (label[0] == 'X' || label[0] == 'x')
            {
                SDL_RenderDrawLine(r, x0, y0, x1, y1);
                SDL_RenderDrawLine(r, x1, y0, x0, y1);
            }
            else if (label[0] == '-')
            {
                SDL_RenderDrawLine(r, x0, cy, x1, cy);
            }
            else if (label[0] == '+')
            {
                SDL_RenderDrawLine(r, x0, cy, x1, cy);
                SDL_RenderDrawLine(r, rc.x + rc.w / 2, y0, rc.x + rc.w / 2, y1);
            }
        }


        static bool findObjectFieldPos(const std::string& text, const std::string& key, std::size_t& outPos)
        {
            const std::string marker = "\"" + key + "\"";
            std::size_t pos = text.find(marker);
            if (pos == std::string::npos) return false;
            pos = text.find(':', pos);
            if (pos == std::string::npos) return false;
            ++pos;
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
            outPos = pos;
            return true;
        }

        static bool parseBoolField(const std::string& obj, const std::string& key, bool& out)
        {
            std::size_t pos = 0;
            if (!findObjectFieldPos(obj, key, pos)) return false;
            if (obj.compare(pos, 4, "true") == 0) { out = true; return true; }
            if (obj.compare(pos, 5, "false") == 0) { out = false; return true; }
            return false;
        }

        static bool parseStringField(const std::string& obj, const std::string& key, std::string& out)
        {
            std::size_t pos = 0;
            if (!findObjectFieldPos(obj, key, pos) || pos >= obj.size() || obj[pos] != '"') return false;
            ++pos;
            std::size_t end = obj.find('"', pos);
            if (end == std::string::npos) return false;
            out = obj.substr(pos, end - pos);
            return true;
        }

        static bool parseFloatField(const std::string& obj, const std::string& key, float& out)
        {
            std::size_t pos = 0;
            if (!findObjectFieldPos(obj, key, pos)) return false;
            std::size_t start = pos;
            if (pos < obj.size() && (obj[pos] == '-' || obj[pos] == '+')) ++pos;
            while (pos < obj.size() && (std::isdigit(static_cast<unsigned char>(obj[pos])) || obj[pos] == '.')) ++pos;
            if (pos == start) return false;
            try { out = std::stof(obj.substr(start, pos - start)); }
            catch (...) { return false; }
            return true;
        }

        static bool parseRectField(const std::string& obj, const std::string& key, SDL_Rect& out)
        {
            std::size_t pos = 0;
            if (!findObjectFieldPos(obj, key, pos) || pos >= obj.size() || obj[pos] != '[') return false;
            ++pos;
            int vals[4]{};
            for (int i = 0; i < 4; ++i)
            {
                while (pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos]))) ++pos;
                std::size_t start = pos;
                if (pos < obj.size() && (obj[pos] == '-' || obj[pos] == '+')) ++pos;
                while (pos < obj.size() && std::isdigit(static_cast<unsigned char>(obj[pos]))) ++pos;
                if (pos == start) return false;
                try { vals[i] = std::stoi(obj.substr(start, pos - start)); }
                catch (...) { return false; }
                while (pos < obj.size() && std::isspace(static_cast<unsigned char>(obj[pos]))) ++pos;
                if (i < 3)
                {
                    if (pos >= obj.size() || obj[pos] != ',') return false;
                    ++pos;
                }
            }
            out = SDL_Rect{ vals[0], vals[1], vals[2], vals[3] };
            return true;
        }

        static std::vector<std::string> splitArrayObjects(const std::string& text, const std::string& key)
        {
            std::vector<std::string> out;
            std::size_t pos = text.find("\"" + key + "\"");
            if (pos == std::string::npos) return out;
            pos = text.find('[', pos);
            if (pos == std::string::npos) return out;
            int depth = 0;
            std::size_t start = std::string::npos;
            for (; pos < text.size(); ++pos)
            {
                if (text[pos] == '{')
                {
                    if (depth == 0) start = pos;
                    ++depth;
                }
                else if (text[pos] == '}')
                {
                    if (depth > 0) --depth;
                    if (depth == 0 && start != std::string::npos)
                    {
                        out.push_back(text.substr(start, pos - start + 1));
                        start = std::string::npos;
                    }
                }
                else if (text[pos] == ']' && depth == 0)
                {
                    break;
                }
            }
            return out;
        }

        static bool isPluginPanelId(const std::string& id)
        {
            return id.rfind("plugin:", 0) == 0;
        }

        static int panelMinWidth(const strova::DockPanel& panel)
        {
            if (panel.id == "Canvas") return 200;
            if (panel.id == "Preview") return 180;
            if (panel.id == "Brush") return 240;
            return strova::DockManager::kMinPanelW;
        }

        static int panelMinHeight(const strova::DockPanel& panel)
        {
            if (panel.id == "Canvas") return 200;
            if (panel.id == "Preview") return 140;
            if (panel.id == "Timeline") return 270;
            return strova::DockManager::kMinPanelH;
        }

        static bool rectEquals(const SDL_Rect& a, const SDL_Rect& b)
        {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }
    }

    DockManager::InternalState& DockManager::stateFor(DockPanel& panel)
    {
        return panelState[panel.id];
    }

    const DockManager::InternalState& DockManager::stateFor(const DockPanel& panel) const
    {
        auto it = panelState.find(panel.id);
        if (it != panelState.end()) return it->second;
        static InternalState fallback{};
        return fallback;
    }

    const char* DockManager::dockToString(DockPosition dock)
    {
        switch (dock)
        {
        case DockPosition::CENTER: return "CENTER";
        case DockPosition::CENTER_LEFT: return "CENTER_LEFT";
        case DockPosition::CENTER_RIGHT: return "CENTER_RIGHT";
        case DockPosition::TOP_LEFT: return "TOP_LEFT";
        case DockPosition::TOP: return "TOP";
        case DockPosition::TOP_RIGHT: return "TOP_RIGHT";
        case DockPosition::BOTTOM_LEFT: return "BOTTOM_LEFT";
        case DockPosition::BOTTOM: return "BOTTOM";
        case DockPosition::BOTTOM_RIGHT: return "BOTTOM_RIGHT";
        case DockPosition::FLOATING:
        default: return "FLOATING";
        }
    }

    DockPosition DockManager::dockFromString(const std::string& text, DockPosition fallback)
    {
        if (text == "CENTER") return DockPosition::CENTER;
        if (text == "CENTER_LEFT") return DockPosition::CENTER_LEFT;
        if (text == "CENTER_RIGHT") return DockPosition::CENTER_RIGHT;
        if (text == "TOP_LEFT") return DockPosition::TOP_LEFT;
        if (text == "TOP") return DockPosition::TOP;
        if (text == "TOP_RIGHT") return DockPosition::TOP_RIGHT;
        if (text == "BOTTOM_LEFT") return DockPosition::BOTTOM_LEFT;
        if (text == "BOTTOM") return DockPosition::BOTTOM;
        if (text == "BOTTOM_RIGHT") return DockPosition::BOTTOM_RIGHT;
        if (text == "FLOATING") return DockPosition::FLOATING;

        if (text == "Left") return DockPosition::CENTER_LEFT;
        if (text == "Right") return DockPosition::CENTER_RIGHT;
        if (text == "Top") return DockPosition::TOP;
        if (text == "Bottom") return DockPosition::BOTTOM;
        if (text == "Center") return DockPosition::CENTER;
        if (text == "Floating") return DockPosition::FLOATING;
        return fallback;
    }

    DockPanel* DockManager::getPanel(const std::string& id)
    {
        for (auto& panel : panels)
            if (panel.id == id)
                return &panel;
        return nullptr;
    }

    const DockPanel* DockManager::getPanel(const std::string& id) const
    {
        for (const auto& panel : panels)
            if (panel.id == id)
                return &panel;
        return nullptr;
    }

    SDL_Rect DockManager::panelHeaderRect(const DockPanel& panel) const
    {
        return panel.headerRect;
    }

    SDL_Rect DockManager::hideButtonRect(const DockPanel& panel) const
    {
        return SDL_Rect{ panel.headerRect.x + std::max(0, panel.headerRect.w - 48), panel.headerRect.y + 4, 18, 18 };
    }

    SDL_Rect DockManager::shrinkButtonRect(const DockPanel& panel) const
    {
        return SDL_Rect{ panel.headerRect.x + std::max(0, panel.headerRect.w - 25), panel.headerRect.y + 4, 18, 18 };
    }

    SDL_Rect DockManager::contentRect(const DockPanel& panel) const
    {
        return panel.contentRect;
    }

    SDL_Rect DockManager::contentRect(const std::string& id) const
    {
        const DockPanel* panel = getPanel(id);
        if (!panel || !panel->visible) return SDL_Rect{ 0, 0, 0, 0 };
        return panel->contentRect;
    }

    void DockManager::markLayoutDirty()
    {
        layoutDirty = true;
    }

    void DockManager::clampSplitRatios(const SDL_Rect& workspace)
    {
        bool colUsed[3]{ false, false, false };
        bool rowUsed[3]{ false, false, false };
        for (const auto& panel : panels)
        {
            if (!panel.visible) continue;
            if (panel.floating || panel.dock == DockPosition::FLOATING) continue;
            switch (panel.dock)
            {
            case DockPosition::TOP_LEFT:
            case DockPosition::CENTER_LEFT:
            case DockPosition::BOTTOM_LEFT: colUsed[0] = true; break;
            case DockPosition::TOP:
            case DockPosition::CENTER:
            case DockPosition::BOTTOM: colUsed[1] = true; break;
            case DockPosition::TOP_RIGHT:
            case DockPosition::CENTER_RIGHT:
            case DockPosition::BOTTOM_RIGHT: colUsed[2] = true; break;
            default: break;
            }
            switch (panel.dock)
            {
            case DockPosition::TOP_LEFT:
            case DockPosition::TOP:
            case DockPosition::TOP_RIGHT: rowUsed[0] = true; break;
            case DockPosition::CENTER_LEFT:
            case DockPosition::CENTER:
            case DockPosition::CENTER_RIGHT: rowUsed[1] = true; break;
            case DockPosition::BOTTOM_LEFT:
            case DockPosition::BOTTOM:
            case DockPosition::BOTTOM_RIGHT: rowUsed[2] = true; break;
            default: break;
            }
        }

        const int minLeftW = colUsed[0] ? kMinPanelW : 0;
        const int minCenterW = colUsed[1] ? kMinPanelW : 0;
        const int minRightW = colUsed[2] ? kMinPanelW : 0;
        const int minTopH = rowUsed[0] ? kMinPanelH : 0;
        const int minCenterH = rowUsed[1] ? kMinPanelH : 0;
        const int minBottomH = rowUsed[2] ? kMinPanelH : 0;

        const float w = (float)std::max(1, workspace.w);
        const float h = (float)std::max(1, workspace.h);

        const float minX1 = (float)minLeftW / w;
        const float maxX1 = 1.0f - ((float)minCenterW + (float)minRightW) / w;
        splitX1 = std::clamp(splitX1, minX1, std::max(minX1, maxX1));

        const float minX2 = splitX1 + (float)minCenterW / w;
        const float maxX2 = 1.0f - (float)minRightW / w;
        splitX2 = std::clamp(splitX2, std::min(minX2, maxX2), std::max(std::min(minX2, maxX2), maxX2));

        const float minY1 = (float)minTopH / h;
        const float maxY1 = 1.0f - ((float)minCenterH + (float)minBottomH) / h;
        splitY1 = std::clamp(splitY1, minY1, std::max(minY1, maxY1));

        const float minY2 = splitY1 + (float)minCenterH / h;
        const float maxY2 = 1.0f - (float)minBottomH / h;
        splitY2 = std::clamp(splitY2, std::min(minY2, maxY2), std::max(std::min(minY2, maxY2), maxY2));
    }

    void DockManager::bringToFront(int index)
    {
        if (index < 0 || index >= (int)panels.size()) return;
        DockPanel panel = panels[(size_t)index];
        panels.erase(panels.begin() + index);
        panels.push_back(panel);
        activePanelIndex = (int)panels.size() - 1;
    }

    void DockManager::centerPanel(DockPanel& panel, const SDL_Rect& workspace)
    {
        InternalState& st = stateFor(panel);
        panel.rect.w = std::max(panelMinWidth(panel), st.preferredW > 0 ? st.preferredW : panel.rect.w);
        panel.rect.h = panel.minimized ? kHeaderH : std::max(panelMinHeight(panel), st.lastExpandedH > 0 ? st.lastExpandedH : st.preferredH);
        panel.rect.x = workspace.x + (workspace.w - panel.rect.w) / 2;
        panel.rect.y = workspace.y + (workspace.h - panel.rect.h) / 2;
        normalizePanelRect(panel, workspace);
    }

    void DockManager::updatePanelRects(DockPanel& panel)
    {
        const int minW = panelMinWidth(panel);
        const int minH = panelMinHeight(panel);
        InternalState& st = stateFor(panel);

        panel.rect.w = std::max(minW, panel.rect.w);
        if (panel.minimized)
            panel.rect.h = kHeaderH;
        else
            panel.rect.h = std::max(minH, panel.rect.h);

        panel.headerRect = SDL_Rect{
            panel.rect.x,
            panel.rect.y,
            panel.rect.w,
            kHeaderH
        };

        panel.contentRect = SDL_Rect{
            panel.rect.x,
            panel.rect.y + kHeaderH,
            panel.rect.w,
            std::max(0, panel.rect.h - kHeaderH)
        };

        st.preferredW = std::max(minW, panel.rect.w);
        if (!panel.minimized)
        {
            st.preferredH = std::max(minH, panel.rect.h);
            st.lastExpandedH = std::max(minH, panel.rect.h);
        }
    }

    void DockManager::normalizePanelRect(DockPanel& panel, const SDL_Rect& workspace)
    {
        const int minW = panelMinWidth(panel);
        const int minH = panelMinHeight(panel);
        InternalState& st = stateFor(panel);

        panel.rect.w = std::max(minW, panel.rect.w);
        panel.rect.h = panel.minimized ? kHeaderH : std::max(minH, panel.rect.h);

        const int maxX = std::max(workspace.x, workspace.x + workspace.w - panel.rect.w);
        const int maxY = std::max(workspace.y, workspace.y + workspace.h - panel.rect.h);
        panel.rect.x = std::clamp(panel.rect.x, workspace.x, maxX);
        panel.rect.y = std::clamp(panel.rect.y, workspace.y, maxY);

        st.preferredW = std::max(minW, panel.rect.w);
        if (!panel.minimized)
        {
            st.preferredH = std::max(minH, panel.rect.h);
            st.lastExpandedH = std::max(minH, panel.rect.h);
        }
        updatePanelRects(panel);
    }

    SDL_Rect DockManager::dockPreviewRect(DockPosition dock, const SDL_Rect& workspace) const
    {
        const int leftW = std::clamp((int)std::lround((double)workspace.w * splitX1), kMinPanelW, std::max(kMinPanelW, workspace.w - kMinPanelW - kMinPanelW));
        const int rightX = std::clamp((int)std::lround((double)workspace.w * splitX2), leftW + kMinPanelW, std::max(leftW + kMinPanelW, workspace.w - kMinPanelW));
        const int centerW = std::max(kMinPanelW, rightX - leftW);
        const int rightW = std::max(kMinPanelW, workspace.w - rightX);

        const int topH = std::clamp((int)std::lround((double)workspace.h * splitY1), kMinPanelH, std::max(kMinPanelH, workspace.h - kMinPanelH - kMinPanelH));
        const int bottomY = std::clamp((int)std::lround((double)workspace.h * splitY2), topH + kMinPanelH, std::max(topH + kMinPanelH, workspace.h - kMinPanelH));
        const int centerH = std::max(kMinPanelH, bottomY - topH);
        const int bottomH = std::max(kMinPanelH, workspace.h - bottomY);

        const int col0X = workspace.x;
        const int col1X = workspace.x + leftW;
        const int col2X = workspace.x + rightX;
        const int row0Y = workspace.y;
        const int row1Y = workspace.y + topH;
        const int row2Y = workspace.y + bottomY;

        switch (dock)
        {
        case DockPosition::CENTER: return SDL_Rect{ col1X, row1Y, centerW, centerH };
        case DockPosition::CENTER_LEFT: return SDL_Rect{ col0X, row1Y, leftW, centerH };
        case DockPosition::CENTER_RIGHT: return SDL_Rect{ col2X, row1Y, rightW, centerH };
        case DockPosition::TOP_LEFT: return SDL_Rect{ col0X, row0Y, leftW, topH };
        case DockPosition::TOP: return SDL_Rect{ col1X, row0Y, centerW, topH };
        case DockPosition::TOP_RIGHT: return SDL_Rect{ col2X, row0Y, rightW, topH };
        case DockPosition::BOTTOM_LEFT: return SDL_Rect{ col0X, row2Y, leftW, bottomH };
        case DockPosition::BOTTOM: return SDL_Rect{ col1X, row2Y, centerW, bottomH };
        case DockPosition::BOTTOM_RIGHT: return SDL_Rect{ col2X, row2Y, rightW, bottomH };
        case DockPosition::FLOATING:
        default: return SDL_Rect{ 0, 0, 0, 0 };
        }
    }

    void DockManager::ensureDefaultPanels(const SDL_Rect& workspace)
    {
        auto addIfMissing = [&](const char* id, const char* title, DockPosition dock, bool floating, bool visible)
        {
            if (getPanel(id)) return;
            DockPanel panel{};
            panel.id = id;
            panel.title = title;
            panel.dock = dock;
            panel.floating = floating;
            panel.visible = visible;
            panel.rect = floating ? SDL_Rect{ workspace.x + workspace.w - 280, workspace.y + 56, 260, 180 } : dockPreviewRect(dock, workspace);

            InternalState& st = panelState[panel.id];
            st.defaultRect = panel.rect;
            st.preferredW = std::max(panelMinWidth(panel), panel.rect.w);
            st.preferredH = std::max(panelMinHeight(panel), std::max(panel.rect.h, kMinPanelH));
            st.lastExpandedH = st.preferredH;

            updatePanelRects(panel);
            panels.push_back(panel);
        };

        addIfMissing("Canvas", "Canvas", DockPosition::CENTER, false, true);
        addIfMissing("Timeline", "Timeline", DockPosition::BOTTOM, false, true);
        if (DockPanel* timeline = getPanel("Timeline"))
        {
            InternalState& st = stateFor(*timeline);
            st.preferredH = std::max(st.preferredH, 300);
            st.lastExpandedH = std::max(st.lastExpandedH, 300);
        }
        addIfMissing("Layers", "Layers", DockPosition::CENTER_LEFT, false, true);
        addIfMissing("Tools", "Tools", DockPosition::TOP_LEFT, false, true);
        addIfMissing("Preview", "Preview", DockPosition::TOP_RIGHT, false, true);
        addIfMissing("Brush", "Brush", DockPosition::CENTER_RIGHT, false, true);
        addIfMissing("Frames", "Frames", DockPosition::BOTTOM_RIGHT, false, true);
        addIfMissing("Color", "Color", DockPosition::BOTTOM_LEFT, false, true);
        addIfMissing("FlowSettings", "Flow Settings", DockPosition::FLOATING, true, true);
        addIfMissing("Plugins", "Plugins", DockPosition::FLOATING, true, true);

        if (DockPanel* canvas = getPanel("Canvas"))
        {
            canvas->title = "Canvas";
            canvas->visible = true;
            canvas->floating = false;
            canvas->dock = DockPosition::CENTER;
            canvas->dragging = false;
            canvas->minimized = false;
            updatePanelRects(*canvas);
        }

        if (DockPanel* preview = getPanel("Preview"))
        {
            preview->title = "Preview";
            if (preview->dock == DockPosition::FLOATING && !preview->floating)
                preview->dock = DockPosition::TOP_RIGHT;
            updatePanelRects(*preview);
        }

        if (DockPanel* plugins = getPanel("Plugins"))
        {
            plugins->title = "Plugins";
            InternalState& st = stateFor(*plugins);
            st.preferredW = std::max(st.preferredW, 420);
            st.preferredH = std::max(st.preferredH, 300);
            st.lastExpandedH = std::max(st.lastExpandedH, 300);
            if (plugins->rect.w < 420) plugins->rect.w = 420;
            if (plugins->rect.h < 300) plugins->rect.h = 300;
            updatePanelRects(*plugins);
        }

        if (DockPanel* flow = getPanel("FlowSettings"))
        {
            flow->title = "Flow Settings";
            InternalState& st = stateFor(*flow);
            st.preferredW = std::max(st.preferredW, 320);
            st.preferredH = std::max(st.preferredH, 280);
            st.lastExpandedH = std::max(st.lastExpandedH, 280);
            if (flow->rect.w < 320) flow->rect.w = 320;
            if (flow->rect.h < 280) flow->rect.h = 280;
            updatePanelRects(*flow);
        }
    }

    void DockManager::makeDockZoneExclusive(const std::string& winnerId, DockPosition dock)
    {
        if (dock == DockPosition::FLOATING) return;
        for (auto& panel : panels)
        {
            if (panel.id == winnerId) continue;
            if (!panel.visible) continue;
            if (!panel.floating && panel.dock == dock)
            {
                panel.floating = true;
                panel.dock = DockPosition::FLOATING;
            }
        }
    }

    void DockManager::applyDockLayout(const SDL_Rect& workspace)
    {
        ensureDefaultPanels(workspace);
        clampSplitRatios(workspace);

        std::unordered_map<int, std::string> winningPanels;
        for (int i = (int)panels.size() - 1; i >= 0; --i)
        {
            DockPanel& panel = panels[(size_t)i];
            if (!panel.visible) continue;
            if (panel.floating || panel.dock == DockPosition::FLOATING) continue;
            const int zoneKey = (int)panel.dock;
            if (winningPanels.find(zoneKey) == winningPanels.end())
                winningPanels[zoneKey] = panel.id;
        }

        const int x0 = workspace.x;
        const int x1 = workspace.x + std::clamp((int)std::lround((double)workspace.w * splitX1), 0, workspace.w);
        const int x2 = workspace.x + std::clamp((int)std::lround((double)workspace.w * splitX2), 0, workspace.w);
        const int x3 = workspace.x + workspace.w;
        const int y0 = workspace.y;
        const int y1 = workspace.y + std::clamp((int)std::lround((double)workspace.h * splitY1), 0, workspace.h);
        const int y2 = workspace.y + std::clamp((int)std::lround((double)workspace.h * splitY2), 0, workspace.h);
        const int y3 = workspace.y + workspace.h;

        SDL_Rect cells[3][3]{};
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const int xs[4]{ x0, x1, x2, x3 };
                const int ys[4]{ y0, y1, y2, y3 };
                cells[row][col] = SDL_Rect{ xs[col], ys[row], std::max(0, xs[col + 1] - xs[col]), std::max(0, ys[row + 1] - ys[row]) };
            }
        }

        int owner[3][3];
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                owner[row][col] = -1;

        auto zoneToCell = [](DockPosition dock, int& row, int& col)
        {
            switch (dock)
            {
            case DockPosition::TOP_LEFT: row = 0; col = 0; return true;
            case DockPosition::TOP: row = 0; col = 1; return true;
            case DockPosition::TOP_RIGHT: row = 0; col = 2; return true;
            case DockPosition::CENTER_LEFT: row = 1; col = 0; return true;
            case DockPosition::CENTER: row = 1; col = 1; return true;
            case DockPosition::CENTER_RIGHT: row = 1; col = 2; return true;
            case DockPosition::BOTTOM_LEFT: row = 2; col = 0; return true;
            case DockPosition::BOTTOM: row = 2; col = 1; return true;
            case DockPosition::BOTTOM_RIGHT: row = 2; col = 2; return true;
            default: break;
            }
            return false;
        };

        for (int i = 0; i < (int)panels.size(); ++i)
        {
            DockPanel& panel = panels[(size_t)i];
            if (!panel.visible) continue;
            if (panel.floating || panel.dock == DockPosition::FLOATING) continue;

            auto it = winningPanels.find((int)panel.dock);
            if (it == winningPanels.end() || it->second != panel.id)
            {
                panel.floating = true;
                panel.dock = DockPosition::FLOATING;
                centerPanel(panel, workspace);
                continue;
            }

            int row = 0;
            int col = 0;
            if (zoneToCell(panel.dock, row, col)) owner[row][col] = i;
        }

        auto absorbRowEmpties = [&](int row)
        {
            int occupiedCols[3];
            int count = 0;
            for (int col = 0; col < 3; ++col)
                if (owner[row][col] >= 0)
                    occupiedCols[count++] = col;
            if (count == 0) return;
            for (int col = 0; col < 3; ++col)
            {
                if (owner[row][col] >= 0) continue;
                int left = -1;
                int right = -1;
                for (int c = col - 1; c >= 0; --c) if (owner[row][c] >= 0) { left = c; break; }
                for (int c = col + 1; c < 3; ++c) if (owner[row][c] >= 0) { right = c; break; }
                if (left >= 0 && right >= 0)
                {
                    const int distL = col - left;
                    const int distR = right - col;
                    owner[row][col] = owner[row][(distL <= distR) ? left : right];
                }
                else if (left >= 0) owner[row][col] = owner[row][left];
                else if (right >= 0) owner[row][col] = owner[row][right];
            }
        };

        auto absorbColEmpties = [&](int col)
        {
            int count = 0;
            for (int row = 0; row < 3; ++row)
                if (owner[row][col] >= 0)
                    ++count;
            if (count == 0) return;
            for (int row = 0; row < 3; ++row)
            {
                if (owner[row][col] >= 0) continue;
                int up = -1;
                int down = -1;
                for (int r = row - 1; r >= 0; --r) if (owner[r][col] >= 0) { up = r; break; }
                for (int r = row + 1; r < 3; ++r) if (owner[r][col] >= 0) { down = r; break; }
                if (up >= 0 && down >= 0)
                {
                    const int distU = row - up;
                    const int distD = down - row;
                    owner[row][col] = owner[(distU <= distD) ? up : down][col];
                }
                else if (up >= 0) owner[row][col] = owner[up][col];
                else if (down >= 0) owner[row][col] = owner[down][col];
            }
        };

        for (int row = 0; row < 3; ++row) absorbRowEmpties(row);
        for (int col = 0; col < 3; ++col) absorbColEmpties(col);

        std::unordered_map<int, SDL_Rect> mergedRects;
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const int idx = owner[row][col];
                if (idx < 0) continue;
                const SDL_Rect& cell = cells[row][col];
                auto it = mergedRects.find(idx);
                if (it == mergedRects.end()) mergedRects[idx] = cell;
                else
                {
                    SDL_Rect& rc = it->second;
                    const int nx = std::min(rc.x, cell.x);
                    const int ny = std::min(rc.y, cell.y);
                    const int nr = std::max(rc.x + rc.w, cell.x + cell.w);
                    const int nb = std::max(rc.y + rc.h, cell.y + cell.h);
                    rc = SDL_Rect{ nx, ny, nr - nx, nb - ny };
                }
            }
        }

        for (int i = 0; i < (int)panels.size(); ++i)
        {
            DockPanel& panel = panels[(size_t)i];
            if (!panel.visible) continue;

            if (panel.floating || panel.dock == DockPosition::FLOATING)
            {
                panel.floating = true;
                panel.dock = DockPosition::FLOATING;
                normalizePanelRect(panel, workspace);
                continue;
            }

            auto itWinner = winningPanels.find((int)panel.dock);
            if (itWinner == winningPanels.end() || itWinner->second != panel.id)
            {
                panel.floating = true;
                panel.dock = DockPosition::FLOATING;
                centerPanel(panel, workspace);
                continue;
            }

            panel.floating = false;
            panel.dragging = false;
            auto itRect = mergedRects.find(i);
            panel.rect = (itRect != mergedRects.end()) ? itRect->second : dockPreviewRect(panel.dock, workspace);
            if (panel.minimized)
                panel.rect.h = kHeaderH;
            updatePanelRects(panel);
        }

        layoutDirty = false;
        haveWorkspace = true;
        lastWorkspace = workspace;
    }

    void DockManager::ensureLayoutUpToDate(const SDL_Rect& workspace)
    {
        ensureDefaultPanels(workspace);
        if (!haveWorkspace || !rectEquals(lastWorkspace, workspace))
            layoutDirty = true;
        if (layoutDirty)
            applyDockLayout(workspace);
    }

    void DockManager::update(const SDL_Rect& workspace)
    {
        ensureLayoutUpToDate(workspace);
    }

    int DockManager::panelIndexAtPoint(int mx, int my, bool headerOnly) const
    {
        for (int i = (int)panels.size() - 1; i >= 0; --i)
        {
            const DockPanel& panel = panels[(size_t)i];
            if (!panel.visible) continue;
            const SDL_Rect& hit = headerOnly ? panel.headerRect : panel.rect;
            if (pointInRect(hit, mx, my)) return i;
        }
        return -1;
    }

    int DockManager::resizeEdgesAtPoint(const DockPanel& panel, int mx, int my) const
    {
        const InternalState& st = stateFor(panel);
        if (!panel.visible || !st.resizable) return ResizeNone;

        const int edge = 6;
        SDL_Rect expanded = panel.rect;
        expanded.x -= edge;
        expanded.y -= edge;
        expanded.w += edge * 2;
        expanded.h += edge * 2;
        if (!pointInRect(expanded, mx, my)) return ResizeNone;

        if (panel.floating)
        {
            int mask = ResizeNone;
            if (std::abs(mx - panel.rect.x) <= edge) mask |= ResizeLeft;
            if (std::abs(mx - (panel.rect.x + panel.rect.w)) <= edge) mask |= ResizeRight;
            if (std::abs(my - panel.rect.y) <= edge) mask |= ResizeTop;
            if (std::abs(my - (panel.rect.y + panel.rect.h)) <= edge) mask |= ResizeBottom;
            return mask;
        }

        if (!haveWorkspace) return ResizeNone;

        auto allowsLeftEdge = [&](DockPosition dock)
        {
            return dock == DockPosition::CENTER || dock == DockPosition::CENTER_RIGHT || dock == DockPosition::TOP || dock == DockPosition::TOP_RIGHT || dock == DockPosition::BOTTOM || dock == DockPosition::BOTTOM_RIGHT;
        };
        auto allowsRightEdge = [&](DockPosition dock)
        {
            return dock == DockPosition::CENTER || dock == DockPosition::CENTER_LEFT || dock == DockPosition::TOP || dock == DockPosition::TOP_LEFT || dock == DockPosition::BOTTOM || dock == DockPosition::BOTTOM_LEFT;
        };
        auto allowsTopEdge = [&](DockPosition dock)
        {
            return dock == DockPosition::CENTER || dock == DockPosition::CENTER_LEFT || dock == DockPosition::CENTER_RIGHT || dock == DockPosition::BOTTOM || dock == DockPosition::BOTTOM_LEFT || dock == DockPosition::BOTTOM_RIGHT;
        };
        auto allowsBottomEdge = [&](DockPosition dock)
        {
            return dock == DockPosition::CENTER || dock == DockPosition::CENTER_LEFT || dock == DockPosition::CENTER_RIGHT || dock == DockPosition::TOP || dock == DockPosition::TOP_LEFT || dock == DockPosition::TOP_RIGHT;
        };

        int mask = ResizeNone;
        if (allowsLeftEdge(panel.dock) && std::abs(mx - panel.rect.x) <= edge) mask |= ResizeLeft;
        if (allowsRightEdge(panel.dock) && std::abs(mx - (panel.rect.x + panel.rect.w)) <= edge) mask |= ResizeRight;
        if (allowsTopEdge(panel.dock) && std::abs(my - panel.rect.y) <= edge) mask |= ResizeTop;
        if (allowsBottomEdge(panel.dock) && std::abs(my - (panel.rect.y + panel.rect.h)) <= edge) mask |= ResizeBottom;

        return mask;
    }

    int DockManager::panelIndexAtResizeEdge(int mx, int my) const
    {
        for (int i = (int)panels.size() - 1; i >= 0; --i)
        {
            const DockPanel& panel = panels[(size_t)i];
            if (resizeEdgesAtPoint(panel, mx, my) != ResizeNone)
                return i;
        }
        return -1;
    }

    DockPosition DockManager::dockPreviewAtPoint(int mx, int my, const SDL_Rect& workspace) const
    {
        const DockPosition order[] = {
            DockPosition::TOP_LEFT,
            DockPosition::TOP,
            DockPosition::TOP_RIGHT,
            DockPosition::CENTER_LEFT,
            DockPosition::CENTER,
            DockPosition::CENTER_RIGHT,
            DockPosition::BOTTOM_LEFT,
            DockPosition::BOTTOM,
            DockPosition::BOTTOM_RIGHT
        };
        for (DockPosition dock : order)
        {
            if (pointInRect(dockPreviewRect(dock, workspace), mx, my))
                return dock;
        }
        return DockPosition::FLOATING;
    }

    bool DockManager::handleEvent(const SDL_Event& e, int mx, int my, const SDL_Rect& workspace, bool& outLayoutChanged)
    {
        outLayoutChanged = false;
        ensureLayoutUpToDate(workspace);

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            const int bodyIdx = panelIndexAtPoint(mx, my, false);
            if (bodyIdx >= 0)
            {
                bringToFront(bodyIdx);
                DockPanel& active = panels.back();
                focusedPanel = active.id;

                const bool hideButtonHovered = (active.id != "Canvas") && pointInRect(hideButtonRect(active), mx, my);
                const bool shrinkButtonHovered = pointInRect(shrinkButtonRect(active), mx, my);
                const bool headerHovered = pointInRect(active.headerRect, mx, my);

                if (hideButtonHovered)
                {
                    active.visible = false;
                    active.dragging = false;
                    activePanelIndex = -1;
                    activeResizeEdges = ResizeNone;
                    headerPressArmed = false;
                    headerPressPanelIndex = -1;
                    showDockPreview = false;
                    hoverDockPreview = DockPosition::FLOATING;
                    markLayoutDirty();
                    ensureLayoutUpToDate(workspace);
                    outLayoutChanged = true;
                    return true;
                }

                if (shrinkButtonHovered)
                {
                    InternalState& st = stateFor(active);
                    active.minimized = !active.minimized;
                    if (!active.minimized)
                        active.rect.h = std::max(panelMinHeight(active), st.lastExpandedH);
                    updatePanelRects(active);
                    activePanelIndex = -1;
                    activeResizeEdges = ResizeNone;
                    headerPressArmed = false;
                    headerPressPanelIndex = -1;
                    showDockPreview = false;
                    hoverDockPreview = DockPosition::FLOATING;
                    markLayoutDirty();
                    ensureLayoutUpToDate(workspace);
                    outLayoutChanged = true;
                    return true;
                }

                if (headerHovered)
                {
                    activePanelIndex = (int)panels.size() - 1;
                    activeResizeEdges = ResizeNone;
                    resizeStartRect = active.rect;
                    resizeStartMouseX = mx;
                    resizeStartMouseY = my;
                    active.dragOffsetX = mx - active.rect.x;
                    active.dragOffsetY = my - active.rect.y;
                    headerPressArmed = false;
                    headerPressPanelIndex = -1;
                    showDockPreview = false;
                    hoverDockPreview = DockPosition::FLOATING;

                    if (active.floating || active.dock == DockPosition::FLOATING)
                    {
                        active.dragging = true;
                        showDockPreview = true;
                        hoverDockPreview = dockPreviewAtPoint(mx, my, workspace);
                    }
                    else
                    {
                        active.dragging = false;
                        headerPressArmed = true;
                        headerPressPanelIndex = activePanelIndex;
                        headerPressMouseX = mx;
                        headerPressMouseY = my;
                    }
                    return true;
                }
            }

            const int resizeIdx = panelIndexAtResizeEdge(mx, my);
            if (resizeIdx >= 0)
            {
                bringToFront(resizeIdx);
                DockPanel& active = panels.back();
                focusedPanel = active.id;
                const int resizeMask = resizeEdgesAtPoint(active, mx, my);
                if (resizeMask != ResizeNone)
                {
                    activePanelIndex = (int)panels.size() - 1;
                    activeResizeEdges = resizeMask;
                    resizeStartRect = active.rect;
                    resizeStartMouseX = mx;
                    resizeStartMouseY = my;
                    headerPressArmed = false;
                    headerPressPanelIndex = -1;
                    showDockPreview = false;
                    hoverDockPreview = DockPosition::FLOATING;
                    return true;
                }
            }
        }

        if (e.type == SDL_MOUSEMOTION)
        {
            const int hoverIdx = panelIndexAtPoint(mx, my, false);
            hoveredPanel = (hoverIdx >= 0) ? panels[(size_t)hoverIdx].id : std::string();
            updateHoverCursor(mx, my);
            if (activePanelIndex >= 0 && activePanelIndex < (int)panels.size())
            {
                DockPanel& active = panels[(size_t)activePanelIndex];
                if (headerPressArmed && headerPressPanelIndex == activePanelIndex && !active.dragging)
                {
                    const int dx = mx - headerPressMouseX;
                    const int dy = my - headerPressMouseY;
                    if ((dx * dx + dy * dy) >= 36)
                    {
                        active.floating = true;
                        active.dock = DockPosition::FLOATING;
                        active.dragging = true;
                        headerPressArmed = false;
                        active.rect.x = mx - active.dragOffsetX;
                        active.rect.y = my - active.dragOffsetY;
                        normalizePanelRect(active, workspace);
                        hoverDockPreview = dockPreviewAtPoint(mx, my, workspace);
                        showDockPreview = true;
                        outLayoutChanged = true;
                        return true;
                    }
                }
                if (active.dragging)
                {
                    active.rect.x = mx - active.dragOffsetX;
                    active.rect.y = my - active.dragOffsetY;
                    normalizePanelRect(active, workspace);
                    hoverDockPreview = dockPreviewAtPoint(mx, my, workspace);
                    showDockPreview = true;
                    return true;
                }
                if (activeResizeEdges != ResizeNone)
                {
                    if (active.floating)
                    {
                        SDL_Rect rc = resizeStartRect;
                        const int dx = mx - resizeStartMouseX;
                        const int dy = my - resizeStartMouseY;
                        if (activeResizeEdges & ResizeLeft) { rc.x += dx; rc.w -= dx; }
                        if (activeResizeEdges & ResizeRight) { rc.w += dx; }
                        if (activeResizeEdges & ResizeTop) { rc.y += dy; rc.h -= dy; }
                        if (activeResizeEdges & ResizeBottom) { rc.h += dy; }
                        active.rect = rc;
                        normalizePanelRect(active, workspace);
                    }
                    else
                    {
                        const auto setSplitX1 = [&]()
                        {
                            splitX1 = (float)(std::clamp(mx, workspace.x, workspace.x + workspace.w) - workspace.x) / (float)std::max(1, workspace.w);
                        };
                        const auto setSplitX2 = [&]()
                        {
                            splitX2 = (float)(std::clamp(mx, workspace.x, workspace.x + workspace.w) - workspace.x) / (float)std::max(1, workspace.w);
                        };
                        const auto setSplitY1 = [&]()
                        {
                            splitY1 = (float)(std::clamp(my, workspace.y, workspace.y + workspace.h) - workspace.y) / (float)std::max(1, workspace.h);
                        };
                        const auto setSplitY2 = [&]()
                        {
                            splitY2 = (float)(std::clamp(my, workspace.y, workspace.y + workspace.h) - workspace.y) / (float)std::max(1, workspace.h);
                        };

                        auto handleVerticalEdge = [&](bool rightEdge)
                        {
                            switch (active.dock)
                            {
                            case DockPosition::CENTER_LEFT:
                            case DockPosition::TOP_LEFT:
                            case DockPosition::BOTTOM_LEFT:
                                if (rightEdge) setSplitX1();
                                break;
                            case DockPosition::CENTER:
                            case DockPosition::TOP:
                            case DockPosition::BOTTOM:
                                if (rightEdge) setSplitX2(); else setSplitX1();
                                break;
                            case DockPosition::CENTER_RIGHT:
                            case DockPosition::TOP_RIGHT:
                            case DockPosition::BOTTOM_RIGHT:
                                if (!rightEdge) setSplitX2();
                                break;
                            default:
                                break;
                            }
                        };

                        auto handleHorizontalEdge = [&](bool bottomEdge)
                        {
                            switch (active.dock)
                            {
                            case DockPosition::TOP_LEFT:
                            case DockPosition::TOP:
                            case DockPosition::TOP_RIGHT:
                                if (bottomEdge) setSplitY1();
                                break;
                            case DockPosition::CENTER_LEFT:
                            case DockPosition::CENTER:
                            case DockPosition::CENTER_RIGHT:
                                if (bottomEdge) setSplitY2(); else setSplitY1();
                                break;
                            case DockPosition::BOTTOM_LEFT:
                            case DockPosition::BOTTOM:
                            case DockPosition::BOTTOM_RIGHT:
                                if (!bottomEdge) setSplitY2();
                                break;
                            default:
                                break;
                            }
                        };

                        if (activeResizeEdges & ResizeLeft)
                            handleVerticalEdge(false);
                        if (activeResizeEdges & ResizeRight)
                            handleVerticalEdge(true);
                        if (activeResizeEdges & ResizeTop)
                            handleHorizontalEdge(false);
                        if (activeResizeEdges & ResizeBottom)
                            handleHorizontalEdge(true);

                        clampSplitRatios(workspace);
                        markLayoutDirty();
                        ensureLayoutUpToDate(workspace);
                    }
                    outLayoutChanged = true;
                    return true;
                }
            }
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        {
            bool consumed = false;
            if (headerPressArmed)
            {
                headerPressArmed = false;
                headerPressPanelIndex = -1;
                consumed = true;
            }
            if (activePanelIndex >= 0 && activePanelIndex < (int)panels.size())
            {
                DockPanel& active = panels[(size_t)activePanelIndex];
                if (active.dragging)
                {
                    active.dragging = false;
                    DockPosition preview = dockPreviewAtPoint(mx, my, workspace);
                    if (preview != DockPosition::FLOATING)
                    {
                        active.floating = false;
                        active.dock = preview;
                        makeDockZoneExclusive(active.id, preview);
                        if (preview == DockPosition::CENTER_LEFT || preview == DockPosition::TOP_LEFT || preview == DockPosition::BOTTOM_LEFT)
                            splitX1 = std::min(splitX1, 0.33f);
                        else if (preview == DockPosition::CENTER_RIGHT || preview == DockPosition::TOP_RIGHT || preview == DockPosition::BOTTOM_RIGHT)
                            splitX2 = std::max(splitX2, 0.67f);
                        if (preview == DockPosition::TOP_LEFT || preview == DockPosition::TOP || preview == DockPosition::TOP_RIGHT)
                            splitY1 = std::min(splitY1, 0.33f);
                        else if (preview == DockPosition::BOTTOM_LEFT || preview == DockPosition::BOTTOM || preview == DockPosition::BOTTOM_RIGHT)
                            splitY2 = std::max(splitY2, 0.67f);
                        clampSplitRatios(workspace);
                        markLayoutDirty();
                        ensureLayoutUpToDate(workspace);
                    }
                    else
                    {
                        active.floating = true;
                        active.dock = DockPosition::FLOATING;
                        normalizePanelRect(active, workspace);
                    }
                    consumed = true;
                    outLayoutChanged = true;
                }
            }
            if (activeResizeEdges != ResizeNone)
            {
                activeResizeEdges = ResizeNone;
                consumed = true;
                outLayoutChanged = true;
            }
            activePanelIndex = -1;
            headerPressPanelIndex = -1;
            showDockPreview = false;
            hoverDockPreview = DockPosition::FLOATING;
            return consumed;
        }

        return false;
    }

    void DockManager::drawBackgrounds(SDL_Renderer* r, const SDL_Rect& workspace) const
    {
        (void)workspace;
        if (!r) return;

        auto drawPanelBase = [&](const DockPanel& panel)
        {
            if (!panel.visible) return;
            strova::uix::drawPanelFrame(r, panel.rect, panel.floating, activePanelIndex >= 0 && panels[(size_t)activePanelIndex].id == panel.id);
        };

        for (const auto& panel : panels)
            if (panel.visible && !panel.floating)
                drawPanelBase(panel);
        for (const auto& panel : panels)
            if (panel.visible && panel.floating)
                drawPanelBase(panel);
    }

    void DockManager::drawHeaders(SDL_Renderer* r, TTF_Font* font, int mx, int my, const SDL_Rect& workspace) const
    {
        if (!r) return;

        auto drawPanelHeader = [&](const DockPanel& panel)
        {
            if (!panel.visible) return;

            const bool active = activePanelIndex >= 0 && panels[(size_t)activePanelIndex].id == panel.id;
            strova::uix::drawPanelHeader(r, font, panel.headerRect, panel.title, active);
            drawRect(r, panel.rect, active ? strova::theme::borderStrong : strova::theme::borderSoft);
            if (panel.contentRect.h > 0)
            {
                SDL_SetRenderDrawColor(r, strova::theme::borderMuted.r, strova::theme::borderMuted.g, strova::theme::borderMuted.b, 180);
                SDL_RenderDrawLine(r, panel.contentRect.x, panel.contentRect.y, panel.contentRect.x + panel.contentRect.w - 1, panel.contentRect.y);
            }
            drawHeaderButton(r, font, hideButtonRect(panel), "X", pointInRect(hideButtonRect(panel), mx, my), panel.id == "Canvas");
            drawHeaderButton(r, font, shrinkButtonRect(panel), panel.minimized ? "+" : "-", pointInRect(shrinkButtonRect(panel), mx, my), false);
        };

        for (const auto& panel : panels)
            if (panel.visible && !panel.floating)
                drawPanelHeader(panel);
        for (const auto& panel : panels)
            if (panel.visible && panel.floating)
                drawPanelHeader(panel);

        if (showDockPreview)
        {
            const DockPosition order[] = {
                DockPosition::TOP_LEFT,
                DockPosition::TOP,
                DockPosition::TOP_RIGHT,
                DockPosition::CENTER_LEFT,
                DockPosition::CENTER,
                DockPosition::CENTER_RIGHT,
                DockPosition::BOTTOM_LEFT,
                DockPosition::BOTTOM,
                DockPosition::BOTTOM_RIGHT
            };
            for (DockPosition dock : order)
            {
                SDL_Rect zone = dockPreviewRect(dock, workspace);
                fillRect(r, zone, dock == hoverDockPreview ? SDL_Color{ strova::theme::accent.r, strova::theme::accent.g, strova::theme::accent.b, 92 } : SDL_Color{ 60, 74, 104, 40 });
                drawRect(r, zone, dock == hoverDockPreview ? strova::theme::borderFocus : strova::theme::borderSoft);
            }
        }
    }

    void DockManager::draw(SDL_Renderer* r, TTF_Font* font, int mx, int my, const SDL_Rect& workspace) const
    {
        drawBackgrounds(r, workspace);
        drawHeaders(r, font, mx, my, workspace);
    }

    void DockManager::ensurePlaceholderPanel(const std::string& id, const std::string& title, const SDL_Rect& workspace, const std::string& ownerPluginId)
    {
        if (id.empty() || getPanel(id)) return;
        DockPanel panel{};
        panel.id = id;
        panel.title = title.empty() ? id : title;
        panel.dock = DockPosition::FLOATING;
        panel.floating = true;
        panel.visible = false;
        panel.pluginPanel = true;
        panel.unresolvedPlaceholder = true;
        panel.ownerPluginId = ownerPluginId;
        panel.statusText = "Saved panel is unavailable until its plugin is restored.";
        panel.rect = SDL_Rect{ workspace.x + workspace.w - 320, workspace.y + 72, 300, 220 };

        InternalState& st = panelState[panel.id];
        st.defaultRect = panel.rect;
        st.preferredW = std::max(kMinPanelW, panel.rect.w);
        st.preferredH = std::max(kMinPanelH, panel.rect.h);
        st.lastExpandedH = st.preferredH;

        updatePanelRects(panel);
        panels.push_back(panel);
    }

    void DockManager::syncPluginPanels(const strova::plugin::DockPanelRegistry& registry, const SDL_Rect& workspace)
    {
        ensureDefaultPanels(workspace);
        for (const auto& desc : registry.items())
        {
            DockPanel* panel = getPanel(desc.id);
            if (!panel)
            {
                DockPanel created{};
                created.id = desc.id;
                created.title = desc.title.empty() ? desc.id : desc.title;
                created.dock = dockFromString(desc.defaultDockZone, DockPosition::FLOATING);
                created.floating = (created.dock == DockPosition::FLOATING);
                created.visible = false;
                created.pluginPanel = true;
                created.unresolvedPlaceholder = false;
                created.ownerPluginId = desc.ownerPluginId;
                created.rect = created.floating ? SDL_Rect{ workspace.x + workspace.w - std::max(220, desc.preferredWidth), workspace.y + 64, std::max(220, desc.preferredWidth), std::max(160, desc.preferredHeight) } : dockPreviewRect(created.dock, workspace);

                InternalState& st = panelState[created.id];
                st.defaultRect = created.rect;
                st.preferredW = std::max(desc.minWidth, desc.preferredWidth);
                st.preferredH = std::max(desc.minHeight, desc.preferredHeight);
                st.lastExpandedH = st.preferredH;

                updatePanelRects(created);
                panels.push_back(created);
                panel = &panels.back();
            }
            if (panel)
            {
                panel->pluginPanel = true;
                panel->ownerPluginId = desc.ownerPluginId;
                panel->title = desc.title.empty() ? panel->title : desc.title;
                if (panel->unresolvedPlaceholder)
                    panel->statusText = "Plugin panel restored as placeholder. Replace or reload its plugin to reactivate it.";
            }
        }
    }

    void DockManager::dockPanel(const std::string& id, DockPosition position)
    {
        DockPanel* panel = getPanel(id);
        if (!panel) return;
        panel->visible = true;
        panel->floating = (position == DockPosition::FLOATING);
        panel->dock = position;
        if (!panel->floating)
        {
            makeDockZoneExclusive(id, position);
            if (position == DockPosition::CENTER_LEFT || position == DockPosition::TOP_LEFT || position == DockPosition::BOTTOM_LEFT)
                splitX1 = std::min(splitX1, 0.33f);
            else if (position == DockPosition::CENTER_RIGHT || position == DockPosition::TOP_RIGHT || position == DockPosition::BOTTOM_RIGHT)
                splitX2 = std::max(splitX2, 0.67f);
            if (position == DockPosition::TOP_LEFT || position == DockPosition::TOP || position == DockPosition::TOP_RIGHT)
                splitY1 = std::min(splitY1, 0.33f);
            else if (position == DockPosition::BOTTOM_LEFT || position == DockPosition::BOTTOM || position == DockPosition::BOTTOM_RIGHT)
                splitY2 = std::max(splitY2, 0.67f);
        }
        markLayoutDirty();
    }

    void DockManager::restorePanel(const std::string& id, const SDL_Rect& workspace)
    {
        DockPanel* panel = getPanel(id);
        if (!panel) return;
        panel->visible = true;
        if (panel->floating || panel->dock == DockPosition::FLOATING)
        {
            panel->floating = true;
            panel->dock = DockPosition::FLOATING;
            centerPanel(*panel, workspace);
        }
        else
        {
            if (panel->dock == DockPosition::CENTER_LEFT || panel->dock == DockPosition::TOP_LEFT || panel->dock == DockPosition::BOTTOM_LEFT)
                splitX1 = std::min(splitX1, 0.33f);
            else if (panel->dock == DockPosition::CENTER_RIGHT || panel->dock == DockPosition::TOP_RIGHT || panel->dock == DockPosition::BOTTOM_RIGHT)
                splitX2 = std::max(splitX2, 0.67f);
            if (panel->dock == DockPosition::TOP_LEFT || panel->dock == DockPosition::TOP || panel->dock == DockPosition::TOP_RIGHT)
                splitY1 = std::min(splitY1, 0.33f);
            else if (panel->dock == DockPosition::BOTTOM_LEFT || panel->dock == DockPosition::BOTTOM || panel->dock == DockPosition::BOTTOM_RIGHT)
                splitY2 = std::max(splitY2, 0.67f);
            clampSplitRatios(workspace);
            markLayoutDirty();
            ensureLayoutUpToDate(workspace);
        }
    }

    bool DockManager::isAnyDragging() const
    {
        for (const auto& panel : panels)
            if (panel.dragging)
                return true;
        return false;
    }

    void DockManager::updateHoverCursor(int mx, int my) const
    {
        if (activeResizeEdges != ResizeNone)
        {
            setCursorForResizeMask(activeResizeEdges);
            return;
        }
        const int idx = panelIndexAtResizeEdge(mx, my);
        if (idx >= 0)
        {
            const int mask = resizeEdgesAtPoint(panels[(size_t)idx], mx, my);
            if (mask != ResizeNone)
            {
                setCursorForResizeMask(mask);
                return;
            }
        }
        if (SDL_Cursor* cursor = cachedCursor(SDL_SYSTEM_CURSOR_ARROW))
            SDL_SetCursor(cursor);
    }

    bool DockManager::wantsCaptureMouse() const
    {
        return isAnyDragging() || activeResizeEdges != ResizeNone || headerPressArmed;
    }

    bool DockManager::hitVisiblePanel(int mx, int my) const
    {
        return panelIndexAtPoint(mx, my, false) >= 0;
    }

    bool DockManager::hitVisibleNonCanvasPanel(int mx, int my) const
    {
        const int idx = panelIndexAtPoint(mx, my, false);
        if (idx < 0) return false;
        const DockPanel& panel = panels[(size_t)idx];
        return panel.id != "Canvas";
    }

    bool DockManager::saveLayout(const std::filesystem::path& path) const
    {
        std::ostringstream out;
        out << "{\n"
            << "  \"version\":3,\n"
            << "  \"focusedPanelId\":\"" << strova::iojson::jsonEscape(focusedPanel) << "\",\n"
            << "  \"splitX1\":" << splitX1 << ",\n"
            << "  \"splitX2\":" << splitX2 << ",\n"
            << "  \"splitY1\":" << splitY1 << ",\n"
            << "  \"splitY2\":" << splitY2 << ",\n"
            << "  \"panels\":[\n";
        for (std::size_t i = 0; i < panels.size(); ++i)
        {
            const DockPanel& p = panels[i];
            out << "    {\"id\":\"" << strova::iojson::jsonEscape(p.id)
                << "\",\"dock\":\"" << dockToString(p.dock)
                << "\",\"floating\":" << (p.floating ? "true" : "false")
                << ",\"visible\":" << (p.visible ? "true" : "false")
                << ",\"minimized\":" << (p.minimized ? "true" : "false")
                << ",\"rect\":[" << p.rect.x << "," << p.rect.y << "," << p.rect.w << "," << p.rect.h << "]}"
                << (i + 1 < panels.size() ? "," : "") << "\n";
        }
        out << "  ]\n}\n";
        return strova::iojson::writeTextFileAtomic(path, out.str());
    }

    bool DockManager::loadLayout(const std::filesystem::path& path, const SDL_Rect& workspace)
    {
        ensureDefaultPanels(workspace);

        std::string text;
        if (!strova::iojson::readTextFile(path, text))
        {
            markLayoutDirty();
            ensureLayoutUpToDate(workspace);
            return false;
        }

        PersistedDockLayout layout;
        (void)parseFloatField(text, "splitX1", layout.splitX1);
        (void)parseFloatField(text, "splitX2", layout.splitX2);
        (void)parseFloatField(text, "splitY1", layout.splitY1);
        (void)parseFloatField(text, "splitY2", layout.splitY2);
        (void)parseStringField(text, "focusedPanelId", layout.focusedPanelId);

        const std::vector<std::string> objs = splitArrayObjects(text, "panels");
        for (const std::string& obj : objs)
        {
            PersistedDockPanelState p;
            if (!parseStringField(obj, "id", p.id)) continue;
            std::string persistedTitle;
            std::string persistedOwner;
            bool persistedPluginPanel = false;
            bool persistedPlaceholder = false;
            (void)parseStringField(obj, "title", persistedTitle);
            (void)parseStringField(obj, "owner_plugin_id", persistedOwner);
            (void)parseBoolField(obj, "plugin_panel", persistedPluginPanel);
            (void)parseBoolField(obj, "unresolved_placeholder", persistedPlaceholder);
            std::string dockStr;
            if (parseStringField(obj, "dock", dockStr)) p.dock = dockFromString(dockStr, DockPosition::FLOATING);
            else p.dock = DockPosition::FLOATING;
            (void)parseBoolField(obj, "floating", p.floating);
            (void)parseBoolField(obj, "visible", p.visible);
            (void)parseBoolField(obj, "minimized", p.minimized);
            (void)parseRectField(obj, "rect", p.rect);
            p.title = persistedTitle;
            p.ownerPluginId = persistedOwner;
            p.pluginPanel = persistedPluginPanel || isPluginPanelId(p.id);
            p.unresolvedPlaceholder = persistedPlaceholder;
            layout.panels.push_back(p);
        }

        std::vector<PersistedDockPanelState> defaults;
        defaults.reserve(panels.size());
        for (const DockPanel& panel : panels)
        {
            PersistedDockPanelState p;
            p.id = panel.id;
            p.title = panel.title;
            p.ownerPluginId = panel.ownerPluginId;
            p.pluginPanel = panel.pluginPanel;
            p.unresolvedPlaceholder = panel.unresolvedPlaceholder;
            p.dock = panel.dock;
            p.floating = panel.floating;
            p.visible = panel.visible;
            p.minimized = panel.minimized;
            p.rect = panel.rect;
            defaults.push_back(p);
        }

        dock_layout_state::sanitizeSplits(layout);
        dock_layout_state::sanitizePanels(layout, defaults);

        splitX1 = layout.splitX1;
        splitX2 = layout.splitX2;
        splitY1 = layout.splitY1;
        splitY2 = layout.splitY2;
        clampSplitRatios(workspace);

        bool anyApplied = false;
        for (const PersistedDockPanelState& persisted : layout.panels)
        {
            DockPanel* panel = getPanel(persisted.id);
            if (!panel && (persisted.pluginPanel || persisted.unresolvedPlaceholder || isPluginPanelId(persisted.id)))
            {
                ensurePlaceholderPanel(persisted.id, persisted.title.empty() ? persisted.id : persisted.title, workspace, persisted.ownerPluginId);
                panel = getPanel(persisted.id);
            }
            if (!panel) continue;
            panel->dock = persisted.dock;
            panel->floating = persisted.floating || persisted.dock == DockPosition::FLOATING;
            panel->visible = persisted.visible;
            panel->minimized = persisted.minimized;
            panel->rect = persisted.rect;
            if (!persisted.title.empty()) panel->title = persisted.title;
            if (!persisted.ownerPluginId.empty()) panel->ownerPluginId = persisted.ownerPluginId;
            panel->pluginPanel = panel->pluginPanel || persisted.pluginPanel || isPluginPanelId(panel->id);
            panel->unresolvedPlaceholder = panel->unresolvedPlaceholder || persisted.unresolvedPlaceholder;
            if (panel->unresolvedPlaceholder && panel->statusText.empty())
                panel->statusText = "Saved panel is unavailable until its plugin is restored.";

            InternalState& st = stateFor(*panel);
            st.preferredW = std::max(panelMinWidth(*panel), panel->rect.w);
            st.preferredH = std::max(panelMinHeight(*panel), std::max(panel->rect.h, kMinPanelH));
            st.lastExpandedH = std::max(panelMinHeight(*panel), std::max(panel->rect.h, kMinPanelH));
            anyApplied = true;
        }

        focusedPanel = layout.focusedPanelId;
        if (focusedPanel.empty() && getPanel("Canvas")) focusedPanel = "Canvas";
        hoveredPanel.clear();
        clearTransientInteractionState();
        markLayoutDirty();
        ensureLayoutUpToDate(workspace);
        return anyApplied;
    }

    void DockManager::clearTransientInteractionState()
    {
        activeResizeEdges = ResizeNone;
        activePanelIndex = -1;
        showDockPreview = false;
        hoverDockPreview = DockPosition::FLOATING;
        headerPressArmed = false;
        headerPressPanelIndex = -1;
        hoveredPanel.clear();
        for (auto& panel : panels) panel.dragging = false;
    }
}
