#pragma once

#include "PluginABI.h"

#include <cstdint>
#include <string>

namespace strova::plugin
{
    constexpr std::uint32_t kAbiVersion = STROVA_PLUGIN_ABI_VERSION;
    constexpr std::uint32_t kHostApiVersion = STROVA_PLUGIN_HOST_API_VERSION;

    enum class Capability : std::uint64_t
    {
        Commands = STROVA_PLUGIN_CAP_COMMANDS,
        DockPanels = STROVA_PLUGIN_CAP_DOCK_PANELS,
        Importers = STROVA_PLUGIN_CAP_IMPORTERS,
        Exporters = STROVA_PLUGIN_CAP_EXPORTERS,
        ProjectContent = STROVA_PLUGIN_CAP_PROJECT_CONTENT,
        FlowProcessors = STROVA_PLUGIN_CAP_FLOW_PROCESSORS,
        FlowLinkProcessors = STROVA_PLUGIN_CAP_FLOWLINK_PROCESSORS,
        CanvasOverlays = STROVA_PLUGIN_CAP_CANVAS_OVERLAYS,
        BrushEffects = STROVA_PLUGIN_CAP_BRUSH_EFFECTS,
        StrokeEffects = STROVA_PLUGIN_CAP_STROKE_EFFECTS,
        AnalysisTools = STROVA_PLUGIN_CAP_ANALYSIS_TOOLS,
        LayerTools = STROVA_PLUGIN_CAP_LAYER_TOOLS,
        ExportPasses = STROVA_PLUGIN_CAP_EXPORT_PASSES,
        DocumentValidators = STROVA_PLUGIN_CAP_DOCUMENT_VALIDATORS
    };

    enum class Permission : std::uint64_t
    {
        ProjectRead = STROVA_PLUGIN_PERM_PROJECT_READ,
        ProjectWrite = STROVA_PLUGIN_PERM_PROJECT_WRITE,
        PluginStorage = STROVA_PLUGIN_PERM_PLUGIN_STORAGE,
        BulkDeleteProjectFiles = STROVA_PLUGIN_PERM_BULK_DELETE_PROJECT_FILES,
        OutsideProjectFs = STROVA_PLUGIN_PERM_OUTSIDE_PROJECT_FS
    };

    inline constexpr std::uint64_t capabilityBit(Capability cap)
    {
        return static_cast<std::uint64_t>(cap);
    }

    inline constexpr std::uint64_t permissionBit(Permission perm)
    {
        return static_cast<std::uint64_t>(perm);
    }

    inline bool hasCapability(std::uint64_t mask, Capability cap)
    {
        return (mask & capabilityBit(cap)) != 0;
    }

    inline bool hasPermission(std::uint64_t mask, Permission perm)
    {
        return (mask & permissionBit(perm)) != 0;
    }

    const char* capabilityName(Capability cap);
    const char* permissionName(Permission perm);
    std::uint64_t capabilityMaskFromString(const std::string& value);
    std::uint64_t permissionMaskFromString(const std::string& value);
}
