/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/DockManager.h
   Module:      Ui
   Purpose:     Dockable panel model, layout, persistence, and tool windows.

   Notes:
   - Windowless docking system rendered entirely inside the SDL editor window.
   - Keeps panel ids stable for save/load compatibility.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <array>
#include "../core/Theme.h"
#include "DockLayoutState.h"
#include "../plugin/PluginRegistry.h"

namespace strova
{

    struct DockPanel
    {
        std::string id;
        std::string title;

        SDL_Rect rect{};

        SDL_Rect headerRect{};
        SDL_Rect contentRect{};

        DockPosition dock = DockPosition::FLOATING;

        bool visible = true;
        bool floating = false;
        bool minimized = false;

        bool dragging = false;
        bool pluginPanel = false;
        bool unresolvedPlaceholder = false;
        std::string ownerPluginId;
        std::string statusText;

        int dragOffsetX = 0;
        int dragOffsetY = 0;
    };

    class DockManager
    {
    public:
        static constexpr int kHeaderH = strova::theme::headerHeight;
        static constexpr int kMinPanelW = 160;
        static constexpr int kMinPanelH = 120;

        std::vector<DockPanel> panels;

        void ensureDefaultPanels(const SDL_Rect& workspace);
        void update(const SDL_Rect& workspace);
        bool handleEvent(const SDL_Event& e, int mx, int my, const SDL_Rect& workspace, bool& outLayoutChanged);
        void drawBackgrounds(SDL_Renderer* r, const SDL_Rect& workspace) const;
        void drawHeaders(SDL_Renderer* r, TTF_Font* font, int mx, int my, const SDL_Rect& workspace) const;
        void draw(SDL_Renderer* r, TTF_Font* font, int mx, int my, const SDL_Rect& workspace) const;

        void dockPanel(const std::string& id, DockPosition position);
        DockPanel* getPanel(const std::string& id);
        const DockPanel* getPanel(const std::string& id) const;
        SDL_Rect contentRect(const DockPanel& panel) const;
        SDL_Rect contentRect(const std::string& id) const;
        void restorePanel(const std::string& id, const SDL_Rect& workspace);
        void syncPluginPanels(const strova::plugin::DockPanelRegistry& registry, const SDL_Rect& workspace);
        void ensurePlaceholderPanel(const std::string& id, const std::string& title, const SDL_Rect& workspace, const std::string& ownerPluginId = std::string());
        bool isAnyDragging() const;
        bool wantsCaptureMouse() const;
        bool hitVisiblePanel(int mx, int my) const;
        bool hitVisibleNonCanvasPanel(int mx, int my) const;
        void updateHoverCursor(int mx, int my) const;
        const std::string& focusedPanelId() const { return focusedPanel; }
        const std::string& hoveredPanelId() const { return hoveredPanel; }
        void clearTransientInteractionState();

        bool saveLayout(const std::filesystem::path& path) const;
        bool loadLayout(const std::filesystem::path& path, const SDL_Rect& workspace);

    private:
        struct InternalState
        {
            SDL_Rect defaultRect{};
            int preferredW = 280;
            int preferredH = 220;
            int lastExpandedH = 220;
            bool resizable = true;
        };

        enum ResizeEdge
        {
            ResizeNone = 0,
            ResizeLeft = 1,
            ResizeRight = 2,
            ResizeTop = 4,
            ResizeBottom = 8
        };

        std::unordered_map<std::string, InternalState> panelState;

        int activeResizeEdges = ResizeNone;
        int activePanelIndex = -1;
        SDL_Rect resizeStartRect{};
        int resizeStartMouseX = 0;
        int resizeStartMouseY = 0;
        DockPosition hoverDockPreview = DockPosition::FLOATING;
        bool showDockPreview = false;

        bool layoutDirty = true;
        bool haveWorkspace = false;
        SDL_Rect lastWorkspace{};

        bool headerPressArmed = false;
        int headerPressPanelIndex = -1;
        int headerPressMouseX = 0;
        int headerPressMouseY = 0;

        std::string focusedPanel;
        std::string hoveredPanel;

        float splitX1 = 0.26f;
        float splitX2 = 0.74f;
        float splitY1 = 0.22f;
        float splitY2 = 0.78f;

        void applyDockLayout(const SDL_Rect& workspace);
        void updatePanelRects(DockPanel& panel);
        void normalizePanelRect(DockPanel& panel, const SDL_Rect& workspace);
        SDL_Rect dockPreviewRect(DockPosition dock, const SDL_Rect& workspace) const;
        void bringToFront(int index);
        int panelIndexAtPoint(int mx, int my, bool headerOnly) const;
        int panelIndexAtResizeEdge(int mx, int my) const;
        int resizeEdgesAtPoint(const DockPanel& panel, int mx, int my) const;
        SDL_Rect panelHeaderRect(const DockPanel& panel) const;
        SDL_Rect hideButtonRect(const DockPanel& panel) const;
        SDL_Rect shrinkButtonRect(const DockPanel& panel) const;
        DockPosition dockPreviewAtPoint(int mx, int my, const SDL_Rect& workspace) const;
        void centerPanel(DockPanel& panel, const SDL_Rect& workspace);
        void markLayoutDirty();
        void clampSplitRatios(const SDL_Rect& workspace);
        void ensureLayoutUpToDate(const SDL_Rect& workspace);
        void makeDockZoneExclusive(const std::string& winnerId, DockPosition dock);
        InternalState& stateFor(DockPanel& panel);
        const InternalState& stateFor(const DockPanel& panel) const;
        static const char* dockToString(DockPosition dock);
        static DockPosition dockFromString(const std::string& text, DockPosition fallback);
    };
}
