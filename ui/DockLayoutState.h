#pragma once

#include <SDL.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace strova {

enum class DockPosition
{
    CENTER,
    CENTER_LEFT,
    CENTER_RIGHT,
    TOP_LEFT,
    TOP,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM,
    BOTTOM_RIGHT,
    FLOATING
};

struct PersistedDockPanelState
{
    std::string id;
    std::string title;
    std::string ownerPluginId;
    DockPosition dock = DockPosition::FLOATING;
    bool floating = false;
    bool visible = true;
    bool minimized = false;
    bool pluginPanel = false;
    bool unresolvedPlaceholder = false;
    SDL_Rect rect{0,0,0,0};
};

struct PersistedDockLayout
{
    int version = 3;
    float splitX1 = 0.26f;
    float splitX2 = 0.74f;
    float splitY1 = 0.22f;
    float splitY2 = 0.78f;
    std::string focusedPanelId;
    std::vector<PersistedDockPanelState> panels;
};

namespace dock_layout_state {

inline bool isUniqueDock(DockPosition dock)
{
    return dock != DockPosition::FLOATING;
}

inline void sanitizeSplits(PersistedDockLayout& layout)
{
    layout.splitX1 = std::clamp(layout.splitX1, 0.10f, 0.60f);
    layout.splitX2 = std::clamp(layout.splitX2, 0.40f, 0.90f);
    layout.splitY1 = std::clamp(layout.splitY1, 0.10f, 0.60f);
    layout.splitY2 = std::clamp(layout.splitY2, 0.40f, 0.90f);
    if (layout.splitX2 - layout.splitX1 < 0.12f)
    {
        const float mid = (layout.splitX1 + layout.splitX2) * 0.5f;
        layout.splitX1 = std::clamp(mid - 0.06f, 0.10f, 0.84f);
        layout.splitX2 = std::clamp(mid + 0.06f, 0.16f, 0.90f);
    }
    if (layout.splitY2 - layout.splitY1 < 0.12f)
    {
        const float mid = (layout.splitY1 + layout.splitY2) * 0.5f;
        layout.splitY1 = std::clamp(mid - 0.06f, 0.10f, 0.84f);
        layout.splitY2 = std::clamp(mid + 0.06f, 0.16f, 0.90f);
    }
}

inline void sanitizePanels(PersistedDockLayout& layout, const std::vector<PersistedDockPanelState>& defaults)
{
    std::unordered_set<std::string> seenIds;
    std::unordered_set<int> usedUniqueDocks;
    std::vector<PersistedDockPanelState> sanitized;
    sanitized.reserve(layout.panels.size());

    for (const auto& panel : layout.panels)
    {
        if (panel.id.empty() || seenIds.count(panel.id)) continue;
        bool known = false;
        for (const auto& def : defaults) if (def.id == panel.id) { known = true; break; }
        if (!known && !(panel.pluginPanel || panel.unresolvedPlaceholder)) continue;

        PersistedDockPanelState s = panel;
        if (s.id == "Canvas")
        {
            s.visible = true;
            s.minimized = false;
            s.floating = false;
            s.dock = DockPosition::CENTER;
        }
        if (isUniqueDock(s.dock))
        {
            const int key = static_cast<int>(s.dock);
            if (usedUniqueDocks.count(key))
            {
                s.floating = true;
                s.dock = DockPosition::FLOATING;
            }
            else
            {
                usedUniqueDocks.insert(key);
            }
        }
        seenIds.insert(s.id);
        sanitized.push_back(s);
    }

    for (const auto& def : defaults)
    {
        if (seenIds.count(def.id)) continue;
        PersistedDockPanelState s;
        s.id = def.id;
        s.dock = def.dock;
        s.floating = def.floating || def.dock == DockPosition::FLOATING;
        s.visible = def.visible;
        s.minimized = def.minimized;
        s.title = def.title;
        s.ownerPluginId = def.ownerPluginId;
        s.pluginPanel = def.pluginPanel;
        s.unresolvedPlaceholder = def.unresolvedPlaceholder;
        s.rect = def.rect;
        sanitized.push_back(s);
    }

    layout.panels = std::move(sanitized);
    bool focusOk = false;
    for (const auto& p : layout.panels) if (p.id == layout.focusedPanelId && p.visible) { focusOk = true; break; }
    if (!focusOk) layout.focusedPanelId.clear();
}

} // namespace dock_layout_state
} // namespace strova
