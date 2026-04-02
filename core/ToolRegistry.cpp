#include "ToolRegistry.h"
#include <algorithm>

namespace
{
    std::vector<ToolDescriptor> buildBuiltInToolDescriptors()
    {
        std::vector<ToolDescriptor> out{
            { ToolType::Brush,       "strova.tool.brush",        "Brush",       "Brush - smooth general drawing",                  "B",  0, true },
            { ToolType::Pencil,      "strova.tool.pencil",       "Pencil",      "Pencil - light textured sketching",               "P",  1, true },
            { ToolType::Pen,         "strova.tool.pen",          "Pen",         "Pen - clean pressure line",                       "N",  2, true },
            { ToolType::Marker,      "strova.tool.marker",       "Marker",      "Marker - broad felt coverage",                    "M",  3, true },
            { ToolType::Airbrush,    "strova.tool.airbrush",     "Airbrush",    "Airbrush - soft sprayed buildup",                 "A",  4, true },
            { ToolType::Calligraphy, "strova.tool.calligraphy",  "Calligraphy", "Calligraphy - angled nib stroke",                "C",  5, true },
            { ToolType::Eraser,      "strova.tool.eraser",       "Eraser",      "Eraser - hard erase",                            "E",  6, true },
            { ToolType::SoftEraser,  "strova.tool.soft_eraser",  "Soft Eraser", "Soft Eraser - fade edges gently",                "SE", 7, true },
            { ToolType::Smudge,      "strova.tool.smudge",       "Smudge",      "Smudge - drag nearby color",                     "S",  8, true },
            { ToolType::Blur,        "strova.tool.blur",         "Blur",        "Blur - soften painted detail",                   "BL", 9, true },
            { ToolType::Glow,        "strova.tool.glow",         "Glow",        "Glow - soft halo with bright center",            "GL",10, true },
            { ToolType::Fill,        "strova.tool.fill",         "Fill",        "Fill - bucket fill closed regions",              "F", 11, true },
            { ToolType::Line,        "strova.tool.line",         "Line",        "Line - two-point line tool",                     "/", 12, true },
            { ToolType::Ruler,       "strova.tool.ruler",        "Ruler",       "Ruler - drag, rotate, then snap strokes",        "R", 13, true },
            { ToolType::Rect,        "strova.tool.rect",         "Rectangle",   "Rectangle - two-point rectangle",                "[]",14, true },
            { ToolType::Ellipse,     "strova.tool.ellipse",      "Ellipse",     "Ellipse - two-point ellipse",                    "()",15, true },
            { ToolType::Select,      "strova.tool.select",       "Select",      "Select - pick and move strokes",                 "SEL",16, true },
            { ToolType::Eyedropper,  "strova.tool.eyedropper",   "Eyedropper",  "Eyedropper - sample canvas color",              "I", 17, true },
        };

        std::sort(out.begin(), out.end(), [](const ToolDescriptor& a, const ToolDescriptor& b)
        {
            if (a.order != b.order) return a.order < b.order;
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        });
        return out;
    }
}

namespace strova::tools
{
    const std::vector<ToolDescriptor>& descriptors()
    {
        static const std::vector<ToolDescriptor> kDescriptors = buildBuiltInToolDescriptors();
        return kDescriptors;
    }

    const ToolDescriptor* findDescriptor(ToolType type)
    {
        const auto& list = descriptors();
        auto it = std::find_if(list.begin(), list.end(), [type](const ToolDescriptor& d)
        {
            return d.type == type;
        });
        return (it != list.end()) ? &(*it) : nullptr;
    }

    const ToolDescriptor& descriptorOrFallback(ToolType type)
    {
        static const ToolDescriptor kFallback{};
        const ToolDescriptor* found = findDescriptor(type);
        return found ? *found : kFallback;
    }

    std::vector<ToolType> orderedToolTypes()
    {
        std::vector<ToolType> out;
        const auto& list = descriptors();
        out.reserve(list.size());
        for (const ToolDescriptor& d : list)
            out.push_back(d.type);
        return out;
    }

    int toolCount()
    {
        return static_cast<int>(descriptors().size());
    }

    const char* displayName(ToolType type)
    {
        return descriptorOrFallback(type).displayName;
    }

    const char* tooltip(ToolType type)
    {
        return descriptorOrFallback(type).tooltip;
    }

    const char* toolbarGlyph(ToolType type)
    {
        return descriptorOrFallback(type).toolbarGlyph;
    }
}
