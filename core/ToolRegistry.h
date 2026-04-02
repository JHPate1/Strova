#pragma once
#include "Tool.h"
#include <string>
#include <vector>

struct ToolDescriptor
{
    ToolType type = ToolType::Brush;
    const char* stableId = "strova.tool.brush";
    const char* displayName = "Brush";
    const char* tooltip = "Brush";
    const char* toolbarGlyph = "B";
    int order = 0;
    bool builtIn = true;
};

namespace strova::tools
{
    const std::vector<ToolDescriptor>& descriptors();
    const ToolDescriptor* findDescriptor(ToolType type);
    const ToolDescriptor& descriptorOrFallback(ToolType type);
    std::vector<ToolType> orderedToolTypes();
    int toolCount();
    const char* displayName(ToolType type);
    const char* tooltip(ToolType type);
    const char* toolbarGlyph(ToolType type);
}
