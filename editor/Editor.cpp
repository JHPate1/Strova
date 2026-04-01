/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        editor/Editor.cpp
   Module:      Editor
   Purpose:     Editor behavior, canvas interaction, and tool routing.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "../app/App.h"
#include "../core/StrovaLimits.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <limits>
#include <memory>
#include <iterator>
#include <cstdlib>
#include <SDL_image.h>
#include "../editor/Editor.h"
#include "../ui/ToolOptionsPanel.h"
#include "../ui/TextRenderer.h"
#include "../ui/UiPrimitives.h"
#include "../core/Theme.h"
#include "../core/ToolRegistry.h"
#include "../core/Stroke.h"
#include "../core/Tool.h"
#include "../core/Exporter.h"
#include "../platform/AppPaths.h"
#include "../platform/FileDialog.h"
#include "../core/LayerRenderUtil.h"
#include "../core/BrushSystem.h"
#include "../render/BrushRenderer.h"
#include "../brush/BrushResourceManager.h"

namespace
{
    static bool sdlSupportsFloatLines = false;
    static bool sdlVersionChecked = false;

    static double perfMs(Uint64 beginCounter, Uint64 endCounter)
    {
        const Uint64 freq = SDL_GetPerformanceFrequency();
        if (freq == 0 || endCounter < beginCounter)
            return 0.0;
        return (double)(endCounter - beginCounter) * 1000.0 / (double)freq;
    }

    static void checkSDLVersion()
    {
        if (!sdlVersionChecked)
        {
            SDL_version compiled, linked;
            SDL_VERSION(&compiled);
            SDL_GetVersion(&linked);
            sdlSupportsFloatLines = (linked.major > 2) ||
                (linked.major == 2 && linked.minor > 0) ||
                (linked.major == 2 && linked.minor == 0 && linked.patch >= 10);
            sdlVersionChecked = true;
        }
    }

    static int toolGridColsForWidth(int w);
    static bool pointInRect(int x, int y, const SDL_Rect& r);
    static int calcRightPanelContentHeight(ToolType activeTool);
    static std::string transformClipLabel(int engineTrackId);
    static bool g_transformAutoKey = false;
    static bool g_keyframePanelFocused = false;
    static std::unordered_map<std::string, SDL_Texture*> g_persistentFrameImageCache;
    static std::unordered_map<std::string, std::uint64_t> g_persistentFrameImageCacheLastUse;
    static BrushResourceManager g_brushResourceManager;

    static ToolOptionsPanel& sharedToolOptionsPanel()
    {
        static ToolOptionsPanel panel;
        return panel;
    }

    struct CanvasProxyState
    {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
        float factor = 1.0f;
        std::uint64_t lastUseFrame = 0;
    };

    static CanvasProxyState g_canvasProxy;
    static int g_canvasProxyBoostFrames = 0;

    static void destroyCanvasProxy()
    {
        if (g_canvasProxy.texture)
        {
            SDL_DestroyTexture(g_canvasProxy.texture);
            g_canvasProxy.texture = nullptr;
        }
        g_canvasProxy.width = 0;
        g_canvasProxy.height = 0;
        g_canvasProxy.factor = 1.0f;
        g_canvasProxy.lastUseFrame = 0;
    }

    static float computeCanvasProxyFactor(const SDL_Rect& canvas, float canvasScale)
    {
        const int area = std::max(1, canvas.w) * std::max(1, canvas.h);
        float factor = 0.66f;
        if (area >= 1600 * 900) factor = 0.50f;
        else if (area >= 1200 * 700) factor = 0.58f;
        if (canvasScale >= 2.0f) factor = std::min(factor, 0.50f);
        if (canvasScale >= 4.0f) factor = std::min(factor, 0.40f);
        return std::clamp(factor, 0.33f, 0.75f);
    }

    static bool ensureCanvasProxyTexture(SDL_Renderer* r, const SDL_Rect& canvas, float factor)
    {
        const int targetW = std::max(64, (int)std::lround((double)std::max(1, canvas.w) * (double)factor));
        const int targetH = std::max(64, (int)std::lround((double)std::max(1, canvas.h) * (double)factor));

        if (g_canvasProxy.texture && g_canvasProxy.width == targetW && g_canvasProxy.height == targetH)
        {
            g_canvasProxy.factor = factor;
            return true;
        }

        destroyCanvasProxy();
        g_canvasProxy.texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, targetW, targetH);
        if (!g_canvasProxy.texture)
            return false;

        g_canvasProxy.width = targetW;
        g_canvasProxy.height = targetH;
        g_canvasProxy.factor = factor;
        SDL_SetTextureBlendMode(g_canvasProxy.texture, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(g_canvasProxy.texture, SDL_ScaleModeLinear);
#endif
        return true;
    }

    static std::string transformClipLabel(int engineTrackId)
    {
        return std::string("[TR]") + std::to_string(engineTrackId);
    }
    static float fclamp(float v, float a, float b) { return std::max(a, std::min(b, v)); }
    static int clampi(int v, int a, int b);
    static strova::LayerTree& activeLayerTree(App& app)
    {
        return app.activeFrameLayerTree();
    }
    static const strova::LayerTree& activeLayerTree(const App& app)
    {
        return const_cast<App&>(app).activeFrameLayerTree();
    }

    static void fillRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c);
    static void drawRect(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c);
    static void fillCircle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c);
    static void drawCircle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c);
    static void fillPill(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c);
    static void strokePill(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c);
    static void drawText(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y, SDL_Color col);
    static int  measureTextW(TTF_Font* font, const std::string& text);
    static void fillRoundRect(SDL_Renderer* r, const SDL_Rect& rc, int rad, SDL_Color c);
    static void strokeRoundRect(SDL_Renderer* r, const SDL_Rect& rc, int rad, SDL_Color c);
    static void drawModernButton(SDL_Renderer* r, const SDL_Rect& rc, bool hover, bool active);
    static void handleBrushPanelAction(App& app, ToolOptionsPanel& optionsPanel);
    static void handleBrushPanelActionImpl(App& app, ToolOptionsPanel& optionsPanel);
    static const char* toolDisplayName(ToolType t)
    {
        return strova::tools::displayName(t);
    }

    static const char* toolTooltipText(ToolType t)
    {
        return strova::tools::tooltip(t);
    }

    static bool g_strokePreviewActive = false;
    static ToolType g_strokePreviewTool = ToolType::Brush;
    static std::vector<StrokePoint> g_strokePreviewPoints;
    static SDL_Color g_strokePreviewColor{ 0,0,0,255 };
    static float g_strokePreviewThickness = 1.0f;
    static GradientConfig g_strokePreviewGradient{};


    /*
       Legacy embedded Brush Creator disabled.
       The dedicated brush/BrushCreatorScreen.cpp workspace is now the only
       live Brush Creator implementation. Keeping the editor-owned creator in
       this translation unit caused duplicate feature ownership, drift in
       preview/save behavior, and inconsistent UI/event routing.
    */
#if 0
    enum class BrushCreatorModalType
    {
        None = 0,
        Validation,
        ImportStamp,
        ExportBrush,
        InstallBrush,
        NewBrush
    };

    struct BrushCreatorState
    {
        bool open = false;
        int activeTab = 0;
        strova::brush::BrushProject project{};
        std::string status = "Brush Creator ready";
        bool dirty = false;

        BrushCreatorModalType modal = BrushCreatorModalType::None;
        std::string modalText;

        bool scriptFocused = false;
        std::size_t scriptCursor = 0;

        int inspectorBgMode = 0;
        int testBgMode = 0;
        bool testUseGradient = false;
        bool testPressure = false;
        bool testDrawing = false;
        SDL_Color testColor{ 20, 20, 20, 255 };
        std::vector<Stroke> testStrokes;
        Stroke liveTestStroke{};

        strova::brush::BrushStamp pendingImportStamp{};
        std::string pendingImportPath;
        strova::brush::MaskSource pendingImportMaskSource = strova::brush::MaskSource::Darkness;
        bool pendingImportInvert = false;

        strova::brush::BrushType pendingNewType = strova::brush::BrushType::Procedural;
        strova::brush::GeneratorType pendingNewGenerator = strova::brush::GeneratorType::SoftCircle;

        strova::brush::MaskSource syncedMaskSource = strova::brush::MaskSource::Alpha;
        bool syncedInvert = false;
        float syncedThreshold = -1.0f;
        float syncedLevelsClamp = -1.0f;
        float syncedEdgeBoost = -1.0f;
    };

    static BrushCreatorState g_brushCreator{};
    static void refreshBrushCreatorPreview();

    static SDL_Rect brushCreatorRect(const App& app)
    {
        const SDL_Rect canvas = app.getUILayout().canvas;
        return SDL_Rect{ canvas.x + 24, canvas.y + 24, std::max(860, canvas.w - 48), std::max(560, canvas.h - 48) };
    }

    static SDL_Rect brushCreatorCloseRect(const SDL_Rect& rc)
    {
        return SDL_Rect{ rc.x + rc.w - 42, rc.y + 10, 30, 26 };
    }

    static SDL_Rect brushCreatorLeftRect(const SDL_Rect& rc)
    {
        return SDL_Rect{ rc.x + 12, rc.y + 52, 220, rc.h - 94 };
    }

    static SDL_Rect brushCreatorRightRect(const SDL_Rect& rc)
    {
        return SDL_Rect{ rc.x + rc.w - 208, rc.y + 52, 196, rc.h - 94 };
    }

    static SDL_Rect brushCreatorCenterRect(const SDL_Rect& rc)
    {
        const SDL_Rect left = brushCreatorLeftRect(rc);
        const SDL_Rect right = brushCreatorRightRect(rc);
        return SDL_Rect{ left.x + left.w + 10, rc.y + 52, right.x - (left.x + left.w + 20), rc.h - 94 };
    }

    static SDL_Rect brushCreatorStatusRect(const SDL_Rect& rc)
    {
        return SDL_Rect{ rc.x + 12, rc.y + rc.h - 30, rc.w - 24, 18 };
    }

    static SDL_Rect brushCreatorTabRect(const SDL_Rect& rc, int index)
    {
        const SDL_Rect left = brushCreatorLeftRect(rc);
        return SDL_Rect{ left.x + 10, left.y + 180 + index * 34, left.w - 20, 28 };
    }

    static SDL_Rect brushCreatorScriptEditorRect(const SDL_Rect& rc)
    {
        const SDL_Rect center = brushCreatorCenterRect(rc);
        return SDL_Rect{ center.x + 12, center.y + 126, center.w - 24, std::max(120, center.h - 138) };
    }

    static SDL_Rect brushCreatorTestCanvasRect(const SDL_Rect& rc)
    {
        const SDL_Rect center = brushCreatorCenterRect(rc);
        return SDL_Rect{ center.x + 12, center.y + 84, center.w - 24, center.h - 96 };
    }

    static const char* brushCreatorTabName(int index)
    {
        static const char* kTabs[7] = {
            "Overview", "Stamp", "Behavior", "Color", "Script", "Test Canvas", "Package"
        };
        if (index < 0 || index >= 7) return "Overview";
        return kTabs[index];
    }

    static const char* brushCreatorMaskModeLabel(strova::brush::MaskSource src)
    {
        switch (src)
        {
        case strova::brush::MaskSource::Alpha: return "Alpha";
        case strova::brush::MaskSource::Luminance: return "Luminance";
        case strova::brush::MaskSource::Darkness: return "Darkness";
        }
        return "Alpha";
    }

    static const char* brushCreatorBgLabel(int mode)
    {
        switch (mode)
        {
        case 0: return "White";
        case 1: return "Dark";
        default: return "Checker";
        }
    }

    static const char* brushCreatorGeneratorLabel(strova::brush::GeneratorType g)
    {
        switch (g)
        {
        case strova::brush::GeneratorType::HardCircle: return "Hard Circle";
        case strova::brush::GeneratorType::SoftCircle: return "Soft Circle";
        case strova::brush::GeneratorType::Square: return "Square";
        case strova::brush::GeneratorType::SoftSquare: return "Soft Square";
        case strova::brush::GeneratorType::SpeckleCluster: return "Speckle Cluster";
        case strova::brush::GeneratorType::GrainyDisk: return "Grainy Disk";
        case strova::brush::GeneratorType::OvalTip: return "Oval Tip";
        case strova::brush::GeneratorType::ChalkPatch: return "Chalk Patch";
        case strova::brush::GeneratorType::NoiseBlob: return "Noise Blob";
        }
        return "Soft Circle";
    }

    static SDL_Color brushCreatorBgColor(int mode)
    {
        switch (mode)
        {
        case 0: return SDL_Color{ 244, 246, 250, 255 };
        case 1: return SDL_Color{ 30, 34, 42, 255 };
        default: return SDL_Color{ 64, 68, 76, 255 };
        }
    }

    static void brushCreatorDrawButton(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const std::string& label, bool active = false)
    {
        fillRect(r, rc, active ? COL_ACCENT : COL_BTN_IDLE);
        drawRect(r, rc, active ? SDL_Color{ 200, 220, 255, 255 } : COL_BORDER_SOFT);
        drawText(r, font, label, rc.x + 8, rc.y + 5, active ? SDL_Color{ 10, 12, 18, 255 } : COL_TEXT_DIM);
    }

    static void brushCreatorDrawChecker(SDL_Renderer* r, const SDL_Rect& rc)
    {
        const int cell = 12;
        for (int y = rc.y; y < rc.y + rc.h; y += cell)
        {
            for (int x = rc.x; x < rc.x + rc.w; x += cell)
            {
                const bool dark = (((x - rc.x) / cell) + ((y - rc.y) / cell)) % 2 == 0;
                SDL_Rect cellRc{ x, y, std::min(cell, rc.x + rc.w - x), std::min(cell, rc.y + rc.h - y) };
                fillRect(r, cellRc, dark ? SDL_Color{ 58, 62, 70, 255 } : SDL_Color{ 84, 88, 96, 255 });
            }
        }
    }

    static void brushCreatorDrawPixels(SDL_Renderer* r, const SDL_Rect& rc, int w, int h, const std::vector<std::uint8_t>& rgba, bool checkerBg, SDL_Color bg)
    {
        if (checkerBg) brushCreatorDrawChecker(r, rc);
        else fillRect(r, rc, bg);
        drawRect(r, rc, COL_BORDER_SOFT);
        if (w <= 0 || h <= 0 || rgba.empty())
            return;

        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!tex) return;
        SDL_UpdateTexture(tex, nullptr, rgba.data(), w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_Rect dst = rc;
        const float sx = (float)(rc.w - 10) / (float)std::max(1, w);
        const float sy = (float)(rc.h - 10) / (float)std::max(1, h);
        const float fit = std::max(0.01f, std::min(sx, sy));
        dst.w = std::max(1, (int)std::lround((float)w * fit));
        dst.h = std::max(1, (int)std::lround((float)h * fit));
        dst.x = rc.x + (rc.w - dst.w) / 2;
        dst.y = rc.y + (rc.h - dst.h) / 2;
        SDL_RenderCopy(r, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }

    static std::vector<std::uint8_t> brushCreatorMaskToRgba(const strova::brush::BrushStamp& stamp, float gamma = 1.0f)
    {
        std::vector<std::uint8_t> out;
        if (stamp.width <= 0 || stamp.height <= 0 || stamp.mask.empty()) return out;
        const size_t pxCount = (size_t)stamp.width * (size_t)stamp.height;
        out.assign(pxCount * 4ull, 0u);
        gamma = std::max(0.01f, gamma);
        for (size_t i = 0; i < pxCount; ++i)
        {
            float v = stamp.mask[i] / 255.0f;
            v = std::pow(v, gamma);
            const std::uint8_t b = (std::uint8_t)std::clamp((int)std::lround(v * 255.0f), 0, 255);
            out[i * 4 + 0] = b;
            out[i * 4 + 1] = b;
            out[i * 4 + 2] = b;
            out[i * 4 + 3] = 255;
        }
        return out;
    }

    static void brushCreatorDrawStampPane(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& pane, const std::string& title,
        int w, int h, const std::vector<std::uint8_t>& rgba, bool checkerBg, SDL_Color bg, const std::string& footer = std::string())
    {
        fillRect(r, pane, COL_BG_PANEL2);
        drawRect(r, pane, COL_BORDER_SOFT);
        drawText(r, font, title, pane.x + 8, pane.y + 6, COL_TEXT_MAIN);
        SDL_Rect content{ pane.x + 8, pane.y + 28, pane.w - 16, pane.h - (footer.empty() ? 36 : 54) };
        brushCreatorDrawPixels(r, content, w, h, rgba, checkerBg, bg);
        if (!footer.empty())
            drawText(r, font, footer, pane.x + 8, pane.y + pane.h - 22, COL_TEXT_DIM);
    }

    static GradientConfig brushCreatorDefaultGradient()
    {
        GradientConfig g{};
        g.enabled = true;
        g.mode = 1;
        g.stopPos = { 0.0f, 0.33f, 0.66f, 1.0f };
        g.stopColor[0] = SDL_Color{ 255, 120, 80, 255 };
        g.stopColor[1] = SDL_Color{ 255, 214, 96, 255 };
        g.stopColor[2] = SDL_Color{ 120, 205, 255, 255 };
        g.stopColor[3] = SDL_Color{ 120, 140, 255, 255 };
        return g;
    }

    static void clampBrushScriptCursor()
    {
        g_brushCreator.scriptCursor = std::min<std::size_t>(g_brushCreator.scriptCursor, g_brushCreator.project.package.scriptSource.size());
    }

    static void setDefaultBrushLuaIfEmpty()
    {
        if (strova::brush::trimCopy(g_brushCreator.project.package.scriptSource).empty())
            g_brushCreator.project.package.scriptSource = "-- Strova Brush Lua\nspacing_scale=1.0\nscatter_boost=0.0\nalpha_scale=1.0\nsize_scale=1.0\nrotation_bias_deg=0.0\n";
        clampBrushScriptCursor();
    }

    static void insertBrushScriptText(const std::string& text)
    {
        clampBrushScriptCursor();
        g_brushCreator.project.package.scriptSource.insert(g_brushCreator.scriptCursor, text);
        g_brushCreator.scriptCursor += text.size();
    }

    static void brushCreatorMarkDirty(const std::string& status)
    {
        g_brushCreator.dirty = true;
        g_brushCreator.status = status;
        refreshBrushCreatorPreview();
    }

    static void brushCreatorSetValidation(const std::string& text)
    {
        g_brushCreator.modal = BrushCreatorModalType::Validation;
        g_brushCreator.modalText = text;
    }

    static void brushCreatorResetSandbox()
    {
        g_brushCreator.testDrawing = false;
        g_brushCreator.liveTestStroke = Stroke{};
        g_brushCreator.testStrokes.clear();
    }

    static Stroke brushCreatorMakeSandboxStroke(const strova::brush::BrushPackage& pkg, SDL_Color color, bool useGradient)
    {
        Stroke s{};
        s.tool = ToolType::Brush;
        s.color = pkg.manifest.color.supportsUserColor ? color : pkg.manifest.color.fixedColor;
        s.thickness = pkg.manifest.params.sizeDefault;
        s.brushId = pkg.manifest.id;
        s.brushName = pkg.manifest.name;
        s.brushVersion = pkg.manifest.version;
        s.settings.brushId = pkg.manifest.id;
        s.settings.brushDisplayName = pkg.manifest.name;
        s.settings.brushVersion = pkg.manifest.version;
        s.settings.brushSupportsGradient = pkg.manifest.color.supportsGradient;
        s.settings.brushSupportsUserColor = pkg.manifest.color.supportsUserColor;
        s.settings.size = pkg.manifest.params.sizeDefault;
        s.settings.opacity = pkg.manifest.params.opacity;
        s.settings.hardness = pkg.manifest.params.hardness;
        s.settings.spacing = pkg.manifest.params.spacing;
        s.settings.flow = pkg.manifest.params.flow;
        s.settings.scatter = pkg.manifest.params.scatter;
        s.settings.jitterSize = pkg.manifest.params.jitterSize;
        s.settings.jitterOpacity = pkg.manifest.params.jitterOpacity;
        s.settings.jitterRotation = pkg.manifest.params.jitterRotation;
        s.settings.spacingJitter = pkg.manifest.params.spacingJitter;
        if (useGradient && pkg.manifest.color.supportsGradient)
            s.gradient = brushCreatorDefaultGradient();
        return s;
    }

    static void brushCreatorAppendSandboxPoint(const SDL_Rect& canvas, Stroke& stroke, int mx, int my, bool pressureSim)
    {
        const float px = (float)(mx - canvas.x);
        const float py = (float)(my - canvas.y);
        if (!stroke.points.empty())
        {
            const StrokePoint& last = stroke.points.back();
            const float dx = px - last.x;
            const float dy = py - last.y;
            if ((dx * dx + dy * dy) < 4.0f)
                return;
        }
        StrokePoint p{};
        p.x = px;
        p.y = py;
        if (pressureSim)
        {
            const float u = std::clamp((float)(py / std::max(1, canvas.h)), 0.0f, 1.0f);
            p.pressure = 0.35f + (1.0f - u) * 0.65f;
        }
        stroke.points.push_back(p);
    }

    static void brushCreatorAddReplayStroke(const SDL_Rect& canvas)
    {
        Stroke s = brushCreatorMakeSandboxStroke(g_brushCreator.project.package, g_brushCreator.testColor, g_brushCreator.testUseGradient);
        const int left = canvas.x + 28;
        const int top = canvas.y + 28;
        const int right = canvas.x + canvas.w - 28;
        const int bottom = canvas.y + canvas.h - 28;
        const int midY = canvas.y + canvas.h / 2;
        const int count = 32;
        for (int i = 0; i < count; ++i)
        {
            const float t = (float)i / (float)(count - 1);
            const float x = (float)(left + (int)std::lround((right - left) * t));
            const float wave = std::sinf(t * 6.2831853f) * (float)std::max(12, canvas.h / 7);
            const float y = (float)midY + wave * (0.35f + t * 0.65f);
            StrokePoint p{};
            p.x = x - (float)canvas.x;
            p.y = std::clamp(y - (float)canvas.y, 12.0f, (float)(canvas.h - 12));
            p.pressure = g_brushCreator.testPressure ? (0.35f + t * 0.65f) : 1.0f;
            s.points.push_back(p);
        }
        if (!s.points.empty())
            g_brushCreator.testStrokes.push_back(s);
    }

    static void openBrushCreatorWithProject(App& app, const strova::brush::BrushProject& proj, const std::string& status)
    {
        g_brushCreator.project = proj;
        g_brushCreator.open = true;
        g_brushCreator.activeTab = 0;
        g_brushCreator.status = status;
        g_brushCreator.dirty = false;
        g_brushCreator.scriptFocused = false;
        g_brushCreator.modal = BrushCreatorModalType::None;
        g_brushCreator.pendingImportStamp = strova::brush::BrushStamp{};
        g_brushCreator.pendingImportPath.clear();
        g_brushCreator.pendingImportMaskSource = g_brushCreator.project.package.manifest.color.maskSource;
        g_brushCreator.pendingImportInvert = g_brushCreator.project.package.manifest.color.invertMask;
        g_brushCreator.syncedMaskSource = (strova::brush::MaskSource)-1;
        g_brushCreator.syncedInvert = !g_brushCreator.project.package.manifest.color.invertMask;
        g_brushCreator.syncedThreshold = -1.0f;
        g_brushCreator.syncedLevelsClamp = -1.0f;
        g_brushCreator.syncedEdgeBoost = -1.0f;
        clampBrushScriptCursor();
        setDefaultBrushLuaIfEmpty();
        brushCreatorResetSandbox();
        refreshBrushCreatorPreview();
        SDL_StartTextInput();
        (void)app;
    }

    static bool saveBrushCreatorProjectInteractive(App& app)
    {
        std::string outPath = g_brushCreator.project.projectPath;
        if (outPath.empty())
        {
            if (!platform::pickSaveBrushProjectFile(outPath, g_brushCreator.project.package.manifest.name))
                return false;
        }
        std::string err;
        refreshBrushCreatorPreview();
        if (!app.brushManager().saveProjectFile(g_brushCreator.project, outPath, err))
        {
            brushCreatorSetValidation(err);
            return true;
        }
        g_brushCreator.project.projectPath = outPath;
        g_brushCreator.dirty = false;
        g_brushCreator.status = "Brush project saved";
        return true;
    }

    static void consumePendingBrushCreatorLaunch(App& app)
    {
        std::string brushProjectPath;
        if (!app.consumePendingBrushCreatorLaunch(brushProjectPath))
            return;

        if (!brushProjectPath.empty())
        {
            strova::brush::BrushProject proj{};
            std::string err;
            if (app.brushManager().loadProjectFile(brushProjectPath, proj, err))
            {
                openBrushCreatorWithProject(app, proj, "Brush project opened from launcher");
            }
            else
            {
                g_brushCreator.project = app.brushManager().makeDefaultProject("New Brush");
                g_brushCreator.open = true;
                g_brushCreator.activeTab = 0;
                g_brushCreator.status = "Brush Creator opened";
                g_brushCreator.dirty = false;
                brushCreatorSetValidation(err);
            }
        }
        else
        {
            g_brushCreator.project = app.brushManager().makeDefaultProject("New Brush");
            g_brushCreator.open = true;
            g_brushCreator.activeTab = 0;
            g_brushCreator.status = "Brush Creator opened from launcher";
            g_brushCreator.dirty = false;
        }
        setDefaultBrushLuaIfEmpty();
        g_brushCreator.scriptCursor = g_brushCreator.project.package.scriptSource.size();
        g_brushCreator.pendingImportMaskSource = g_brushCreator.project.package.manifest.color.maskSource;
        g_brushCreator.pendingImportInvert = g_brushCreator.project.package.manifest.color.invertMask;
        g_brushCreator.syncedMaskSource = (strova::brush::MaskSource)-1;
        g_brushCreator.syncedInvert = !g_brushCreator.project.package.manifest.color.invertMask;
        g_brushCreator.syncedThreshold = -1.0f;
        g_brushCreator.syncedLevelsClamp = -1.0f;
        g_brushCreator.syncedEdgeBoost = -1.0f;
        brushCreatorResetSandbox();
        SDL_StartTextInput();
    }

    static void ensureBrushCreatorProject(App& app)
    {
        if (g_brushCreator.project.package.manifest.id.empty())
            g_brushCreator.project = app.brushManager().makeDefaultProject("New Brush");
    }

    static void refreshBrushCreatorPreview()
    {
        if (!g_brushCreator.project.package.stamp.empty())
        {
            auto& stamp = g_brushCreator.project.package.stamp;
            auto& col = g_brushCreator.project.package.manifest.color;
            if (!col.supportsGradient)
                col.gradientMode = strova::brush::GradientMode::None;
            if (!col.supportsUserColor && !col.supportsGradient && col.fixedColor.a == 0)
                col.fixedColor = col.previewTint;
            if (stamp.mask.empty() ||
                g_brushCreator.syncedMaskSource != col.maskSource ||
                g_brushCreator.syncedInvert != col.invertMask ||
                std::fabs(g_brushCreator.syncedThreshold - stamp.threshold) >= 0.0001f ||
                std::fabs(g_brushCreator.syncedLevelsClamp - stamp.levelsClamp) >= 0.0001f ||
                std::fabs(g_brushCreator.syncedEdgeBoost - stamp.edgeBoost) >= 0.0001f)
            {
                strova::brush::normalizeStamp(
                    stamp,
                    col.maskSource,
                    col.invertMask,
                    stamp.threshold,
                    stamp.levelsClamp,
                    stamp.edgeBoost);
                g_brushCreator.syncedMaskSource = col.maskSource;
                g_brushCreator.syncedInvert = col.invertMask;
                g_brushCreator.syncedThreshold = stamp.threshold;
                g_brushCreator.syncedLevelsClamp = stamp.levelsClamp;
                g_brushCreator.syncedEdgeBoost = stamp.edgeBoost;
            }
        }
        g_brushCreator.project.package.manifest.generator = g_brushCreator.project.generator;
        g_brushCreator.project.package.preview = strova::brush::buildPackagePreview(g_brushCreator.project.package);
        g_brushCreator.project.package.validation = strova::brush::validate(g_brushCreator.project.package);
    }

    static void selectBrushIntoTool(App& app, const strova::brush::BrushPackage& pkg)
    {
        auto& s = app.toolBank.get(ToolType::Brush);
        s.brushId = pkg.manifest.id;
        s.brushDisplayName = pkg.manifest.name;
        s.brushVersion = pkg.manifest.version;
        s.brushSupportsUserColor = pkg.manifest.color.supportsUserColor;
        s.brushSupportsGradient = pkg.manifest.color.supportsGradient;
        app.brushManager().select(pkg.manifest.id);
        if (app.getEditorUiState().activeTool == ToolType::Brush)
        {
            app.replaceToolSettingsCommand(ToolType::Brush, s);
            app.getEngine().setBrushSelection(s.brushId, s.brushVersion, s.brushDisplayName);
        }
    }

    static void handleBrushPanelActionImpl(App& app, ToolOptionsPanel& optionsPanel)
    {

        ToolOptionsPanel::PanelAction action{};
        std::string brushId;
        if (!optionsPanel.consumeAction(action, brushId))
            return;

        switch (action)
        {
        case ToolOptionsPanel::PanelAction::OpenBrushCreator:
            app.openBrushCreatorWorkspace("", true);
            break;
        case ToolOptionsPanel::PanelAction::ManageBrushes:
            g_brushResourceManager.open(brushId.empty() ? app.toolBank.get(ToolType::Brush).brushId : brushId);
            break;
        case ToolOptionsPanel::PanelAction::InstallBrush:
        {
            std::string path;
            if (platform::pickOpenBrushOrProject(path))
            {
                std::string err;
                std::string installed;
                const std::filesystem::path pth(path);
                if (pth.extension() == ".sbrushproj" || pth.extension() == ".png")
                {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Strova Brush Install", "Editor brush panel only installs ready .sbrush packages now. Open .sbrushproj or PNG stamp from the Launcher Brush Creator.", app.windowHandle());
                }
                else if (!app.brushManager().installPackageFile(path, installed, err))
                {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Strova Brush Install", err.c_str(), app.windowHandle());
                }
                else
                {
                    if (const auto* pkg = app.brushManager().selected())
                        selectBrushIntoTool(app, *pkg);
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Strova Brush Install", "Brush installed.", app.windowHandle());
                }
            }
            break;
        }
        case ToolOptionsPanel::PanelAction::ExportSelectedBrush:
        {
            const auto* pkg = app.brushManager().findById(brushId);
            if (!pkg) break;
            std::string outPath;
            if (platform::pickSaveBrushFile(outPath, pkg->manifest.name))
            {
                std::string err;
                if (!app.brushManager().exportPackage(*pkg, outPath, err))
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Strova Brush Install", err.c_str(), app.windowHandle());
                else
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Strova Brush Export", "Brush exported.", app.windowHandle());
            }
            break;
        }
        case ToolOptionsPanel::PanelAction::RefreshBrushes:
        {
            const std::string preferredId = app.toolBank.get(ToolType::Brush).brushId;
            app.brushManager().refresh();
            if (!preferredId.empty())
                app.brushManager().select(preferredId);
            if (const auto* pkg = app.brushManager().selected())
                selectBrushIntoTool(app, *pkg);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Strova Brush Browser", "Brush catalog refreshed.", app.windowHandle());
            break;
        }
        default: break;
        }
    }
    static void handleBrushPanelAction(App& app, ToolOptionsPanel& optionsPanel)
    {
        handleBrushPanelActionImpl(app, optionsPanel);
    }
    static bool handleBrushCreatorEvent(App& app, const SDL_Event& e, int mx, int my)
    {
        if (!g_brushCreator.open) return false;
        ensureBrushCreatorProject(app);
        const SDL_Rect rc = brushCreatorRect(app);
        const SDL_Rect center = brushCreatorCenterRect(rc);
        const SDL_Rect canvas = brushCreatorTestCanvasRect(rc);

        auto closeAnyModal = [&]() -> bool
            {
                if (g_brushCreator.modal == BrushCreatorModalType::None) return false;
                g_brushCreator.modal = BrushCreatorModalType::None;
                g_brushCreator.modalText.clear();
                return true;
            };

        if (g_brushCreator.modal != BrushCreatorModalType::None)
        {
            SDL_Rect modal{ rc.x + rc.w / 2 - 220, rc.y + rc.h / 2 - 150, 440, 300 };
            SDL_Rect cancelR{ modal.x + modal.w - 116, modal.y + modal.h - 38, 96, 26 };
            SDL_Rect okR{ modal.x + modal.w - 222, modal.y + modal.h - 38, 96, 26 };
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
            {
                if (!pointInRect(mx, my, modal))
                    return closeAnyModal();

                if (pointInRect(mx, my, cancelR))
                    return closeAnyModal();

                if (g_brushCreator.modal == BrushCreatorModalType::Validation)
                {
                    if (pointInRect(mx, my, okR))
                        return closeAnyModal();
                    return true;
                }
                if (g_brushCreator.modal == BrushCreatorModalType::ImportStamp)
                {
                    SDL_Rect chooseR{ modal.x + 16, modal.y + 44, 140, 28 };
                    SDL_Rect alphaR{ modal.x + 16, modal.y + 214, 96, 26 };
                    SDL_Rect lumaR{ modal.x + 120, modal.y + 214, 110, 26 };
                    SDL_Rect darkR{ modal.x + 238, modal.y + 214, 110, 26 };
                    SDL_Rect invertR{ modal.x + 16, modal.y + 248, 140, 26 };
                    if (pointInRect(mx, my, chooseR))
                    {
                        std::string path;
                        if (platform::pickOpenFile(path))
                        {
                            std::string err;
                            strova::brush::BrushStamp stamp{};
                            if (strova::brush::loadRgbaFromImage(path, stamp, err))
                            {
                                g_brushCreator.pendingImportStamp = stamp;
                                g_brushCreator.pendingImportPath = path;
                                g_brushCreator.modalText = "PNG ready to import";
                            }
                            else
                            {
                                g_brushCreator.modalText = err;
                            }
                        }
                        return true;
                    }
                    if (pointInRect(mx, my, alphaR)) { g_brushCreator.pendingImportMaskSource = strova::brush::MaskSource::Alpha; return true; }
                    if (pointInRect(mx, my, lumaR)) { g_brushCreator.pendingImportMaskSource = strova::brush::MaskSource::Luminance; return true; }
                    if (pointInRect(mx, my, darkR)) { g_brushCreator.pendingImportMaskSource = strova::brush::MaskSource::Darkness; return true; }
                    if (pointInRect(mx, my, invertR)) { g_brushCreator.pendingImportInvert = !g_brushCreator.pendingImportInvert; return true; }
                    if (pointInRect(mx, my, okR))
                    {
                        if (g_brushCreator.pendingImportStamp.empty())
                        {
                            g_brushCreator.modalText = "Pick a PNG first.";
                            return true;
                        }
                        g_brushCreator.project.package.stamp = g_brushCreator.pendingImportStamp;
                        g_brushCreator.project.package.manifest.type = strova::brush::BrushType::RasterStamp;
                        g_brushCreator.project.package.manifest.color.maskSource = g_brushCreator.pendingImportMaskSource;
                        g_brushCreator.project.package.manifest.color.invertMask = g_brushCreator.pendingImportInvert;
                        g_brushCreator.project.package.manifest.name = std::filesystem::path(g_brushCreator.pendingImportPath).stem().string();
                        g_brushCreator.project.package.manifest.id = strova::brush::sanitizeId("strova.local." + std::filesystem::path(g_brushCreator.pendingImportPath).stem().string());
                        strova::brush::normalizeStamp(g_brushCreator.project.package.stamp,
                            g_brushCreator.project.package.manifest.color.maskSource,
                            g_brushCreator.project.package.manifest.color.invertMask,
                            g_brushCreator.project.package.stamp.threshold,
                            g_brushCreator.project.package.stamp.levelsClamp,
                            g_brushCreator.project.package.stamp.edgeBoost);
                        g_brushCreator.modal = BrushCreatorModalType::None;
                        g_brushCreator.activeTab = 1;
                        brushCreatorMarkDirty("Stamp imported");
                        return true;
                    }
                    return true;
                }
                if (g_brushCreator.modal == BrushCreatorModalType::ExportBrush)
                {
                    if (pointInRect(mx, my, okR))
                    {
                        std::string outPath;
                        if (platform::pickSaveBrushFile(outPath, g_brushCreator.project.package.manifest.name))
                        {
                            std::string err;
                            refreshBrushCreatorPreview();
                            if (!app.brushManager().exportPackage(g_brushCreator.project.package, outPath, err))
                            {
                                g_brushCreator.modalText = err;
                                g_brushCreator.modal = BrushCreatorModalType::Validation;
                            }
                            else
                            {
                                g_brushCreator.modal = BrushCreatorModalType::None;
                                g_brushCreator.status = "Brush exported";
                            }
                        }
                        return true;
                    }
                    return true;
                }
                if (g_brushCreator.modal == BrushCreatorModalType::InstallBrush)
                {
                    if (pointInRect(mx, my, okR))
                    {
                        std::string err;
                        refreshBrushCreatorPreview();
                        const std::filesystem::path dst = strova::brush::userDir() / (strova::brush::sanitizeId(g_brushCreator.project.package.manifest.id) + ".sbrush");
                        if (!app.brushManager().exportPackage(g_brushCreator.project.package, dst.string(), err))
                        {
                            g_brushCreator.modalText = err;
                            g_brushCreator.modal = BrushCreatorModalType::Validation;
                        }
                        else
                        {
                            app.brushManager().refresh();
                            if (const auto* pkg = app.brushManager().findById(g_brushCreator.project.package.manifest.id))
                                selectBrushIntoTool(app, *pkg);
                            g_brushCreator.modal = BrushCreatorModalType::None;
                            g_brushCreator.status = "Brush installed locally";
                        }
                        return true;
                    }
                    return true;
                }
                if (g_brushCreator.modal == BrushCreatorModalType::NewBrush)
                {
                    SDL_Rect typeR{ modal.x + 16, modal.y + 54, 170, 28 };
                    SDL_Rect genR{ modal.x + 16, modal.y + 92, 170, 28 };
                    if (pointInRect(mx, my, typeR))
                    {
                        using BT = strova::brush::BrushType;
                        g_brushCreator.pendingNewType = (g_brushCreator.pendingNewType == BT::Procedural) ? BT::RasterStamp :
                            (g_brushCreator.pendingNewType == BT::RasterStamp) ? BT::ScriptedRaster : BT::Procedural;
                        return true;
                    }
                    if (pointInRect(mx, my, genR))
                    {
                        int g = (int)g_brushCreator.pendingNewGenerator;
                        g = (g + 1) % 9;
                        g_brushCreator.pendingNewGenerator = (strova::brush::GeneratorType)g;
                        return true;
                    }
                    if (pointInRect(mx, my, okR))
                    {
                        auto proj = app.brushManager().makeDefaultProject("New Brush");
                        proj.requestedType = g_brushCreator.pendingNewType;
                        proj.generator = g_brushCreator.pendingNewGenerator;
                        proj.package.manifest.type = g_brushCreator.pendingNewType;
                        proj.package.stamp = strova::brush::makeProceduralStamp(g_brushCreator.pendingNewGenerator, 128);
                        proj.package.manifest.id = strova::brush::sanitizeId("strova.local.new_brush");
                        proj.package.manifest.name = "New Brush";
                        if (proj.package.manifest.type == strova::brush::BrushType::ScriptedRaster)
                            proj.package.scriptSource = "spacing_scale=1.0\nscatter_boost=0.0\nalpha_scale=1.0\nsize_scale=1.0\nrotation_bias_deg=0.0\n";
                        openBrushCreatorWithProject(app, proj, "Created new brush project");
                        g_brushCreator.dirty = true;
                        g_brushCreator.modal = BrushCreatorModalType::None;
                        return true;
                    }
                    return true;
                }
            }
            return true;
        }

        if (g_brushCreator.activeTab == 4)
        {
            const SDL_Rect textR = brushCreatorScriptEditorRect(rc);
            if (e.type == SDL_TEXTINPUT && g_brushCreator.scriptFocused)
            {
                insertBrushScriptText(e.text.text);
                brushCreatorMarkDirty("Lua buffer edited");
                return true;
            }
            if (e.type == SDL_KEYDOWN && g_brushCreator.scriptFocused)
            {
                const bool ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;
                clampBrushScriptCursor();
                switch (e.key.keysym.sym)
                {
                case SDLK_BACKSPACE:
                    if (g_brushCreator.scriptCursor > 0)
                    {
                        g_brushCreator.project.package.scriptSource.erase(g_brushCreator.scriptCursor - 1, 1);
                        --g_brushCreator.scriptCursor;
                        brushCreatorMarkDirty("Lua buffer edited");
                    }
                    return true;
                case SDLK_DELETE:
                    if (g_brushCreator.scriptCursor < g_brushCreator.project.package.scriptSource.size())
                    {
                        g_brushCreator.project.package.scriptSource.erase(g_brushCreator.scriptCursor, 1);
                        brushCreatorMarkDirty("Lua buffer edited");
                    }
                    return true;
                case SDLK_LEFT:
                    if (g_brushCreator.scriptCursor > 0) --g_brushCreator.scriptCursor;
                    return true;
                case SDLK_RIGHT:
                    if (g_brushCreator.scriptCursor < g_brushCreator.project.package.scriptSource.size()) ++g_brushCreator.scriptCursor;
                    return true;
                case SDLK_HOME:
                    g_brushCreator.scriptCursor = 0;
                    return true;
                case SDLK_END:
                    g_brushCreator.scriptCursor = g_brushCreator.project.package.scriptSource.size();
                    return true;
                case SDLK_RETURN:
                    [[fallthrough]];
                case SDLK_KP_ENTER:
                    insertBrushScriptText("\n");
                    brushCreatorMarkDirty("Lua buffer edited");
                    return true;
                case SDLK_TAB:
                    insertBrushScriptText("    ");
                    brushCreatorMarkDirty("Lua buffer edited");
                    return true;
                case SDLK_s:
                    if (ctrl)
                    {
                        saveBrushCreatorProjectInteractive(app);
                        return true;
                    }
                    break;
                default: break;
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
            {
                g_brushCreator.scriptFocused = pointInRect(mx, my, textR);
                if (g_brushCreator.scriptFocused) SDL_StartTextInput();
            }
        }

        if (g_brushCreator.activeTab == 5)
        {
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && pointInRect(mx, my, canvas))
            {
                g_brushCreator.liveTestStroke = brushCreatorMakeSandboxStroke(g_brushCreator.project.package, g_brushCreator.testColor, g_brushCreator.testUseGradient);
                brushCreatorAppendSandboxPoint(canvas, g_brushCreator.liveTestStroke, mx, my, g_brushCreator.testPressure);
                g_brushCreator.testDrawing = true;
                return true;
            }
            if (e.type == SDL_MOUSEMOTION && g_brushCreator.testDrawing)
            {
                brushCreatorAppendSandboxPoint(canvas, g_brushCreator.liveTestStroke, mx, my, g_brushCreator.testPressure);
                return true;
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && g_brushCreator.testDrawing)
            {
                if (g_brushCreator.liveTestStroke.points.size() >= 1)
                    g_brushCreator.testStrokes.push_back(g_brushCreator.liveTestStroke);
                g_brushCreator.liveTestStroke = Stroke{};
                g_brushCreator.testDrawing = false;
                return true;
            }
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            if (pointInRect(mx, my, brushCreatorCloseRect(rc)))
            {
                g_brushCreator.open = false;
                g_brushCreator.scriptFocused = false;
                g_brushCreator.testDrawing = false;
                SDL_StopTextInput();
                return true;
            }

            for (int i = 0; i < 7; ++i)
            {
                if (pointInRect(mx, my, brushCreatorTabRect(rc, i)))
                {
                    g_brushCreator.activeTab = i;
                    g_brushCreator.scriptFocused = (i == 4);
                    if (g_brushCreator.scriptFocused) SDL_StartTextInput();
                    return true;
                }
            }

            const SDL_Rect left = brushCreatorLeftRect(rc);
            const SDL_Rect right = brushCreatorRightRect(rc);
            (void)left;

            SDL_Rect newR{ center.x + 12, center.y + 12, 120, 28 };
            SDL_Rect saveR{ center.x + 140, center.y + 12, 120, 28 };
            SDL_Rect validateR{ center.x + 268, center.y + 12, 120, 28 };
            SDL_Rect exportR{ center.x + 396, center.y + 12, 120, 28 };
            SDL_Rect installR{ center.x + 524, center.y + 12, 120, 28 };

            if (g_brushCreator.activeTab == 0)
            {
                if (pointInRect(mx, my, newR)) { g_brushCreator.modal = BrushCreatorModalType::NewBrush; g_brushCreator.modalText.clear(); return true; }
                if (pointInRect(mx, my, saveR)) { saveBrushCreatorProjectInteractive(app); return true; }
                if (pointInRect(mx, my, validateR)) { refreshBrushCreatorPreview(); brushCreatorSetValidation(g_brushCreator.project.package.validation.summary()); return true; }
                if (pointInRect(mx, my, exportR)) { g_brushCreator.modal = BrushCreatorModalType::ExportBrush; return true; }
                if (pointInRect(mx, my, installR)) { g_brushCreator.modal = BrushCreatorModalType::InstallBrush; return true; }
            }
            else if (g_brushCreator.activeTab == 1)
            {
                SDL_Rect importR{ center.x + 12, center.y + 12, 120, 28 };
                SDL_Rect clearR{ center.x + 140, center.y + 12, 120, 28 };
                if (pointInRect(mx, my, importR))
                {
                    g_brushCreator.pendingImportStamp = strova::brush::BrushStamp{};
                    g_brushCreator.pendingImportPath.clear();
                    g_brushCreator.pendingImportMaskSource = g_brushCreator.project.package.manifest.color.maskSource;
                    g_brushCreator.pendingImportInvert = g_brushCreator.project.package.manifest.color.invertMask;
                    g_brushCreator.modal = BrushCreatorModalType::ImportStamp;
                    g_brushCreator.modalText.clear();
                    return true;
                }
                if (pointInRect(mx, my, clearR))
                {
                    g_brushCreator.project.package.stamp = strova::brush::BrushStamp{};
                    brushCreatorMarkDirty("Stamp cleared");
                    return true;
                }
                SDL_Rect genR[9];
                for (int i = 0; i < 9; ++i)
                    genR[i] = SDL_Rect{ center.x + 12 + (i % 3) * 128, center.y + 304 + (i / 3) * 34, 120, 28 };
                static const strova::brush::GeneratorType gens[9] = {
                    strova::brush::GeneratorType::HardCircle,
                    strova::brush::GeneratorType::SoftCircle,
                    strova::brush::GeneratorType::Square,
                    strova::brush::GeneratorType::SoftSquare,
                    strova::brush::GeneratorType::SpeckleCluster,
                    strova::brush::GeneratorType::GrainyDisk,
                    strova::brush::GeneratorType::OvalTip,
                    strova::brush::GeneratorType::ChalkPatch,
                    strova::brush::GeneratorType::NoiseBlob
                };
                for (int i = 0; i < 9; ++i)
                {
                    if (pointInRect(mx, my, genR[i]))
                    {
                        g_brushCreator.project.generator = gens[i];
                        g_brushCreator.project.package.stamp = strova::brush::makeProceduralStamp(gens[i], 128);
                        g_brushCreator.project.package.manifest.type = strova::brush::BrushType::Procedural;
                        brushCreatorMarkDirty("Generated procedural stamp");
                        return true;
                    }
                }
                SDL_Rect alphaR{ center.x + 408, center.y + 12, 90, 28 };
                SDL_Rect lumaR{ center.x + 506, center.y + 12, 108, 28 };
                SDL_Rect darkR{ center.x + 622, center.y + 12, 108, 28 };
                SDL_Rect invertR{ center.x + 408, center.y + 48, 140, 28 };
                if (pointInRect(mx, my, alphaR)) { g_brushCreator.project.package.manifest.color.maskSource = strova::brush::MaskSource::Alpha; brushCreatorMarkDirty("Mask source set to alpha"); return true; }
                if (pointInRect(mx, my, lumaR)) { g_brushCreator.project.package.manifest.color.maskSource = strova::brush::MaskSource::Luminance; brushCreatorMarkDirty("Mask source set to luminance"); return true; }
                if (pointInRect(mx, my, darkR)) { g_brushCreator.project.package.manifest.color.maskSource = strova::brush::MaskSource::Darkness; brushCreatorMarkDirty("Mask source set to darkness"); return true; }
                if (pointInRect(mx, my, invertR)) { g_brushCreator.project.package.manifest.color.invertMask = !g_brushCreator.project.package.manifest.color.invertMask; brushCreatorMarkDirty("Mask invert toggled"); return true; }
                SDL_Rect thrMinus{ center.x + 408, center.y + 210, 24, 24 };
                SDL_Rect thrPlus{ center.x + 474, center.y + 210, 24, 24 };
                SDL_Rect lvlMinus{ center.x + 408, center.y + 262, 24, 24 };
                SDL_Rect lvlPlus{ center.x + 474, center.y + 262, 24, 24 };
                SDL_Rect edgeMinus{ center.x + 408, center.y + 314, 24, 24 };
                SDL_Rect edgePlus{ center.x + 474, center.y + 314, 24, 24 };
                auto& stamp = g_brushCreator.project.package.stamp;
                if (pointInRect(mx, my, thrMinus)) { stamp.threshold = std::max(0.0f, stamp.threshold - 0.05f); brushCreatorMarkDirty("Threshold adjusted"); return true; }
                if (pointInRect(mx, my, thrPlus)) { stamp.threshold = std::min(1.0f, stamp.threshold + 0.05f); brushCreatorMarkDirty("Threshold adjusted"); return true; }
                if (pointInRect(mx, my, lvlMinus)) { stamp.levelsClamp = std::max(0.05f, stamp.levelsClamp - 0.05f); brushCreatorMarkDirty("Levels clamp adjusted"); return true; }
                if (pointInRect(mx, my, lvlPlus)) { stamp.levelsClamp = std::min(1.0f, stamp.levelsClamp + 0.05f); brushCreatorMarkDirty("Levels clamp adjusted"); return true; }
                if (pointInRect(mx, my, edgeMinus)) { stamp.edgeBoost = std::max(0.0f, stamp.edgeBoost - 0.1f); brushCreatorMarkDirty("Edge boost adjusted"); return true; }
                if (pointInRect(mx, my, edgePlus)) { stamp.edgeBoost = std::min(4.0f, stamp.edgeBoost + 0.1f); brushCreatorMarkDirty("Edge boost adjusted"); return true; }
            }
            else if (g_brushCreator.activeTab == 2)
            {
                auto& p = g_brushCreator.project.package.manifest.params;
                SDL_Rect sizeMinus{ center.x + 208, center.y + 52, 24, 24 };
                SDL_Rect sizePlus{ center.x + 274, center.y + 52, 24, 24 };
                SDL_Rect spacingMinus{ center.x + 208, center.y + 88, 24, 24 };
                SDL_Rect spacingPlus{ center.x + 274, center.y + 88, 24, 24 };
                SDL_Rect opMinus{ center.x + 208, center.y + 124, 24, 24 };
                SDL_Rect opPlus{ center.x + 274, center.y + 124, 24, 24 };
                SDL_Rect flowMinus{ center.x + 208, center.y + 160, 24, 24 };
                SDL_Rect flowPlus{ center.x + 274, center.y + 160, 24, 24 };
                SDL_Rect scatterMinus{ center.x + 208, center.y + 196, 24, 24 };
                SDL_Rect scatterPlus{ center.x + 274, center.y + 196, 24, 24 };
                SDL_Rect smoothMinus{ center.x + 208, center.y + 232, 24, 24 };
                SDL_Rect smoothPlus{ center.x + 274, center.y + 232, 24, 24 };
                SDL_Rect rotModeR{ center.x + 352, center.y + 52, 150, 28 };
                SDL_Rect blendR{ center.x + 352, center.y + 88, 150, 28 };
                SDL_Rect accumR{ center.x + 352, center.y + 124, 150, 28 };
                if (pointInRect(mx, my, sizeMinus)) { p.sizeDefault = std::max(1.0f, p.sizeDefault - 1.0f); brushCreatorMarkDirty("Default size adjusted"); return true; }
                if (pointInRect(mx, my, sizePlus)) { p.sizeDefault = std::min(1024.0f, p.sizeDefault + 1.0f); brushCreatorMarkDirty("Default size adjusted"); return true; }
                if (pointInRect(mx, my, spacingMinus)) { p.spacing = std::max(0.01f, p.spacing - 0.01f); brushCreatorMarkDirty("Spacing adjusted"); return true; }
                if (pointInRect(mx, my, spacingPlus)) { p.spacing = std::min(1.50f, p.spacing + 0.01f); brushCreatorMarkDirty("Spacing adjusted"); return true; }
                if (pointInRect(mx, my, opMinus)) { p.opacity = std::max(0.05f, p.opacity - 0.05f); brushCreatorMarkDirty("Opacity adjusted"); return true; }
                if (pointInRect(mx, my, opPlus)) { p.opacity = std::min(1.0f, p.opacity + 0.05f); brushCreatorMarkDirty("Opacity adjusted"); return true; }
                if (pointInRect(mx, my, flowMinus)) { p.flow = std::max(0.05f, p.flow - 0.05f); brushCreatorMarkDirty("Flow adjusted"); return true; }
                if (pointInRect(mx, my, flowPlus)) { p.flow = std::min(1.0f, p.flow + 0.05f); brushCreatorMarkDirty("Flow adjusted"); return true; }
                if (pointInRect(mx, my, scatterMinus)) { p.scatter = std::max(0.0f, p.scatter - 0.05f); brushCreatorMarkDirty("Scatter adjusted"); return true; }
                if (pointInRect(mx, my, scatterPlus)) { p.scatter = std::min(1.0f, p.scatter + 0.05f); brushCreatorMarkDirty("Scatter adjusted"); return true; }
                if (pointInRect(mx, my, smoothMinus)) { p.smoothing = std::max(0.0f, p.smoothing - 0.05f); brushCreatorMarkDirty("Smoothing adjusted"); return true; }
                if (pointInRect(mx, my, smoothPlus)) { p.smoothing = std::min(1.0f, p.smoothing + 0.05f); brushCreatorMarkDirty("Smoothing adjusted"); return true; }
                if (pointInRect(mx, my, rotModeR))
                {
                    using RM = strova::brush::RotationMode;
                    p.rotationMode = (p.rotationMode == RM::Stroke) ? RM::Fixed : (p.rotationMode == RM::Fixed) ? RM::Random : RM::Stroke;
                    brushCreatorMarkDirty("Rotation mode changed");
                    return true;
                }
                if (pointInRect(mx, my, blendR))
                {
                    using BM = strova::brush::BlendMode;
                    p.blendMode = (p.blendMode == BM::Normal) ? BM::Screen :
                        (p.blendMode == BM::Screen) ? BM::Additive :
                        (p.blendMode == BM::Additive) ? BM::Erase : BM::Normal;
                    brushCreatorMarkDirty("Blend mode changed");
                    return true;
                }
                if (pointInRect(mx, my, accumR)) { p.accumulate = !p.accumulate; brushCreatorMarkDirty("Accumulate toggled"); return true; }
            }
            else if (g_brushCreator.activeTab == 3)
            {
                auto& c = g_brushCreator.project.package.manifest.color;
                SDL_Rect userColR{ center.x + 12, center.y + 12, 170, 28 };
                SDL_Rect gradR{ center.x + 190, center.y + 12, 170, 28 };
                SDL_Rect fixedR{ center.x + 368, center.y + 12, 170, 28 };
                SDL_Rect modeR{ center.x + 12, center.y + 52, 220, 28 };
                SDL_Rect swatches[4] = {
                    { center.x + 12, center.y + 92, 32, 32 },
                    { center.x + 52, center.y + 92, 32, 32 },
                    { center.x + 92, center.y + 92, 32, 32 },
                    { center.x + 132, center.y + 92, 32, 32 }
                };
                if (pointInRect(mx, my, userColR)) { c.supportsUserColor = !c.supportsUserColor; brushCreatorMarkDirty("User color support toggled"); return true; }
                if (pointInRect(mx, my, gradR)) { c.supportsGradient = !c.supportsGradient; brushCreatorMarkDirty("Gradient support toggled"); return true; }
                if (pointInRect(mx, my, fixedR))
                {
                    static SDL_Color fixedChoices[4] = {
                        SDL_Color{255,255,255,255}, SDL_Color{32,32,32,255}, SDL_Color{255,186,96,255}, SDL_Color{120,190,255,255}
                    };
                    int next = 0;
                    for (int i = 0; i < 4; ++i) if (std::memcmp(&c.fixedColor, &fixedChoices[i], sizeof(SDL_Color)) == 0) next = (i + 1) % 4;
                    c.fixedColor = fixedChoices[next];
                    brushCreatorMarkDirty("Fixed color changed");
                    return true;
                }
                if (pointInRect(mx, my, modeR))
                {
                    using GM = strova::brush::GradientMode;
                    c.gradientMode = (c.gradientMode == GM::None) ? GM::StrokeProgress :
                        (c.gradientMode == GM::StrokeProgress) ? GM::Pressure :
                        (c.gradientMode == GM::Pressure) ? GM::Random : GM::None;
                    brushCreatorMarkDirty("Gradient mode changed");
                    return true;
                }
                static SDL_Color palette[4] = {
                    SDL_Color{255, 96, 96, 255}, SDL_Color{255, 214, 96, 255}, SDL_Color{96, 214, 160, 255}, SDL_Color{120, 160, 255, 255}
                };
                for (int i = 0; i < 4; ++i)
                {
                    if (pointInRect(mx, my, swatches[i]))
                    {
                        c.stops[(size_t)i].color = palette[i];
                        c.stops[(size_t)i].pos = (float)i / 3.0f;
                        brushCreatorMarkDirty("Gradient stops updated");
                        return true;
                    }
                }
            }
            else if (g_brushCreator.activeTab == 4)
            {
                SDL_Rect modeR{ center.x + 12, center.y + 12, 180, 28 };
                SDL_Rect loadR{ center.x + 200, center.y + 12, 110, 28 };
                SDL_Rect saveLuaR{ center.x + 318, center.y + 12, 110, 28 };
                SDL_Rect templateR{ center.x + 436, center.y + 12, 150, 28 };
                SDL_Rect clearR{ center.x + 594, center.y + 12, 90, 28 };
                if (pointInRect(mx, my, modeR))
                {
                    auto& pkg = g_brushCreator.project.package;
                    pkg.manifest.type = (pkg.manifest.type == strova::brush::BrushType::ScriptedRaster) ? strova::brush::BrushType::RasterStamp : strova::brush::BrushType::ScriptedRaster;
                    if (pkg.manifest.type == strova::brush::BrushType::ScriptedRaster && strova::brush::trimCopy(pkg.scriptSource).empty())
                        setDefaultBrushLuaIfEmpty();
                    brushCreatorMarkDirty("Scripted raster mode toggled");
                    return true;
                }
                if (pointInRect(mx, my, loadR))
                {
                    std::string path;
                    if (platform::pickOpenLuaFile(path))
                    {
                        std::ifstream in(path, std::ios::binary);
                        if (in)
                        {
                            g_brushCreator.project.package.scriptSource.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                            g_brushCreator.scriptCursor = g_brushCreator.project.package.scriptSource.size();
                            brushCreatorMarkDirty("Loaded behavior.lua");
                        }
                    }
                    return true;
                }
                if (pointInRect(mx, my, saveLuaR))
                {
                    std::string path;
                    if (platform::pickSaveLuaFile(path, "behavior"))
                    {
                        std::ofstream out(path, std::ios::binary);
                        if (out)
                        {
                            out << g_brushCreator.project.package.scriptSource;
                            g_brushCreator.status = "Saved behavior.lua";
                        }
                    }
                    return true;
                }
                if (pointInRect(mx, my, templateR))
                {
                    setDefaultBrushLuaIfEmpty();
                    g_brushCreator.scriptCursor = g_brushCreator.project.package.scriptSource.size();
                    brushCreatorMarkDirty("Inserted script template");
                    return true;
                }
                if (pointInRect(mx, my, clearR))
                {
                    g_brushCreator.project.package.scriptSource.clear();
                    g_brushCreator.scriptCursor = 0;
                    brushCreatorMarkDirty("Cleared behavior.lua");
                    return true;
                }
            }
            else if (g_brushCreator.activeTab == 5)
            {
                SDL_Rect clearR{ center.x + 12, center.y + 12, 100, 28 };
                SDL_Rect replayR{ center.x + 120, center.y + 12, 130, 28 };
                SDL_Rect bgR{ center.x + 258, center.y + 12, 120, 28 };
                SDL_Rect gradR{ center.x + 386, center.y + 12, 120, 28 };
                SDL_Rect pressureR{ center.x + 514, center.y + 12, 150, 28 };
                SDL_Rect colorR[4] = {
                    { center.x + 12, center.y + 48, 28, 28 },
                    { center.x + 48, center.y + 48, 28, 28 },
                    { center.x + 84, center.y + 48, 28, 28 },
                    { center.x + 120, center.y + 48, 28, 28 }
                };
                if (pointInRect(mx, my, clearR)) { brushCreatorResetSandbox(); g_brushCreator.status = "Test canvas cleared"; return true; }
                if (pointInRect(mx, my, replayR)) { brushCreatorAddReplayStroke(canvas); g_brushCreator.status = "Sample stroke replayed"; return true; }
                if (pointInRect(mx, my, bgR)) { g_brushCreator.testBgMode = (g_brushCreator.testBgMode + 1) % 3; return true; }
                if (pointInRect(mx, my, gradR)) { g_brushCreator.testUseGradient = !g_brushCreator.testUseGradient; return true; }
                if (pointInRect(mx, my, pressureR)) { g_brushCreator.testPressure = !g_brushCreator.testPressure; return true; }
                static SDL_Color swatches[4] = {
                    SDL_Color{20,20,20,255}, SDL_Color{255,108,92,255}, SDL_Color{120,205,255,255}, SDL_Color{120,216,150,255}
                };
                for (int i = 0; i < 4; ++i)
                {
                    if (pointInRect(mx, my, colorR[i]))
                    {
                        g_brushCreator.testColor = swatches[i];
                        return true;
                    }
                }
            }
            else if (g_brushCreator.activeTab == 6)
            {
                if (pointInRect(mx, my, saveR)) { saveBrushCreatorProjectInteractive(app); return true; }
                if (pointInRect(mx, my, validateR)) { refreshBrushCreatorPreview(); brushCreatorSetValidation(g_brushCreator.project.package.validation.summary()); return true; }
                if (pointInRect(mx, my, exportR)) { g_brushCreator.modal = BrushCreatorModalType::ExportBrush; return true; }
                if (pointInRect(mx, my, installR)) { g_brushCreator.modal = BrushCreatorModalType::InstallBrush; return true; }
            }

            if (pointInRect(mx, my, right))
            {
                SDL_Rect bgR{ right.x + 12, right.y + right.h - 34, right.w - 24, 24 };
                if (pointInRect(mx, my, bgR))
                {
                    g_brushCreator.inspectorBgMode = (g_brushCreator.inspectorBgMode + 1) % 3;
                    return true;
                }
            }
        }

        return pointInRect(mx, my, rc);
    }

    static void drawBrushCreator(App& app, SDL_Renderer* r, TTF_Font* font)
    {
        if (!g_brushCreator.open) return;
        ensureBrushCreatorProject(app);
        refreshBrushCreatorPreview();

        const SDL_Rect rc = brushCreatorRect(app);
        const SDL_Rect left = brushCreatorLeftRect(rc);
        const SDL_Rect center = brushCreatorCenterRect(rc);
        const SDL_Rect right = brushCreatorRightRect(rc);
        const SDL_Rect status = brushCreatorStatusRect(rc);

        fillRect(r, rc, SDL_Color{ 10, 14, 20, 246 });
        drawRect(r, rc, SDL_Color{ 110, 122, 152, 230 });
        fillRect(r, SDL_Rect{ rc.x + 1, rc.y + 1, rc.w - 2, 38 }, SDL_Color{ 14, 18, 26, 250 });

        drawText(r, font, "Brush Creator", rc.x + 14, rc.y + 12, COL_TEXT_MAIN);
        drawText(r, font, g_brushCreator.project.package.manifest.name, rc.x + 150, rc.y + 12, COL_TEXT_DIM);
        drawText(r, font, std::string("Type: ") + strova::brush::brushTypeName(g_brushCreator.project.package.manifest.type), rc.x + 350, rc.y + 12, COL_TEXT_DIM);
        drawText(r, font, g_brushCreator.dirty ? "Dirty" : "Saved", rc.x + rc.w - 132, rc.y + 12, g_brushCreator.dirty ? SDL_Color{ 255, 196, 96, 255 } : SDL_Color{ 118, 218, 154, 255 });

        SDL_Rect closeR = brushCreatorCloseRect(rc);
        fillRect(r, closeR, SDL_Color{ 60, 22, 26, 255 });
        drawRect(r, closeR, SDL_Color{ 138, 88, 96, 255 });
        drawText(r, font, "X", closeR.x + 10, closeR.y + 4, COL_TEXT_MAIN);

        fillRect(r, left, COL_BG_PANEL);
        drawRect(r, left, COL_BORDER_SOFT);
        fillRect(r, center, COL_BG_PANEL);
        drawRect(r, center, COL_BORDER_SOFT);
        fillRect(r, right, COL_BG_PANEL);
        drawRect(r, right, COL_BORDER_SOFT);
        fillRect(r, status, COL_BG_PANEL2);
        drawRect(r, status, COL_BORDER_SOFT);

        drawText(r, font, "Brush Identity", left.x + 10, left.y + 10, COL_TEXT_MAIN);
        SDL_Rect thumb{ left.x + 10, left.y + 34, 72, 72 };
        brushCreatorDrawPixels(r, thumb, g_brushCreator.project.package.preview.width, g_brushCreator.project.package.preview.height, g_brushCreator.project.package.preview.rgba, true, brushCreatorBgColor(2));
        drawText(r, font, g_brushCreator.project.package.manifest.name, left.x + 92, left.y + 38, COL_TEXT_DIM);
        drawText(r, font, "Id: " + g_brushCreator.project.package.manifest.id, left.x + 92, left.y + 60, COL_TEXT_DIM);
        drawText(r, font, "Type: " + strova::brush::brushTypeName(g_brushCreator.project.package.manifest.type), left.x + 92, left.y + 82, COL_TEXT_DIM);
        drawText(r, font, g_brushCreator.project.package.validation.ok ? "Validation: OK" : "Validation: Needs fixes", left.x + 10, left.y + 122,
            g_brushCreator.project.package.validation.ok ? SDL_Color{ 118,218,154,255 } : SDL_Color{ 255,148,120,255 });
        drawText(r, font, "Tabs", left.x + 10, left.y + 154, COL_TEXT_MAIN);

        for (int i = 0; i < 7; ++i)
            brushCreatorDrawButton(r, font, brushCreatorTabRect(rc, i), brushCreatorTabName(i), i == g_brushCreator.activeTab);

        drawText(r, font, "Inspector", right.x + 10, right.y + 10, COL_TEXT_MAIN);
        drawText(r, font, "Mask Preview", right.x + 10, right.y + 34, COL_TEXT_DIM);
        SDL_Rect maskPrevR{ right.x + 12, right.y + 54, right.w - 24, 98 };
        brushCreatorDrawPixels(r, maskPrevR, g_brushCreator.project.package.stamp.width, g_brushCreator.project.package.stamp.height,
            brushCreatorMaskToRgba(g_brushCreator.project.package.stamp, 1.0f), false, brushCreatorBgColor(g_brushCreator.inspectorBgMode));
        drawText(r, font, "Final Dab", right.x + 10, right.y + 160, COL_TEXT_DIM);
        SDL_Rect dabPrevR{ right.x + 12, right.y + 180, right.w - 24, 98 };
        brushCreatorDrawPixels(r, dabPrevR, g_brushCreator.project.package.preview.width, g_brushCreator.project.package.preview.height,
            g_brushCreator.project.package.preview.rgba, true, brushCreatorBgColor(g_brushCreator.inspectorBgMode));
        drawText(r, font, "Size: " + std::to_string((int)std::lround(g_brushCreator.project.package.manifest.params.sizeDefault)), right.x + 12, right.y + 290, COL_TEXT_DIM);
        drawText(r, font, "Opacity: " + std::to_string((int)std::lround(g_brushCreator.project.package.manifest.params.opacity * 100.0f)) + "%", right.x + 12, right.y + 312, COL_TEXT_DIM);
        drawText(r, font, "Flow: " + std::to_string((int)std::lround(g_brushCreator.project.package.manifest.params.flow * 100.0f)) + "%", right.x + 12, right.y + 334, COL_TEXT_DIM);
        drawText(r, font, "Blend: " + strova::brush::blendModeName(g_brushCreator.project.package.manifest.params.blendMode), right.x + 12, right.y + 356, COL_TEXT_DIM);
        drawText(r, font, "Mask: " + strova::brush::maskSourceName(g_brushCreator.project.package.manifest.color.maskSource), right.x + 12, right.y + 378, COL_TEXT_DIM);
        brushCreatorDrawButton(r, font, SDL_Rect{ right.x + 12, right.y + right.h - 34, right.w - 24, 24 }, std::string("Background: ") + brushCreatorBgLabel(g_brushCreator.inspectorBgMode), false);

        if (g_brushCreator.activeTab == 0)
        {
            drawText(r, font, "Overview", center.x + 12, center.y + 12, COL_TEXT_MAIN);
            SDL_Rect newR{ center.x + 12, center.y + 12, 120, 28 };
            SDL_Rect saveR{ center.x + 140, center.y + 12, 120, 28 };
            SDL_Rect validateR{ center.x + 268, center.y + 12, 120, 28 };
            SDL_Rect exportR{ center.x + 396, center.y + 12, 120, 28 };
            SDL_Rect installR{ center.x + 524, center.y + 12, 120, 28 };
            brushCreatorDrawButton(r, font, newR, "New Brush...");
            brushCreatorDrawButton(r, font, saveR, "Save Project");
            brushCreatorDrawButton(r, font, validateR, "Validate");
            brushCreatorDrawButton(r, font, exportR, "Export...");
            brushCreatorDrawButton(r, font, installR, "Install...");
            drawText(r, font, "Authoring-first flow", center.x + 12, center.y + 58, COL_TEXT_MAIN);
            drawText(r, font, "Stamp -> Behavior -> Color -> Script -> Test Canvas -> Package", center.x + 12, center.y + 82, COL_TEXT_DIM);
            drawText(r, font, "Brush Name: " + g_brushCreator.project.package.manifest.name, center.x + 12, center.y + 126, COL_TEXT_DIM);
            drawText(r, font, "Internal Id: " + g_brushCreator.project.package.manifest.id, center.x + 12, center.y + 148, COL_TEXT_DIM);
            drawText(r, font, "Author: " + g_brushCreator.project.package.manifest.author, center.x + 12, center.y + 170, COL_TEXT_DIM);
            drawText(r, font, "Brush Type: " + strova::brush::brushTypeName(g_brushCreator.project.package.manifest.type), center.x + 12, center.y + 192, COL_TEXT_DIM);
            drawText(r, font, "Stamp Type: " + std::string(g_brushCreator.project.package.stamp.empty() ? "none" : "rgba + mask"), center.x + 12, center.y + 214, COL_TEXT_DIM);
            drawText(r, font, "Mask Mode: " + strova::brush::maskSourceName(g_brushCreator.project.package.manifest.color.maskSource), center.x + 12, center.y + 236, COL_TEXT_DIM);
            drawText(r, font, "Blend Mode: " + strova::brush::blendModeName(g_brushCreator.project.package.manifest.params.blendMode), center.x + 12, center.y + 258, COL_TEXT_DIM);
            drawText(r, font, "Installed Status: local project", center.x + 12, center.y + 280, COL_TEXT_DIM);
            drawText(r, font, g_brushCreator.project.package.validation.summary(), center.x + 12, center.y + 320,
                g_brushCreator.project.package.validation.ok ? SDL_Color{ 118,218,154,255 } : SDL_Color{ 255,148,120,255 });
        }
        else if (g_brushCreator.activeTab == 1)
        {
            drawText(r, font, "Stamp", center.x + 12, center.y + 12, COL_TEXT_MAIN);
            SDL_Rect importR{ center.x + 12, center.y + 12, 120, 28 };
            SDL_Rect clearR{ center.x + 140, center.y + 12, 120, 28 };
            SDL_Rect alphaR{ center.x + 408, center.y + 12, 90, 28 };
            SDL_Rect lumaR{ center.x + 506, center.y + 12, 108, 28 };
            SDL_Rect darkR{ center.x + 622, center.y + 12, 108, 28 };
            SDL_Rect invertR{ center.x + 408, center.y + 48, 140, 28 };
            brushCreatorDrawButton(r, font, importR, "Import PNG...");
            brushCreatorDrawButton(r, font, clearR, "Clear Stamp");
            brushCreatorDrawButton(r, font, alphaR, "Alpha", g_brushCreator.project.package.manifest.color.maskSource == strova::brush::MaskSource::Alpha);
            brushCreatorDrawButton(r, font, lumaR, "Luminance", g_brushCreator.project.package.manifest.color.maskSource == strova::brush::MaskSource::Luminance);
            brushCreatorDrawButton(r, font, darkR, "Darkness", g_brushCreator.project.package.manifest.color.maskSource == strova::brush::MaskSource::Darkness);
            brushCreatorDrawButton(r, font, invertR, g_brushCreator.project.package.manifest.color.invertMask ? "Invert Mask: On" : "Invert Mask: Off", g_brushCreator.project.package.manifest.color.invertMask);

            SDL_Rect paneA{ center.x + 12, center.y + 84, (center.w - 36) / 2, 200 };
            SDL_Rect paneB{ paneA.x + paneA.w + 12, paneA.y, (center.w - 36) / 2, 200 };
            SDL_Rect paneC{ center.x + 12, paneA.y + paneA.h + 12, (center.w - 36) / 2, 200 };
            SDL_Rect paneD{ paneC.x + paneC.w + 12, paneC.y, (center.w - 36) / 2, 200 };
            brushCreatorDrawStampPane(r, font, paneA, "Raw Source", g_brushCreator.project.package.stamp.width, g_brushCreator.project.package.stamp.height,
                g_brushCreator.project.package.stamp.rgba, true, brushCreatorBgColor(2), "Original imported or generated RGBA.");
            brushCreatorDrawStampPane(r, font, paneB, "Interpreted Mask", g_brushCreator.project.package.stamp.width, g_brushCreator.project.package.stamp.height,
                brushCreatorMaskToRgba(g_brushCreator.project.package.stamp, 1.0f), false, SDL_Color{ 24, 26, 30, 255 }, "Neutral grayscale. No color tint.");
            brushCreatorDrawStampPane(r, font, paneC, "Mask Strength", g_brushCreator.project.package.stamp.width, g_brushCreator.project.package.stamp.height,
                brushCreatorMaskToRgba(g_brushCreator.project.package.stamp, 0.7f), false, SDL_Color{ 10, 10, 10, 255 }, "Darker = more paint. White = weaker.");
            brushCreatorDrawStampPane(r, font, paneD, "Final Dab Preview", g_brushCreator.project.package.preview.width, g_brushCreator.project.package.preview.height,
                g_brushCreator.project.package.preview.rgba, true, brushCreatorBgColor(2), "Tint/gradient belongs here, not in the mask pane.");

            drawText(r, font, "Alpha mode: alpha decides paint strength", center.x + 408, center.y + 92, COL_TEXT_DIM);
            drawText(r, font, "Luminance mode: white = more paint, black = less paint", center.x + 408, center.y + 114, COL_TEXT_DIM);
            drawText(r, font, "Darkness mode: black = more paint, white = less paint", center.x + 408, center.y + 136, COL_TEXT_DIM);

            auto& stamp = g_brushCreator.project.package.stamp;
            drawText(r, font, "Threshold: " + std::to_string((int)std::lround(stamp.threshold * 100.0f)) + "%", center.x + 408, center.y + 192, COL_TEXT_DIM);
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 408, center.y + 210, 24, 24 }, "-");
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 474, center.y + 210, 24, 24 }, "+");
            drawText(r, font, "Levels Clamp: " + std::to_string((int)std::lround(stamp.levelsClamp * 100.0f)) + "%", center.x + 408, center.y + 244, COL_TEXT_DIM);
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 408, center.y + 262, 24, 24 }, "-");
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 474, center.y + 262, 24, 24 }, "+");
            drawText(r, font, "Edge Boost: " + std::to_string((int)std::lround(stamp.edgeBoost * 100.0f)) + "%", center.x + 408, center.y + 296, COL_TEXT_DIM);
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 408, center.y + 314, 24, 24 }, "-");
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 474, center.y + 314, 24, 24 }, "+");

            static const char* names[9] = { "Hard Circle", "Soft Circle", "Square", "Soft Square", "Speckle", "Grainy Disk", "Oval Tip", "Chalk Patch", "Noise Blob" };
            for (int i = 0; i < 9; ++i)
            {
                SDL_Rect gr{ center.x + 12 + (i % 3) * 128, center.y + 304 + (i / 3) * 34, 120, 28 };
                brushCreatorDrawButton(r, font, gr, names[i]);
            }
        }
        else if (g_brushCreator.activeTab == 2)
        {
            const auto& p = g_brushCreator.project.package.manifest.params;
            drawText(r, font, "Behavior", center.x + 12, center.y + 12, COL_TEXT_MAIN);
            drawText(r, font, "Readable, live-updating sections. This affects Brush Tool only.", center.x + 12, center.y + 36, COL_TEXT_DIM);

            auto drawRow = [&](int row, const std::string& label, const std::string& value)
                {
                    const int y = center.y + 52 + row * 36;
                    drawText(r, font, label + ": " + value, center.x + 12, y + 2, COL_TEXT_DIM);
                    brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 208, y, 24, 24 }, "-");
                    brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 274, y, 24, 24 }, "+");
                };
            drawRow(0, "Default Size", std::to_string((int)std::lround(p.sizeDefault)));
            drawRow(1, "Spacing", std::to_string((int)std::lround(p.spacing * 100.0f)) + "%");
            drawRow(2, "Opacity", std::to_string((int)std::lround(p.opacity * 100.0f)) + "%");
            drawRow(3, "Flow", std::to_string((int)std::lround(p.flow * 100.0f)) + "%");
            drawRow(4, "Scatter", std::to_string((int)std::lround(p.scatter * 100.0f)) + "%");
            drawRow(5, "Smoothing", std::to_string((int)std::lround(p.smoothing * 100.0f)) + "%");
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 352, center.y + 52, 150, 28 }, "Rotation: " + strova::brush::rotationModeName(p.rotationMode));
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 352, center.y + 88, 150, 28 }, "Blend: " + strova::brush::blendModeName(p.blendMode));
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 352, center.y + 124, 150, 28 }, p.accumulate ? "Accumulate: On" : "Accumulate: Off", p.accumulate);
            drawText(r, font, "Pressure size influence: " + std::to_string((int)std::lround(p.pressureSize * 100.0f)) + "%", center.x + 352, center.y + 172, COL_TEXT_DIM);
            drawText(r, font, "Pressure opacity influence: " + std::to_string((int)std::lround(p.pressureOpacity * 100.0f)) + "%", center.x + 352, center.y + 194, COL_TEXT_DIM);
            drawText(r, font, "Pressure flow influence: " + std::to_string((int)std::lround(p.pressureFlow * 100.0f)) + "%", center.x + 352, center.y + 216, COL_TEXT_DIM);
        }
        else if (g_brushCreator.activeTab == 3)
        {
            auto& c = g_brushCreator.project.package.manifest.color;
            drawText(r, font, "Color", center.x + 12, center.y + 12, COL_TEXT_MAIN);
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 12, center.y + 12, 170, 28 }, c.supportsUserColor ? "Supports User Color: On" : "Supports User Color: Off", c.supportsUserColor);
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 190, center.y + 12, 170, 28 }, c.supportsGradient ? "Supports Gradient: On" : "Supports Gradient: Off", c.supportsGradient);
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 368, center.y + 12, 170, 28 }, "Fixed Color", !c.supportsUserColor);
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 12, center.y + 52, 220, 28 }, "Gradient Mode: " + strova::brush::gradientModeName(c.gradientMode));
            drawText(r, font, "Mask view stays neutral. Paint view is edited here.", center.x + 12, center.y + 90, COL_TEXT_DIM);
            SDL_Rect swatches[4] = {
                { center.x + 12, center.y + 122, 32, 32 },
                { center.x + 52, center.y + 122, 32, 32 },
                { center.x + 92, center.y + 122, 32, 32 },
                { center.x + 132, center.y + 122, 32, 32 }
            };
            for (int i = 0; i < 4; ++i)
            {
                fillRect(r, swatches[i], c.stops[(size_t)i].color);
                drawRect(r, swatches[i], COL_BORDER_SOFT);
                drawText(r, font, std::to_string(i + 1), swatches[i].x + 10, swatches[i].y + 7, SDL_Color{ 20,20,20,255 });
            }
            drawText(r, font, "Click a swatch to snap that stop to the current palette preview.", center.x + 12, center.y + 164, COL_TEXT_DIM);
            SDL_Rect previewR{ center.x + 12, center.y + 196, 240, 96 };
            fillRect(r, previewR, SDL_Color{ 22, 26, 30, 255 });
            drawRect(r, previewR, COL_BORDER_SOFT);
            for (int x = 0; x < previewR.w; ++x)
            {
                const float t = (float)x / (float)std::max(1, previewR.w - 1);
                SDL_Color col = strova::brush::sampleGradient(c, t);
                SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
                SDL_RenderDrawLine(r, previewR.x + x, previewR.y + 12, previewR.x + x, previewR.y + previewR.h - 12);
            }
        }
        else if (g_brushCreator.activeTab == 4)
        {
            const bool scripted = g_brushCreator.project.package.manifest.type == strova::brush::BrushType::ScriptedRaster;
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 12, center.y + 12, 180, 28 }, scripted ? "Scripted Raster: On" : "Enable Scripted Raster", scripted);
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 200, center.y + 12, 110, 28 }, "Load Lua");
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 318, center.y + 12, 110, 28 }, "Save Lua");
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 436, center.y + 12, 150, 28 }, "Insert Template");
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 594, center.y + 12, 90, 28 }, "Clear");
            drawText(r, font, scripted ? "Script validation applies only when scripted mode is enabled." : "Script stays quiet and secondary for non-scripted brushes.", center.x + 12, center.y + 56, COL_TEXT_DIM);
            drawText(r, font, "behavior.lua sandbox  |  Ctrl+S saves the .sbrushproj", center.x + 12, center.y + 82, COL_TEXT_MAIN);
            SDL_Rect textR = brushCreatorScriptEditorRect(rc);
            fillRect(r, textR, SDL_Color{ 18, 22, 30, 255 });
            drawRect(r, textR, g_brushCreator.scriptFocused ? SDL_Color{ 110,160,255,255 } : COL_BORDER_SOFT);
            std::vector<std::string> lines;
            {
                std::string cur;
                for (char cch : g_brushCreator.project.package.scriptSource)
                {
                    if (cch == '\n') { lines.push_back(cur); cur.clear(); }
                    else if (cch != '\r') cur.push_back(cch);
                }
                lines.push_back(cur);
            }
            if (lines.empty()) lines.push_back("");
            const int lineH = 18;
            int drawY = textR.y + 8;
            for (int i = 0; i < (int)lines.size() && drawY + lineH <= textR.y + textR.h - 6; ++i)
            {
                drawText(r, font, std::to_string(i + 1), textR.x + 8, drawY, SDL_Color{ 118,126,146,255 });
                drawText(r, font, lines[(size_t)i], textR.x + 40, drawY, COL_TEXT_DIM);
                drawY += lineH;
            }
            std::size_t cursor = std::min<std::size_t>(g_brushCreator.scriptCursor, g_brushCreator.project.package.scriptSource.size());
            int curLine = 0;
            int curCol = 0;
            for (std::size_t i = 0; i < cursor; ++i)
            {
                if (g_brushCreator.project.package.scriptSource[i] == '\n') { ++curLine; curCol = 0; }
                else ++curCol;
            }
            if (g_brushCreator.scriptFocused)
            {
                SDL_Rect caret{ textR.x + 40 + curCol * 8, textR.y + 8 + curLine * lineH, 2, lineH - 2 };
                if (caret.y + caret.h <= textR.y + textR.h - 4)
                    fillRect(r, caret, SDL_Color{ 230,236,248,255 });
            }
        }
        else if (g_brushCreator.activeTab == 5)
        {
            drawText(r, font, "Test Canvas", center.x + 12, center.y + 12, COL_TEXT_MAIN);
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 12, center.y + 12, 100, 28 }, "Clear Canvas");
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 120, center.y + 12, 130, 28 }, "Replay Sample");
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 258, center.y + 12, 120, 28 }, std::string("Background: ") + brushCreatorBgLabel(g_brushCreator.testBgMode));
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 386, center.y + 12, 120, 28 }, g_brushCreator.testUseGradient ? "Gradient: On" : "Gradient: Off", g_brushCreator.testUseGradient);
            brushCreatorDrawButton(r, font, SDL_Rect{ center.x + 514, center.y + 12, 150, 28 }, g_brushCreator.testPressure ? "Pressure Sim: On" : "Pressure Sim: Off", g_brushCreator.testPressure);
            SDL_Rect colorR[4] = {
                { center.x + 12, center.y + 48, 28, 28 },
                { center.x + 48, center.y + 48, 28, 28 },
                { center.x + 84, center.y + 48, 28, 28 },
                { center.x + 120, center.y + 48, 28, 28 }
            };
            static SDL_Color swatches[4] = {
                SDL_Color{20,20,20,255}, SDL_Color{255,108,92,255}, SDL_Color{120,205,255,255}, SDL_Color{120,216,150,255}
            };
            for (int i = 0; i < 4; ++i) { fillRect(r, colorR[i], swatches[i]); drawRect(r, colorR[i], COL_BORDER_SOFT); }
            SDL_Rect canvasR = brushCreatorTestCanvasRect(rc);
            if (g_brushCreator.testBgMode == 2) brushCreatorDrawChecker(r, canvasR);
            else fillRect(r, canvasR, brushCreatorBgColor(g_brushCreator.testBgMode));
            drawRect(r, canvasR, SDL_Color{ 100, 112, 144, 255 });
            if (app.brushRendererHandle())
            {
                SDL_RenderSetClipRect(r, &canvasR);
                for (const Stroke& s2 : g_brushCreator.testStrokes)
                    app.brushRendererHandle()->drawStrokeWithPackage(s2, &g_brushCreator.project.package, 1.0f, 0.0f, 0.0f, canvasR.x, canvasR.y);
                if (g_brushCreator.testDrawing && !g_brushCreator.liveTestStroke.points.empty())
                    app.brushRendererHandle()->drawStrokeWithPackage(g_brushCreator.liveTestStroke, &g_brushCreator.project.package, 1.0f, 0.0f, 0.0f, canvasR.x, canvasR.y);
                SDL_RenderSetClipRect(r, nullptr);
            }
            drawText(r, font, "This sandbox uses the brush runtime path and does not touch project layers.", center.x + 12, center.y + center.h - 22, COL_TEXT_DIM);
        }
        else if (g_brushCreator.activeTab == 6)
        {
            drawText(r, font, "Package", center.x + 12, center.y + 12, COL_TEXT_MAIN);
            SDL_Rect saveR{ center.x + 140, center.y + 12, 120, 28 };
            SDL_Rect validateR{ center.x + 268, center.y + 12, 120, 28 };
            SDL_Rect exportR{ center.x + 396, center.y + 12, 120, 28 };
            SDL_Rect installR{ center.x + 524, center.y + 12, 120, 28 };
            brushCreatorDrawButton(r, font, saveR, "Save Project");
            brushCreatorDrawButton(r, font, validateR, "Validate");
            brushCreatorDrawButton(r, font, exportR, "Export...");
            brushCreatorDrawButton(r, font, installR, "Install...");
            drawText(r, font, "Manifest Summary", center.x + 12, center.y + 62, COL_TEXT_MAIN);
            drawText(r, font, "Brush Type: " + strova::brush::brushTypeName(g_brushCreator.project.package.manifest.type), center.x + 12, center.y + 88, COL_TEXT_DIM);
            drawText(r, font, "Stamp: " + std::to_string(g_brushCreator.project.package.stamp.width) + " x " + std::to_string(g_brushCreator.project.package.stamp.height), center.x + 12, center.y + 110, COL_TEXT_DIM);
            drawText(r, font, "Project File: " + (g_brushCreator.project.projectPath.empty() ? std::string("unsaved") : g_brushCreator.project.projectPath), center.x + 12, center.y + 132, COL_TEXT_DIM);
            drawText(r, font, "Validation", center.x + 12, center.y + 172, COL_TEXT_MAIN);
            drawText(r, font, g_brushCreator.project.package.validation.summary(), center.x + 12, center.y + 198,
                g_brushCreator.project.package.validation.ok ? SDL_Color{ 118,218,154,255 } : SDL_Color{ 255,148,120,255 });
        }

        drawText(r, font, std::string("Tab: ") + brushCreatorTabName(g_brushCreator.activeTab) + "  |  Mask: " + brushCreatorMaskModeLabel(g_brushCreator.project.package.manifest.color.maskSource) +
            "  |  Script: " + (g_brushCreator.project.package.manifest.type == strova::brush::BrushType::ScriptedRaster ? "enabled" : "disabled") +
            "  |  Status: " + g_brushCreator.status, status.x + 8, status.y + 1, COL_TEXT_DIM);

        if (g_brushCreator.modal != BrushCreatorModalType::None)
        {
            SDL_Rect modal{ rc.x + rc.w / 2 - 220, rc.y + rc.h / 2 - 150, 440, 300 };
            fillRect(r, modal, SDL_Color{ 16, 18, 26, 248 });
            drawRect(r, modal, SDL_Color{ 118, 130, 160, 240 });
            SDL_Rect okR{ modal.x + modal.w - 222, modal.y + modal.h - 38, 96, 26 };
            SDL_Rect cancelR{ modal.x + modal.w - 116, modal.y + modal.h - 38, 96, 26 };
            std::string title = "Dialog";
            if (g_brushCreator.modal == BrushCreatorModalType::Validation) title = "Validation Results";
            else if (g_brushCreator.modal == BrushCreatorModalType::ImportStamp) title = "Import Stamp";
            else if (g_brushCreator.modal == BrushCreatorModalType::ExportBrush) title = "Export .sbrush";
            else if (g_brushCreator.modal == BrushCreatorModalType::InstallBrush) title = "Install Local";
            else if (g_brushCreator.modal == BrushCreatorModalType::NewBrush) title = "New Brush";
            drawText(r, font, title, modal.x + 16, modal.y + 12, COL_TEXT_MAIN);
            if (g_brushCreator.modal == BrushCreatorModalType::Validation)
            {
                drawText(r, font, g_brushCreator.modalText, modal.x + 16, modal.y + 48, COL_TEXT_DIM);
                brushCreatorDrawButton(r, font, okR, "OK");
            }
            else if (g_brushCreator.modal == BrushCreatorModalType::ImportStamp)
            {
                brushCreatorDrawButton(r, font, SDL_Rect{ modal.x + 16, modal.y + 44, 140, 28 }, "Choose PNG...");
                drawText(r, font, g_brushCreator.pendingImportPath.empty() ? "No file selected" : g_brushCreator.pendingImportPath, modal.x + 168, modal.y + 50, COL_TEXT_DIM);
                SDL_Rect previewR{ modal.x + 16, modal.y + 82, 180, 116 };
                brushCreatorDrawPixels(r, previewR, g_brushCreator.pendingImportStamp.width, g_brushCreator.pendingImportStamp.height, g_brushCreator.pendingImportStamp.rgba, true, brushCreatorBgColor(2));
                drawText(r, font, "Interpretation", modal.x + 16, modal.y + 214, COL_TEXT_MAIN);
                brushCreatorDrawButton(r, font, SDL_Rect{ modal.x + 16, modal.y + 214, 96, 26 }, "Alpha", g_brushCreator.pendingImportMaskSource == strova::brush::MaskSource::Alpha);
                brushCreatorDrawButton(r, font, SDL_Rect{ modal.x + 120, modal.y + 214, 110, 26 }, "Luminance", g_brushCreator.pendingImportMaskSource == strova::brush::MaskSource::Luminance);
                brushCreatorDrawButton(r, font, SDL_Rect{ modal.x + 238, modal.y + 214, 110, 26 }, "Darkness", g_brushCreator.pendingImportMaskSource == strova::brush::MaskSource::Darkness);
                brushCreatorDrawButton(r, font, SDL_Rect{ modal.x + 16, modal.y + 248, 140, 26 }, g_brushCreator.pendingImportInvert ? "Invert: On" : "Invert: Off", g_brushCreator.pendingImportInvert);
                drawText(r, font, g_brushCreator.modalText, modal.x + 204, modal.y + 116, COL_TEXT_DIM);
                brushCreatorDrawButton(r, font, okR, "Import");
                brushCreatorDrawButton(r, font, cancelR, "Cancel");
            }
            else if (g_brushCreator.modal == BrushCreatorModalType::ExportBrush)
            {
                drawText(r, font, "Package summary", modal.x + 16, modal.y + 48, COL_TEXT_MAIN);
                drawText(r, font, g_brushCreator.project.package.manifest.name, modal.x + 16, modal.y + 74, COL_TEXT_DIM);
                drawText(r, font, g_brushCreator.project.package.validation.summary(), modal.x + 16, modal.y + 98, COL_TEXT_DIM);
                drawText(r, font, "Confirm to choose a destination and export the .sbrush package.", modal.x + 16, modal.y + 132, COL_TEXT_DIM);
                brushCreatorDrawButton(r, font, okR, "Export");
                brushCreatorDrawButton(r, font, cancelR, "Cancel");
            }
            else if (g_brushCreator.modal == BrushCreatorModalType::InstallBrush)
            {
                drawText(r, font, "Install target", modal.x + 16, modal.y + 48, COL_TEXT_MAIN);
                drawText(r, font, strova::brush::userDir().string(), modal.x + 16, modal.y + 74, COL_TEXT_DIM);
                drawText(r, font, g_brushCreator.project.package.validation.summary(), modal.x + 16, modal.y + 106, COL_TEXT_DIM);
                brushCreatorDrawButton(r, font, okR, "Install");
                brushCreatorDrawButton(r, font, cancelR, "Cancel");
            }
            else if (g_brushCreator.modal == BrushCreatorModalType::NewBrush)
            {
                brushCreatorDrawButton(r, font, SDL_Rect{ modal.x + 16, modal.y + 54, 170, 28 }, "Type: " + std::string(strova::brush::brushTypeName(g_brushCreator.pendingNewType)));
                brushCreatorDrawButton(r, font, SDL_Rect{ modal.x + 16, modal.y + 92, 170, 28 }, "Generator: " + std::string(brushCreatorGeneratorLabel(g_brushCreator.pendingNewGenerator)));
                drawText(r, font, "Create a fresh brush project window with a real starting stamp.", modal.x + 16, modal.y + 138, COL_TEXT_DIM);
                brushCreatorDrawButton(r, font, okR, "Create");
                brushCreatorDrawButton(r, font, cancelR, "Cancel");
            }
        }
    }
#endif

    static void selectBrushIntoTool(App& app, const strova::brush::BrushPackage& pkg)
    {
        auto& s = app.toolBank.get(ToolType::Brush);
        s.brushId = pkg.manifest.id;
        s.brushDisplayName = pkg.manifest.name;
        s.brushVersion = pkg.manifest.version;
        s.brushSupportsUserColor = pkg.manifest.color.supportsUserColor;
        s.brushSupportsGradient = pkg.manifest.color.supportsGradient;
        app.brushManager().select(pkg.manifest.id);
        if (app.getEditorUiState().activeTool == ToolType::Brush)
        {
            app.replaceToolSettingsCommand(ToolType::Brush, s);
            app.getEngine().setBrushSelection(s.brushId, s.brushVersion, s.brushDisplayName);
        }
    }

    static void handleBrushPanelActionImpl(App& app, ToolOptionsPanel& optionsPanel)
    {
        ToolOptionsPanel::PanelAction action{};
        std::string brushId;
        if (!optionsPanel.consumeAction(action, brushId))
            return;

        switch (action)
        {
        case ToolOptionsPanel::PanelAction::OpenBrushCreator:
            app.openBrushCreatorWorkspace("", true);
            break;

        case ToolOptionsPanel::PanelAction::ManageBrushes:
            g_brushResourceManager.open(
                brushId.empty() ? app.toolBank.get(ToolType::Brush).brushId : brushId);
            break;

        case ToolOptionsPanel::PanelAction::InstallBrush:
        {
            std::string path;
            if (platform::pickOpenBrushOrProject(path))
            {
                std::string err;
                std::string installed;
                const std::filesystem::path pth(path);
                if (pth.extension() == ".sbrushproj" || pth.extension() == ".png")
                {
                    SDL_ShowSimpleMessageBox(
                        SDL_MESSAGEBOX_INFORMATION,
                        "Strova Brush Install",
                        "Editor brush panel only installs ready .sbrush packages now. Open .sbrushproj or PNG stamp from the Launcher Brush Creator.",
                        app.windowHandle());
                }
                else if (!app.brushManager().installPackageFile(path, installed, err))
                {
                    SDL_ShowSimpleMessageBox(
                        SDL_MESSAGEBOX_ERROR,
                        "Strova Brush Install",
                        err.c_str(),
                        app.windowHandle());
                }
                else
                {
                    if (const auto* pkg = app.brushManager().selected())
                        selectBrushIntoTool(app, *pkg);

                    SDL_ShowSimpleMessageBox(
                        SDL_MESSAGEBOX_INFORMATION,
                        "Strova Brush Install",
                        "Brush installed.",
                        app.windowHandle());
                }
            }
            break;
        }

        case ToolOptionsPanel::PanelAction::ExportSelectedBrush:
        {
            const auto* pkg = app.brushManager().findById(brushId);
            if (!pkg)
                break;

            std::string outPath;
            if (platform::pickSaveBrushFile(outPath, pkg->manifest.name))
            {
                std::string err;
                if (!app.brushManager().exportPackage(*pkg, outPath, err))
                {
                    SDL_ShowSimpleMessageBox(
                        SDL_MESSAGEBOX_ERROR,
                        "Strova Brush Export",
                        err.c_str(),
                        app.windowHandle());
                }
                else
                {
                    SDL_ShowSimpleMessageBox(
                        SDL_MESSAGEBOX_INFORMATION,
                        "Strova Brush Export",
                        "Brush exported.",
                        app.windowHandle());
                }
            }
            break;
        }

        case ToolOptionsPanel::PanelAction::RefreshBrushes:
        {
            const std::string preferredId = app.toolBank.get(ToolType::Brush).brushId;
            app.brushManager().refresh();
            if (!preferredId.empty())
                app.brushManager().select(preferredId);

            if (const auto* pkg = app.brushManager().selected())
                selectBrushIntoTool(app, *pkg);

            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_INFORMATION,
                "Strova Brush Browser",
                "Brush catalog refreshed.",
                app.windowHandle());
            break;
        }

        default:
            break;
        }
    }

    static bool pointInRect(int x, int y, const SDL_Rect& r)
    {
        if (r.w <= 0 || r.h <= 0) return false;
        return x >= r.x && x <= (r.x + r.w) && y >= r.y && y <= (r.y + r.h);
    }

    static std::vector<const strova::plugin::RuntimeRecord*> pluginLoadedRuntimeRecords(const App& app)
    {
        std::vector<const strova::plugin::RuntimeRecord*> out;
        for (const auto& record : app.pluginManager().records())
        {
            if (record.state == strova::plugin::RuntimeState::Loaded)
                out.push_back(&record);
        }
        std::sort(out.begin(), out.end(), [](const auto* a, const auto* b)
            {
                const std::string an = !a->query.displayName.empty() ? a->query.displayName : a->package.manifest.name;
                const std::string bn = !b->query.displayName.empty() ? b->query.displayName : b->package.manifest.name;
                return an < bn;
            });
        return out;
    }

    static const char* pluginRuntimeStateLabel(strova::plugin::RuntimeState state)
    {
        using strova::plugin::RuntimeState;
        switch (state)
        {
        case RuntimeState::Discovered: return "Discovered";
        case RuntimeState::Disabled:   return "Disabled";
        case RuntimeState::Loaded:     return "Loaded";
        case RuntimeState::Faulted:    return "Faulted";
        case RuntimeState::Invalid:    return "Invalid";
        case RuntimeState::Missing:    return "Missing";
        default:                       return "Unknown";
        }
    }

    static std::string pluginMaskSummary(std::uint64_t mask, bool permissions)
    {
        using strova::plugin::Capability;
        using strova::plugin::Permission;
        std::vector<std::string> names;
        if (permissions)
        {
            const Permission values[] = {
                Permission::ProjectRead,
                Permission::ProjectWrite,
                Permission::PluginStorage,
                Permission::BulkDeleteProjectFiles,
                Permission::OutsideProjectFs
            };
            for (Permission value : values)
            {
                if (strova::plugin::hasPermission(mask, value))
                    names.emplace_back(strova::plugin::permissionName(value));
            }
        }
        else
        {
            const Capability values[] = {
                Capability::Commands,
                Capability::DockPanels,
                Capability::Importers,
                Capability::Exporters,
                Capability::ProjectContent,
                Capability::FlowProcessors,
                Capability::FlowLinkProcessors,
                Capability::CanvasOverlays,
                Capability::BrushEffects,
                Capability::StrokeEffects,
                Capability::AnalysisTools,
                Capability::LayerTools,
                Capability::ExportPasses,
                Capability::DocumentValidators
            };
            for (Capability value : values)
            {
                if (strova::plugin::hasCapability(mask, value))
                    names.emplace_back(strova::plugin::capabilityName(value));
            }
        }

        if (names.empty()) return permissions ? "Permissions: none" : "Capabilities: none";

        std::string joined = permissions ? "Permissions: " : "Capabilities: ";
        for (std::size_t i = 0; i < names.size(); ++i)
        {
            if (i > 0) joined += ", ";
            joined += names[i];
            if (joined.size() > 92 && i + 1 < names.size())
            {
                joined += ", ...";
                break;
            }
        }
        return joined;
    }

    static SDL_Rect pluginLoadedMenuRectForCount(const SDL_Rect& parentMenu, int itemCount)
    {
        const int rowH = 32;
        const int minRows = std::max(1, itemCount);
        return SDL_Rect{ parentMenu.x + parentMenu.w + 4, parentMenu.y + rowH, 230, minRows * rowH };
    }

    static bool openPluginFromLoadedSubmenu(App& app, const strova::plugin::RuntimeRecord& record, const SDL_Rect& workspace)
    {
        const auto& regs = app.pluginManager().registries();
        const auto& panels = regs.dockPanels.items();
        for (const auto& panel : panels)
        {
            if (panel.ownerPluginId != record.package.manifest.id) continue;
            app.dockManager().restorePanel(panel.id, workspace);
            app.saveDockLayoutForCurrentContext();
            return true;
        }
        const auto& commands = regs.commands.items();
        for (const auto& command : commands)
        {
            if (command.ownerPluginId != record.package.manifest.id) continue;
            std::string err;
            app.pluginManager().invokeCommand(command.id, err);
            return true;
        }
        app.dockManager().restorePanel("Plugins", workspace);
        app.saveDockLayoutForCurrentContext();
        return false;
    }

    static bool pluginMenuShouldStayOpen(int mx, int my, const SDL_Rect& pluginBtnR, const SDL_Rect& pluginMenuR, bool pluginLoadedMenuOpen, const SDL_Rect& pluginLoadedMenuR)
    {
        if (pointInRect(mx, my, pluginBtnR)) return true;
        if (pointInRect(mx, my, pluginMenuR)) return true;
        if (pluginLoadedMenuOpen && pointInRect(mx, my, pluginLoadedMenuR)) return true;
        SDL_Rect loadedItem{ pluginMenuR.x, pluginMenuR.y + 32, pluginMenuR.w, 32 };
        if (pointInRect(mx, my, loadedItem)) return true;
        return false;
    }


    static void clearStrokePreview()
    {
        g_strokePreviewActive = false;
        g_strokePreviewTool = ToolType::Brush;
        g_strokePreviewPoints.clear();
        g_strokePreviewColor = SDL_Color{ 0,0,0,255 };
        g_strokePreviewThickness = 1.0f;
        g_strokePreviewGradient = GradientConfig{};
    }

    static void appendStrokePreviewPoint(const SDL_FPoint& pt)
    {
        StrokePoint sp{};
        sp.x = pt.x;
        sp.y = pt.y;
        sp.pressure = 1.0f;
        if (!g_strokePreviewPoints.empty())
        {
            const StrokePoint& last = g_strokePreviewPoints.back();
            const float dx = sp.x - last.x;
            const float dy = sp.y - last.y;
            if ((dx * dx + dy * dy) < 0.0004f)
                return;
        }
        g_strokePreviewPoints.push_back(sp);
    }

    static void beginStrokePreview(App& app, ToolType tool, const ToolSettings& settings, const SDL_FPoint& pt)
    {
        clearStrokePreview();
        g_strokePreviewActive = true;
        g_strokePreviewTool = tool;
        g_strokePreviewColor = app.getEngine().getBrushColor();
        g_strokePreviewThickness = app.getEngine().getBrushSize();
        g_strokePreviewGradient = app.getEngine().getGradientConfig();
        if (settings.size > 0.0f)
            g_strokePreviewThickness = settings.size;
        appendStrokePreviewPoint(pt);
    }

    static bool commitStrokePreview(App& app, const ToolSettings& settings)
    {
        if (!g_strokePreviewActive || g_strokePreviewPoints.empty())
        {
            clearStrokePreview();
            return false;
        }

        app.getEngine().setTool(g_strokePreviewTool);
        app.getEngine().setToolSettings(settings);
        app.getEngine().setGradientConfig(g_strokePreviewGradient);
        app.getEngine().setColor(g_strokePreviewColor);

        const StrokePoint first = g_strokePreviewPoints.front();
        app.getEngine().beginStroke(first.x, first.y);
        if (g_strokePreviewPoints.size() == 1)
        {
            app.getEngine().addPoint(first.x, first.y);
        }
        else
        {
            for (size_t i = 1; i < g_strokePreviewPoints.size(); ++i)
                app.getEngine().addPoint(g_strokePreviewPoints[i].x, g_strokePreviewPoints[i].y);
        }
        app.getEngine().endStroke();
        clearStrokePreview();
        return true;
    }

    struct LayerPanelLayout
    {
        SDL_Rect panel{};
        SDL_Rect header{};
        SDL_Rect rows{};
        SDL_Rect addBtn{};
        SDL_Rect childBtn{};
        SDL_Rect groupBtn{};
        SDL_Rect upBtn{};
        SDL_Rect downBtn{};
        SDL_Rect deleteBtn{};
        SDL_Rect copyBtn{};
        SDL_Rect pasteBtn{};
        SDL_Rect nextBtn{};
    };

    static int g_layerPanelScroll = 0;
    static constexpr int kLayerPanelWidth = 318;
    static constexpr int kLayerPanelGap = 8;

    struct LayerClipboardItem
    {
        int trackId = 0;
        DrawingEngine::TrackLayer layer;
    };

    static std::vector<LayerClipboardItem> g_layerClipboard;

    static strova::TimelineTrack* findUiTrackByEngineTrackId(App& app, int engineTrackId)
    {
        for (auto& t : app.timeline.state().tracks)
            if (t.engineTrackId == engineTrackId)
                return &t;
        return nullptr;
    }

    static const strova::TimelineTrack* findUiTrackByEngineTrackId(const App& app, int engineTrackId)
    {
        for (const auto& t : app.timeline.state().tracks)
            if (t.engineTrackId == engineTrackId)
                return &t;
        return nullptr;
    }

    static strova::TimelineTrack* findUiTrackByName(App& app, const char* name)
    {
        if (!name) return nullptr;
        for (auto& t : app.timeline.state().tracks)
            if (t.name == name)
                return &t;
        return nullptr;
    }

    static const strova::TimelineTrack* focusedUiTrack(const App& app)
    {
        if (!app.timeline.isFocused())
            return nullptr;
        return app.timeline.findTrack(app.timeline.focusedTrackId());
    }

    static bool isFlowLinkUiTrack(const strova::TimelineTrack* tr)
    {
        return tr && (tr->kind == strova::TrackKind::FlowLink || tr->name == "FlowLink" || tr->name == "Keyframe");
    }

    static bool isKeyframeUiTrack(const strova::TimelineTrack* tr)
    {
        return false;
    }

    static bool isKeyframeModeActive(const App&)
    {
        return false;
    }

    static void closeKeyframeActionModal();

    static void syncEditorModeFlags(const App& app)
    {
        g_keyframePanelFocused = isKeyframeModeActive(app);
        if (!g_keyframePanelFocused)
            closeKeyframeActionModal();
    }

    static LayerPanelLayout buildLayerPanelLayout(const SDL_Rect& contentRc)
    {
        LayerPanelLayout l{};
        l.panel = contentRc;
        l.header = { l.panel.x + 6, l.panel.y + 2, std::max(0, l.panel.w - 12), 22 };

        const int btnW = 38;
        const int btnH = 22;
        const int gap = 4;
        const int by0 = l.header.y + l.header.h + 4;
        const int by1 = by0 + btnH + 6;

        l.addBtn = { l.panel.x + 8 + (btnW + gap) * 0, by0, btnW, btnH };
        l.childBtn = { l.panel.x + 8 + (btnW + gap) * 1, by0, btnW, btnH };
        l.groupBtn = { l.panel.x + 8 + (btnW + gap) * 2, by0, btnW, btnH };
        l.upBtn = { l.panel.x + 8 + (btnW + gap) * 3, by0, btnW, btnH };
        l.downBtn = { l.panel.x + 8 + (btnW + gap) * 4, by0, btnW, btnH };
        l.deleteBtn = { l.panel.x + 8 + (btnW + gap) * 5, by0, btnW, btnH };
        l.copyBtn = { l.panel.x + 8 + (btnW + gap) * 0, by1, 54, btnH };
        l.pasteBtn = { l.copyBtn.x + l.copyBtn.w + gap, by1, 54, btnH };
        l.nextBtn = { l.pasteBtn.x + l.pasteBtn.w + gap, by1, 98, btnH };

        l.rows = { l.panel.x + 6, by1 + btnH + 8, std::max(0, l.panel.w - 12), l.panel.h - (by1 + btnH + 8 - l.panel.y) - 6 };
        if (l.rows.h < 48) l.rows.h = 48;
        return l;
    }

    static SDL_Rect layerRowRect(const LayerPanelLayout& layout, int rowIndex, int maxScroll)
    {
        const int rowH = 36;
        return SDL_Rect{ layout.rows.x, layout.rows.y + rowIndex * rowH - g_layerPanelScroll, layout.rows.w - (maxScroll > 0 ? 8 : 0), rowH };
    }

    static SDL_Rect layerThumbRect(const SDL_Rect& rr)
    {
        return SDL_Rect{ rr.x + 6, rr.y + 6, 28, std::max(20, rr.h - 12) };
    }

    static SDL_Rect layerEyeRect(const SDL_Rect& rr)
    {
        return SDL_Rect{ rr.x + rr.w - 64, rr.y + 9, 14, 14 };
    }

    static SDL_Rect layerLockRect(const SDL_Rect& rr)
    {
        return SDL_Rect{ rr.x + rr.w - 46, rr.y + 9, 14, 14 };
    }

    static SDL_Rect layerFocusRect(const SDL_Rect& rr)
    {
        return SDL_Rect{ rr.x + rr.w - 28, rr.y + 9, 14, 14 };
    }

    static SDL_Rect expandHitRect(const SDL_Rect& rc, int padX, int padY)
    {
        return SDL_Rect{ rc.x - padX, rc.y - padY, rc.w + padX * 2, rc.h + padY * 2 };
    }

    static int layerRowAtPoint(const App& app, const LayerPanelLayout& layout, int mx, int my)
    {
        if (!pointInRect(mx, my, layout.rows)) return 0;
        const auto rows = activeLayerTree(app).buildRows();
        const int rowH = 36;
        int localY = my - layout.rows.y + g_layerPanelScroll;
        int idx = localY / rowH;
        if (idx < 0 || idx >= (int)rows.size()) return 0;
        return rows[(size_t)idx].nodeId;
    }

    static void syncPrimaryLayerSelectionToEngine(App& app)
    {
        activeLayerTree(app).syncExistingFromTimeline(app.timeline);
        app.refreshLayerPanelForActiveFrame();
        int trackId = activeLayerTree(app).primarySelectedTrackId();
        if (trackId != 0)
            app.getEngine().setActiveTrack(trackId);
    }

    static std::vector<int> orderedDrawTrackIdsFromLayerTree(const App& app)
    {
        std::vector<int> out;
        for (const auto& row : activeLayerTree(app).buildRows())
        {
            const auto* node = activeLayerTree(app).findNode(row.nodeId);
            if (node && !node->isGroup && node->trackId != 0)
                out.push_back(node->trackId);
        }
        return out;
    }

    static void syncDrawTrackOrderFromLayerTree(App& app)
    {
        const std::vector<int> drawOrder = orderedDrawTrackIdsFromLayerTree(app);
        if (drawOrder.empty()) return;

        std::vector<DrawingEngine::TrackId> engineOrder;
        for (int id : drawOrder) engineOrder.push_back(id);
        for (const auto& tr : app.getEngine().getTracks())
        {
            bool found = false;
            for (int id : drawOrder)
            {
                if (id == tr.id) { found = true; break; }
            }
            if (!found) engineOrder.push_back(tr.id);
        }
        app.getEngine().setTrackOrder(engineOrder);

        auto& uiTracks = app.timeline.state().tracks;
        std::vector<strova::TimelineTrack> drawTracks;
        std::vector<strova::TimelineTrack> others;
        for (const auto& tr : uiTracks)
        {
            if (tr.kind == strova::TrackKind::Draw) drawTracks.push_back(tr);
            else others.push_back(tr);
        }

        std::vector<strova::TimelineTrack> reordered;
        reordered.reserve(uiTracks.size());
        for (int engineTrackId : drawOrder)
        {
            for (const auto& tr : drawTracks)
            {
                if (tr.engineTrackId == engineTrackId)
                {
                    reordered.push_back(tr);
                    break;
                }
            }
        }
        for (const auto& tr : drawTracks)
        {
            bool found = false;
            for (int engineTrackId : drawOrder)
            {
                if (tr.engineTrackId == engineTrackId) { found = true; break; }
            }
            if (!found) reordered.push_back(tr);
        }
        reordered.insert(reordered.end(), others.begin(), others.end());
        if (reordered.size() == uiTracks.size())
            uiTracks = std::move(reordered);
    }

    static void moveSelectedLayerFromPanel(App& app, int dir)
    {
        bool moved = (dir < 0) ? activeLayerTree(app).movePrimarySelectionUp() : activeLayerTree(app).movePrimarySelectionDown();
        if (!moved) return;
        app.refreshLayerPanelForActiveFrame();
        syncDrawTrackOrderFromLayerTree(app);
        app.dirtyAllThumbs();
        app.requestSaveProjectNow();
    }

    static void copySelectedLayersToClipboard(App& app)
    {
        g_layerClipboard.clear();
        const size_t fi = app.getEngine().getCurrentFrameIndex();
        for (int trackId : activeLayerTree(app).selectedTrackIds())
        {
            LayerClipboardItem item;
            item.trackId = trackId;
            item.layer = app.getEngine().getEvaluatedFrameTrackLayerCopy(fi, trackId);
            g_layerClipboard.push_back(std::move(item));
        }
    }

    static void pasteClipboardToSelectedLayers(App& app)
    {
        if (g_layerClipboard.empty()) return;
        const size_t fi = app.getEngine().getCurrentFrameIndex();
        std::vector<int> selected = activeLayerTree(app).selectedTrackIds();
        if (selected.empty()) return;

        auto applyLayer = [&](int dstTrackId, const DrawingEngine::TrackLayer& src)
            {
                app.getEngine().setFrameTrackStrokes(fi, dstTrackId, src.strokes);
                app.getEngine().setFrameTrackTransform(fi, dstTrackId, src.transform);
                if (!src.image.empty())
                    app.getEngine().setFrameTrackImage(fi, dstTrackId, src.image, src.transform);
                else
                    app.getEngine().clearFrameTrackImage(fi, dstTrackId);
            };

        if (selected.size() == g_layerClipboard.size())
        {
            for (size_t i = 0; i < selected.size(); ++i)
                applyLayer(selected[i], g_layerClipboard[i].layer);
        }
        else if (!g_layerClipboard.empty())
        {
            for (int trackId : selected)
                applyLayer(trackId, g_layerClipboard.front().layer);
        }
        app.markFrameEditedAndSave(fi);
    }

    static void copySelectedLayersToNextFrame(App& app)
    {
        const size_t cur = app.getEngine().getCurrentFrameIndex();
        size_t next = cur + 1;
        if (next >= app.getEngine().getFrameCount())
        {
            app.storeCurrentDrawFrameLayerTree();
            app.getEngine().addFrame();
            app.initFreshLayerTreeForFrame(next);
        }

        for (int trackId : activeLayerTree(app).selectedTrackIds())
        {
            const DrawingEngine::TrackLayer layer = app.getEngine().getEvaluatedFrameTrackLayerCopy(cur, trackId);
            if (layer.trackId != 0)
            {
                app.getEngine().setFrameTrackStrokes(next, trackId, layer.strokes);
                app.getEngine().setFrameTrackTransform(next, trackId, layer.transform);
                if (!layer.image.empty())
                    app.getEngine().setFrameTrackImage(next, trackId, layer.image, layer.transform);
                else
                    app.getEngine().clearFrameTrackImage(next, trackId);
            }
        }

        app.switchToFrameIndex(next);
        app.timeline.setTotalFrames(std::max(1, (int)app.getEngine().getFrameCount()));
        app.dirtyAllThumbs();
        app.requestSaveProjectNow();
    }

    static void createLayerFromPanel(App& app, bool asChild)
    {
        int parentId = asChild ? activeLayerTree(app).selectedParentForNewNode() : 0;

        int ordinal = 1;
        for (const auto& n : activeLayerTree(app).getNodes())
            if (!n.isGroup) ordinal++;

        std::string name = std::string("Layer ") + std::to_string(ordinal);
        int uiTrackId = app.timeline.addTrack(strova::TrackKind::Draw, name.c_str());
        if (uiTrackId == 0)
            return;
        auto* uiTrack = app.timeline.findTrack(uiTrackId);
        if (!uiTrack) return;
        uiTrack->engineTrackId = app.getEngine().createTrack(DrawingEngine::TrackKind::Draw, name);
        if (uiTrack->engineTrackId == 0)
        {
            auto& tracks = app.timeline.state().tracks;
            tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const strova::TimelineTrack& track)
                {
                    return track.id == uiTrackId;
                }), tracks.end());
            return;
        }
        activeLayerTree(app).addLayerNode(name, uiTrack->engineTrackId, parentId);
        app.refreshLayerPanelForActiveFrame();
        syncDrawTrackOrderFromLayerTree(app);
        syncPrimaryLayerSelectionToEngine(app);
        app.requestSaveProjectNow();
    }

    static void groupSelectedLayersFromPanel(App& app)
    {
        int gid = activeLayerTree(app).groupSelection("Group");
        if (gid != 0)
        {
            app.refreshLayerPanelForActiveFrame();
            syncDrawTrackOrderFromLayerTree(app);
            app.requestSaveProjectNow();
        }
    }

    static void deleteSelectedLayersFromPanel(App& app)
    {
        std::vector<int> nodeIds = activeLayerTree(app).getSelection();
        if (nodeIds.empty()) return;

        bool removedAny = false;
        for (int nodeId : nodeIds)
        {
            const auto* node = activeLayerTree(app).findNode(nodeId);
            if (!node || node->isGroup) continue;
            int trackId = node->trackId;
            int nodeIdCopy = node->id;
            if (trackId != 0)
            {
                app.getEngine().removeTrack(trackId);
                auto& tracks = app.timeline.state().tracks;
                tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const strova::TimelineTrack& t)
                    {
                        return t.engineTrackId == trackId;
                    }), tracks.end());
                auto& clips = app.timeline.state().clips;
                const std::string trLabel = transformClipLabel(trackId);
                clips.erase(std::remove_if(clips.begin(), clips.end(), [&](const strova::TimelineClip& c)
                    {
                        return c.label == trLabel;
                    }), clips.end());
                if (app.getEditorUiState().isolatedLayerTrackId == trackId) app.setLayerFocusCommand(trackId);
            }
            activeLayerTree(app).removeNodeOnly(nodeIdCopy);
            removedAny = true;
        }
        if (removedAny)
        {
            activeLayerTree(app).syncExistingFromTimeline(app.timeline);
            app.refreshLayerPanelForActiveFrame();
            syncDrawTrackOrderFromLayerTree(app);
            syncPrimaryLayerSelectionToEngine(app);
            app.dirtyAllThumbs();
            app.requestSaveProjectNow();
        }
    }

    static bool keyframeTransformToolsVisible(const App& app)
    {
        return activeLayerTree(app).primarySelectedTrackId() != 0;
    }

    static bool quickAnimPanelVisible(const App& app)
    {
        return app.flowCapturer().armed && app.flowLinkEnabledValue() && activeLayerTree(app).primarySelectedTrackId() != 0;
    }

    static bool flowSettingsPanelVisible(const App& app)
    {
        const auto* p = app.dockManager().getPanel("FlowSettings");
        return p && p->visible && p->contentRect.w > 0 && p->contentRect.h > 0;
    }

    static SDL_Rect flowSettingsPanelRect(const App& app)
    {
        return app.dockManager().contentRect("FlowSettings");
    }

    static bool flowLinkClipSampleAtFrame(const App& app, int frame, int trackId, FlowLinkFrameSample& outSample, bool& outRelative)
    {
        const auto& clips = app.getEngine().getFlowLinkClips(trackId);
        for (const auto& clip : clips)
        {
            if (clip.samples.empty() || clip.duration <= 0) continue;
            if (frame < clip.startFrame) continue;
            if (!clip.loop && frame >= clip.startFrame + clip.duration) continue;
            int local = frame - clip.startFrame;
            if (clip.loop && clip.duration > 0) local %= clip.duration;
            local = std::clamp(local, 0, std::max(0, clip.duration - 1));
            const FlowLinkFrameSample* chosen = nullptr;
            for (const auto& sample : clip.samples)
            {
                if (sample.frameOffset == local) { chosen = &sample; break; }
                if (!chosen || sample.frameOffset < local) chosen = &sample;
            }
            if (chosen)
            {
                outSample = *chosen;
                outRelative = clip.relative;
                return true;
            }
        }
        return false;
    }

    static DrawingEngine::LayerTransform flowLinkDisplayToBaseTransform(const App& app, int frame, int trackId, const DrawingEngine::LayerTransform& display)
    {
        DrawingEngine::LayerTransform base = display;
        FlowLinkFrameSample sample{};
        bool relative = false;
        if (flowLinkClipSampleAtFrame(app, frame, trackId, sample, relative) && relative)
        {
            base.posX -= sample.posX;
            base.posY -= sample.posY;
            base.rotation -= sample.rotation;
        }
        return base;
    }

    static int ensureFlowLinkLaneTrack(App& app, int startFrame, int duration)
    {
        auto overlaps = [&](const strova::TimelineTrack& track) -> bool
            {
                for (const auto& clip : app.timeline.state().clips)
                {
                    if (clip.trackId != track.id) continue;
                    const int a0 = clip.startFrame;
                    const int a1 = clip.startFrame + std::max(1, clip.lengthFrames);
                    const int b0 = startFrame;
                    const int b1 = startFrame + std::max(1, duration);
                    if (b0 < a1 && a0 < b1) return true;
                }
                return false;
            };

        int bestTrackId = 0;
        int bestLane = -1;
        for (const auto& track : app.timeline.state().tracks)
        {
            if (track.kind != strova::TrackKind::FlowLink) continue;
            int lane = 0;
            if (track.name.rfind("FlowLink", 0) == 0)
            {
                const char* tail = track.name.c_str() + 8;
                while (*tail == ' ') ++tail;
                if (*tail) lane = std::max(0, std::atoi(tail));
            }
            if (!overlaps(track))
            {
                if (bestTrackId == 0 || lane < bestLane)
                {
                    bestTrackId = track.id;
                    bestLane = lane;
                }
            }
            bestLane = std::max(bestLane, lane);
        }
        if (bestTrackId != 0)
            return bestTrackId;

        const int newLane = std::max(0, bestLane + 1);
        const std::string trackName = newLane > 0 ? (std::string("FlowLink ") + std::to_string(newLane)) : std::string("FlowLink");
        const int uiTrackId = app.timeline.addTrack(strova::TrackKind::FlowLink, trackName.c_str());
        if (auto* uiTrack = app.timeline.findTrack(uiTrackId))
            uiTrack->engineTrackId = app.getEngine().createTrack(DrawingEngine::TrackKind::FlowLink, trackName.c_str());
        return uiTrackId;
    }

    static int flowLinkLaneIndexFromUiTrack(const strova::TimelineTrack* laneTrack)
    {
        if (!laneTrack) return 0;
        if (laneTrack->name.rfind("FlowLink", 0) != 0)
            return 0;
        const char* tail = laneTrack->name.c_str() + 8;
        while (*tail == ' ') ++tail;
        return *tail ? std::max(0, std::atoi(tail)) : 0;
    }

    static void ensureTimelineFrameCoverageForClip(App& app, int clipEndFrameExclusive)
    {
        const int requiredFrames = std::max(1, clipEndFrameExclusive);
        if ((int)app.getEngine().getFrameCount() < requiredFrames)
        {
            app.storeCurrentDrawFrameLayerTree();
            while ((int)app.getEngine().getFrameCount() < requiredFrames)
            {
                const std::size_t beforeGrow = app.getEngine().getFrameCount();
                app.getEngine().addFrame();
                if (app.getEngine().getFrameCount() == beforeGrow)
                    break;
                app.initFreshLayerTreeForFrame(app.getEngine().getFrameCount() - 1);
            }
            app.dirtyAllThumbs();
        }
        app.timeline.setTotalFrames(std::max(1, (int)app.getEngine().getFrameCount()));
        app.timeline.setPlayheadFrame((int)app.getEngine().getCurrentFrameIndex());
    }

    static bool flowLinkClipsEquivalent(const FlowLinkClip& a, const FlowLinkClip& b)
    {
        if (a.targetTrackId != b.targetTrackId) return false;
        if (a.startFrame != b.startFrame) return false;
        if (a.duration != b.duration) return false;
        if (a.relative != b.relative) return false;
        if (a.samples.size() != b.samples.size()) return false;
        for (size_t i = 0; i < a.samples.size(); ++i)
        {
            const auto& sa = a.samples[i];
            const auto& sb = b.samples[i];
            if (sa.frameOffset != sb.frameOffset) return false;
            if (std::fabs(sa.posX - sb.posX) > 0.001f) return false;
            if (std::fabs(sa.posY - sb.posY) > 0.001f) return false;
            if (std::fabs(sa.rotation - sb.rotation) > 0.001f) return false;
        }
        return true;
    }

    static bool commitFlowLinkCaptureClip(App& app, int targetTrackId, FlowLinkClip clip)
    {
        if (targetTrackId == 0 || clip.empty())
            return false;

        const int laneTrackId = ensureFlowLinkLaneTrack(app, clip.startFrame, clip.duration);
        clip.laneIndex = flowLinkLaneIndexFromUiTrack(app.timeline.findTrack(laneTrackId));
        ensureTimelineFrameCoverageForClip(app, clip.startFrame + std::max(1, clip.duration));

        const auto& existing = app.getEngine().getFlowLinkClips(targetTrackId);
        if (!existing.empty() && flowLinkClipsEquivalent(existing.back(), clip))
            return false;

        app.getEngine().addFlowLinkClip(targetTrackId, clip);
        if (laneTrackId != 0)
        {
            const auto* laneTrack = app.timeline.findTrack(laneTrackId);
            const std::string clipLabel = laneTrack ? laneTrack->name : std::string("FlowLink");
            app.timeline.addClip(laneTrackId, clip.startFrame, clip.duration, clipLabel.c_str());
        }
        app.timeline.setTotalFrames(std::max(1, (int)app.getEngine().getFrameCount()));
        return true;
    }

    struct SelectedTransformKeyUi
    {
        bool valid = false;
        int trackId = 0;
        DrawingEngine::TransformChannel channel = DrawingEngine::TransformChannel::PosX;
        int frameIndex = 0;
    };

    static SelectedTransformKeyUi g_selectedTransformKey{};

    enum class EditorInputMode : int
    {
        Draw = 0,
        Transform = 1
    };

    enum class KeyframeModalAction : int
    {
        None = 0,
        Add,
        Start,
        End
    };

    struct KeyframeActionModal
    {
        bool visible = false;
        bool confirmDuplicate = false;
        int targetFrame = 0;
        int targetTrackId = 0;
        KeyframeModalAction pendingAction = KeyframeModalAction::None;
        SDL_Rect anchorRect{ 0,0,0,0 };
    };

    static EditorInputMode g_editorInputMode = EditorInputMode::Draw;
    static KeyframeActionModal g_keyframeActionModal{};

    static const char* transformChannelLabel(DrawingEngine::TransformChannel channel)
    {
        switch (channel)
        {
        case DrawingEngine::TransformChannel::PosX: return "X Position";
        case DrawingEngine::TransformChannel::PosY: return "Y Position";
        case DrawingEngine::TransformChannel::Rotation: return "Rotation";
        default: return "Channel";
        }
    }

    static SDL_Rect keyframeModalRect(const KeyframeActionModal& modal)
    {
        const int w = modal.confirmDuplicate ? 280 : 180;
        const int h = modal.confirmDuplicate ? 110 : 110;
        return SDL_Rect{ modal.anchorRect.x, modal.anchorRect.y, w, h };
    }

    static SDL_Rect keyframeModalButtonRect(const SDL_Rect& card, int index, bool confirmMode)
    {
        const int btnH = confirmMode ? 28 : 24;
        const int pad = 8;
        if (confirmMode)
        {
            const int btnW = 84;
            const int y = card.y + card.h - btnH - 8;
            if (index == 0) return SDL_Rect{ card.x + 8, y, btnW, btnH };
            return SDL_Rect{ card.x + card.w - btnW - 8, y, btnW, btnH };
        }
        return SDL_Rect{ card.x + pad, card.y + 8 + index * (btnH + 6), card.w - pad * 2, btnH };
    }

    static void closeKeyframeActionModal()
    {
        g_keyframeActionModal.visible = false;
        g_keyframeActionModal.confirmDuplicate = false;
        g_keyframeActionModal.pendingAction = KeyframeModalAction::None;
        g_keyframeActionModal.targetFrame = 0;
        g_keyframeActionModal.targetTrackId = 0;
        g_keyframeActionModal.anchorRect = SDL_Rect{ 0,0,0,0 };
    }

    static float layerTransformChannelValue(const DrawingEngine::LayerTransform& tr, DrawingEngine::TransformChannel channel)
    {
        switch (channel)
        {
        case DrawingEngine::TransformChannel::PosX: return tr.posX;
        case DrawingEngine::TransformChannel::PosY: return tr.posY;
        case DrawingEngine::TransformChannel::Rotation: return tr.rotation;
        default: return 0.0f;
        }
    }

    static bool nearlyEqualFloat(float a, float b, float eps = 0.05f)
    {
        return std::fabs(a - b) <= eps;
    }

    static bool transformsDiffer(const DrawingEngine::LayerTransform& a, const DrawingEngine::LayerTransform& b)
    {
        return !nearlyEqualFloat(a.posX, b.posX) ||
            !nearlyEqualFloat(a.posY, b.posY) ||
            !nearlyEqualFloat(a.rotation, b.rotation, 0.1f) ||
            !nearlyEqualFloat(a.pivotX, b.pivotX) ||
            !nearlyEqualFloat(a.pivotY, b.pivotY);
    }

    static bool g_timelineTxnActive = false;

    static void beginTimelineTransaction(App& app)
    {
        if (g_timelineTxnActive)
            return;
        app.getEngine().beginTimelineTransaction();
        g_timelineTxnActive = true;
    }

    static void finishTimelineTransaction(App& app, bool commit)
    {
        if (!g_timelineTxnActive)
            return;
        if (commit) app.getEngine().commitTimelineTransaction();
        else        app.getEngine().rollbackTimelineTransaction();
        g_timelineTxnActive = false;
    }

    template<typename Fn>
    static bool runTimelineMutation(App& app, Fn&& fn)
    {
        const bool ownsTransaction = !g_timelineTxnActive;
        if (ownsTransaction)
            beginTimelineTransaction(app);

        const bool changed = fn();

        if (ownsTransaction)
            finishTimelineTransaction(app, changed);

        return changed;
    }

    static void addTransformKeysAtCurrentFrame(App& app, int engineTrackId)
    {
        if (engineTrackId == 0) return;
        const size_t frame = app.getEngine().getCurrentFrameIndex();
        const DrawingEngine::TrackLayer layer = app.getEngine().getEvaluatedFrameTrackLayerCopy(frame, engineTrackId);
        if (layer.trackId == 0 || !layer.visible) return;

        const bool changed = runTimelineMutation(app, [&]() -> bool
            {
                app.getEngine().setTransformKeyframe(engineTrackId, DrawingEngine::TransformChannel::PosX, (int)frame, layer.transform.posX);
                app.getEngine().setTransformKeyframe(engineTrackId, DrawingEngine::TransformChannel::PosY, (int)frame, layer.transform.posY);
                app.getEngine().setTransformKeyframe(engineTrackId, DrawingEngine::TransformChannel::Rotation, (int)frame, layer.transform.rotation);
                return true;
            });
        if (changed)
            app.markCurrentFrameEditedAndSave();
    }

    static void deleteTransformKeysAtCurrentFrame(App& app, int engineTrackId)
    {
        if (engineTrackId == 0) return;
        const int frame = (int)app.getEngine().getCurrentFrameIndex();
        const bool changed = runTimelineMutation(app, [&]() -> bool
            {
                bool any = false;
                any = app.getEngine().removeTransformKeyframe(engineTrackId, DrawingEngine::TransformChannel::PosX, frame) || any;
                any = app.getEngine().removeTransformKeyframe(engineTrackId, DrawingEngine::TransformChannel::PosY, frame) || any;
                any = app.getEngine().removeTransformKeyframe(engineTrackId, DrawingEngine::TransformChannel::Rotation, frame) || any;
                return any;
            });
        if (changed)
            app.markCurrentFrameEditedAndSave();
    }

    static void appendTransformKeyframeStacked(App& app, int engineTrackId, int frame, const DrawingEngine::LayerTransform& tr)
    {
        if (engineTrackId == 0) return;
        auto appendChannel = [&](DrawingEngine::TransformChannel channel, float value)
            {
                auto* keys = app.getEngine().getTransformKeyframesMutable(engineTrackId, channel);
                if (!keys) return;
                keys->push_back({ std::max(0, frame), value });
                std::stable_sort(keys->begin(), keys->end(), [](const DrawingEngine::TransformKeyframe& a, const DrawingEngine::TransformKeyframe& b)
                    {
                        if (a.frameIndex != b.frameIndex) return a.frameIndex < b.frameIndex;
                        return false;
                    });
            };
        appendChannel(DrawingEngine::TransformChannel::PosX, tr.posX);
        appendChannel(DrawingEngine::TransformChannel::PosY, tr.posY);
        appendChannel(DrawingEngine::TransformChannel::Rotation, tr.rotation);
        app.getEngine().syncFrameTransformsFromKeyframes();
    }

    static bool hasTransformKeyAtFrame(const App& app, int engineTrackId, int frame)
    {
        if (engineTrackId == 0) return false;
        auto hasChannel = [&](DrawingEngine::TransformChannel channel)
            {
                const auto* keys = app.getEngine().getTransformKeyframes(engineTrackId, channel);
                if (!keys) return false;
                for (const auto& key : *keys)
                    if (key.frameIndex == frame)
                        return true;
                return false;
            };
        return hasChannel(DrawingEngine::TransformChannel::PosX) || hasChannel(DrawingEngine::TransformChannel::PosY) || hasChannel(DrawingEngine::TransformChannel::Rotation);
    }

    static void applyStackedKeyframeAction(App& app, int engineTrackId, int frame)
    {
        if (engineTrackId == 0) return;
        const DrawingEngine::TrackLayer layer = app.getEngine().getEvaluatedFrameTrackLayerCopy(app.getEngine().getCurrentFrameIndex(), engineTrackId);
        if (layer.trackId == 0 || !layer.visible) return;
        const bool changed = runTimelineMutation(app, [&]() -> bool
            {
                appendTransformKeyframeStacked(app, engineTrackId, frame, layer.transform);
                return true;
            });
        if (changed)
        {
            app.switchToFrameIndex((size_t)std::max(0, frame));
            app.markCurrentFrameEditedAndSave();
            g_selectedTransformKey.valid = false;
        }
    }


    static void applyVisibilityStartAction(App& app, int engineTrackId, int frame)
    {
        if (engineTrackId == 0) return;
        const bool changed = runTimelineMutation(app, [&]() -> bool
            {
                app.getEngine().setVisibilityKeyframe(engineTrackId, 0, false);
                app.getEngine().setVisibilityKeyframe(engineTrackId, std::max(0, frame), true);
                return true;
            });
        if (changed)
        {
            app.switchToFrameIndex((size_t)std::max(0, frame));
            app.markCurrentFrameEditedAndSave();
        }
    }

    static void applyVisibilityEndAction(App& app, int engineTrackId, int frame)
    {
        if (engineTrackId == 0) return;
        const bool changed = runTimelineMutation(app, [&]() -> bool
            {
                app.getEngine().setVisibilityKeyframe(engineTrackId, std::max(0, frame), true);
                app.getEngine().setVisibilityKeyframe(engineTrackId, std::max(0, frame + 1), false);
                return true;
            });
        if (changed)
        {
            app.switchToFrameIndex((size_t)std::max(0, frame));
            app.markCurrentFrameEditedAndSave();
        }
    }

    static bool touchTransformKeyframe(App& app, int engineTrackId, const DrawingEngine::LayerTransform& before, const DrawingEngine::LayerTransform& after)
    {
        if (engineTrackId == 0 || !transformsDiffer(before, after))
            return false;

        const int frame = (int)app.getEngine().getCurrentFrameIndex();
        const bool shouldAutoKey = g_transformAutoKey ||
            app.getEngine().hasTransformKeys(engineTrackId) ||
            hasTransformKeyAtFrame(app, engineTrackId, frame);

        if (!shouldAutoKey)
            return false;

        return runTimelineMutation(app, [&]() -> bool
            {
                app.getEngine().setTransformKeyframe(engineTrackId, DrawingEngine::TransformChannel::PosX, frame, after.posX);
                app.getEngine().setTransformKeyframe(engineTrackId, DrawingEngine::TransformChannel::PosY, frame, after.posY);
                app.getEngine().setTransformKeyframe(engineTrackId, DrawingEngine::TransformChannel::Rotation, frame, after.rotation);
                return true;
            });
    }

    static bool currentLayerWorldBounds(App& app, int trackId, strova::layer_render::Bounds& out)
    {
        DrawingEngine::TrackLayer layer = app.getEngine().getEvaluatedFrameTrackLayerCopy(app.getEngine().getCurrentFrameIndex(), trackId);
        if (layer.trackId == 0 || !layer.visible) return false;
        return strova::layer_render::calcWorldBounds(layer, out);
    }

    static int pickTransformTrackAtWorldPoint(App& app, const SDL_FPoint& p)
    {
        auto rows = activeLayerTree(app).buildRows();
        for (auto it = rows.rbegin(); it != rows.rend(); ++it)
        {
            const auto* node = activeLayerTree(app).findNode(it->nodeId);
            if (!node || node->isGroup || node->trackId == 0) continue;
            strova::layer_render::Bounds bounds{};
            if (!currentLayerWorldBounds(app, node->trackId, bounds)) continue;
            if (p.x >= bounds.minX && p.x <= bounds.maxX && p.y >= bounds.minY && p.y <= bounds.maxY)
                return node->trackId;
        }
        return 0;
    }

    static SDL_FPoint layerBoundsCenter(const strova::layer_render::Bounds& b)
    {
        return SDL_FPoint{ (b.minX + b.maxX) * 0.5f, (b.minY + b.maxY) * 0.5f };
    }

    static SDL_FPoint layerTransformPivotWorld(const DrawingEngine::TrackLayer& layer)
    {
        if (!layer.image.empty())
            return SDL_FPoint{ layer.transform.posX, layer.transform.posY };
        if (!layer.strokes.empty())
        {
            SDL_FPoint pivot = strova::layer_render::strokeLayerPivot(layer);
            return SDL_FPoint{ pivot.x + layer.transform.posX, pivot.y + layer.transform.posY };
        }
        strova::layer_render::Bounds b{};
        if (strova::layer_render::calcLocalBounds(layer, b))
        {
            return SDL_FPoint{ (b.minX + b.maxX) * 0.5f + layer.transform.posX, (b.minY + b.maxY) * 0.5f + layer.transform.posY };
        }
        return SDL_FPoint{ layer.transform.posX, layer.transform.posY };
    }

    static SDL_Rect transformButtonRect(const SDL_Rect& toolsArea, int index)
    {
        const int gap = 8;
        const int cols = 2;
        const int btnW = 100;
        const int btnH = 34;
        const int x0 = toolsArea.x + 10;
        const int y0 = toolsArea.y + 8;
        const int row = index / cols;
        const int col = index % cols;
        return SDL_Rect{ x0 + col * (btnW + gap), y0 + row * (btnH + gap), btnW, btnH };
    }

    static bool importImageIntoActiveFrame(App& app)
    {
        std::string path;
        if (!platform::pickOpenFile(path))
            return false;

        SDL_Surface* loaded = IMG_Load(path.c_str());
        if (!loaded)
            return false;

        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded);
        if (!rgba)
            return false;

        DrawingEngine::LayerImage image{};
        std::vector<std::uint8_t> importedPixels((size_t)rgba->w * (size_t)rgba->h * 4ull);
        std::memcpy(importedPixels.data(), rgba->pixels, importedPixels.size());
        image.setData(rgba->w, rgba->h, std::move(importedPixels), path);
        SDL_FreeSurface(rgba);

        int ordinal = 1;
        for (const auto& n : activeLayerTree(app).getNodes())
            if (!n.isGroup) ordinal++;

        const std::string stem = std::filesystem::path(path).stem().string();
        const std::string name = stem.empty() ? (std::string("Image ") + std::to_string(ordinal)) : stem;

        const int uiTrackId = app.timeline.addTrack(strova::TrackKind::Draw, name.c_str());
        if (uiTrackId == 0)
            return false;
        auto* uiTrack = app.timeline.findTrack(uiTrackId);
        if (!uiTrack)
            return false;

        uiTrack->engineTrackId = app.getEngine().createTrack(DrawingEngine::TrackKind::Draw, name);
        if (uiTrack->engineTrackId == 0)
        {
            auto& tracks = app.timeline.state().tracks;
            tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const strova::TimelineTrack& track)
                {
                    return track.id == uiTrackId;
                }), tracks.end());
            return false;
        }
        activeLayerTree(app).addLayerNode(name, uiTrack->engineTrackId, 0);

        DrawingEngine::LayerTransform transform{};
        transform.posX = app.getProjectW() * 0.5f;
        transform.posY = app.getProjectH() * 0.5f;
        transform.rotation = 0.0f;
        transform.pivotX = transform.posX;
        transform.pivotY = transform.posY;

        const size_t fi = app.getEngine().getCurrentFrameIndex();
        app.getEngine().setFrameTrackImage(fi, uiTrack->engineTrackId, image, transform);
        app.getEngine().setFrameTrackTransform(fi, uiTrack->engineTrackId, transform);

        app.refreshLayerPanelForActiveFrame();
        syncDrawTrackOrderFromLayerTree(app);
        syncPrimaryLayerSelectionToEngine(app);
        app.markCurrentFrameEditedAndSave();
        return true;
    }

    static void drawLayerThumbPreview(App& app, SDL_Renderer* r, const SDL_Rect& rc, int trackId)
    {
        fillRoundRect(r, rc, 5, SDL_Color{ 34, 38, 46, 230 });
        strokeRoundRect(r, rc, 5, SDL_Color{ 72, 78, 90, 180 });

        DrawingEngine::TrackLayer layer = app.getEngine().getEvaluatedFrameTrackLayerCopy(app.getEngine().getCurrentFrameIndex(), trackId);
        if (layer.trackId == 0 || !layer.visible) return;

        const float docW = std::max(1, app.getProjectW());
        const float docH = std::max(1, app.getProjectH());
        const float pad = 2.0f;
        const float availW = std::max(1.0f, (float)rc.w - pad * 2.0f);
        const float availH = std::max(1.0f, (float)rc.h - pad * 2.0f);
        const float sc = std::max(0.01f, std::min(availW / docW, availH / docH));
        const float ox = pad + (availW - docW * sc) * 0.5f;
        const float oy = pad + (availH - docH * sc) * 0.5f;

        SDL_Rect clipRc{ rc.x + 1, rc.y + 1, std::max(1, rc.w - 2), std::max(1, rc.h - 2) };
        SDL_Rect oldClip{};
        const SDL_bool hadClip = SDL_RenderIsClipEnabled(r);
        SDL_RenderGetClipRect(r, &oldClip);
        SDL_RenderSetClipRect(r, &clipRc);

        auto& imageCache = g_persistentFrameImageCache;
        const std::string imageKey = std::to_string((int)app.getEngine().getCurrentFrameIndex()) + ":thumb:" + std::to_string(trackId) + ":" + std::to_string(layer.celId) + ":" + std::to_string(layer.imageRevision);
        const std::string strokeKey = std::to_string((int)app.getEngine().getCurrentFrameIndex()) + ":thumb:" + std::to_string(trackId) + ":" + std::to_string(layer.contentRevision) + ":" + std::to_string(layer.transformRevision);
        strova::layer_render::drawTrackLayer(
            r,
            *app.brushRendererHandle(),
            layer,
            sc,
            ox,
            oy,
            rc.x,
            rc.y,
            1.0f,
            &imageCache,
            imageKey,
            strokeKey,
            app.runtimeStateRef().diagnostics.frameOrdinal);
        g_persistentFrameImageCacheLastUse[imageKey] = app.runtimeStateRef().diagnostics.frameOrdinal;

        if (hadClip) SDL_RenderSetClipRect(r, &oldClip);
        else SDL_RenderSetClipRect(r, nullptr);
    }

    static void drawLayerPanel(App& app, SDL_Renderer* r, int mx, int my, const SDL_Rect& contentRc, std::string& ioTooltipText)
    {
        LayerPanelLayout layout = buildLayerPanelLayout(contentRc);

        SDL_Rect oldClip{};
        const SDL_bool hadClip = SDL_RenderIsClipEnabled(r);
        if (hadClip) SDL_RenderGetClipRect(r, &oldClip);
        SDL_RenderSetClipRect(r, &layout.panel);

        drawText(r, app.getUiFont(), app.getEditorUiState().isolatedLayerTrackId != 0 ? "Focus On" : "Focus Off", layout.header.x + layout.header.w - 84, layout.header.y + 1, COL_TEXT_DIM);

        auto drawMiniBtn = [&](const SDL_Rect& rr, const char* label, const char* tip)
            {
                bool hover = pointInRect(mx, my, rr);
                drawModernButton(r, rr, hover, false);
                int tw = measureTextW(app.getUiFont(), label);
                const int th = std::max(1, TTF_FontHeight(app.getUiFont()));
                drawText(r, app.getUiFont(), label, rr.x + (rr.w - tw) / 2, rr.y + std::max(0, (rr.h - th) / 2), hover ? COL_TEXT_MAIN : COL_TEXT_DIM);
                if (hover) ioTooltipText = tip;
            };

        drawMiniBtn(layout.addBtn, "+", "Add draw layer");
        drawMiniBtn(layout.childBtn, "C", "Add child layer under current selection");
        drawMiniBtn(layout.groupBtn, "G", "Group selected layers");
        drawMiniBtn(layout.upBtn, "Up", "Move selected layer up");
        drawMiniBtn(layout.downBtn, "Dn", "Move selected layer down");
        drawMiniBtn(layout.deleteBtn, "Del", "Delete selected draw layers");
        drawMiniBtn(layout.copyBtn, "Copy", "Copy selected layers on this frame");
        drawMiniBtn(layout.pasteBtn, "Paste", "Paste clipboard onto selected layers");
        drawMiniBtn(layout.nextBtn, "To Next", "Copy selected layers to next frame");

        SDL_RenderSetClipRect(r, &layout.rows);
        const auto rows = activeLayerTree(app).buildRows();
        const int rowH = 36;
        const int contentH = (int)rows.size() * rowH;
        const int maxScroll = std::max(0, contentH - layout.rows.h);
        g_layerPanelScroll = clampi(g_layerPanelScroll, 0, maxScroll);

        for (int rowIndex = 0; rowIndex < (int)rows.size(); ++rowIndex)
        {
            const auto& row = rows[(size_t)rowIndex];
            const auto* node = activeLayerTree(app).findNode(row.nodeId);
            if (!node) continue;

            SDL_Rect rr = layerRowRect(layout, rowIndex, maxScroll);
            if (rr.y + rr.h < layout.rows.y || rr.y >= layout.rows.y + layout.rows.h)
                continue;

            bool hover = pointInRect(mx, my, rr);
            bool selected = activeLayerTree(app).isSelected(node->id);
            if (selected) fillRoundRect(r, rr, 8, SDL_Color{ 72, 96, 140, 220 });
            else if (hover) fillRoundRect(r, rr, 8, SDL_Color{ 48, 52, 62, 220 });

            SDL_Rect thumbRc = layerThumbRect(rr);
            if (!node->isGroup && node->trackId != 0)
                drawLayerThumbPreview(app, r, thumbRc, node->trackId);
            else
            {
                fillRoundRect(r, thumbRc, 5, SDL_Color{ 60, 54, 36, 220 });
                strokeRoundRect(r, thumbRc, 5, SDL_Color{ 110, 98, 64, 180 });
            }

            int x = thumbRc.x + thumbRc.w + 8 + row.depth * 14;
            bool hasKids = node->isGroup || activeLayerTree(app).hasChild(node->id);
            if (hasKids)
            {
                const char* arrow = node->expanded ? "v" : ">";
                drawText(r, app.getUiFont(), arrow, x, rr.y + 12, selected ? COL_TEXT_MAIN : COL_TEXT_DIM);
                x += 14;
            }

            SDL_Color bullet = node->isGroup ? SDL_Color{ 210,180,90,255 } : SDL_Color{ 170,180,205,255 };
            fillCircle(r, x + 4, rr.y + 20, 4, bullet);
            x += 12;

            std::string label = node->name;
            if (!node->isGroup && node->trackId == app.getEngine().getActiveTrack())
                label += " *";
            drawText(r, app.getUiFont(), label, x, rr.y + 8, selected ? COL_TEXT_MAIN : COL_TEXT_DIM);

            if (!node->isGroup && node->trackId != 0)
            {
                const auto* uiTrack = findUiTrackByEngineTrackId(app, node->trackId);
                bool visible = uiTrack ? uiTrack->visible : true;
                bool locked = uiTrack ? uiTrack->locked : false;
                bool focused = (app.getEditorUiState().isolatedLayerTrackId == node->trackId);
                SDL_Rect eyeRc = layerEyeRect(rr);
                SDL_Rect lockRc = layerLockRect(rr);
                SDL_Rect focusRc = layerFocusRect(rr);

                drawText(r, app.getUiFont(), visible ? "o" : "-", eyeRc.x, eyeRc.y - 1, visible ? COL_TEXT_MAIN : COL_TEXT_DIM);
                drawText(r, app.getUiFont(), locked ? "L" : "U", lockRc.x, lockRc.y - 1, locked ? SDL_Color{ 255, 196, 96, 255 } : COL_TEXT_DIM);
                drawText(r, app.getUiFont(), focused ? "F" : "f", focusRc.x, focusRc.y - 1, focused ? SDL_Color{ 135, 196, 255, 255 } : COL_TEXT_DIM);

                if (pointInRect(mx, my, expandHitRect(eyeRc, 6, 6))) ioTooltipText = visible ? "Hide layer" : "Show layer";
                else if (pointInRect(mx, my, expandHitRect(lockRc, 6, 6))) ioTooltipText = locked ? "Unlock layer" : "Lock layer";
                else if (pointInRect(mx, my, expandHitRect(focusRc, 6, 6))) ioTooltipText = focused ? "Exit focus mode" : "Focus only this layer on canvas";
                else if (hover) ioTooltipText = "Ctrl-click or Shift-click to multi-select layers";
            }
            else if (hover)
            {
                ioTooltipText = "Layer group";
            }
        }

        SDL_RenderSetClipRect(r, &layout.panel);

        if (maxScroll > 0)
        {
            SDL_Rect track{ layout.rows.x + layout.rows.w - 6, layout.rows.y, 4, layout.rows.h };
            fillRect(r, track, SDL_Color{ 44,46,52,180 });
            int thumbH = std::max(24, layout.rows.h * layout.rows.h / std::max(layout.rows.h, contentH));
            int thumbY = track.y + (layout.rows.h - thumbH) * g_layerPanelScroll / std::max(1, maxScroll);
            SDL_Rect thumb{ track.x, thumbY, track.w, thumbH };
            fillRect(r, thumb, SDL_Color{ 120,126,138,220 });
        }

        if (hadClip) SDL_RenderSetClipRect(r, &oldClip);
        else SDL_RenderSetClipRect(r, nullptr);
    }
    static void drawLineFCompat(SDL_Renderer* r, float x0, float y0, float x1, float y1)
    {
        checkSDLVersion();
        if (sdlSupportsFloatLines)
        {
            SDL_RenderDrawLineF(r, x0, y0, x1, y1);
        }
        else
        {
            SDL_RenderDrawLine(r,
                (int)std::lround(x0), (int)std::lround(y0),
                (int)std::lround(x1), (int)std::lround(y1));
        }
    }

    static int clampi(int v, int a, int b) { return (v < a) ? a : (v > b) ? b : v; }

    static void setCol(SDL_Renderer* r, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    }

    static void fillRoundRect(SDL_Renderer* r, const SDL_Rect& rc, int rad, SDL_Color c)
    {
        if (!r) return;
        if (rc.w <= 0 || rc.h <= 0) return;

        const int maxRad = std::min(rc.w, rc.h) / 2;
        rad = clampi(rad, 0, maxRad);

        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        setCol(r, c);

        if (rad <= 0)
        {
            SDL_RenderFillRect(r, &rc);
            return;
        }

        const float left = (float)rc.x;
        const float right = (float)(rc.x + rc.w);
        const float top = (float)rc.y;
        const float bottom = (float)(rc.y + rc.h);
        const float rr = (float)rad;

        const float cTLx = left + rr;
        const float cTRx = right - rr;
        const float cBLx = left + rr;
        const float cBRx = right - rr;
        const float cTLy = top + rr;
        const float cTRy = top + rr;
        const float cBLy = bottom - rr;
        const float cBRy = bottom - rr;

        for (int y = rc.y; y < rc.y + rc.h; ++y)
        {
            const float fy = (float)y + 0.5f;
            float sx = left;
            float ex = right;

            if (fy < top + rr)
            {
                const float dyL = fy - cTLy;
                const float dxL = std::sqrt(std::max(0.0f, rr * rr - dyL * dyL));
                sx = std::max(sx, cTLx - dxL);

                const float dyR = fy - cTRy;
                const float dxR = std::sqrt(std::max(0.0f, rr * rr - dyR * dyR));
                ex = std::min(ex, cTRx + dxR);
            }
            else if (fy > bottom - rr)
            {
                const float dyL = fy - cBLy;
                const float dxL = std::sqrt(std::max(0.0f, rr * rr - dyL * dyL));
                sx = std::max(sx, cBLx - dxL);

                const float dyR = fy - cBRy;
                const float dxR = std::sqrt(std::max(0.0f, rr * rr - dyR * dyR));
                ex = std::min(ex, cBRx + dxR);
            }

            const int ix0 = (int)std::ceil(sx);
            const int ix1 = (int)std::floor(ex) - 1;
            if (ix1 >= ix0)
                SDL_RenderDrawLine(r, ix0, y, ix1, y);
        }
    }

    static void strokeRoundRect(SDL_Renderer* r, const SDL_Rect& rc, int rad, SDL_Color c)
    {
        if (rc.w <= 0 || rc.h <= 0) return;
        setCol(r, c);

        int rady = std::min(rad, rc.h / 2);
        int radx = std::min(rad, rc.w / 2);
        rad = std::min(radx, rady);

        SDL_RenderDrawLine(r, rc.x + rad, rc.y, rc.x + rc.w - rad - 1, rc.y);
        SDL_RenderDrawLine(r, rc.x + rad, rc.y + rc.h - 1, rc.x + rc.w - rad - 1, rc.y + rc.h - 1);
        SDL_RenderDrawLine(r, rc.x, rc.y + rad, rc.x, rc.y + rc.h - rad - 1);
        SDL_RenderDrawLine(r, rc.x + rc.w - 1, rc.y + rad, rc.x + rc.w - 1, rc.y + rc.h - rad - 1);

        for (int dy = 0; dy < rad; ++dy)
        {
            float fy = (float)(rad - 1 - dy);
            int dx = (int)std::floor(std::sqrt((float)(rad * rad) - fy * fy));

            int xL = rc.x + (rad - dx);
            int xR = rc.x + rc.w - (rad - dx) - 1;
            int yTop = rc.y + dy;
            int yBot = rc.y + rc.h - 1 - dy;

            SDL_RenderDrawPoint(r, xL, yTop);
            SDL_RenderDrawPoint(r, xR, yTop);
            SDL_RenderDrawPoint(r, xL, yBot);
            SDL_RenderDrawPoint(r, xR, yBot);
        }
    }

    struct ToolBtn
    {
        ToolType t;
        SDL_Texture* (*icon)(App& app);
    };

    static SDL_Texture* iconForTool(App& app, ToolType t)
    {
        switch (t)
        {
        case ToolType::Brush:       return app.iconBrush;
        case ToolType::Pencil:      return app.iconPencil;
        case ToolType::Pen:         return app.iconPen;
        case ToolType::Marker:      return app.iconMarker;
        case ToolType::Airbrush:    return app.iconAirbrush;
        case ToolType::Calligraphy: return app.iconCalligraphy;
        case ToolType::Eraser:      return app.iconEraser;
        case ToolType::SoftEraser:  return app.iconSoften;
        case ToolType::Smudge:      return app.iconSmudge;
        case ToolType::Blur:        return app.iconBlur;
        case ToolType::Glow:        return app.iconGlow;
        case ToolType::Fill:        return app.iconBucket;
        case ToolType::Line:        return app.iconLine;
        case ToolType::Ruler:       return app.iconRuler;
        case ToolType::Rect:        return app.iconRectangle;
        case ToolType::Ellipse:     return app.iconOval;
        case ToolType::Select:      return app.iconSelect;
        case ToolType::Eyedropper:  return app.iconDropper;
        default:                    return nullptr;
        }
    }

    static const ToolBtn kToolBtns[] = {
        { ToolType::Brush,       [](App& a) { return iconForTool(a, ToolType::Brush); } },
        { ToolType::Pencil,      [](App& a) { return iconForTool(a, ToolType::Pencil); } },
        { ToolType::Pen,         [](App& a) { return iconForTool(a, ToolType::Pen); } },
        { ToolType::Marker,      [](App& a) { return iconForTool(a, ToolType::Marker); } },
        { ToolType::Airbrush,    [](App& a) { return iconForTool(a, ToolType::Airbrush); } },
        { ToolType::Calligraphy, [](App& a) { return iconForTool(a, ToolType::Calligraphy); } },
        { ToolType::Eraser,      [](App& a) { return iconForTool(a, ToolType::Eraser); } },
        { ToolType::SoftEraser,  [](App& a) { return iconForTool(a, ToolType::SoftEraser); } },
        { ToolType::Smudge,      [](App& a) { return iconForTool(a, ToolType::Smudge); } },
        { ToolType::Blur,        [](App& a) { return iconForTool(a, ToolType::Blur); } },
        { ToolType::Glow,        [](App& a) { return iconForTool(a, ToolType::Glow); } },
        { ToolType::Fill,        [](App& a) { return iconForTool(a, ToolType::Fill); } },
        { ToolType::Line,        [](App& a) { return iconForTool(a, ToolType::Line); } },
        { ToolType::Ruler,       [](App& a) { return iconForTool(a, ToolType::Ruler); } },
        { ToolType::Rect,        [](App& a) { return iconForTool(a, ToolType::Rect); } },
        { ToolType::Ellipse,     [](App& a) { return iconForTool(a, ToolType::Ellipse); } },
        { ToolType::Select,      [](App& a) { return iconForTool(a, ToolType::Select); } },
        { ToolType::Eyedropper,  [](App& a) { return iconForTool(a, ToolType::Eyedropper); } },
    };


    static bool       g_actionActive = false;
    static ToolType   g_actionTool = ToolType::Brush;
    static SDL_FPoint g_actionA{ 0,0 };
    static SDL_FPoint g_actionB{ 0,0 };

    static bool       g_hasSelection = false;
    static SDL_FPoint g_selA{ 0,0 };
    static SDL_FPoint g_selB{ 0,0 };
    static std::vector<size_t> g_selectedStrokeIndices;
    static bool       g_draggingSelection = false;
    static SDL_FPoint g_selectionDragLast{ 0,0 };

    enum class TransformToolMode : int { None = 0, Move, Rotate };
    static TransformToolMode g_transformToolMode = TransformToolMode::None;
    static int g_toolGridExtraTop = 0;
    static int g_toolsPanelScroll = 0;
    static SDL_Rect g_toolModeDrawBtnR{};
    static SDL_Rect g_toolModeMoveBtnR{};
    static SDL_Rect g_toolModeRotateBtnR{};
    static SDL_Rect g_toolModeSelectBtnR{};
    static SDL_Rect g_quickAnimPanelR{};
    static int g_quickAnimScroll = 0;
    static bool g_transformPreviewActive = false;
    static DrawingEngine::LayerTransform g_transformPreviewTransform{};
    static DrawingEngine::LayerTransform g_transformCommittedPreview{};
    static bool g_flowLinkConflictModalOpen = false;
    static bool g_flowLinkStitchModePending = false;
    static int g_flowLinkCaptureStartFrame = 0;
    static bool g_transformTxnChanged = false;
    static bool g_keyframeTxnChanged = false;
    static bool g_transformDragging = false;
    static int g_transformDragTrackId = 0;
    static SDL_FPoint g_transformDragStartWorld{ 0.0f, 0.0f };
    static SDL_FPoint g_transformDragLast{ 0.0f, 0.0f };
    static float g_transformBaseRotation = 0.0f;
    static float g_transformStartMouseAngle = 0.0f;
    static DrawingEngine::LayerTransform g_transformStartTransform{};
    static SDL_Rect g_importBtnR{};
    static SDL_Rect g_pluginBtnR{};
    static SDL_Rect g_pluginMenuR{};
    static SDL_Rect g_pluginLoadedMenuR{};
    static bool g_pluginMenuOpen = false;
    static bool g_pluginLoadedMenuOpen = false;
    static SDL_Rect g_transformMoveBtnR{};
    static SDL_Rect g_transformRotateBtnR{};
    static SDL_Rect g_transformAutoBtnR{};
    static SDL_Rect g_transformAddKeyBtnR{};
    static SDL_Rect g_transformDeleteKeyBtnR{};

    static SDL_Rect g_keyframePanelRect{};
    static SDL_Rect g_keyframeSplitterRect{};
    static bool g_keyframePanelResizing = false;
    static int g_keyframePanelHeight = 140;
    static int g_keyframeResizeStartY = 0;
    static int g_keyframeResizeStartHeight = 140;
    static bool g_keyframeDragging = false;
    static int g_keyframeDragTrackId = 0;
    static DrawingEngine::TransformChannel g_keyframeDragChannel = DrawingEngine::TransformChannel::PosX;
    static int g_keyframeDragStartFrame = 0;

    static SDL_FPoint g_rulerCenter{ 960.0f, 540.0f };
    static float      g_rulerAngleDeg = 0.0f;
    static float      g_rulerLength = 520.0f;
    enum class RulerDragMode : int { None = 0, Body, Rotate };
    static RulerDragMode g_rulerDragMode = RulerDragMode::None;

    enum class OnionSliderDrag : int
    {
        None = 0,
        PrevAlpha,
        NextAlpha
    };
    static OnionSliderDrag g_onionSliderDrag = OnionSliderDrag::None;


    enum class RightSlider : int
    {
        None = 0,
        Size,
        Opacity,
        Stabilizer,
        Hardness,
        Spacing,
        Flow,
        Scatter,
        Strength,
        AngleDeg,
        Aspect,
        AirRadius,
        AirDensity,
        EraserStrength,
        SmudgeStrength,
        BlurRadius,
        FillTolerance,
        FillGap
    };

    static RightSlider g_rightSliderDrag = RightSlider::None;

    static int g_rightPanelScroll = 0;
    static float g_rightPanelScrollTarget = 0.0f;

    static SDL_Rect g_onionPanelRectRight{ 0,0,0,0 };
    static int g_rightPanelContentH = 0;
    static bool g_leftColorPickerOpen = true;

    enum class ExportFormat : int { MP4 = 0, PNGSEQ = 1, GIF = 2 };

    static bool        g_exportMenuOpen = false;
    static bool        g_exportSettingsOpen = false;
    static ExportFormat g_exportFmt = ExportFormat::MP4;

    static int  g_exportW = 1920;
    static int  g_exportH = 1080;
    static int  g_exportFps = 30;

    static int  g_exportStart = 0;
    static int  g_exportEnd = 0;

    static bool g_exportTransparent = false;

    static int  g_mp4Crf = 18;
    static int  g_mp4BitrateKbps = 0;
    static int  g_mp4Preset = 2;
    static bool g_mp4UseYuv420 = true;

    static int  g_gifColors = 256;
    static bool g_gifDither = true;
    static bool g_gifLoop = true;
    static int  g_gifScalePct = 100;

    static int  g_pngCompression = 6;
    static bool g_pngInterlace = false;

    static SDL_Rect g_exportBtnR{};
    static SDL_Rect g_exportMenuR{};
    static bool g_windowMenuOpen = false;
    static SDL_Rect g_windowBtnR{};
    static SDL_Rect g_windowMenuR{};

    enum class ModalKind : int
    {
        WindowMenu = 0,
        ExportMenu,
        ExportSettings
    };

    struct ModalWindow
    {
        ModalKind kind = ModalKind::WindowMenu;
        SDL_Rect rect{};
    };

    static std::vector<ModalWindow> g_activeModals;

    enum class ExportField : int
    {
        None = 0,
        Width,
        Height,
        FPS,
        Start,
        End,
        Mp4Crf,
        Mp4Preset,
        GifColors,
        GifScalePct,
        PngCompression
    };

    static ExportField g_exportFocus = ExportField::None;

    static std::string g_inW;
    static std::string g_inH;
    static std::string g_inFps;
    static std::string g_inStart;
    static std::string g_inEnd;

    static std::string g_inMp4Crf;
    static std::string g_inMp4Preset;

    static std::string g_inGifColors;
    static std::string g_inGifScale;

    static std::string g_inPngComp;

    static void fillCircle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c)
    {
        if (radius < 0) return;
        setCol(r, c);
        for (int dy = -radius; dy <= radius; ++dy)
        {
            int yy = cy + dy;
            double dy_d = static_cast<double>(dy);
            double rad_d = static_cast<double>(radius);
            double dx_sq = rad_d * rad_d - dy_d * dy_d;
            if (dx_sq < 0) continue;
            int dx = (int)std::floor(std::sqrt(dx_sq));
            SDL_RenderDrawLine(r, cx - dx, yy, cx + dx, yy);
        }
    }

    static void drawCircle(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c)
    {
        if (radius < 0) return;
        setCol(r, c);
        const int steps = std::max(24, radius * 6);
        for (int i = 0; i < steps; ++i)
        {
            float a0 = (float)i / (float)steps * 6.2831853f;
            float a1 = (float)(i + 1) / (float)steps * 6.2831853f;
            int x0 = cx + (int)std::lround(SDL_cosf(a0) * radius);
            int y0 = cy + (int)std::lround(SDL_sinf(a0) * radius);
            int x1 = cx + (int)std::lround(SDL_cosf(a1) * radius);
            int y1 = cy + (int)std::lround(SDL_sinf(a1) * radius);
            SDL_RenderDrawLine(r, x0, y0, x1, y1);
        }
    }

    static void fillPill(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
    {
        if (rc.w <= 0 || rc.h <= 0) return;
        int rad = rc.h / 2;
        if (rad <= 0) { fillRect(r, rc, c); return; }

        SDL_Rect mid{ rc.x + rad, rc.y, std::max(0, rc.w - rad * 2), rc.h };
        fillRect(r, mid, c);

        fillCircle(r, rc.x + rad, rc.y + rad, rad, c);
        fillCircle(r, rc.x + rc.w - rad - 1, rc.y + rad, rad, c);
    }

    static void strokePill(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
    {
        if (rc.w <= 0 || rc.h <= 0) return;
        int rad = rc.h / 2;
        if (rad <= 0) { drawRect(r, rc, c); return; }

        setCol(r, c);
        SDL_RenderDrawLine(r, rc.x + rad, rc.y, rc.x + rc.w - rad - 1, rc.y);
        SDL_RenderDrawLine(r, rc.x + rad, rc.y + rc.h - 1, rc.x + rc.w - rad - 1, rc.y + rc.h - 1);
        drawCircle(r, rc.x + rad, rc.y + rad, rad, c);
        drawCircle(r, rc.x + rc.w - rad - 1, rc.y + rad, rad, c);
    }

    static void exportClearFocus()
    {
        g_exportFocus = ExportField::None;
        SDL_StopTextInput();
    }

    static void exportFocus(ExportField f)
    {
        g_exportFocus = f;
        SDL_StartTextInput();
    }

    static int parseIntOr(const std::string& s, int fallback)
    {
        try
        {
            if (s.empty()) return fallback;
            size_t idx = 0;
            long long v = std::stoll(s, &idx, 10);
            if (idx == 0) return fallback;
            if (v > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
            if (v < std::numeric_limits<int>::min()) return std::numeric_limits<int>::min();
            return static_cast<int>(v);
        }
        catch (...)
        {
            return fallback;
        }
    }

    static void clampApplyExportInputs(App& app)
    {
        (void)app;
        g_exportW = std::clamp(parseIntOr(g_inW, g_exportW), 64, 8192);
        g_exportH = std::clamp(parseIntOr(g_inH, g_exportH), 64, 8192);
        g_exportFps = std::clamp(parseIntOr(g_inFps, g_exportFps), 1, 240);
        g_exportStart = std::max(0, parseIntOr(g_inStart, g_exportStart));
        g_exportEnd = std::max(0, parseIntOr(g_inEnd, g_exportEnd));
        g_mp4Crf = std::clamp(parseIntOr(g_inMp4Crf, g_mp4Crf), 0, 51);
        g_mp4Preset = std::clamp(parseIntOr(g_inMp4Preset, g_mp4Preset), 1, 8);
        g_gifColors = std::clamp(parseIntOr(g_inGifColors, g_gifColors), 2, 256);
        g_gifScalePct = std::clamp(parseIntOr(g_inGifScale, g_gifScalePct), 10, 100);
        g_pngCompression = std::clamp(parseIntOr(g_inPngComp, g_pngCompression), 0, 9);

        g_inW = std::to_string(g_exportW);
        g_inH = std::to_string(g_exportH);
        g_inFps = std::to_string(g_exportFps);
        g_inStart = std::to_string(g_exportStart);
        g_inEnd = std::to_string(g_exportEnd);
        g_inMp4Crf = std::to_string(g_mp4Crf);
        g_inMp4Preset = std::to_string(g_mp4Preset);
        g_inGifColors = std::to_string(g_gifColors);
        g_inGifScale = std::to_string(g_gifScalePct);
        g_inPngComp = std::to_string(g_pngCompression);
    }

    static void syncExportInputsFromValues()
    {
        g_inW = std::to_string(g_exportW);
        g_inH = std::to_string(g_exportH);
        g_inFps = std::to_string(g_exportFps);
        g_inStart = std::to_string(g_exportStart);
        g_inEnd = std::to_string(g_exportEnd);
        g_inMp4Crf = std::to_string(g_mp4Crf);
        g_inMp4Preset = std::to_string(g_mp4Preset);
        g_inGifColors = std::to_string(g_gifColors);
        g_inGifScale = std::to_string(g_gifScalePct);
        g_inPngComp = std::to_string(g_pngCompression);
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

    static void drawDividerH(SDL_Renderer* r, int x1, int x2, int y, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLine(r, x1, y, x2, y);
    }

    static void drawDividerV(SDL_Renderer* r, int y1, int y2, int x, SDL_Color c)
    {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLine(r, x, y1, x, y2);
    }

    static int safeRoundRadius(const SDL_Rect& rc, int wanted)
    {
        if (rc.w <= 0 || rc.h <= 0) return 0;
        int maxR = std::min(rc.w, rc.h) / 2;
        if (maxR < 0) maxR = 0;
        return std::min(wanted, maxR);
    }

    static void drawModernButton(SDL_Renderer* r, const SDL_Rect& rc, bool hover, bool active)
    {
        if (rc.w <= 0 || rc.h <= 0) return;

        const bool smallControl = (rc.w < 40 || rc.h < 22);
        const int rad = safeRoundRadius(rc, smallControl ? 6 : 10);



        SDL_Color bg = active ? COL_BTN_ACTIVE : (hover ? COL_BTN_HOVER : COL_BTN_IDLE);
        fillRoundRect(r, rc, rad, bg);
    }

    static void drawSubtleGrid(SDL_Renderer* r, const SDL_Rect& area)
    {
        SDL_SetRenderDrawColor(r, 255, 255, 255, 8);
        for (int x = area.x; x < area.x + area.w; x += 20)
            SDL_RenderDrawLine(r, x, area.y, x, area.y + area.h);
        for (int y = area.y; y < area.y + area.h; y += 20)
            SDL_RenderDrawLine(r, area.x, y, area.x + area.w, y);
    }

    struct TextTex
    {
        SDL_Texture* tex = nullptr;
        int w = 0;
        int h = 0;
    };

    static uint64_t fnv1a64(const void* data, size_t len)
    {
        const uint8_t* p = (const uint8_t*)data;
        uint64_t h = 1469598103934665603ULL;
        for (size_t i = 0; i < len; ++i)
        {
            h ^= (uint64_t)p[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    static uint64_t hashTextKey(TTF_Font* font, const std::string& text, SDL_Color c)
    {
        uint64_t h = 1469598103934665603ULL;
        h ^= fnv1a64(&font, sizeof(font));
        h *= 1099511628211ULL;
        h ^= fnv1a64(text.data(), text.size());
        h *= 1099511628211ULL;
        uint32_t rgba = ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8) | (uint32_t)c.a;
        h ^= fnv1a64(&rgba, sizeof(rgba));
        h *= 1099511628211ULL;
        return h;
    }

    struct TextCache {
        std::unordered_map<uint64_t, std::pair<TextTex, std::list<uint64_t>::iterator>> cache;
        std::list<uint64_t> lru;
        size_t cap = 600;
        SDL_Renderer* currentRenderer = nullptr;

        void invalidateRenderer(SDL_Renderer* r)
        {
            if (r != currentRenderer)
            {
                clear();
                currentRenderer = r;
            }
        }

        void clear()
        {
            for (auto& [key, val] : cache)
            {
                if (val.first.tex) SDL_DestroyTexture(val.first.tex);
            }
            cache.clear();
            lru.clear();
        }

        ~TextCache()
        {
            clear();
        }
    };

    static TextCache g_textCache;

    static void textCacheTouch(uint64_t key)
    {
        auto it = g_textCache.cache.find(key);
        if (it == g_textCache.cache.end()) return;
        g_textCache.lru.erase(it->second.second);
        g_textCache.lru.push_front(key);
        it->second.second = g_textCache.lru.begin();
    }

    static void textCacheEvictIfNeeded()
    {
        while (g_textCache.cache.size() > g_textCache.cap && !g_textCache.lru.empty())
        {
            uint64_t old = g_textCache.lru.back();
            g_textCache.lru.pop_back();
            auto it = g_textCache.cache.find(old);
            if (it != g_textCache.cache.end())
            {
                if (it->second.first.tex) SDL_DestroyTexture(it->second.first.tex);
                g_textCache.cache.erase(it);
            }
        }

    }

    static void drawText(SDL_Renderer* r, TTF_Font* font, const std::string& text, int x, int y, SDL_Color col)
    {
        if (!r || !font || text.empty()) return;
        g_textCache.invalidateRenderer(r);
        uint64_t key = hashTextKey(font, text, col);

        auto it = g_textCache.cache.find(key);
        if (it == g_textCache.cache.end())
        {
            SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), col);
            if (!surf) return;
            SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
            if (!tex) { SDL_FreeSurface(surf); return; }

            TextTex tt;
            tt.tex = tex;
            tt.w = surf->w;
            tt.h = surf->h;
            SDL_FreeSurface(surf);

            g_textCache.lru.push_front(key);
            g_textCache.cache[key] = { tt, g_textCache.lru.begin() };
            textCacheEvictIfNeeded();
            it = g_textCache.cache.find(key);
        }
        else
        {
            textCacheTouch(key);
        }

        const TextTex& tt = it->second.first;
        SDL_Rect dst{ x, y, tt.w, tt.h };
        SDL_RenderCopy(r, tt.tex, nullptr, &dst);
    }

    static int measureTextW(TTF_Font* font, const std::string& text)
    {
        if (!font || text.empty()) return 0;
        int w = 0, h = 0;
        if (TTF_SizeUTF8(font, text.c_str(), &w, &h) != 0) return 0;
        return w;
    }

    static SDL_FPoint screenToWorldPoint(int sx, int sy, const UILayout& ui, float panX, float panY, float scale)
    {
        SDL_FPoint out{ 0, 0 };
        if (scale == 0.0f) return out;
        out.x = (float)(sx - ui.canvas.x - panX) / scale;
        out.y = (float)(sy - ui.canvas.y - panY) / scale;
        return out;
    }

    static SDL_FPoint worldToScreenPoint(float wx, float wy, const UILayout& ui, float panX, float panY, float scale)
    {
        SDL_FPoint out;
        out.x = (float)ui.canvas.x + panX + wx * scale;
        out.y = (float)ui.canvas.y + panY + wy * scale;
        return out;
    }

    static SDL_FRect getPageRectF(const UILayout& ui, int projectW, int projectH, float panX, float panY, float scale)
    {
        SDL_FRect page;
        page.x = (float)ui.canvas.x + panX;
        page.y = (float)ui.canvas.y + panY;
        page.w = (float)projectW * scale;
        page.h = (float)projectH * scale;
        return page;
    }

    static bool pointInRectF(int x, int y, const SDL_FRect& r)
    {
        if (r.w <= 0 || r.h <= 0) return false;
        return (x >= (int)r.x) && (x < (int)(r.x + r.w)) && (y >= (int)r.y) && (y < (int)(r.y + r.h));
    }

    static void buildTopBarButtonRects(const UILayout& ui, SDL_Rect& outUndo, SDL_Rect& outRedo, SDL_Rect& outOnion, SDL_Rect& outColor)
    {
        const int btnSize = 40;
        const int pad = 6;
        outUndo = { ui.topBar.x + pad, ui.topBar.y + pad, btnSize - pad * 2, btnSize - pad * 2 };
        outRedo = { outUndo.x + outUndo.w + pad, outUndo.y, outUndo.w, outUndo.h };
        outOnion = { 0, 0, 0, 0 };
        outColor = { outRedo.x + outRedo.w + pad, outUndo.y, outUndo.w, outUndo.h };
    }

    static SDL_Rect toolsColorPickerButtonRect(const SDL_Rect& toolsArea)
    {
        return SDL_Rect{ toolsArea.x + 10, toolsArea.y + std::max(0, toolsArea.h - 46), std::max(0, toolsArea.w - 20), 36 };
    }

    static void rebuildActiveModals(int w, int h)
    {
        g_activeModals.clear();
        if (g_windowMenuOpen) g_activeModals.push_back(ModalWindow{ ModalKind::WindowMenu, g_windowMenuR });
        if (g_exportMenuOpen) g_activeModals.push_back(ModalWindow{ ModalKind::ExportMenu, g_exportMenuR });
        if (g_exportSettingsOpen)
            g_activeModals.push_back(ModalWindow{ ModalKind::ExportSettings, SDL_Rect{ w / 2 - 320, h / 2 - 240, 640, 480 } });
    }

    static void handleLeftBarResize(SDL_Event& e, const UILayout& ui, int windowW, float& leftBarRatio, bool& resizingLeftBar)
    {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            int edgeX = ui.leftBar.x + ui.leftBar.w;
            if (std::abs(e.button.x - edgeX) < 6) resizingLeftBar = true;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) resizingLeftBar = false;
        if (e.type == SDL_MOUSEMOTION && resizingLeftBar)
        {
            float ratio = (float)e.motion.x / (float)windowW;
            leftBarRatio = fclamp(ratio, 0.05f, 0.28f);
        }
    }

    static void splitLeftBar(const SDL_Rect& leftBar, SDL_Rect& toolsArea, SDL_Rect& pickerArea, SDL_Rect& optionsArea)
    {
        const int pad = 10;
        const int buttonH = 40;
        const int footerH = pad + buttonH + pad;
        toolsArea = leftBar;
        toolsArea.h = std::max(0, leftBar.h - footerH);
        pickerArea = leftBar;
        pickerArea.y = toolsArea.y + toolsArea.h;
        pickerArea.h = std::max(0, leftBar.y + leftBar.h - pickerArea.y);
        optionsArea = leftBar;
        optionsArea.y = leftBar.y + leftBar.h;
        optionsArea.h = 0;
    }

    static SDL_Rect leftColorButtonRect(const SDL_Rect& pickerArea)
    {
        const int pad = 10;
        const int buttonH = 40;
        int y = pickerArea.y + pad;
        return SDL_Rect{ pickerArea.x + pad, y, std::max(0, pickerArea.w - pad * 2), buttonH };
    }

    static SDL_Rect leftColorPopupRect(const SDL_Rect& leftBar, const SDL_Rect& buttonRc, const SDL_Rect& topBar, const SDL_Rect& bottomBar, int windowW)
    {
        const int margin = 10;
        const int popupW = 320;
        int minY = topBar.y + topBar.h + margin;
        int maxYBottom = bottomBar.y - margin;
        int availH = std::max(180, maxYBottom - minY);
        int popupH = std::min(560, availH);
        int x = leftBar.x + leftBar.w + margin;
        int y = buttonRc.y - (popupH / 2) + (buttonRc.h / 2);
        int maxX = std::max(margin, windowW - popupW - margin);
        x = std::clamp(x, margin, maxX);
        int maxY = std::max(minY, maxYBottom - popupH);
        y = std::clamp(y, minY, maxY);
        return SDL_Rect{ x, y, popupW, popupH };
    }

    static SDL_Rect leftColorPopupInnerRect(const SDL_Rect& popupRc)
    {
        const int inset = 8;
        return SDL_Rect{
            popupRc.x + inset,
            popupRc.y + inset,
            std::max(0, popupRc.w - inset * 2),
            std::max(0, popupRc.h - inset * 2)
        };
    }
    static SDL_Rect dockWorkspace(const App& app, int w, int h)
    {
        const SDL_Rect& top = app.getUILayout().topBar;
        const int topY = top.y + top.h;
        return SDL_Rect{ 0, topY, std::max(1, w), std::max(1, h - topY) };
    }

    static void syncDockPanelsToLegacyLayout(App& app)
    {
        const SDL_Rect canvasRc = app.dockManager().contentRect("Canvas");
        const SDL_Rect toolsRc = app.dockManager().contentRect("Tools");
        const SDL_Rect timelineRc = app.dockManager().contentRect("Timeline");

        auto applyIfUsable = [](SDL_Rect& dst, const SDL_Rect& src)
            {
                if (src.w > 0 && src.h > 0)
                    dst = src;
            };

        applyIfUsable(app.getUILayout().canvas, canvasRc);
        applyIfUsable(app.getUILayout().leftBar, toolsRc);
        applyIfUsable(app.getUILayout().bottomBar, timelineRc);
    }

    struct TimelinePanelRects
    {
        SDL_Rect transport{};
        SDL_Rect strip{};
        SDL_Rect keyframes{};
        SDL_Rect splitter{};
        SDL_Rect status{};
        SDL_Rect play{};
        SDL_Rect stop{};
        SDL_Rect add{};
    };

    static TimelinePanelRects buildTimelinePanelRects(const App& app, const SDL_Rect& contentRc)
    {
        TimelinePanelRects out{};
        if (contentRc.w <= 0 || contentRc.h <= 0) return out;

        const bool keyframeMode = false;
        const int pad = 8;
        const int gap = 10;
        const int statusH = 22;
        const int innerW = std::max(0, contentRc.w - pad * 2);
        const int innerH = std::max(0, contentRc.h - pad * 2);
        const int transportW = std::min(std::max(260, innerW / 4), std::max(220, innerW));

        out.transport = {
            contentRc.x + pad,
            contentRc.y + pad,
            std::max(0, std::min(transportW, innerW)),
            std::max(56, innerH)
        };

        if (out.transport.w > innerW)
            out.transport.w = innerW;
        if (out.transport.h > innerH)
            out.transport.h = innerH;

        const int btnY = out.transport.y + 10;
        const int btnGap = 8;
        const int btnW = std::clamp((std::max(0, out.transport.w - 20) - btnGap * 2) / 3, 56, 100);
        const int btnH = 34;
        out.play = { out.transport.x + 10, btnY, btnW, btnH };
        out.stop = { out.play.x + out.play.w + btnGap, btnY, btnW, btnH };
        out.add = { out.stop.x + out.stop.w + btnGap, btnY, btnW, btnH };
        out.status = {
            out.transport.x + 10,
            std::max(out.transport.y + 10, out.transport.y + out.transport.h - statusH - 8),
            std::max(0, out.transport.w - 20),
            statusH
        };

        const int stripX = out.transport.x + out.transport.w + gap;
        const int rightW = std::max(0, contentRc.x + contentRc.w - stripX - pad);
        const int minPanelH = 120;
        const int splitterH = 6;
        int keyH = g_keyframePanelHeight;
        if (rightW > 0)
        {
            if (keyframeMode)
            {
                const int minNeeded = minPanelH * 2 + splitterH;
                if (innerH >= minNeeded)
                    keyH = clampi(keyH, minPanelH, innerH - minPanelH - splitterH);
                else
                    keyH = std::max(48, (innerH - splitterH) / 2);

                out.keyframes = { stripX, contentRc.y + pad, rightW, std::max(0, keyH) };
                out.splitter = { stripX, out.keyframes.y + out.keyframes.h, rightW, splitterH };
                out.strip = { stripX, out.splitter.y + splitterH, rightW, std::max(0, innerH - out.keyframes.h - splitterH) };
            }
            else
            {
                out.keyframes = SDL_Rect{};
                out.splitter = SDL_Rect{};
                out.strip = { stripX, contentRc.y + pad, rightW, innerH };
            }
        }

        const SDL_Rect bounds = contentRc;
        auto clampRect = [&](SDL_Rect& rc)
            {
                if (rc.w <= 0 || rc.h <= 0) return;
                if (rc.x < bounds.x) { rc.w -= (bounds.x - rc.x); rc.x = bounds.x; }
                if (rc.y < bounds.y) { rc.h -= (bounds.y - rc.y); rc.y = bounds.y; }
                const int maxRight = bounds.x + bounds.w;
                const int maxBottom = bounds.y + bounds.h;
                if (rc.x + rc.w > maxRight) rc.w = std::max(0, maxRight - rc.x);
                if (rc.y + rc.h > maxBottom) rc.h = std::max(0, maxBottom - rc.y);
            };

        clampRect(out.transport);
        clampRect(out.play);
        clampRect(out.stop);
        clampRect(out.add);
        clampRect(out.status);
        clampRect(out.keyframes);
        clampRect(out.splitter);
        clampRect(out.strip);
        g_keyframePanelRect = out.keyframes;
        g_keyframeSplitterRect = out.splitter;
        return out;
    }

    static SDL_Rect timelineQuickButtonRect(const TimelinePanelRects& r, int index)
    {
        const int w = std::clamp((std::max(0, r.transport.w - 20) - 16) / 3, 56, 100);
        const int h = 34;
        const int y = r.play.y + r.play.h + 28;
        return SDL_Rect{ r.transport.x + 10 + index * (w + 8), y, w, h };
    }

    static SDL_Rect timelineManualButtonRect(const TimelinePanelRects& r, int index)
    {
        const int w = std::clamp((std::max(0, r.transport.w - 20) - 16) / 3, 56, 100);
        const int h = 34;
        const int row = (index < 3) ? 0 : 1;
        const int col = (index < 3) ? index : (index - 3);
        const int y0 = r.play.y + r.play.h + 28;
        const int y = y0 + row * (h + 28);
        return SDL_Rect{ r.transport.x + 10 + col * (w + 8), y, w, h };
    }

    static SDL_Rect timelineRangeButtonRect(const TimelinePanelRects& r, bool endRange, bool increment)
    {
        const int w = 24;
        const int h = 22;
        const int baseY = r.status.y - h - 8;
        const int baseX = r.transport.x + 10 + (endRange ? 132 : 0);
        return SDL_Rect{ baseX + (increment ? 70 : 0), baseY, w, h };
    }

    static int keyframePanelLabelW() { return 96; }
    static int keyframePanelRulerH() { return 24; }
    static int keyframePanelRowH() { return 34; }

    static SDL_Rect keyframeFrameRect(const SDL_Rect& panel)
    {
        const int labelW = keyframePanelLabelW();
        return SDL_Rect{ panel.x + labelW, panel.y, std::max(0, panel.w - labelW), panel.h };
    }

    static SDL_Rect keyframeRowRect(const SDL_Rect& panel, int rowIndex)
    {
        const int top = panel.y + keyframePanelRulerH() + 10;
        return SDL_Rect{ panel.x + 4, top + rowIndex * keyframePanelRowH(), std::max(0, panel.w - 8), keyframePanelRowH() - 2 };
    }

    static SDL_Rect keyframeLaneRect(const SDL_Rect& panel, int rowIndex)
    {
        SDL_Rect row = keyframeRowRect(panel, rowIndex);
        const int labelW = keyframePanelLabelW();
        return SDL_Rect{ panel.x + labelW, row.y, std::max(0, panel.w - labelW - 4), row.h };
    }

    static int keyframeFrameToX(const App& app, const SDL_Rect& panel, int frameIndex)
    {
        const auto& st = app.timeline.state();
        const float px = std::max(st.minPxPerFrame, st.pxPerFrame);
        const SDL_Rect area = keyframeFrameRect(panel);
        return area.x + (int)std::lround((double)frameIndex * px) - st.scrollX + (int)std::lround(px * 0.5);
    }

    static int keyframeXToFrame(const App& app, const SDL_Rect& panel, int x)
    {
        const auto& st = app.timeline.state();
        const float px = std::max(st.minPxPerFrame, st.pxPerFrame);
        const SDL_Rect area = keyframeFrameRect(panel);
        const float local = (float)(x - area.x + st.scrollX);
        const int frame = (int)std::floor(local / std::max(1.0f, px));
        const int totalFrames = std::max(1, (int)app.getEngine().getFrameCount());
        return clampi(frame, 0, std::max(0, totalFrames - 1));
    }

    static const std::vector<DrawingEngine::TransformKeyframe>* trackChannelKeys(
        const App& app,
        int trackId,
        DrawingEngine::TransformChannel channel)
    {
        return app.getEngine().getTransformKeyframes(trackId, channel);
    }

    static bool findTransformKeyAtPoint(
        App& app,
        const SDL_Rect& panel,
        int mx,
        int my,
        DrawingEngine::TransformChannel& outChannel,
        int& outFrame)
    {
        const int trackId = activeLayerTree(app).primarySelectedTrackId();
        if (trackId == 0)
            return false;

        const DrawingEngine::TransformChannel channels[3] = {
            DrawingEngine::TransformChannel::PosX,
            DrawingEngine::TransformChannel::PosY,
            DrawingEngine::TransformChannel::Rotation
        };

        for (int row = 0; row < 3; ++row)
        {
            SDL_Rect lane = keyframeLaneRect(panel, row);
            if (!pointInRect(mx, my, lane))
                continue;

            const auto* keys = trackChannelKeys(app, trackId, channels[row]);
            if (!keys)
                continue;

            for (const auto& key : *keys)
            {
                const int keyX = keyframeFrameToX(app, panel, key.frameIndex);
                SDL_Rect hit{ keyX - 8, lane.y + 4, 16, std::max(12, lane.h - 8) };
                if (pointInRect(mx, my, hit))
                {
                    outChannel = channels[row];
                    outFrame = key.frameIndex;
                    return true;
                }
            }
        }
        return false;
    }

    static void drawTransformKeyframePanel(App& app, SDL_Renderer* r, TTF_Font* font, const SDL_Rect& panel, int mx, int my)
    {
        if (panel.w <= 0 || panel.h <= 0) return;

        fillRect(r, panel, SDL_Color{ 18, 22, 30, 220 });
        drawRect(r, panel, g_keyframePanelFocused ? SDL_Color{ 110, 170, 255, 210 } : COL_BORDER_SOFT);

        SDL_Rect frameArea = keyframeFrameRect(panel);
        SDL_Rect ruler{ frameArea.x, panel.y + 2, frameArea.w, keyframePanelRulerH() };
        SDL_Rect labelHeader{ panel.x + 4, panel.y + 2, keyframePanelLabelW() - 8, keyframePanelRulerH() };
        drawText(r, font, "FlowLink Samples", labelHeader.x + 4, labelHeader.y + 4, COL_TEXT_MAIN);

        const float px = std::max(app.timeline.state().minPxPerFrame, app.timeline.state().pxPerFrame);
        const int totalFrames = std::max(1, std::max(1, (int)app.getEngine().getFrameCount()));
        const int firstFrame = std::max(0, keyframeXToFrame(app, panel, frameArea.x));
        const int lastFrame = std::min(totalFrames - 1, keyframeXToFrame(app, panel, frameArea.x + frameArea.w + (int)px));
        for (int frame = firstFrame; frame <= lastFrame; ++frame)
        {
            const int x = keyframeFrameToX(app, panel, frame);
            const bool major = (frame % 5) == 0;
            SDL_SetRenderDrawColor(r, 70, 78, 96, major ? 180 : 90);
            SDL_RenderDrawLine(r, x, panel.y + keyframePanelRulerH(), x, panel.y + panel.h - 4);
            if (major)
                drawText(r, font, std::to_string(frame + 1), x - 8, ruler.y + 4, COL_TEXT_DIM);
        }

        const int playhead = (int)app.getEngine().getCurrentFrameIndex();
        const int playheadX = keyframeFrameToX(app, panel, playhead);
        SDL_SetRenderDrawColor(r, COL_ACCENT.r, COL_ACCENT.g, COL_ACCENT.b, 220);
        SDL_RenderDrawLine(r, playheadX, panel.y + 2, playheadX, panel.y + panel.h - 4);

        const int trackId = activeLayerTree(app).primarySelectedTrackId();
        const DrawingEngine::TransformChannel channels[3] = {
            DrawingEngine::TransformChannel::PosX,
            DrawingEngine::TransformChannel::PosY,
            DrawingEngine::TransformChannel::Rotation
        };

        for (int row = 0; row < 3; ++row)
        {
            SDL_Rect rowRect = keyframeRowRect(panel, row);
            SDL_Rect lane = keyframeLaneRect(panel, row);
            fillRect(r, lane, SDL_Color{ 24, 28, 38, static_cast<Uint8>(row == 1 ? 210 : 190) });
            drawRect(r, lane, SDL_Color{ 60, 68, 84, 180 });
            drawText(r, font, transformChannelLabel(channels[row]), rowRect.x + 6, rowRect.y + 7, COL_TEXT_DIM);

            if (trackId != 0)
            {
                const auto* keys = trackChannelKeys(app, trackId, channels[row]);
                if (keys)
                {
                    for (const auto& key : *keys)
                    {
                        const int x = keyframeFrameToX(app, panel, key.frameIndex);
                        const int cy = lane.y + lane.h / 2;
                        const bool selected = g_selectedTransformKey.valid &&
                            g_selectedTransformKey.trackId == trackId &&
                            g_selectedTransformKey.channel == channels[row] &&
                            g_selectedTransformKey.frameIndex == key.frameIndex;
                        const bool hover = std::abs(mx - x) <= 8 && my >= lane.y && my < lane.y + lane.h;
                        fillCircle(r, x, cy, selected ? 7 : 5, selected ? SDL_Color{ 255, 220, 120, 255 } : (hover ? SDL_Color{ 170, 210, 255, 240 } : SDL_Color{ 235, 240, 248, 230 }));
                        drawCircle(r, x, cy, selected ? 8 : 6, SDL_Color{ 24, 28, 36, 255 });
                    }
                }
            }
        }

        if (trackId == 0)
            drawText(r, font, "Select a layer to capture transforms", frameArea.x + 10, panel.y + keyframePanelRulerH() + 10, COL_TEXT_DIM);
    }

    static void drawMiniPanelTitle(SDL_Renderer* r, TTF_Font* font, const char* title, const SDL_Rect& rc)
    {
        drawText(r, font, title ? title : "", rc.x + 6, rc.y + 2, COL_TEXT_DIM);
        SDL_Rect line{ rc.x, rc.y + 20, rc.w, 1 };
        fillRect(r, line, SDL_Color{ 66, 72, 86, 180 });
    }

    static void drawColorSummaryPanel(App& app, SDL_Renderer* r, TTF_Font* font, const SDL_Rect& contentRc, int mx, int my)
    {
        if (contentRc.w <= 0 || contentRc.h <= 0) return;
        drawMiniPanelTitle(r, font, "Current Color", contentRc);
        SDL_Color c = app.colorPickerWidget().getColorRGBA();
        SDL_Rect sw{ contentRc.x + 10, contentRc.y + 34, 72, 72 };
        fillRoundRect(r, sw, 8, c);
        strokeRoundRect(r, sw, 8, SDL_Color{ 255,255,255,60 });
        drawText(r, font, "Open floating picker", sw.x + sw.w + 14, sw.y + 10, pointInRect(mx, my, SDL_Rect{ sw.x + sw.w + 10, sw.y, std::max(120, contentRc.w - 110), 28 }) ? COL_TEXT_MAIN : COL_TEXT_DIM);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "RGBA %d %d %d %d", (int)c.r, (int)c.g, (int)c.b, (int)c.a);
        drawText(r, font, buf, sw.x + sw.w + 14, sw.y + 38, COL_TEXT_DIM);
    }

    static SDL_Rect colorPanelOpenButtonRect(const SDL_Rect& contentRc)
    {
        return SDL_Rect{ contentRc.x + 96, contentRc.y + 30, std::max(120, contentRc.w - 106), 32 };
    }

    static void drawFramesPanel(App& app, SDL_Renderer* r, TTF_Font* font, const SDL_Rect& contentRc, int mx, int my)
    {
        if (contentRc.w <= 0 || contentRc.h <= 0) return;
        drawMiniPanelTitle(r, font, "Frame Navigation", contentRc);
        const int total = std::max(1, (int)app.getEngine().getFrameCount());
        const int current = (int)app.getEngine().getCurrentFrameIndex() + 1;
        drawText(r, font, std::string("Current: ") + std::to_string(current) + " / " + std::to_string(total), contentRc.x + 10, contentRc.y + 34, COL_TEXT_MAIN);
        SDL_Rect prev{ contentRc.x + 10, contentRc.y + 68, 64, 30 };
        SDL_Rect next{ prev.x + prev.w + 8, prev.y, 64, 30 };
        SDL_Rect add{ next.x + next.w + 8, prev.y, 64, 30 };
        drawModernButton(r, prev, pointInRect(mx, my, prev), false);
        drawModernButton(r, next, pointInRect(mx, my, next), false);
        drawModernButton(r, add, pointInRect(mx, my, add), false);
        drawText(r, font, "Prev", prev.x + 14, prev.y + 6, COL_TEXT_MAIN);
        drawText(r, font, "Next", next.x + 14, next.y + 6, COL_TEXT_MAIN);
        drawText(r, font, "Add", add.x + 18, add.y + 6, COL_TEXT_MAIN);
    }

    static SDL_Rect framesPrevButtonRect(const SDL_Rect& contentRc) { return SDL_Rect{ contentRc.x + 10, contentRc.y + 68, 64, 30 }; }
    static SDL_Rect framesNextButtonRect(const SDL_Rect& contentRc) { SDL_Rect prev = framesPrevButtonRect(contentRc); return SDL_Rect{ prev.x + prev.w + 8, prev.y, 64, 30 }; }
    static SDL_Rect framesAddButtonRect(const SDL_Rect& contentRc) { SDL_Rect next = framesNextButtonRect(contentRc); return SDL_Rect{ next.x + next.w + 8, next.y, 64, 30 }; }

    static void drawPreviewPanel(App& app, SDL_Renderer* r, TTF_Font* font, const SDL_Rect& contentRc)
    {
        if (contentRc.w <= 0 || contentRc.h <= 0) return;

        SDL_Rect body = contentRc;
        fillRoundRect(r, body, 8, SDL_Color{ 18, 20, 26, 255 });
        strokeRoundRect(r, body, 8, SDL_Color{ 86, 94, 116, 180 });

        const size_t frameIndex = app.getEngine().getCurrentFrameIndex();
        bool hasAnyLayer = false;
        for (const auto& tr : app.getEngine().getTracks())
        {
            if (tr.kind != DrawingEngine::TrackKind::Draw && tr.kind != DrawingEngine::TrackKind::Flow) continue;
            const DrawingEngine::TrackLayer layer = app.getEngine().getEvaluatedFrameTrackLayerCopy(frameIndex, tr.id);
            if (layer.trackId == 0 || !layer.visible) continue;
            if (!layer.strokes.empty() || !layer.image.empty())
            {
                hasAnyLayer = true;
                break;
            }
        }
        if (!hasAnyLayer)
        {
            drawText(r, font, "No frame preview", body.x + 12, body.y + 12, COL_TEXT_DIM);
            return;
        }

        const float docW = std::max(1, app.getProjectW());
        const float docH = std::max(1, app.getProjectH());
        const float scaleX = (float)body.w / docW;
        const float scaleY = (float)body.h / docH;
        const float sc = std::max(0.01f, std::min(scaleX, scaleY));
        const float previewW = docW * sc;
        const float previewH = docH * sc;
        const float ox = (float)body.x + ((float)body.w - previewW) * 0.5f;
        const float oy = (float)body.y + ((float)body.h - previewH) * 0.5f;

        SDL_Rect oldClip{};
        const SDL_bool hadClip = SDL_RenderIsClipEnabled(r);
        if (hadClip) SDL_RenderGetClipRect(r, &oldClip);
        SDL_RenderSetClipRect(r, &body);

        SDL_FRect page{ ox, oy, previewW, previewH };
        SDL_SetRenderDrawColor(r, COL_PAGE.r, COL_PAGE.g, COL_PAGE.b, 255);
        SDL_RenderFillRectF(r, &page);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 48);
        if (sdlSupportsFloatLines)
            SDL_RenderDrawRectF(r, &page);
        else
        {
            SDL_Rect pageI{ (int)page.x, (int)page.y, (int)page.w, (int)page.h };
            SDL_RenderDrawRect(r, &pageI);
        }

        auto& imageCache = g_persistentFrameImageCache;
        const std::uint64_t cacheFrameOrdinal = app.runtimeStateRef().diagnostics.frameOrdinal;
        for (const auto& tr : app.getEngine().getTracks())
        {
            if (tr.kind != DrawingEngine::TrackKind::Draw && tr.kind != DrawingEngine::TrackKind::Flow) continue;
            if (!tr.visible || tr.muted) continue;
            DrawingEngine::TrackLayer layer = app.getEngine().getEvaluatedFrameTrackLayerCopy(frameIndex, tr.id);
            if (layer.trackId == 0 || !layer.visible) continue;
            if (layer.strokes.empty() && layer.image.empty()) continue;
            const std::string imageKey = std::to_string((int)frameIndex) + ":preview:" + std::to_string(tr.id) + ":" + std::to_string(layer.celId) + ":" + std::to_string(layer.imageRevision);
            const std::string strokeKey = std::to_string((int)frameIndex) + ":preview:" + std::to_string(tr.id) + ":" + std::to_string(layer.contentRevision) + ":" + std::to_string(layer.transformRevision);
            strova::layer_render::drawTrackLayer(r, *app.brushRendererHandle(), layer, sc, ox, oy, 0, 0, 1.0f, &imageCache, imageKey, strokeKey, cacheFrameOrdinal);
            g_persistentFrameImageCacheLastUse[imageKey] = cacheFrameOrdinal;
        }

        if (hadClip) SDL_RenderSetClipRect(r, &oldClip); else SDL_RenderSetClipRect(r, nullptr);
    }

    static void drawFloatingColorPickerWindow(App& app, SDL_Renderer* r, TTF_Font* font, int mx, int my)
    {
        auto& st = app.colorPickerWindowState();
        if (!st.visible) return;
        SDL_Rect shadow = st.rect;
        shadow.x += 5; shadow.y += 5;
        drawRect(r, shadow, SDL_Color{ 0,0,0,40 });
        fillRoundRect(r, st.rect, 10, SDL_Color{ 24, 28, 36, 246 });
        strokeRoundRect(r, st.rect, 10, SDL_Color{ 96, 104, 126, 220 });
        SDL_Rect header{ st.rect.x, st.rect.y, st.rect.w, 26 };
        fillRect(r, header, SDL_Color{ 56, 66, 94, 240 });
        drawText(r, font, "Color Selector", header.x + 8, header.y + 4, COL_TEXT_MAIN);
        SDL_Rect closeR{ header.x + header.w - 22, header.y + 4, 16, 16 };
        drawModernButton(r, closeR, pointInRect(mx, my, closeR), false);
        drawText(r, font, "X", closeR.x + 4, closeR.y - 1, COL_TEXT_MAIN);
        SDL_Rect inner = leftColorPopupInnerRect(SDL_Rect{ st.rect.x + 2, st.rect.y + 26, std::max(0, st.rect.w - 4), std::max(0, st.rect.h - 28) });
        app.colorPickerWidget().layout(inner);
        SDL_RenderSetClipRect(r, &inner);
        app.colorPickerWidget().draw(r);
        SDL_RenderSetClipRect(r, nullptr);
    }

    static SDL_Rect floatingColorPickerHeaderRect(const App& app)
    {
        const auto& st = app.colorPickerWindowState();
        return SDL_Rect{ st.rect.x, st.rect.y, st.rect.w, 26 };
    }

    static SDL_Rect floatingColorPickerCloseRect(const App& app)
    {
        const auto& st = app.colorPickerWindowState();
        return SDL_Rect{ st.rect.x + st.rect.w - 22, st.rect.y + 4, 16, 16 };
    }

    static int toolGridColsForWidth(int w)
    {
        const int pad = 10;
        const int gap = 10;
        const int buttonSize = 44;
        const int avail = std::max(1, w - pad * 2);
        return std::max(1, (avail + gap) / (buttonSize + gap));
    }

    struct ToolGridMetrics
    {
        int cols = 1;
        int rows = 0;
        int btn = 44;
        int x0 = 0;
        int y0 = 0;
    };

    static int toolModeButtonCols(const SDL_Rect& toolsArea)
    {
        return (toolsArea.w >= 340) ? 4 : 2;
    }

    static int toolModeStripHeight(const SDL_Rect& toolsArea)
    {
        const int cols = toolModeButtonCols(toolsArea);
        const int rows = (4 + cols - 1) / cols;
        return rows * 32 + std::max(0, rows - 1) * 8;
    }

    static int toolsPanelContentHeight(const SDL_Rect& toolsArea)
    {
        const int pad = 10;
        const int gap = 10;
        const int count = (int)(sizeof(kToolBtns) / sizeof(kToolBtns[0]));
        const int cols = std::max(1, toolGridColsForWidth(toolsArea.w));
        const int availW = std::max(1, toolsArea.w - pad * 2);
        const int btn = std::clamp((availW - (cols - 1) * gap) / cols, 28, 44);
        const int rows = (count + cols - 1) / cols;
        const int gridTop = 8 + toolModeStripHeight(toolsArea) + 12;
        const int gridHeight = rows * btn + std::max(0, rows - 1) * gap;
        return gridTop + gridHeight + 8;
    }

    static SDL_Rect toolsScrollBodyRect(const SDL_Rect& toolsArea)
    {
        SDL_Rect pickerBtn = toolsColorPickerButtonRect(toolsArea);
        return SDL_Rect{
            toolsArea.x,
            toolsArea.y,
            toolsArea.w,
            std::max(0, pickerBtn.y - toolsArea.y - 8)
        };
    }

    static int toolsPanelMaxScroll(const SDL_Rect& toolsArea)
    {
        const SDL_Rect body = toolsScrollBodyRect(toolsArea);
        return std::max(0, toolsPanelContentHeight(toolsArea) - std::max(0, body.h));
    }

    static ToolGridMetrics calcToolGridMetrics(const SDL_Rect& toolsArea)
    {
        const int pad = 10;
        const int gap = 10;
        const int count = (int)(sizeof(kToolBtns) / sizeof(kToolBtns[0]));
        ToolGridMetrics m{};
        int availW = std::max(1, toolsArea.w - pad * 2);
        m.cols = std::max(1, toolGridColsForWidth(toolsArea.w));
        int maxBtnByWidth = (availW - (m.cols - 1) * gap) / m.cols;
        m.btn = std::clamp(maxBtnByWidth, 28, 44);
        m.rows = (count + m.cols - 1) / m.cols;
        int usedW = m.cols * m.btn + (m.cols - 1) * gap;
        m.x0 = toolsArea.x + std::max(0, (toolsArea.w - usedW) / 2);
        m.y0 = toolsArea.y + 8 + toolModeStripHeight(toolsArea) + 12 - g_toolsPanelScroll;
        return m;
    }

    static SDL_Rect toolModeButtonRect(const SDL_Rect& toolsArea, int index)
    {
        const int gap = 8;
        const int cols = toolModeButtonCols(toolsArea);
        const int row = index / cols;
        const int col = index % cols;
        const int x0 = toolsArea.x + 10;
        const int y0 = toolsArea.y + 8 - g_toolsPanelScroll;
        const int innerW = std::max(1, toolsArea.w - 20);
        const int bw = (innerW - gap * (cols - 1)) / cols;
        const int bh = 32;
        return SDL_Rect{ x0 + col * (bw + gap), y0 + row * (bh + gap), std::max(52, bw), bh };
    }

    static SDL_Rect quickAnimPanelRect(const App& app)
    {
        SDL_Rect panel = flowSettingsPanelRect(app);
        if (panel.w > 0 && panel.h > 0) return panel;
        const SDL_Rect rr = app.rightBarRect();
        return SDL_Rect{ rr.x + 9, rr.y + 9, std::max(260, rr.w - 18), std::max(220, rr.h - 18) };
    }

    static SDL_Rect quickAnimRowRect(const SDL_Rect& panel, int rowIndex, int scroll)
    {
        return SDL_Rect{ panel.x + 12, panel.y + 38 + rowIndex * 34 - scroll, std::max(0, panel.w - 24), 28 };
    }
    static SDL_Rect quickAnimScrollBodyRect(const SDL_Rect& panel)
    {
        return SDL_Rect{ panel.x + 6, panel.y + 30, std::max(0, panel.w - 12), std::max(0, panel.h - 36) };
    }

    static int quickAnimPanelMaxScroll()
    {
        const int rowCount = 8;
        const int rowPitch = 34;
        const int rowHeight = 28;
        const int contentTop = 38;
        const int contentBottom = contentTop + (rowCount - 1) * rowPitch + rowHeight;
        const int contentHeight = contentBottom + 6;
        const SDL_Rect panel = g_quickAnimPanelR;
        const SDL_Rect body = quickAnimScrollBodyRect(panel);
        return std::max(0, contentHeight - body.h);
    }


    static void syncLogicalToolMode(App& app)
    {
        switch (app.activeToolModeValue())
        {
        case ToolMode::Move:
            g_transformToolMode = TransformToolMode::Move;
            g_editorInputMode = EditorInputMode::Transform;
            break;
        case ToolMode::Rotate:
            g_transformToolMode = TransformToolMode::Rotate;
            g_editorInputMode = EditorInputMode::Transform;
            break;
        case ToolMode::Select:
            g_transformToolMode = TransformToolMode::None;
            g_editorInputMode = EditorInputMode::Draw;
            if (app.getEditorUiState().activeTool != ToolType::Select)
                app.setToolCommand(ToolType::Select);
            break;
        case ToolMode::Draw:
        default:
            g_transformToolMode = TransformToolMode::None;
            g_editorInputMode = EditorInputMode::Draw;
            if (app.getEditorUiState().activeTool == ToolType::Select)
                app.setToolCommand(ToolType::Brush);
            break;
        }
    }

    static void armFlowLink(App& app)
    {
        if (app.flowCapturer().armed) app.flowCapturer().disarm();
        else app.flowCapturer().arm();
        if (!app.flowCapturer().armed) return;
        if (activeLayerTree(app).primarySelectedTrackId() == 0) return;
        app.activeToolModeRef() = ToolMode::Move;
        syncLogicalToolMode(app);
        g_quickAnimPanelR = quickAnimPanelRect(app);
    }

    static void drawTransformToolStrip(App& app, SDL_Renderer* r, TTF_Font* font, const SDL_Rect& toolsArea)
    {
        g_toolModeDrawBtnR = toolModeButtonRect(toolsArea, 0);
        g_toolModeMoveBtnR = toolModeButtonRect(toolsArea, 1);
        g_toolModeRotateBtnR = toolModeButtonRect(toolsArea, 2);
        g_toolModeSelectBtnR = toolModeButtonRect(toolsArea, 3);
        g_transformMoveBtnR = g_toolModeMoveBtnR;
        g_transformRotateBtnR = g_toolModeRotateBtnR;
        g_transformAutoBtnR = SDL_Rect{};
        g_transformAddKeyBtnR = SDL_Rect{};
        g_transformDeleteKeyBtnR = SDL_Rect{};

        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        drawModernButton(r, g_toolModeDrawBtnR, pointInRect(mx, my, g_toolModeDrawBtnR), app.activeToolModeValue() == ToolMode::Draw);
        drawModernButton(r, g_toolModeMoveBtnR, pointInRect(mx, my, g_toolModeMoveBtnR), app.activeToolModeValue() == ToolMode::Move);
        drawModernButton(r, g_toolModeRotateBtnR, pointInRect(mx, my, g_toolModeRotateBtnR), app.activeToolModeValue() == ToolMode::Rotate);
        drawModernButton(r, g_toolModeSelectBtnR, pointInRect(mx, my, g_toolModeSelectBtnR), app.activeToolModeValue() == ToolMode::Select);

        drawText(r, font, "Draw", g_toolModeDrawBtnR.x + 18, g_toolModeDrawBtnR.y + 8, COL_TEXT_MAIN);
        drawText(r, font, "Move", g_toolModeMoveBtnR.x + 18, g_toolModeMoveBtnR.y + 8, COL_TEXT_MAIN);
        drawText(r, font, "Rotate", g_toolModeRotateBtnR.x + 12, g_toolModeRotateBtnR.y + 8, COL_TEXT_MAIN);
        drawText(r, font, "Select", g_toolModeSelectBtnR.x + 12, g_toolModeSelectBtnR.y + 8, COL_TEXT_MAIN);
    }

    static void drawQuickAnimPanel(App& app, SDL_Renderer* r, TTF_Font* font)
    {
        if (!flowSettingsPanelVisible(app)) { g_quickAnimPanelR = SDL_Rect{}; return; }
        g_quickAnimPanelR = quickAnimPanelRect(app);
        SDL_Rect body{ g_quickAnimPanelR.x + 6, g_quickAnimPanelR.y + 6, g_quickAnimPanelR.w - 12, g_quickAnimPanelR.h - 12 };
        SDL_RenderSetClipRect(r, &body);
        const auto& fs = app.flowCapturer().settings;
        const bool armed = app.flowCapturer().armed;
        const bool flowEnabled = app.flowLinkEnabledValue();
        const bool haveTarget = activeLayerTree(app).primarySelectedTrackId() != 0;
        drawText(r, font, armed ? "Armed" : "Idle", body.x + 8, body.y + 2, armed ? SDL_Color{ 170,220,180,255 } : COL_TEXT_DIM);
        drawText(r, font, flowEnabled ? "FlowLink ON" : "FlowLink OFF", body.x + 90, body.y + 2, flowEnabled ? SDL_Color{ 170,220,180,255 } : COL_TEXT_DIM);
        drawText(r, font, haveTarget ? "Target OK" : "No Layer Selected", body.x + 200, body.y + 2, haveTarget ? SDL_Color{ 170,220,180,255 } : SDL_Color{ 220,150,120,255 });
        auto drawRow = [&](int row, const std::string& name, const std::string& value, bool active)
            {
                SDL_Rect rr = quickAnimRowRect(body, row, g_quickAnimScroll);
                if (rr.y + rr.h < body.y || rr.y > body.y + body.h) return;
                drawModernButton(r, rr, false, active);
                drawText(r, font, name, rr.x + 8, rr.y + 6, COL_TEXT_MAIN);
                drawText(r, font, value, rr.x + rr.w - 100, rr.y + 6, active ? SDL_Color{ 170,220,180,255 } : COL_TEXT_DIM);
            };
        drawRow(0, "FPS", std::to_string(fs.projectFps), false);
        std::string sm = fs.flowLinkSmoothing == FlowLinkSmoothingMode::CatmullRom ? "Catmull" : (fs.flowLinkSmoothing == FlowLinkSmoothingMode::Linear ? "Linear" : "None");
        drawRow(1, "Smoothing", sm, fs.flowLinkSmoothing == FlowLinkSmoothingMode::CatmullRom);
        char bufA[32]; std::snprintf(bufA, sizeof(bufA), "%.2f", fs.catmullAlpha);
        drawRow(2, "Catmull Strength", bufA, false);
        drawRow(3, "Position", fs.capturePosition ? "ON" : "OFF", fs.capturePosition);
        drawRow(4, "Rotation", fs.captureRotation ? "ON" : "OFF", fs.captureRotation);
        drawRow(5, "Overlay Mode", fs.overlayMode ? "ON" : "OFF", fs.overlayMode);
        drawRow(6, "Stitch Mode", fs.stitchMode ? "ON" : "OFF", fs.stitchMode);
        char bufB[32]; std::snprintf(bufB, sizeof(bufB), "%.2f", fs.motionDampening);
        drawRow(7, "Motion Dampening", bufB, false);
        SDL_RenderSetClipRect(r, nullptr);
    }

    static void drawToolGrid(App& app, SDL_Renderer* r, TTF_Font* font, const SDL_Rect& toolsArea)
    {
        const int pad = 10;
        const int gap = 10;
        ToolGridMetrics gm = calcToolGridMetrics(toolsArea);
        int cols = gm.cols;
        int btn = gm.btn;
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        int x0 = gm.x0;
        int y0 = gm.y0;
        const int count = (int)(sizeof(kToolBtns) / sizeof(kToolBtns[0]));

        SDL_Rect pickerBtn = toolsColorPickerButtonRect(toolsArea);
        for (int i = 0; i < count; ++i)
        {
            int cx = i % cols;
            int cy = i / cols;
            SDL_Rect rc{ x0 + cx * (btn + gap), y0 + cy * (btn + gap), btn, btn };
            if (rc.y + rc.h > pickerBtn.y - 8) break;
            bool hover = pointInRect(mx, my, rc);
            bool active = (app.getEditorUiState().activeTool == kToolBtns[i].t);
            drawModernButton(r, rc, hover, active);
            SDL_Texture* tex = kToolBtns[i].icon ? kToolBtns[i].icon(app) : nullptr;
            if (tex)
            {
                const int iconSize = std::max(14, std::min(26, rc.w - 12));
                SDL_Rect dst{ rc.x + (rc.w - iconSize) / 2, rc.y + (rc.h - iconSize) / 2, iconSize, iconSize };
                Uint8 a = 255;
                if (!hover && !active) a = 170;
                SDL_SetTextureAlphaMod(tex, a);
                SDL_RenderCopy(r, tex, nullptr, &dst);
                SDL_SetTextureAlphaMod(tex, 255);
            }
            else
            {
                const char* fallback = "?";
                int tw = measureTextW(font, fallback);
                SDL_Color tc = (hover || active) ? COL_TEXT_MAIN : COL_TEXT_DIM;
                drawText(r, font, fallback, rc.x + (rc.w - tw) / 2, rc.y + (rc.h - 18) / 2, tc);
            }
        }

        bool pickerHover = pointInRect(mx, my, pickerBtn);
        drawModernButton(r, pickerBtn, pickerHover, app.colorPickerWindowState().visible);
        const std::string pickerLabel = "Color Picker";
        int pickerTw = measureTextW(font, pickerLabel);
        drawText(r, font, pickerLabel, pickerBtn.x + std::max(10, (pickerBtn.w - pickerTw) / 2), pickerBtn.y + 9, COL_TEXT_MAIN);
    }

    static bool hoveredToolInGrid(const SDL_Rect& toolsArea, int mx, int my, ToolType& outTool)
    {
        const int gap = 10;
        ToolGridMetrics gm = calcToolGridMetrics(toolsArea);
        int cols = gm.cols;
        int btn = gm.btn;
        int x0 = gm.x0;
        int y0 = gm.y0;
        int count = (int)(sizeof(kToolBtns) / sizeof(kToolBtns[0]));
        SDL_Rect pickerBtn = toolsColorPickerButtonRect(toolsArea);
        for (int i = 0; i < count; ++i)
        {
            int cx = i % cols;
            int cy = i / cols;
            SDL_Rect rc{ x0 + cx * (btn + gap), y0 + cy * (btn + gap), btn, btn };
            if (rc.y + rc.h > pickerBtn.y - 8) break;
            if (pointInRect(mx, my, rc))
            {
                outTool = kToolBtns[i].t;
                return true;
            }
        }
        return false;
    }


    static std::vector<std::string> splitPluginTextLines(const std::string& text)
    {
        std::vector<std::string> lines;
        std::string current;
        for (char c : text)
        {
            if (c == '\r') continue;
            if (c == '\n')
            {
                lines.push_back(current);
                current.clear();
                continue;
            }
            current.push_back(c);
        }
        if (!current.empty()) lines.push_back(current);
        return lines;
    }


    static SDL_Color pluginOverlayCommandColor(const std::vector<int>& values, SDL_Color fallback)
    {
        if (values.size() >= 4)
            return SDL_Color{ (Uint8)std::clamp(values[0], 0, 255), (Uint8)std::clamp(values[1], 0, 255), (Uint8)std::clamp(values[2], 0, 255), (Uint8)std::clamp(values[3], 0, 255) };
        if (values.size() >= 3)
            return SDL_Color{ (Uint8)std::clamp(values[0], 0, 255), (Uint8)std::clamp(values[1], 0, 255), (Uint8)std::clamp(values[2], 0, 255), fallback.a };
        return fallback;
    }

    static void renderPluginCanvasOverlayCommands(SDL_Renderer* r, TTF_Font* font, const std::vector<std::string>& commands)
    {
        for (const std::string& raw : commands)
        {
            if (raw.empty()) continue;
            std::istringstream ss(raw);
            std::string op;
            ss >> op;
            if (op == "LINE")
            {
                float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
                ss >> x1 >> y1 >> x2 >> y2;
                std::vector<int> rgba;
                int v = 0;
                while (ss >> v) rgba.push_back(v);
                SDL_Color color = pluginOverlayCommandColor(rgba, SDL_Color{ 110, 200, 255, 220 });
                SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
                drawLineFCompat(r, x1, y1, x2, y2);
            }
            else if (op == "RECT")
            {
                int x = 0, y = 0, w = 0, h = 0;
                ss >> x >> y >> w >> h;
                std::vector<int> rgba;
                int v = 0;
                while (ss >> v) rgba.push_back(v);
                SDL_Color color = pluginOverlayCommandColor(rgba, SDL_Color{ 110, 200, 255, 220 });
                SDL_Rect rc{ x, y, w, h };
                SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
                SDL_RenderDrawRect(r, &rc);
            }
            else if (op == "FILLRECT")
            {
                int x = 0, y = 0, w = 0, h = 0;
                ss >> x >> y >> w >> h;
                std::vector<int> rgba;
                int v = 0;
                while (ss >> v) rgba.push_back(v);
                fillRect(r, SDL_Rect{ x, y, w, h }, pluginOverlayCommandColor(rgba, SDL_Color{ 110, 200, 255, 70 }));
            }
            else if (op == "CIRCLE")
            {
                int x = 0, y = 0, radius = 0;
                ss >> x >> y >> radius;
                std::vector<int> rgba;
                int v = 0;
                while (ss >> v) rgba.push_back(v);
                fillCircle(r, x, y, std::max(1, radius), pluginOverlayCommandColor(rgba, SDL_Color{ 110, 200, 255, 170 }));
            }
            else if (op == "POLYLINE")
            {
                std::vector<float> values;
                float fv = 0.0f;
                while (ss >> fv) values.push_back(fv);
                if (values.size() >= 4)
                {
                    const size_t pairCount = (values.size() / 2);
                    for (size_t i = 1; i < pairCount; ++i)
                        drawLineFCompat(r, values[(i - 1) * 2], values[(i - 1) * 2 + 1], values[i * 2], values[i * 2 + 1]);
                }
            }
            else if (op == "TEXT")
            {
                int x = 0, y = 0;
                ss >> x >> y;
                std::string text;
                std::getline(ss, text);
                while (!text.empty() && text.front() == ' ')
                    text.erase(text.begin());
                if (!text.empty())
                    drawText(r, font, text, x, y, SDL_Color{ 210, 230, 255, 235 });
            }
        }
    }

    static void drawPluginSurfaceCard(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const std::string& title, const std::vector<std::string>& lines, SDL_Color accent)
    {
        if (rc.w <= 0 || rc.h <= 0) return;
        fillRect(r, rc, SDL_Color{ 14, 18, 26, 238 });
        drawRect(r, rc, SDL_Color{ 86, 94, 116, 220 });
        SDL_Rect stripe{ rc.x, rc.y, 4, rc.h };
        fillRect(r, stripe, accent);
        drawText(r, font, title, rc.x + 12, rc.y + 10, COL_TEXT_MAIN);
        int y = rc.y + 36;
        for (const std::string& line : lines)
        {
            if (y + 16 > rc.y + rc.h - 6) break;
            drawText(r, font, line, rc.x + 12, y, COL_TEXT_DIM);
            y += 18;
        }
    }

    static void drawPluginPanels(App& app, SDL_Renderer* r, TTF_Font* font)
    {
        const auto& regs = app.pluginManager().registries();
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        for (const auto& panel : app.dockManager().panels)
        {
            if (!panel.visible || !panel.pluginPanel) continue;
            const SDL_Rect rc = app.dockManager().contentRect(panel);
            if (rc.w <= 0 || rc.h <= 0) continue;
            std::vector<std::string> lines;
            std::string title = panel.title.empty() ? panel.id : panel.title;
            if (!panel.ownerPluginId.empty()) lines.push_back("Plugin: " + panel.ownerPluginId);
            if (panel.unresolvedPlaceholder)
            {
                lines.push_back("Status: unavailable placeholder");
                if (!panel.statusText.empty()) lines.push_back(panel.statusText);
                lines.push_back("Open after restoring or replacing the plugin.");
                drawPluginSurfaceCard(r, font, rc, title, lines, SDL_Color{ 190, 130, 90, 255 });
                continue;
            }

            const auto* desc = regs.dockPanels.findById(panel.id);
            if (desc)
            {
                std::string renderText;
                std::string err;
                if (app.pluginManager().renderDockPanel(panel.id, title, rc, mx, my, app.dockManager().focusedPanelId() == panel.id, renderText, err))
                {
                    const std::vector<std::string> pluginLines = splitPluginTextLines(renderText);
                    if (!pluginLines.empty())
                        lines.insert(lines.end(), pluginLines.begin(), pluginLines.end());
                    else
                        lines.push_back("Plugin panel rendered with no text payload.");
                    drawPluginSurfaceCard(r, font, rc, title, lines, SDL_Color{ 90, 150, 215, 255 });
                    continue;
                }

                lines.push_back("Status: registered");
                lines.push_back("Dock zone: " + (desc->defaultDockZone.empty() ? std::string("FLOATING") : desc->defaultDockZone));
                lines.push_back("Min size: " + std::to_string(desc->minWidth) + " x " + std::to_string(desc->minHeight));
                if (!err.empty()) lines.push_back(err);
            }
            else
            {
                lines.push_back("Status: unresolved panel");
                lines.push_back("This saved panel is not registered right now.");
            }
            drawPluginSurfaceCard(r, font, rc, title, lines, SDL_Color{ 90, 150, 215, 255 });
        }
    }

    static const char* pluginDependencyStatusLabel(strova::plugin::DependencyStatus status)
    {
        using strova::plugin::DependencyStatus;
        switch (status)
        {
        case DependencyStatus::Available: return "Available";
        case DependencyStatus::Missing: return "Missing";
        case DependencyStatus::Disabled: return "Disabled";
        case DependencyStatus::Faulted: return "Faulted";
        case DependencyStatus::IncompatibleVersion: return "Incompatible version";
        case DependencyStatus::SchemaUnsupported: return "Schema unsupported";
        default: return "Unknown";
        }
    }

    static void drawPluginWarningsOverlay(App& app, SDL_Renderer* r, TTF_Font* font, const SDL_Rect& workspace)
    {
        const auto& missing = app.pluginManager().missingRecords();
        const auto& issues = app.pluginManager().lastDependencyIssues();
        if (missing.empty() && issues.empty()) return;

        SDL_Rect card{ workspace.x + workspace.w - 430, workspace.y + 10, 420, 88 };
        fillRect(r, card, SDL_Color{ 20, 18, 16, 238 });
        drawRect(r, card, SDL_Color{ 150, 112, 72, 220 });
        drawText(r, font, "Plugin attention needed", card.x + 12, card.y + 10, COL_TEXT_MAIN);
        drawText(r, font, std::string("Missing installs: ") + std::to_string((int)missing.size()) + "   Project issues: " + std::to_string((int)issues.size()), card.x + 12, card.y + 30, COL_TEXT_DIM);
        if (!missing.empty())
            drawText(r, font, std::string("Missing: ") + missing.front().pluginId, card.x + 12, card.y + 50, SDL_Color{ 220, 182, 140, 255 });
        else if (!issues.empty())
            drawText(r, font, std::string("Issue: ") + issues.front().pluginId + " / " + pluginDependencyStatusLabel(issues.front().status), card.x + 12, card.y + 50, SDL_Color{ 220, 182, 140, 255 });
        drawText(r, font, "Panels stay visible as placeholders until the plugin is restored.", card.x + 12, card.y + 68, COL_TEXT_DIM);
    }

    namespace
    {
        struct PluginPanelLayout
        {
            SDL_Rect installBtn{};
            SDL_Rect reloadBtn{};
            SDL_Rect statsLine{};
            SDL_Rect listClip{};
            int contentStartY = 0;
            int actionAreaBottom = 0;
        };

        static int g_pluginPanelScroll = 0;

        static PluginPanelLayout buildPluginPanelLayout(const SDL_Rect& panelRc)
        {
            PluginPanelLayout out{};
            const int pad = 10;
            const int gap = 8;
            const int btnH = 28;
            const int usableW = std::max(80, panelRc.w - pad * 2);
            if (usableW >= 250)
            {
                const int bw = std::max(72, (usableW - gap) / 2);
                out.installBtn = SDL_Rect{ panelRc.x + pad, panelRc.y + pad, bw, btnH };
                out.reloadBtn = SDL_Rect{ out.installBtn.x + out.installBtn.w + gap, panelRc.y + pad, std::max(72, panelRc.x + panelRc.w - pad - (out.installBtn.x + out.installBtn.w + gap)), btnH };
                out.actionAreaBottom = out.installBtn.y + out.installBtn.h;
            }
            else
            {
                out.installBtn = SDL_Rect{ panelRc.x + pad, panelRc.y + pad, usableW, btnH };
                out.reloadBtn = SDL_Rect{ panelRc.x + pad, out.installBtn.y + btnH + gap, usableW, btnH };
                out.actionAreaBottom = out.reloadBtn.y + out.reloadBtn.h;
            }

            out.statsLine = SDL_Rect{ panelRc.x + pad, out.actionAreaBottom + 12, std::max(20, panelRc.w - pad * 2), 18 };
            out.contentStartY = out.statsLine.y + out.statsLine.h + 8;
            out.listClip = SDL_Rect{ panelRc.x + 6, out.contentStartY, std::max(10, panelRc.w - 12), std::max(10, panelRc.h - (out.contentStartY - panelRc.y) - 6) };
            return out;
        }
    }

    static void drawPluginManagerPanel(App& app, SDL_Renderer* r, TTF_Font* font, int mx, int my)
    {
        const SDL_Rect rc = app.dockManager().contentRect("Plugins");
        if (rc.w <= 0 || rc.h <= 0) return;

        fillRect(r, rc, SDL_Color{ 8, 13, 22, 255 });
        drawRect(r, rc, COL_BORDER_SOFT);

        const PluginPanelLayout layout = buildPluginPanelLayout(rc);
        drawModernButton(r, layout.installBtn, pointInRect(mx, my, layout.installBtn), false);
        drawModernButton(r, layout.reloadBtn, pointInRect(mx, my, layout.reloadBtn), false);
        strova::ui_text::drawTextCentered(r, font, "Install Plugin", layout.installBtn, COL_TEXT_MAIN);
        strova::ui_text::drawTextCentered(r, font, "Reload", layout.reloadBtn, COL_TEXT_MAIN);

        const auto& records = app.pluginManager().records();
        const auto& missing = app.pluginManager().missingRecords();
        int loadedCount = 0;
        int faultedCount = 0;
        for (const auto& record : records)
        {
            if (record.state == strova::plugin::RuntimeState::Loaded) ++loadedCount;
            if (record.state == strova::plugin::RuntimeState::Faulted) ++faultedCount;
        }
        strova::ui_text::drawTextLeftMiddle(r, font, std::string("Installed: ") + std::to_string((int)records.size()) + " | Loaded: " + std::to_string(loadedCount), SDL_Rect{ layout.statsLine.x, layout.statsLine.y, std::max(40, layout.statsLine.w / 2 - 6), layout.statsLine.h }, COL_TEXT_DIM);
        strova::ui_text::drawTextLeftMiddle(r, font, std::string("Missing: ") + std::to_string((int)missing.size()) + " | Faulted: " + std::to_string(faultedCount), SDL_Rect{ layout.statsLine.x + layout.statsLine.w / 2, layout.statsLine.y, std::max(40, layout.statsLine.w - layout.statsLine.w / 2), layout.statsLine.h }, faultedCount > 0 ? SDL_Color{ 220, 182, 140, 255 } : COL_TEXT_DIM);

        int contentHeight = 0;
        if (records.empty() && missing.empty())
            contentHeight = 30;
        else
            contentHeight = (int)records.size() * (72 + 8) + (int)missing.size() * (52 + 8);
        const int maxScroll = std::max(0, contentHeight - layout.listClip.h);
        g_pluginPanelScroll = std::clamp(g_pluginPanelScroll, 0, maxScroll);

        SDL_Rect oldClip{};
        const SDL_bool hadClip = SDL_RenderIsClipEnabled(r);
        SDL_RenderGetClipRect(r, &oldClip);
        SDL_RenderSetClipRect(r, &layout.listClip);

        int y = layout.listClip.y + 2 - g_pluginPanelScroll;
        if (records.empty() && missing.empty())
        {
            strova::ui_text::drawTextLeftMiddle(r, font, "No plugins installed yet. Use Install Plugin to add a .strovin package.", SDL_Rect{ layout.listClip.x + 6, layout.listClip.y + 6, layout.listClip.w - 12, 20 }, COL_TEXT_DIM);
        }
        else
        {
            for (const auto& record : records)
            {
                SDL_Rect item{ layout.listClip.x + 4, y, layout.listClip.w - 8, 72 };
                fillRect(r, item, COL_BG_PANEL2);
                drawRect(r, item, COL_BORDER_SOFT);
                const int actionW = 34;
                const int actionGap = 6;
                SDL_Rect openBtn{ item.x + item.w - (actionW * 4 + actionGap * 3) - 8, item.y + 7, actionW, 20 };
                SDL_Rect cmdBtn{ openBtn.x + actionW + actionGap, item.y + 7, actionW, 20 };
                SDL_Rect impBtn{ cmdBtn.x + actionW + actionGap, item.y + 7, actionW, 20 };
                SDL_Rect expBtn{ impBtn.x + actionW + actionGap, item.y + 7, actionW, 20 };
                const int textW = std::max(40, openBtn.x - item.x - 18);
                strova::ui_text::drawTextEllipsized(r, font, record.package.manifest.name.empty() ? record.package.manifest.id : record.package.manifest.name, SDL_Rect{ item.x + 10, item.y + 7, textW, 18 }, COL_TEXT_MAIN);
                strova::ui_text::drawTextEllipsized(r, font, std::string("Id: ") + record.package.manifest.id, SDL_Rect{ item.x + 10, item.y + 27, std::max(40, item.w - 20), 18 }, COL_TEXT_DIM);
                std::string status = std::string("State: ") + pluginRuntimeStateLabel(record.state);
                if (!record.enabled) status += " | disabled";
                if (!record.lastError.empty()) status += std::string(" | ") + record.lastError;
                else if (!record.lastWarning.empty()) status += std::string(" | ") + record.lastWarning;
                strova::ui_text::drawTextEllipsized(r, font, status, SDL_Rect{ item.x + 10, item.y + 41, std::max(40, item.w - 20), 14 }, record.state == strova::plugin::RuntimeState::Loaded ? SDL_Color{ 144, 220, 164, 255 } : (record.state == strova::plugin::RuntimeState::Faulted ? SDL_Color{ 230, 170, 120, 255 } : SDL_Color{ 220, 190, 120, 255 }));
                const std::string capsAndPerms = pluginMaskSummary(record.query.capabilityMask, false) + " | perms: " + pluginMaskSummary(record.query.permissionMask, true);
                strova::ui_text::drawTextEllipsized(r, font, capsAndPerms, SDL_Rect{ item.x + 10, item.y + 55, std::max(40, item.w - 20), 14 }, COL_TEXT_DIM);
                drawModernButton(r, openBtn, pointInRect(mx, my, openBtn), false);
                drawModernButton(r, cmdBtn, pointInRect(mx, my, cmdBtn), false);
                drawModernButton(r, impBtn, pointInRect(mx, my, impBtn), false);
                drawModernButton(r, expBtn, pointInRect(mx, my, expBtn), false);
                strova::ui_text::drawTextCentered(r, font, "Open", openBtn, COL_TEXT_MAIN);
                strova::ui_text::drawTextCentered(r, font, "Cmd", cmdBtn, COL_TEXT_MAIN);
                strova::ui_text::drawTextCentered(r, font, "Imp", impBtn, COL_TEXT_MAIN);
                strova::ui_text::drawTextCentered(r, font, "Exp", expBtn, COL_TEXT_MAIN);
                y += item.h + 8;
            }
            for (const auto& miss : missing)
            {
                SDL_Rect item{ layout.listClip.x + 4, y, layout.listClip.w - 8, 52 };
                fillRect(r, item, SDL_Color{ 30, 20, 16, 255 });
                drawRect(r, item, SDL_Color{ 150, 112, 72, 220 });
                strova::ui_text::drawTextEllipsized(r, font, miss.pluginId, SDL_Rect{ item.x + 10, item.y + 7, std::max(40, item.w - 20), 18 }, SDL_Color{ 230, 210, 180, 255 });
                strova::ui_text::drawTextEllipsized(r, font, miss.expectedPath, SDL_Rect{ item.x + 10, item.y + 27, std::max(40, item.w - 20), 18 }, COL_TEXT_DIM);
                y += item.h + 8;
            }
        }

        if (maxScroll > 0)
        {
            SDL_Rect track{ layout.listClip.x + layout.listClip.w - strova::theme::scrollbarWidth - 1, layout.listClip.y + 2, strova::theme::scrollbarWidth, std::max(24, layout.listClip.h - 4) };
            strova::uix::drawScrollbar(r, track, g_pluginPanelScroll, layout.listClip.h, contentHeight);
        }

        if (hadClip) SDL_RenderSetClipRect(r, &oldClip); else SDL_RenderSetClipRect(r, nullptr);
    }

    static bool dispatchPluginDockPanelEvent(App& app, const SDL_Event& e, int mx, int my)
    {
        const bool isMouseEvent =
            e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP ||
            e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEWHEEL;
        const bool isKeyboardEvent =
            e.type == SDL_KEYDOWN || e.type == SDL_KEYUP ||
            e.type == SDL_TEXTINPUT;

        if (!isMouseEvent && !isKeyboardEvent)
            return false;

        const std::string focusedPanelId = app.dockManager().focusedPanelId();
        for (auto it = app.dockManager().panels.rbegin(); it != app.dockManager().panels.rend(); ++it)
        {
            const auto& panel = *it;
            if (!panel.visible || !panel.pluginPanel || panel.unresolvedPlaceholder)
                continue;

            const SDL_Rect pluginRc = app.dockManager().contentRect(panel);
            if (pluginRc.w <= 0 || pluginRc.h <= 0)
                continue;

            bool targetPanel = false;
            if (isMouseEvent)
                targetPanel = pointInRect(mx, my, pluginRc);
            else if (isKeyboardEvent)
                targetPanel = !focusedPanelId.empty() && focusedPanelId == panel.id;

            if (!targetPanel)
                continue;

            std::string pluginErr;
            if (!app.pluginManager().handleDockPanelEvent(panel.id, e, mx, my, pluginErr) && !pluginErr.empty())
                SDL_Log("Plugin panel event failed: %s", pluginErr.c_str());
            return true;
        }

        return false;
    }

    static bool handlePluginManagerPanelEvent(App& app, const SDL_Event& e, int mx, int my, const SDL_Rect& workspace)
    {
        const SDL_Rect rc = app.dockManager().contentRect("Plugins");
        if (rc.w <= 0 || rc.h <= 0 || !pointInRect(mx, my, rc)) return false;

        const PluginPanelLayout layout = buildPluginPanelLayout(rc);
        const auto& records = app.pluginManager().records();
        const auto& missing = app.pluginManager().missingRecords();
        const int contentHeight = records.empty() && missing.empty() ? 30 : (int)records.size() * (72 + 8) + (int)missing.size() * (52 + 8);
        const int maxScroll = std::max(0, contentHeight - layout.listClip.h);
        g_pluginPanelScroll = std::clamp(g_pluginPanelScroll, 0, maxScroll);

        if (e.type == SDL_MOUSEWHEEL)
        {
            g_pluginPanelScroll -= e.wheel.y * 32;
            g_pluginPanelScroll = std::clamp(g_pluginPanelScroll, 0, maxScroll);
            return true;
        }

        if (e.type != SDL_MOUSEBUTTONDOWN || e.button.button != SDL_BUTTON_LEFT) return false;

        int rowY = layout.listClip.y + 2 - g_pluginPanelScroll;
        for (const auto& record : records)
        {
            SDL_Rect item{ layout.listClip.x + 4, rowY, layout.listClip.w - 8, 72 };
            if (item.y + item.h >= layout.listClip.y && item.y <= layout.listClip.y + layout.listClip.h)
            {
                const int actionW = 34;
                const int actionGap = 6;
                SDL_Rect openBtn{ item.x + item.w - (actionW * 4 + actionGap * 3) - 8, item.y + 7, actionW, 20 };
                SDL_Rect cmdBtn{ openBtn.x + actionW + actionGap, item.y + 7, actionW, 20 };
                SDL_Rect impBtn{ cmdBtn.x + actionW + actionGap, item.y + 7, actionW, 20 };
                SDL_Rect expBtn{ impBtn.x + actionW + actionGap, item.y + 7, actionW, 20 };
                if (pointInRect(mx, my, openBtn))
                {
                    openPluginFromLoadedSubmenu(app, record, workspace);
                    return true;
                }
                if (pointInRect(mx, my, cmdBtn))
                {
                    for (const auto& command : app.pluginManager().registries().commands.items())
                    {
                        if (command.ownerPluginId != record.package.manifest.id) continue;
                        std::string err;
                        if (!app.pluginManager().invokeCommand(command.id, err) && !err.empty())
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin Command Failed", err.c_str(), app.windowHandle());
                        return true;
                    }
                    for (const auto& proc : app.pluginManager().registries().flowProcessors.items())
                    {
                        if (proc.ownerPluginId != record.package.manifest.id) continue;
                        std::string instanceId;
                        std::string err;
                        if (!app.pluginManager().processFlow(proc.id, instanceId, err) && !err.empty())
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin Flow Processor Failed", err.c_str(), app.windowHandle());
                        return true;
                    }
                    for (const auto& proc : app.pluginManager().registries().flowLinkProcessors.items())
                    {
                        if (proc.ownerPluginId != record.package.manifest.id) continue;
                        std::string instanceId;
                        std::string err;
                        if (!app.pluginManager().processFlowLink(proc.id, app.getEngine().getActiveTrack(), instanceId, err) && !err.empty())
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin FlowLink Processor Failed", err.c_str(), app.windowHandle());
                        return true;
                    }
                    {
                        std::string analysisText;
                        std::string err;
                        if (!app.pluginManager().runAnalysis(record.package.manifest.id, analysisText, err))
                        {
                            if (!err.empty())
                                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin Analysis Failed", err.c_str(), app.windowHandle());
                        }
                        else
                        {
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Plugin Analysis", analysisText.empty() ? "Plugin returned no analysis text." : analysisText.c_str(), app.windowHandle());
                            return true;
                        }
                    }
                    {
                        std::vector<std::string> reports;
                        std::string err;
                        if (!app.pluginManager().runDocumentValidators(reports, err))
                        {
                            if (!err.empty())
                                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin Validator Failed", err.c_str(), app.windowHandle());
                        }
                        else if (!reports.empty())
                        {
                            std::string merged;
                            for (std::size_t i = 0; i < reports.size(); ++i)
                            {
                                if (i > 0) merged += "\n\n";
                                merged += reports[i];
                            }
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Plugin Validation", merged.c_str(), app.windowHandle());
                        }
                    }
                    return true;
                }
                if (pointInRect(mx, my, impBtn))
                {
                    for (const auto& importer : app.pluginManager().registries().importers.items())
                    {
                        if (importer.ownerPluginId != record.package.manifest.id) continue;
                        std::string path;
                        std::string err;
                        if (platform::pickOpenAnyFile(path) && !app.pluginManager().runImporter(importer.id, path, err) && !err.empty())
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin Import Failed", err.c_str(), app.windowHandle());
                        return true;
                    }
                    return true;
                }
                if (pointInRect(mx, my, expBtn))
                {
                    for (const auto& exporter : app.pluginManager().registries().exporters.items())
                    {
                        if (exporter.ownerPluginId != record.package.manifest.id) continue;
                        std::string path;
                        std::string err;
                        if (platform::pickSaveAnyFile(path, exporter.displayName) && !app.pluginManager().runExporter(exporter.id, path, err) && !err.empty())
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin Export Failed", err.c_str(), app.windowHandle());
                        return true;
                    }
                    {
                        std::vector<std::string> effects;
                        std::string err;
                        const SDL_Rect canvasRect = app.getUILayout().canvas;
                        if (!app.pluginManager().runExportPasses(canvasRect, effects, err))
                        {
                            if (!err.empty())
                                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin Export Pass Failed", err.c_str(), app.windowHandle());
                        }
                        else
                        {
                            std::string merged;
                            for (std::size_t i = 0; i < effects.size(); ++i)
                            {
                                if (i > 0) merged += "\n\n";
                                merged += effects[i];
                            }
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Plugin Export Passes", merged.empty() ? "Plugin export passes ran with no text result." : merged.c_str(), app.windowHandle());
                        }
                    }
                    return true;
                }
            }
            rowY += item.h + 8;
        }

        std::string err;
        if (pointInRect(mx, my, layout.installBtn))
        {
            std::string path;
            if (platform::pickOpenFile(path))
            {
                if (!app.pluginManager().installPackage(path, true, err))
                {
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin Install Failed", err.empty() ? "Unable to install plugin package." : err.c_str(), app.windowHandle());
                }
                else
                {
                    app.dockManager().syncPluginPanels(app.pluginManager().registries().dockPanels, workspace);
                    app.saveDockLayoutForCurrentContext();
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Plugin Installed", "Plugin installed and indexed successfully.", app.windowHandle());
                }
            }
            return true;
        }
        if (pointInRect(mx, my, layout.reloadBtn))
        {
            if (!app.pluginManager().reload(err))
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Reload Plugins Failed", err.empty() ? "Unable to reload plugins." : err.c_str(), app.windowHandle());
            else
            {
                app.dockManager().syncPluginPanels(app.pluginManager().registries().dockPanels, workspace);
                app.saveDockLayoutForCurrentContext();
            }
            return true;
        }
        return true;
    }

    static void drawTooltipBubble(SDL_Renderer* r, TTF_Font* font, const std::string& text, int mx, int my, int windowW, int windowH)
    {
        if (!r || !font || text.empty()) return;
        int tw = measureTextW(font, text);
        int th = 18;
        SDL_Rect rc{ mx + 18, my + 20, tw + 22, th + 14 };
        if (rc.x + rc.w > windowW - 8) rc.x = std::max(8, windowW - rc.w - 8);
        if (rc.y + rc.h > windowH - 8) rc.y = std::max(8, windowH - rc.h - 8);
        fillRoundRect(r, rc, 10, SDL_Color{ 10, 14, 20, 236 });
        strokeRoundRect(r, rc, 10, SDL_Color{ 255,255,255,24 });
        drawText(r, font, text, rc.x + 11, rc.y + 7, COL_TEXT_MAIN);
    }

    static std::string formatBytesShort(std::size_t bytes)
    {
        const char* units[] = { "B", "KB", "MB", "GB" };
        double value = (double)bytes;
        int unit = 0;
        while (value >= 1024.0 && unit < 3)
        {
            value /= 1024.0;
            ++unit;
        }
        char buf[64];
        if (unit == 0) std::snprintf(buf, sizeof(buf), "%zu%s", bytes, units[unit]);
        else           std::snprintf(buf, sizeof(buf), "%.1f%s", value, units[unit]);
        return buf;
    }

#include "EditorDiagnosticsOverlay.inl"

    static bool handleToolGridClick(const SDL_Rect& toolsArea, int mx, int my, ToolType& outTool)
    {
        const int gap = 10;
        ToolGridMetrics gm = calcToolGridMetrics(toolsArea);
        int cols = gm.cols;
        int btn = gm.btn;
        int x0 = gm.x0;
        int y0 = gm.y0;
        int count = (int)(sizeof(kToolBtns) / sizeof(kToolBtns[0]));
        SDL_Rect pickerBtn = toolsColorPickerButtonRect(toolsArea);
        for (int i = 0; i < count; ++i)
        {
            int cx = i % cols;
            int cy = i / cols;
            SDL_Rect rc{ x0 + cx * (btn + gap), y0 + cy * (btn + gap), btn, btn };
            if (rc.y + rc.h > pickerBtn.y - 8) break;
            if (pointInRect(mx, my, rc))
            {
                outTool = kToolBtns[i].t;
                return true;
            }
        }
        return false;
    }

    static bool isStrokeTool(ToolType t)
    {
        switch (t)
        {
        case ToolType::Brush:
        case ToolType::Pencil:
        case ToolType::Pen:
        case ToolType::Marker:
        case ToolType::Airbrush:
        case ToolType::Calligraphy:
        case ToolType::Eraser:
        case ToolType::SoftEraser:
        case ToolType::Smudge:
        case ToolType::Blur:
        case ToolType::Glow:
            return true;
        default:
            return false;
        }
    }

    static bool isActionTool(ToolType t)
    {
        switch (t)
        {
        case ToolType::Fill:
        case ToolType::Line:
        case ToolType::Rect:
        case ToolType::Ellipse:
        case ToolType::Select:
            return true;
        default:
            return false;
        }
    }

    static void fillPillHorizontal(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c)
    {
        if (w <= 0 || h <= 0) return;
        int rad = h / 2;
        SDL_Rect mid{ x + rad, y, std::max(0, w - rad * 2), h };
        fillRect(r, mid, c);
        fillCircle(r, x + rad, y + rad, rad, c);
        fillCircle(r, x + w - rad - 1, y + rad, rad, c);
    }

    static void drawPillKnob(SDL_Renderer* r, int cx, int cy, int radius, SDL_Color c, SDL_Color border, bool glow)
    {
        if (glow && radius > 4)
        {
            SDL_Color glowCol = c;
            glowCol.a = 60;
            fillCircle(r, cx, cy, radius + 2, glowCol);
            glowCol.a = 30;
            fillCircle(r, cx, cy, radius + 4, glowCol);
        }
        fillCircle(r, cx, cy, radius, c);
        drawCircle(r, cx, cy, radius, border);
        SDL_Color highlight = c;
        highlight.r = std::min(255, c.r + 40);
        highlight.g = std::min(255, c.g + 40);
        highlight.b = std::min(255, c.b + 40);
        fillCircle(r, cx - radius / 3, cy - radius / 3, radius / 3, highlight);
    }

    static void drawPillSlider(SDL_Renderer* r, const SDL_Rect& track, float t01, bool hover, bool active,
        const char* label, const std::string& valueText, TTF_Font* font)
    {
        t01 = fclamp(t01, 0.0f, 1.0f);
        int trackH = track.h;
        int trackRad = trackH / 2;
        SDL_Rect shadow = track;
        shadow.y += 1;
        SDL_Color shadowCol = { 0, 0, 0, 40 };
        fillPillHorizontal(r, shadow.x, shadow.y, shadow.w, trackH, shadowCol);
        SDL_Color trackBg = { 40, 40, 45, 255 };
        fillPillHorizontal(r, track.x, track.y, track.w, trackH, trackBg);
        int fillW = (int)(t01 * track.w);
        if (fillW > 0)
        {
            SDL_Color fillStart = hover ? SDL_Color{ 120, 132, 150, 255 } : COL_ACCENT;
            SDL_Color fillEnd = hover ? SDL_Color{ 136, 146, 164, 255 } : COL_ACCENT;
            fillEnd.r = std::min(255, (int)(fillEnd.r * 11 / 10));
            fillEnd.g = std::min(255, (int)(fillEnd.g * 11 / 10));
            fillEnd.b = std::min(255, (int)(fillEnd.b * 11 / 10));
            int seg = fillW / 4;
            for (int i = 0; i < 4 && seg > 0; i++)
            {
                float mix = (float)i / 3.0f;
                SDL_Color c;
                c.r = (Uint8)(fillStart.r * (1 - mix) + fillEnd.r * mix);
                c.g = (Uint8)(fillStart.g * (1 - mix) + fillEnd.g * mix);
                c.b = (Uint8)(fillStart.b * (1 - mix) + fillEnd.b * mix);
                c.a = 255;
                int x = track.x + i * seg;
                int w = (i == 3) ? (fillW - i * seg) : seg;
                if (w > 0)
                {
                    SDL_Rect segRect = { x, track.y, w, trackH };
                    if (i == 0 && fillW > trackRad * 2)
                    {
                        fillCircle(r, x + trackRad, track.y + trackRad, trackRad, c);
                        SDL_Rect mid{ x + trackRad, track.y, w - trackRad, trackH };
                        fillRect(r, mid, c);
                    }
                    else
                    {
                        fillRect(r, segRect, c);
                    }
                }
            }
        }
        SDL_Color borderCol = { 60, 60, 70, 255 };
        strokePill(r, track, borderCol);
        int knobRadius = trackH / 2 + 3;
        int knobX = track.x + (int)(t01 * (track.w - 1));
        int knobY = track.y + trackH / 2;
        fillCircle(r, knobX, knobY + 2, knobRadius, SDL_Color{ 0, 0, 0, 60 });
        SDL_Color knobCol = hover ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 230, 230, 235, 255 };
        SDL_Color knobBorder = hover ? SDL_Color{ 150, 156, 170, 255 } : SDL_Color{ 120, 120, 130, 255 };
        drawPillKnob(r, knobX, knobY, knobRadius, knobCol, knobBorder, false);
        if (label && font)
            drawText(r, font, label, track.x, track.y - 22, COL_TEXT_DIM);
        if (!valueText.empty() && font)
        {
            int tw = measureTextW(font, valueText);
            drawText(r, font, valueText, track.x + track.w - tw, track.y - 22, COL_TEXT_MAIN);
        }
    }

    static SDL_Color readPixel(SDL_Renderer* r, int sx, int sy)
    {
        SDL_Color out{ 0,0,0,255 };
        Uint32 px = 0;
        SDL_Rect rc{ sx, sy, 1, 1 };
        if (!r) return out;
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(r, &info) != 0) return out;
        if (SDL_RenderReadPixels(r, &rc, SDL_PIXELFORMAT_RGBA8888, &px, sizeof(Uint32)) != 0)
        {
            if (SDL_RenderReadPixels(r, &rc, SDL_PIXELFORMAT_ABGR8888, &px, sizeof(Uint32)) != 0)
                return out;
            out.r = (px >> 0) & 0xFF;
            out.g = (px >> 8) & 0xFF;
            out.b = (px >> 16) & 0xFF;
            out.a = (px >> 24) & 0xFF;
            return out;
        }
        out.r = (px >> 24) & 0xFF;
        out.g = (px >> 16) & 0xFF;
        out.b = (px >> 8) & 0xFF;
        out.a = (px >> 0) & 0xFF;
        return out;
    }

    static Uint8 u8clamp(int v) { return (Uint8)std::clamp(v, 0, 255); }

    static void applyTint(Stroke& s, SDL_Color tint, float strength01)
    {
        strength01 = fclamp(strength01, 0.0f, 1.0f);
        int r = (int)(s.color.r * (1.0f - strength01) + tint.r * strength01);
        int g = (int)(s.color.g * (1.0f - strength01) + tint.g * strength01);
        int b = (int)(s.color.b * (1.0f - strength01) + tint.b * strength01);
        s.color.r = u8clamp(r);
        s.color.g = u8clamp(g);
        s.color.b = u8clamp(b);
    }

    static void drawOnionButton(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, bool hover, bool enabled, int steps)
    {
        drawModernButton(r, rc, hover, enabled);
        std::string lab = enabled ? "On" : "Off";
        SDL_Color tc = (hover || enabled) ? COL_TEXT_MAIN : COL_TEXT_DIM;
        int tw = measureTextW(font, lab);
        drawText(r, font, lab, rc.x + (rc.w - tw) / 2, rc.y + 3, tc);
        std::string s2 = std::to_string(std::clamp(steps, 1, 5));
        int tw2 = measureTextW(font, s2);
        drawText(r, font, s2, rc.x + (rc.w - tw2) / 2, rc.y + 19, tc);
    }

    struct OnionPanelLayout
    {
        SDL_Rect panel{};
        SDL_Rect enabledBtn{};
        SDL_Rect tintBtn{};
        SDL_Rect stepMinus{};
        SDL_Rect stepPlus{};
        SDL_Rect stepValue{};
        SDL_Rect prevTrack{};
        SDL_Rect nextTrack{};
    };

    static OnionPanelLayout buildOnionPanelLayout(const SDL_Rect& panel)
    {
        OnionPanelLayout L{};
        L.panel = panel;
        const int pad = 10;
        int y = panel.y + pad + 20;
        const int btnH = 24;
        L.enabledBtn = { panel.x + pad, y, panel.w - pad * 2, btnH };
        y += btnH + 8;
        L.tintBtn = { panel.x + pad, y, panel.w - pad * 2, btnH };
        y += btnH + 8;
        const int smallW = 24;
        L.stepMinus = { panel.x + pad, y, smallW, btnH };
        L.stepPlus = { panel.x + panel.w - pad - smallW, y, smallW, btnH };
        L.stepValue = { L.stepMinus.x + smallW + 6, y, panel.w - pad * 2 - smallW * 2 - 12, btnH };
        y += btnH + 14;
        L.prevTrack = { panel.x + pad, y, panel.w - pad * 2, 10 };
        y += 32;
        L.nextTrack = { panel.x + pad, y, panel.w - pad * 2, 10 };
        return L;
    }

    static float alphaFromTrackX(const SDL_Rect& track, int mx)
    {
        if (track.w <= 1) return 0.0f;
        float t = (float)(mx - track.x) / (float)(track.w - 1);
        return fclamp(t, 0.0f, 1.0f);
    }

    static void drawOnionSettingsPanel(SDL_Renderer* r, TTF_Font* font, const OnionPanelLayout& L,
        int mx, int my, bool enabled, int steps, bool tint, float prevAlpha, float nextAlpha)
    {
        fillRect(r, L.panel, COL_BG_PANEL2);
        drawRect(r, L.panel, COL_BORDER_SOFT);
        drawText(r, font, "Onion", L.panel.x + 10, L.panel.y + 6, COL_TEXT_MAIN);
        bool hoverEnable = pointInRect(mx, my, L.enabledBtn);
        bool hoverTint = pointInRect(mx, my, L.tintBtn);
        bool hoverMinus = pointInRect(mx, my, L.stepMinus);
        bool hoverPlus = pointInRect(mx, my, L.stepPlus);
        drawModernButton(r, L.enabledBtn, hoverEnable, enabled);
        drawModernButton(r, L.tintBtn, hoverTint, tint);
        drawModernButton(r, L.stepMinus, hoverMinus, false);
        drawModernButton(r, L.stepPlus, hoverPlus, false);
        fillRoundRect(r, L.stepValue, safeRoundRadius(L.stepValue, 8), COL_BG_PANEL);
        strokeRoundRect(r, L.stepValue, safeRoundRadius(L.stepValue, 8), COL_BORDER_SOFT);
        drawText(r, font, enabled ? "Enabled" : "Disabled", L.enabledBtn.x + 8, L.enabledBtn.y + 4,
            enabled ? COL_TEXT_MAIN : COL_TEXT_DIM);
        drawText(r, font, tint ? "Tint ON" : "Tint OFF", L.tintBtn.x + 8, L.tintBtn.y + 4,
            tint ? COL_TEXT_MAIN : COL_TEXT_DIM);
        drawText(r, font, "-", L.stepMinus.x + 8, L.stepMinus.y + 3, COL_TEXT_MAIN);
        drawText(r, font, "+", L.stepPlus.x + 8, L.stepPlus.y + 3, COL_TEXT_MAIN);
        std::string stepTxt = std::string("Steps ") + std::to_string(std::clamp(steps, 1, 5));
        int stw = measureTextW(font, stepTxt);
        drawText(r, font, stepTxt, L.stepValue.x + (L.stepValue.w - stw) / 2, L.stepValue.y + 4, COL_TEXT_MAIN);

        auto drawTrack = [&](const char* label, const SDL_Rect& track, float value)
            {
                value = fclamp(value, 0.0f, 1.0f);
                drawText(r, font, label, track.x, track.y - 18, COL_TEXT_DIM);
                fillRoundRect(r, track, safeRoundRadius(track, 4), SDL_Color{ (Uint8)42,(Uint8)42,(Uint8)52,(Uint8)255 });
                strokeRoundRect(r, track, safeRoundRadius(track, 4), COL_BORDER_SOFT);
                SDL_Rect fill = track;
                fill.w = std::max(1, (int)std::lround((float)track.w * value));
                fillRoundRect(r, fill, safeRoundRadius(fill, 4), COL_ACCENT);
                int kx = track.x + (int)std::lround((track.w - 1) * value);
                SDL_Rect knob{ kx - 6, track.y - 4, 12, track.h + 8 };
                fillRoundRect(r, knob, safeRoundRadius(knob, 6), SDL_Color{ (Uint8)230, (Uint8)235, (Uint8)245, (Uint8)255 });

                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d%%", (int)std::lround(value * 100.0f));
                int tw = measureTextW(font, buf);
                drawText(r, font, buf, track.x + track.w - tw, track.y - 18, COL_TEXT_MAIN);
            };

        drawTrack("Prev", L.prevTrack, prevAlpha);
        drawTrack("Next", L.nextTrack, nextAlpha);
    }

    static void drawRightBarImpl(App& app, SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rightBarBase,
        bool rightPanelOpen, float rightPanelAnim,
        ToolType activeTool,
        ToolBank& toolBank)
    {
        int slideOffset = (int)((1.0f - rightPanelAnim) * rightBarBase.w);
        SDL_Rect rightBar = rightBarBase;
        rightBar.x += slideOffset;

        if (rightBar.w <= 0 || rightBar.h <= 0) return;
        if (!rightPanelOpen && rightPanelAnim <= 0.01f) return;

        fillRect(r, rightBar, COL_BG_PANEL);
        drawDividerV(r, rightBar.y, rightBar.y + rightBar.h, rightBar.x, COL_BORDER_SOFT);

        SDL_Rect inner = rightBar;
        inner.x += 12;
        inner.y += 12;
        inner.w = inner.w - 24;
        if (inner.w < 0) inner.w = 0;
        inner.h = inner.h - 24;
        if (inner.h < 0) inner.h = 0;

        SDL_Rect header = inner;
        header.h = 44;
        fillRect(r, header, COL_BG_PANEL2);
        drawRect(r, header, COL_BORDER_SOFT);

        drawText(r, font, "Tool Options", header.x + 12, header.y + 12, COL_TEXT_MAIN);

        SDL_Rect content = inner;
        content.y += header.h + 10;
        content.h = content.h - header.h - 10;
        if (content.h < 0) content.h = 0;

        int totalContentH = calcRightPanelContentHeight(activeTool);
        g_rightPanelContentH = std::max(0, totalContentH);
        int maxScroll = std::max(0, g_rightPanelContentH - content.h);
        if (g_rightPanelScroll > maxScroll) g_rightPanelScroll = maxScroll;
        if (g_rightPanelScroll < 0) g_rightPanelScroll = 0;
        g_rightPanelScrollTarget = fclamp(g_rightPanelScrollTarget, 0.0f, (float)maxScroll);
        g_rightPanelScroll = clampi(g_rightPanelScroll, 0, maxScroll);

        if (g_rightSliderDrag != RightSlider::None)
            g_rightPanelScrollTarget = (float)g_rightPanelScroll;
        else
        {
            float cur = (float)g_rightPanelScroll;
            float next = cur + (g_rightPanelScrollTarget - cur) * 0.22f;
            if (std::fabs(g_rightPanelScrollTarget - next) < 0.5f) next = g_rightPanelScrollTarget;
            g_rightPanelScroll = clampi((int)std::lround(next), 0, maxScroll);
        }

        SDL_RenderSetClipRect(r, &content);
        int cursorY = content.y - g_rightPanelScroll;
        drawText(r, font, "Tool Settings", content.x + 2, cursorY, COL_TEXT_MAIN);
        cursorY += 26;

        auto drawSlider = [&](const char* label, float val, float minV, float maxV, const char* suffix) {
            const int rowH = 52;
            SDL_Rect row{ content.x, cursorY, content.w, 40 };
            float t = 0.0f;
            if (maxV > minV) t = (val - minV) / (maxV - minV);
            t = fclamp(t, 0.0f, 1.0f);
            int trackH = 12;
            SDL_Rect track{ row.x, row.y + 18, row.w, trackH };
            char buf[64];
            if ((maxV - minV) <= 1.01f)
                snprintf(buf, sizeof(buf), "%.0f%s", val * 100.0f, suffix);
            else
                snprintf(buf, sizeof(buf), "%.1f%s", val, suffix);
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            bool hover = (mx >= track.x && mx < track.x + track.w &&
                (my + g_rightPanelScroll) >= track.y && (my + g_rightPanelScroll) < track.y + track.h);
            drawPillSlider(r, track, t, hover, false, label, buf, font);
            cursorY += rowH;
            };

        auto drawSliderInt = [&](const char* label, int val, int minV, int maxV) {
            const int rowH = 52;
            SDL_Rect row{ content.x, cursorY, content.w, 40 };
            float t = 0.0f;
            if (maxV > minV) t = (float)(val - minV) / (float)(maxV - minV);
            t = fclamp(t, 0.0f, 1.0f);
            int trackH = 12;
            SDL_Rect track{ row.x, row.y + 18, content.w, trackH };
            std::string vs = std::to_string(val);
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            bool hover = (mx >= track.x && mx < track.x + track.w &&
                (my + g_rightPanelScroll) >= track.y && (my + g_rightPanelScroll) < track.y + track.h);
            drawPillSlider(r, track, t, hover, false, label, vs, font);
            cursorY += rowH;
            };

        ToolSettings& ts = toolBank.get(activeTool);
        drawSlider("Size", ts.size, 1.0f, 200.0f, "");
        drawSlider("Opacity", ts.opacity, 0.0f, 1.0f, "%");
        drawSlider("Stabilizer", ts.stabilizer, 0.0f, 1.0f, "%");
        drawSlider("Hardness", ts.hardness, 0.0f, 1.0f, "%");
        drawSlider("Spacing", ts.spacing, 0.0f, 1.0f, "%");
        drawSlider("Flow", ts.flow, 0.0f, 1.0f, "%");
        drawSlider("Scatter", ts.scatter, 0.0f, 1.0f, "%");

        if (activeTool == ToolType::Smudge || activeTool == ToolType::Blur)
            drawSlider("Strength", ts.strength, 0.0f, 1.0f, "%");
        if (activeTool == ToolType::Calligraphy)
        {
            drawSlider("Angle", ts.angleDeg, 0.0f, 180.0f, " deg");
            drawSlider("Aspect", ts.aspect, 0.0f, 1.0f, "%");
        }
        if (activeTool == ToolType::Airbrush)
        {
            drawSlider("Air Radius", ts.airRadius, 2.0f, 120.0f, "px");
            drawSlider("Air Density", ts.airDensity, 0.0f, 1.0f, "%");
        }
        if (activeTool == ToolType::Eraser || activeTool == ToolType::SoftEraser)
            drawSlider("Eraser Str", ts.eraserStrength, 0.0f, 1.0f, "%");
        if (activeTool == ToolType::Smudge)
            drawSlider("Smudge Str", ts.smudgeStrength, 0.0f, 1.0f, "%");
        if (activeTool == ToolType::Blur)
            drawSlider("Blur Radius", ts.blurRadius, 1.0f, 50.0f, "px");

        if (activeTool == ToolType::Fill)
        {
            drawDividerH(r, content.x, content.x + content.w, cursorY - 6, COL_BORDER_SOFT);
            cursorY += 10;
            drawText(r, font, "Fill", content.x + 2, cursorY, COL_TEXT_MAIN);
            cursorY += 26;
            drawSliderInt("Tolerance", ts.fillTolerance, 0, 255);
            drawSliderInt("Gap close (px)", app.getEditorUiState().fillGapClose, 0, 12);
        }


        drawDividerH(r, content.x, content.x + content.w, cursorY - 6, COL_BORDER_SOFT);
        cursorY += 10;
        drawText(r, font, "Onion Skin", content.x + 2, cursorY, COL_TEXT_MAIN);
        cursorY += 26;

        SDL_Rect onionRc{ content.x, cursorY, content.w, 190 };
        g_onionPanelRectRight = onionRc;

        OnionPanelLayout onionL = buildOnionPanelLayout(onionRc);

        drawOnionSettingsPanel(
            r,
            font,
            onionL,
            SDL_GetMouseState(NULL, NULL),
            SDL_GetMouseState(NULL, NULL),
            app.isOnionSkinEnabled(),
            app.getEditorUiState().onionSteps,
            app.getEditorUiState().onionTint,
            app.onionPrevAlphaValue(),
            app.onionNextAlphaValue()
        );

        cursorY += onionRc.h;

        SDL_RenderSetClipRect(r, nullptr);
        g_rightPanelContentH = std::max(0, calcRightPanelContentHeight(activeTool));
        int maxScroll2 = std::max(0, g_rightPanelContentH - content.h);
        g_rightPanelScrollTarget = fclamp(g_rightPanelScrollTarget, 0.0f, (float)maxScroll2);
        g_rightPanelScroll = clampi(g_rightPanelScroll, 0, maxScroll2);
    }

    static int calcRightPanelContentHeight(ToolType activeTool)
    {
        int rowCount = 7;
        if (activeTool == ToolType::Smudge || activeTool == ToolType::Blur) rowCount += 1;
        if (activeTool == ToolType::Calligraphy) rowCount += 2;
        if (activeTool == ToolType::Airbrush) rowCount += 2;
        if (activeTool == ToolType::Eraser || activeTool == ToolType::SoftEraser) rowCount += 1;
        if (activeTool == ToolType::Smudge) rowCount += 1;
        if (activeTool == ToolType::Blur) rowCount += 1;
        int h = 26 + rowCount * 52;
        if (activeTool == ToolType::Fill)
            h += 10 + 26 + 2 * 52;



        h += 10 + 26 + 190;


        h += 24;
        return h;
    }

    static const char* exportFmtLabel(ExportFormat f)
    {
        switch (f)
        {
        case ExportFormat::MP4:    return "MP4";
        case ExportFormat::PNGSEQ: return "PNG SEQ";
        case ExportFormat::GIF:    return "GIF";
        default:                  return "MP4";
        }
    }

    static std::string safeProjectFolderName(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char ch : s)
        {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')
                out.push_back(ch);
            else if (ch == ' ') out.push_back('_');
        }
        if (out.empty()) out = "project";
        return out;
    }

    static void openExportSettingsDefault(App& app, ExportFormat fmt)
    {
        g_exportSettingsOpen = true;
        g_exportMenuOpen = false;
        g_exportFmt = fmt;
        g_exportW = app.getProjectW();
        g_exportH = app.getProjectH();
        g_exportFps = app.getProjectFPS();
        g_exportTransparent = false;
        int total = (int)app.getEngine().getFrameCount();
        if (total < 1) total = 1;
        g_exportStart = 0;
        g_exportEnd = total - 1;

        if (fmt == ExportFormat::MP4)
        {
            g_mp4Crf = 18;
            g_mp4BitrateKbps = 0;
            g_mp4Preset = 2;
            g_mp4UseYuv420 = true;
        }
        else if (fmt == ExportFormat::GIF)
        {
            g_gifColors = 256;
            g_gifDither = true;
            g_gifLoop = true;
            g_gifScalePct = 100;
        }
        else
        {
            g_pngCompression = 6;
            g_pngInterlace = false;
        }
        g_mp4Preset = std::clamp(g_mp4Preset, 1, 8);
        syncExportInputsFromValues();
        exportClearFocus();
    }

    static bool runExportNow(App& app)
    {
        using namespace strova::exporter;
        Settings s{};
        s.width = std::max(1, g_exportW);
        s.height = std::max(1, g_exportH);
        s.fps = std::max(1, g_exportFps);
        s.includeAlpha = g_exportTransparent;
        s.bgColor = g_exportTransparent ? SDL_Color{ 0,0,0,0 } : SDL_Color{ 255,255,255,255 };
        s.startFrame = g_exportStart;
        s.endFrame = g_exportEnd;
        s.mp4Crf = g_mp4Crf;
        s.mp4BitrateKbps = g_mp4BitrateKbps;
        s.mp4PresetIndex = std::clamp(g_mp4Preset, 1, 8);
        s.mp4UseYuv420 = g_mp4UseYuv420;
        s.mp4FastStart = true;
        s.gifMaxColors = g_gifColors;
        s.gifDither = g_gifDither;
        s.gifLoop = g_gifLoop;
        s.gifScalePct = g_gifScalePct;
        s.pngCompression = g_pngCompression;
        s.pngInterlace = g_pngInterlace;

        const std::string proj = safeProjectFolderName(app.getProjectNameStr());
        std::string base = (strova::paths::getExportsDir() / proj).string();
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        if (ec) {
            SDL_Log("Failed to create directory %s: %s", base.c_str(), ec.message().c_str());
            return false;
        }

        if (g_exportFmt == ExportFormat::PNGSEQ)
        {
            std::string folder = base + "/png_sequence";
            std::filesystem::create_directories(folder, ec);
            if (ec) {
                SDL_Log("Failed to create directory %s: %s", folder.c_str(), ec.message().c_str());
                return false;
            }
            return strova::exporter::exportPNGSequence(app.getRenderer(), app.getEngine(), folder, s);
        }
        else if (g_exportFmt == ExportFormat::GIF)
        {
            std::string out = base + "/animation.gif";
            return strova::exporter::exportGIF(app.getRenderer(), app.getEngine(), out, s);
        }
        else
        {
            std::string out = base + "/animation.mp4";
            return strova::exporter::exportMP4(app.getRenderer(), app.getEngine(), out, s);
        }
    }

    static void drawSmallLabel(SDL_Renderer* r, TTF_Font* f, int x, int y, const std::string& s)
    {
        drawText(r, f, s, x, y, COL_TEXT_DIM);
    }

    static void drawValue(SDL_Renderer* r, TTF_Font* f, int x, int y, const std::string& s)
    {
        drawText(r, f, s, x, y, COL_TEXT_MAIN);
    }

    static void drawToggle(SDL_Renderer* r, TTF_Font* f, const SDL_Rect& row, const char* label, bool on, SDL_Rect& outBox)
    {
        drawSmallLabel(r, f, row.x, row.y, label);
        outBox = { row.x + row.w - 64, row.y - 2, 56, 24 };
        int mx = 0, my = 0; SDL_GetMouseState(&mx, &my);
        drawModernButton(r, outBox, pointInRect(mx, my, outBox), on);
        drawText(r, f, on ? "On" : "Off", outBox.x + 12, outBox.y + 3, COL_TEXT_MAIN);
    }

    static SDL_Rect calcInputRect(const SDL_Rect& row)
    {
        const int boxW = 140;
        const int boxH = 26;
        return SDL_Rect{ row.x + row.w - boxW, row.y - 3, boxW, boxH };
    }

    static void drawIntInputRow(SDL_Renderer* r, TTF_Font* f, const SDL_Rect& row, const char* label,
        const std::string& valueText, bool focused, SDL_Rect& outBox)
    {
        drawSmallLabel(r, f, row.x, row.y, label);
        outBox = calcInputRect(row);
        int mx = 0, my = 0; SDL_GetMouseState(&mx, &my);
        bool hover = pointInRect(mx, my, outBox);
        drawModernButton(r, outBox, hover, focused);
        std::string shown = valueText;
        if (shown.size() > 12) shown = shown.substr(0, 12);
        drawText(r, f, shown, outBox.x + 10, outBox.y + 4, COL_TEXT_MAIN);
        if (focused)
        {
            int tw = measureTextW(f, shown);
            int cx = outBox.x + 10 + tw + 2;
            SDL_SetRenderDrawColor(r, COL_TEXT_MAIN.r, COL_TEXT_MAIN.g, COL_TEXT_MAIN.b, 200);
            SDL_RenderDrawLine(r, cx, outBox.y + 5, cx, outBox.y + outBox.h - 6);
        }
    }
}

class EditorState {
public:
    std::vector<SDL_FPoint> selPoly;
    bool lassoActive;
    bool rectSelectMode;
    EditorState() : lassoActive(false), rectSelectMode(false) {}
    void clearSelection() {
        selPoly.clear();
        selPoly.shrink_to_fit();
        lassoActive = false;
        rectSelectMode = false;
    }
};

static EditorState g_editorState;

void Editor::handleEvent(App& app, SDL_Event& e)
{
    auto pointInPoly = [](float x, float y, const std::vector<SDL_FPoint>& poly) -> bool
        {
            if (poly.size() < 3) return false;
            bool c = false;
            size_t n = poly.size();
            for (size_t i = 0, j = n - 1; i < n; j = i++)
            {
                const auto& a = poly[i];
                const auto& b = poly[j];
                float denom = (b.y - a.y);
                if (std::fabs(denom) < 1e-6f) denom = (denom < 0 ? -1e-6f : 1e-6f);
                bool cond = ((a.y > y) != (b.y > y)) && (x < (b.x - a.x) * (y - a.y) / denom + a.x);
                if (cond) c = !c;
            }
            return c;
        };

    auto pointInSelection = [&](float wx, float wy) -> bool
        {
            if (!g_hasSelection) return true;
            if (g_editorState.selPoly.size() >= 3) return pointInPoly(wx, wy, g_editorState.selPoly);
            float x1 = std::min(g_selA.x, g_selB.x);
            float y1 = std::min(g_selA.y, g_selB.y);
            float x2 = std::max(g_selA.x, g_selB.x);
            float y2 = std::max(g_selA.y, g_selB.y);
            return (wx >= x1 && wx <= x2 && wy >= y1 && wy <= y2);
        };

    auto snapToRuler = [&](SDL_FPoint p) -> SDL_FPoint
        {
            if (!app.getEditorUiState().rulerVisible) return p;
            const float ang = g_rulerAngleDeg * 0.01745329252f;
            SDL_FPoint dir{ std::cos(ang), std::sin(ang) };
            SDL_FPoint d{ p.x - g_rulerCenter.x, p.y - g_rulerCenter.y };
            float proj = d.x * dir.x + d.y * dir.y;
            float halfLen = g_rulerLength * 0.5f;
            proj = fclamp(proj, -halfLen, halfLen);
            SDL_FPoint out;
            out.x = g_rulerCenter.x + dir.x * proj;
            out.y = g_rulerCenter.y + dir.y * proj;
            return out;
        };

    auto selectedStrokeTrackId = [&]() -> int
        {
            const int layerTrackId = activeLayerTree(app).primarySelectedTrackId();
            if (layerTrackId != 0)
                return layerTrackId;
            return app.getEngine().getActiveTrack();
        };

    auto currentEditableStrokes = [&]() -> std::vector<Stroke>
        {
            const int trackId = selectedStrokeTrackId();
            if (trackId != 0)
                return app.getEngine().getFrameTrackStrokes(app.getEngine().getCurrentFrameIndex(), trackId);
            return app.getEngine().getCurrentStrokes();
        };

    auto rebuildSelectionFromIndices = [&](const std::vector<Stroke>& strokes)
        {
            if (g_selectedStrokeIndices.empty())
            {
                g_hasSelection = false;
                return;
            }
            bool found = false;
            float minx = 0.0f, miny = 0.0f, maxx = 0.0f, maxy = 0.0f;
            for (size_t idx : g_selectedStrokeIndices)
            {
                if (idx >= strokes.size()) continue;
                for (const auto& pt : strokes[idx].points)
                {
                    if (!found)
                    {
                        minx = maxx = pt.x;
                        miny = maxy = pt.y;
                        found = true;
                    }
                    else
                    {
                        minx = std::min(minx, pt.x); miny = std::min(miny, pt.y);
                        maxx = std::max(maxx, pt.x); maxy = std::max(maxy, pt.y);
                    }
                }
            }
            if (!found)
            {
                g_selectedStrokeIndices.clear();
                g_hasSelection = false;
                return;
            }
            g_hasSelection = true;
            g_editorState.selPoly.clear();
            g_selA = SDL_FPoint{ minx, miny };
            g_selB = SDL_FPoint{ maxx, maxy };
        };

    auto selectStrokesInCurrentSelection = [&]()
        {
            const auto strokes = currentEditableStrokes();
            g_selectedStrokeIndices.clear();
            for (size_t si = 0; si < strokes.size(); ++si)
            {
                const auto& st = strokes[si];
                bool inside = false;
                for (const auto& pt : st.points)
                {
                    if (pointInSelection(pt.x, pt.y))
                    {
                        inside = true;
                        break;
                    }
                }
                if (inside) g_selectedStrokeIndices.push_back(si);
            }
            rebuildSelectionFromIndices(strokes);
        };

    auto moveSelectedStrokesBy = [&](float dx, float dy)
        {
            if (g_selectedStrokeIndices.empty()) return;
            const int trackId = selectedStrokeTrackId();
            std::vector<Stroke> strokes = currentEditableStrokes();
            for (size_t idx : g_selectedStrokeIndices)
            {
                if (idx >= strokes.size()) continue;
                for (auto& pt : strokes[idx].points)
                {
                    pt.x += dx;
                    pt.y += dy;
                }
            }
            if (trackId != 0)
                app.getEngine().setFrameTrackStrokes(app.getEngine().getCurrentFrameIndex(), trackId, strokes);
            else
                app.getEngine().setFrameStrokes(app.getEngine().getCurrentFrameIndex(), strokes);
            rebuildSelectionFromIndices(strokes);
            app.markCurrentFrameEdited();
        };

    auto pointHitsSelectedStrokeBounds = [&](float wx, float wy) -> bool
        {
            if (!g_hasSelection || g_selectedStrokeIndices.empty()) return false;
            float x1 = std::min(g_selA.x, g_selB.x);
            float y1 = std::min(g_selA.y, g_selB.y);
            float x2 = std::max(g_selA.x, g_selB.x);
            float y2 = std::max(g_selA.y, g_selB.y);
            const float pad = 8.0f;
            return (wx >= x1 - pad && wx <= x2 + pad && wy >= y1 - pad && wy <= y2 + pad);
        };

    auto bucketFillAt = [&](float startWx, float startWy) -> bool
        {
            if (!pointInSelection(startWx, startWy)) return false;
            SDL_Renderer* r = app.getRenderer();
            const int W = app.getProjectW();
            const int H = app.getProjectH();
            if (!r || W <= 0 || H <= 0) return false;
            int sx = (int)std::floor(startWx);
            int sy = (int)std::floor(startWy);
            if (sx < 0 || sy < 0 || sx >= W || sy >= H) return false;

            size_t pixelCount = static_cast<size_t>(W) * static_cast<size_t>(H);
            if (pixelCount > std::numeric_limits<size_t>::max() / sizeof(Uint32))
            {
                SDL_Log("Canvas too large for fill operation");
                return false;
            }

            SDL_Texture* prevTarget = SDL_GetRenderTarget(r);
            SDL_BlendMode prevBlend = SDL_BLENDMODE_BLEND;
            SDL_GetRenderDrawBlendMode(r, &prevBlend);
            SDL_Texture* rt = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, W, H);
            if (!rt) return false;

            SDL_SetTextureBlendMode(rt, SDL_BLENDMODE_BLEND);
            SDL_SetRenderTarget(r, rt);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            SDL_RenderClear(r);

            const auto& strokes = app.getEngine().getCurrentStrokes();
            for (const Stroke& s : strokes)
                app.brushRendererHandle()->drawStrokeExport(s);

            std::vector<Uint32> pix(pixelCount);
            if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA8888, pix.data(), W * (int)sizeof(Uint32)) != 0)
            {
                SDL_SetRenderTarget(r, prevTarget);
                SDL_SetRenderDrawBlendMode(r, prevBlend);
                SDL_DestroyTexture(rt);
                return false;
            }

            SDL_SetRenderTarget(r, prevTarget);
            SDL_SetRenderDrawBlendMode(r, prevBlend);
            SDL_DestroyTexture(rt);

            auto unpack = [](Uint32 p) -> SDL_Color
                {
                    SDL_Color c;
                    c.r = (p >> 24) & 0xFF;
                    c.g = (p >> 16) & 0xFF;
                    c.b = (p >> 8) & 0xFF;
                    c.a = (p >> 0) & 0xFF;
                    return c;
                };

            auto colorDist = [](const SDL_Color& a, const SDL_Color& b) -> int
                {
                    return std::abs((int)a.r - (int)b.r) + std::abs((int)a.g - (int)b.g) +
                        std::abs((int)a.b - (int)b.b) + std::abs((int)a.a - (int)b.a);
                };

            const int tol = std::clamp(app.toolBank.get(ToolType::Fill).fillTolerance, 0, 255);
            const int gap = std::clamp(app.getEditorUiState().fillGapClose, 0, 12);
            SDL_Color target = unpack(pix[(size_t)sy * (size_t)W + (size_t)sx]);
            SDL_Color fillCol = app.getEngine().getBrushColor();
            if (colorDist(target, fillCol) <= 6) return false;

            std::vector<uint8_t> isLine((size_t)W * (size_t)H, 0);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    size_t idx = (size_t)y * (size_t)W + (size_t)x;
                    SDL_Color c = unpack(pix[idx]);
                    isLine[idx] = (colorDist(c, target) > tol) ? 1 : 0;
                }

            std::vector<uint8_t> lineDil((size_t)W * (size_t)H, 0);
            if (gap > 0)
            {
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x)
                    {
                        size_t idx = (size_t)y * (size_t)W + (size_t)x;
                        if (!isLine[idx]) continue;
                        for (int dy = -gap; dy <= gap; ++dy)
                        {
                            int yy = y + dy;
                            if (yy < 0 || yy >= H) continue;
                            int maxdx = gap - std::abs(dy);
                            int x0 = std::max(0, x - maxdx);
                            int x1 = std::min(W - 1, x + maxdx);
                            size_t row = (size_t)yy * (size_t)W;
                            for (int xx = x0; xx <= x1; ++xx)
                                lineDil[row + (size_t)xx] = 1;
                        }
                    }
            }

            std::vector<uint8_t> mask((size_t)W * (size_t)H, 0);
            std::vector<uint8_t> visited((size_t)W * (size_t)H, 0);
            std::vector<int> qx;
            std::vector<int> qy;
            qx.reserve(8192);
            qy.reserve(8192);

            auto push = [&](int x, int y, bool isSeed)
                {
                    size_t idx = (size_t)y * (size_t)W + (size_t)x;
                    if (visited[idx]) return;
                    float wx = (float)x + 0.5f;
                    float wy = (float)y + 0.5f;
                    if (!pointInSelection(wx, wy))
                    {
                        visited[idx] = 1;
                        return;
                    }
                    if (!isSeed && gap > 0 && lineDil[idx])
                    {
                        visited[idx] = 1;
                        return;
                    }
                    SDL_Color c = unpack(pix[idx]);
                    if (colorDist(c, target) > tol)
                    {
                        visited[idx] = 1;
                        return;
                    }
                    visited[idx] = 1;
                    mask[idx] = 1;
                    qx.push_back(x);
                    qy.push_back(y);
                };

            push(sx, sy, true);
            for (size_t qi = 0; qi < qx.size(); ++qi)
            {
                int x = qx[qi];
                int y = qy[qi];
                if (x > 0) push(x - 1, y, false);
                if (x + 1 < W) push(x + 1, y, false);
                if (y > 0) push(x, y - 1, false);
                if (y + 1 < H) push(x, y + 1, false);
            }

            Stroke fillStroke{};
            fillStroke.tool = ToolType::Fill;
            fillStroke.color = fillCol;
            fillStroke.thickness = 1.0f;
            fillStroke.fillTolerance = tol;
            fillStroke.fillGapClose = gap;
            fillStroke.points.reserve(4096);



            std::vector<std::pair<int, int>> prevSegs;
            std::vector<std::pair<int, int>> segs;
            int runStartY = 0;
            bool hasRun = false;

            auto flushRun = [&](int endYExclusive)
                {
                    if (!hasRun) return;
                    if (prevSegs.empty()) { hasRun = false; return; }

                    for (const auto& seg : prevSegs)
                    {
                        int x0 = seg.first;
                        int x1 = seg.second;

                        StrokePoint a{}; a.x = (float)x0; a.y = (float)runStartY; a.pressure = 1.0f;
                        StrokePoint b{}; b.x = (float)x1; b.y = (float)endYExclusive; b.pressure = 1.0f;
                        fillStroke.points.push_back(a);
                        fillStroke.points.push_back(b);
                    }
                    hasRun = false;
                };

            for (int y = 0; y < H; ++y)
            {
                segs.clear();

                int x = 0;
                while (x < W)
                {
                    size_t idx = (size_t)y * (size_t)W + (size_t)x;
                    if (!mask[idx]) { x++; continue; }

                    int x0 = x;
                    while (x < W)
                    {
                        size_t idx2 = (size_t)y * (size_t)W + (size_t)x;
                        if (!mask[idx2]) break;
                        x++;
                    }
                    int x1 = x;


                    if (!segs.empty() && x0 <= segs.back().second + 1)
                        segs.back().second = std::max(segs.back().second, x1);
                    else
                        segs.emplace_back(x0, x1);
                }

                if (!hasRun)
                {
                    prevSegs = segs;
                    runStartY = y;
                    hasRun = true;
                    continue;
                }

                if (segs == prevSegs)
                {

                    continue;
                }


                flushRun(y);
                prevSegs = segs;
                runStartY = y;
                hasRun = true;
            }

            flushRun(H);




            const size_t kRectCount = fillStroke.points.size() / 2;
            if (kRectCount > 8000)
            {
                int block = 4;
                if (kRectCount > 30000) block = 8;
                if (kRectCount > 120000) block = 16;

                const int Wb = (W + block - 1) / block;
                const int Hb = (H + block - 1) / block;

                std::vector<uint8_t> bm((size_t)Wb * (size_t)Hb, 0);
                for (int y = 0; y < H; ++y)
                {
                    const int by = y / block;
                    size_t row = (size_t)y * (size_t)W;
                    for (int x = 0; x < W; ++x)
                    {
                        if (!mask[row + (size_t)x]) continue;
                        bm[(size_t)by * (size_t)Wb + (size_t)(x / block)] = 1;
                    }
                }

                Stroke simplified = fillStroke;
                simplified.points.clear();

                std::vector<std::pair<int, int>> pSeg;
                std::vector<std::pair<int, int>> cSeg;
                int runY0 = 0;
                bool runOn = false;

                auto flush = [&](int endY)
                    {
                        if (!runOn) return;
                        if (pSeg.empty()) { runOn = false; return; }

                        for (const auto& seg : pSeg)
                        {
                            int x0 = seg.first * block;
                            int x1 = seg.second * block;
                            int y0 = runY0 * block;
                            int y1 = endY * block;

                            if (x0 < 0) x0 = 0;
                            if (y0 < 0) y0 = 0;
                            if (x1 > W) x1 = W;
                            if (y1 > H) y1 = H;

                            StrokePoint a{}; a.x = (float)x0; a.y = (float)y0; a.pressure = 1.0f;
                            StrokePoint b{}; b.x = (float)x1; b.y = (float)y1; b.pressure = 1.0f;
                            simplified.points.push_back(a);
                            simplified.points.push_back(b);
                        }
                        runOn = false;
                    };

                for (int y = 0; y < Hb; ++y)
                {
                    cSeg.clear();
                    int x = 0;
                    while (x < Wb)
                    {
                        if (!bm[(size_t)y * (size_t)Wb + (size_t)x]) { x++; continue; }
                        int x0 = x;
                        while (x < Wb && bm[(size_t)y * (size_t)Wb + (size_t)x]) x++;
                        int x1 = x;

                        if (!cSeg.empty() && x0 <= cSeg.back().second)
                            cSeg.back().second = std::max(cSeg.back().second, x1);
                        else
                            cSeg.emplace_back(x0, x1);
                    }

                    if (!runOn)
                    {
                        pSeg = cSeg;
                        runY0 = y;
                        runOn = true;
                        continue;
                    }

                    if (cSeg == pSeg)
                        continue;

                    flush(y);
                    pSeg = cSeg;
                    runY0 = y;
                    runOn = true;
                }
                flush(Hb);

                if (!simplified.points.empty())
                    fillStroke = std::move(simplified);
            }

            if (fillStroke.points.size() < 2) return false;
            app.getEngine().setTool(ToolType::Fill);
            app.getEngine().beginStroke(fillStroke.points[0].x, fillStroke.points[0].y);
            for (size_t i = 1; i < fillStroke.points.size(); ++i)
                app.getEngine().addPoint(fillStroke.points[i].x, fillStroke.points[i].y);
            app.getEngine().endStroke();
            app.markCurrentFrameEditedAndSave();
            return true;
        };

    if (e.type == SDL_QUIT)
    {
        app.requestSaveProjectNow();
        app.requestQuit();
        return;
    }

    int w = 0, h = 0;
    SDL_GetWindowSize(app.windowHandle(), &w, &h);
    app.validateEditorState();
    app.refreshUILayout(w, h);

    int mx = 0;
    int my = 0;
    Uint32 mouseButtons = 0;
    switch (e.type)
    {
    case SDL_MOUSEMOTION:
        mx = e.motion.x;
        my = e.motion.y;
        mouseButtons = e.motion.state;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        mx = e.button.x;
        my = e.button.y;
        mouseButtons = SDL_GetMouseState(nullptr, nullptr);
        break;
    default:
        mouseButtons = SDL_GetMouseState(&mx, &my);
        break;
    }
    SDL_Point mp{ mx, my };

    SDL_Rect workspace = dockWorkspace(app, w, h);
    app.dockManager().update(workspace);
    app.dockManager().updateHoverCursor(mx, my);
    syncDockPanelsToLegacyLayout(app);

    SDL_Rect undoR0{}, redoR0{}, onionR0{}, colorR0{};
    buildTopBarButtonRects(app.getUILayout(), undoR0, redoR0, onionR0, colorR0);
    g_windowBtnR = SDL_Rect{ colorR0.x + colorR0.w + 10, colorR0.y, 96, colorR0.h };
    g_importBtnR = SDL_Rect{ g_windowBtnR.x + g_windowBtnR.w + 10, colorR0.y, 104, colorR0.h };
    g_pluginBtnR = SDL_Rect{ g_importBtnR.x + g_importBtnR.w + 10, colorR0.y, 104, colorR0.h };
    g_exportBtnR = SDL_Rect{
        app.getUILayout().topBar.x + app.getUILayout().topBar.w - 140,
        app.getUILayout().topBar.y + 6,
        120,
        app.getUILayout().topBar.h - 12
    };
    const char* modalWindowItems[] = { "Canvas", "Timeline", "Layers", "Tools", "Color", "Frames", "Brush", "Preview", "Flow Settings", "Color Picker", "Plugins" };
    const int modalWindowItemCount = (int)(sizeof(modalWindowItems) / sizeof(modalWindowItems[0]));
    g_windowMenuR = SDL_Rect{ g_windowBtnR.x, g_windowBtnR.y + g_windowBtnR.h + 4, 160, modalWindowItemCount * 32 };
    g_pluginMenuR = SDL_Rect{ g_pluginBtnR.x, g_pluginBtnR.y + g_pluginBtnR.h + 4, 190, 4 * 32 };
    g_exportMenuR = SDL_Rect{ g_exportBtnR.x, g_exportBtnR.y + g_exportBtnR.h + 4, g_exportBtnR.w, 3 * 36 };
    rebuildActiveModals(w, h);

    const bool textInputActive = app.colorPickerWidget().isEditingText() || (g_exportFocus != ExportField::None);
    const bool modalInputActive = g_exportSettingsOpen || g_windowMenuOpen || g_pluginMenuOpen || g_exportMenuOpen || g_keyframeActionModal.visible;
    app.runtimeStateRef().ui.textInputActive = textInputActive;
    app.runtimeStateRef().ui.modalActive = modalInputActive;
    app.beginEditorInputRouting(mx, my, mouseButtons);

    if (modalInputActive)
        app.captureEditorUi(EditorRuntimeState::InputOwner::Modal, true);
    else if (app.colorPickerWindowState().dragging)
        app.captureEditorUi(EditorRuntimeState::InputOwner::ColorPicker, false);
    else if (app.dockManager().wantsCaptureMouse())
        app.captureEditorUi(EditorRuntimeState::InputOwner::Dock, false);

    if (g_exportSettingsOpen)
    {
        g_exportMenuOpen = false;
        SDL_Rect card{ w / 2 - 320, h / 2 - 240, 640, 480 };
        SDL_Rect closeR{ card.x + card.w - 44, card.y + 10, 34, 28 };
        SDL_Rect exportR{ card.x + card.w - 240, card.y + card.h - 56, 110, 38 };
        SDL_Rect cancelR{ card.x + card.w - 120, card.y + card.h - 56, 110, 38 };
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
        {
            g_exportSettingsOpen = false;
            exportClearFocus();
            SDL_StopTextInput();
            return;
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            if (pointInRect(mx, my, closeR) || pointInRect(mx, my, cancelR))
            {
                g_exportSettingsOpen = false;
                exportClearFocus();
                SDL_StopTextInput();
                return;
            }
            if (pointInRect(mx, my, exportR))
            {
                clampApplyExportInputs(app);
                g_exportSettingsOpen = false;
                exportClearFocus();
                SDL_StopTextInput();
                const bool ok = runExportNow(app);
                SDL_Log(ok ? "Export complete." : "Export failed.");
                return;
            }
        }
        return;
    }

    if (g_windowMenuOpen && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        if (pointInRect(mx, my, g_windowBtnR))
        {
            g_windowMenuOpen = false;
            return;
        }
        for (int i = 0; i < modalWindowItemCount; ++i)
        {
            SDL_Rect item{ g_windowMenuR.x, g_windowMenuR.y + i * 32, g_windowMenuR.w, 32 };
            if (!pointInRect(mx, my, item)) continue;
            if (std::string(modalWindowItems[i]) == "Color Picker")
            {
                app.colorPickerWindowState().visible = true;
                app.saveColorPickerWindowState();
            }
            else
            {
                app.dockManager().restorePanel(std::string(modalWindowItems[i]) == "Flow Settings" ? "FlowSettings" : modalWindowItems[i], workspace);
                app.saveDockLayoutForCurrentContext();
            }
            g_windowMenuOpen = false;
            return;
        }
        if (!pointInRect(mx, my, g_windowMenuR) && !pointInRect(mx, my, g_windowBtnR))
        {
            g_windowMenuOpen = false;
            return;
        }
        return;
    }

    if (g_pluginMenuOpen && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        if (pointInRect(mx, my, g_pluginBtnR))
        {
            g_pluginMenuOpen = false;
            g_pluginLoadedMenuOpen = false;
            return;
        }

        const auto loadedPlugins = pluginLoadedRuntimeRecords(app);
        g_pluginLoadedMenuR = pluginLoadedMenuRectForCount(g_pluginMenuR, (int)loadedPlugins.size());

        if (g_pluginLoadedMenuOpen && pointInRect(mx, my, g_pluginLoadedMenuR))
        {
            for (int i = 0; i < (int)loadedPlugins.size(); ++i)
            {
                SDL_Rect item{ g_pluginLoadedMenuR.x, g_pluginLoadedMenuR.y + i * 32, g_pluginLoadedMenuR.w, 32 };
                if (!pointInRect(mx, my, item)) continue;
                openPluginFromLoadedSubmenu(app, *loadedPlugins[(size_t)i], workspace);
                g_pluginMenuOpen = false;
                g_pluginLoadedMenuOpen = false;
                return;
            }
        }

        for (int i = 0; i < 4; ++i)
        {
            SDL_Rect item{ g_pluginMenuR.x, g_pluginMenuR.y + i * 32, g_pluginMenuR.w, 32 };
            if (!pointInRect(mx, my, item)) continue;
            std::string err;
            if (i == 0)
            {
                app.dockManager().restorePanel("Plugins", workspace);
                app.saveDockLayoutForCurrentContext();
                g_pluginMenuOpen = false;
                g_pluginLoadedMenuOpen = false;
                return;
            }
            if (i == 1)
            {
                g_pluginLoadedMenuOpen = !loadedPlugins.empty();
                return;
            }
            if (i == 2)
            {
                std::string path;
                if (platform::pickOpenFile(path))
                {
                    if (!app.pluginManager().installPackage(path, true, err))
                        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin Install Failed", err.empty() ? "Unable to install plugin package." : err.c_str(), app.windowHandle());
                    else
                    {
                        app.dockManager().syncPluginPanels(app.pluginManager().registries().dockPanels, workspace);
                        app.dockManager().restorePanel("Plugins", workspace);
                        app.saveDockLayoutForCurrentContext();
                    }
                }
                g_pluginMenuOpen = false;
                g_pluginLoadedMenuOpen = false;
                return;
            }
            app.pluginManager().reload(err);
            app.dockManager().syncPluginPanels(app.pluginManager().registries().dockPanels, workspace);
            app.saveDockLayoutForCurrentContext();
            g_pluginMenuOpen = false;
            g_pluginLoadedMenuOpen = false;
            return;
        }
        if (!pluginMenuShouldStayOpen(mx, my, g_pluginBtnR, g_pluginMenuR, g_pluginLoadedMenuOpen, g_pluginLoadedMenuR))
        {
            g_pluginMenuOpen = false;
            g_pluginLoadedMenuOpen = false;
            return;
        }
        return;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && pointInRect(mx, my, g_importBtnR))
    {
        app.playingRef() = false;
        importImageIntoActiveFrame(app);
        return;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && pointInRect(mx, my, g_pluginBtnR))
    {
        g_pluginMenuOpen = !g_pluginMenuOpen;
        g_windowMenuOpen = false;
        return;
    }

    if (g_exportMenuOpen && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        if (pointInRect(mx, my, g_exportBtnR))
        {
            g_exportMenuOpen = false;
            return;
        }
        for (int i = 0; i < 3; ++i)
        {
            SDL_Rect item{ g_exportMenuR.x, g_exportMenuR.y + i * 36, g_exportMenuR.w, 36 };
            if (!pointInRect(mx, my, item)) continue;
            app.playingRef() = false;
            if (i == 0) openExportSettingsDefault(app, ExportFormat::MP4);
            if (i == 1) openExportSettingsDefault(app, ExportFormat::PNGSEQ);
            if (i == 2) openExportSettingsDefault(app, ExportFormat::GIF);
            return;
        }
        if (!pointInRect(mx, my, g_exportMenuR) && !pointInRect(mx, my, g_exportBtnR))
        {
            g_exportMenuOpen = false;
            return;
        }
        return;
    }

    auto& cpwEarly = app.colorPickerWindowState();
    if (cpwEarly.visible)
    {
        SDL_Rect closeRc = floatingColorPickerCloseRect(app);
        SDL_Rect headerRc = floatingColorPickerHeaderRect(app);
        SDL_Rect innerRc = leftColorPopupInnerRect(SDL_Rect{ cpwEarly.rect.x + 2, cpwEarly.rect.y + 26, std::max(0, cpwEarly.rect.w - 4), std::max(0, cpwEarly.rect.h - 28) });
        app.colorPickerWidget().layout(innerRc);

        if (e.type == SDL_TEXTINPUT && app.colorPickerWidget().handleTextInput(e.text.text)) { app.syncColorPickerCommand(); return; }
        if (e.type == SDL_KEYDOWN)
        {
            if (e.key.keysym.sym == SDLK_ESCAPE && pointInRect(mx, my, cpwEarly.rect))
            {
                cpwEarly.visible = false;
                app.saveColorPickerWindowState();
                SDL_StopTextInput();
                return;
            }
            if (app.colorPickerWidget().handleKeyDown(e.key.keysym.sym)) { app.syncColorPickerCommand(); return; }
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            if (pointInRect(mx, my, closeRc))
            {
                cpwEarly.visible = false;
                app.saveColorPickerWindowState();
                SDL_StopTextInput();
                return;
            }
            if (pointInRect(mx, my, headerRc))
            {
                cpwEarly.dragging = true;
                cpwEarly.dragOffsetX = mx - cpwEarly.rect.x;
                cpwEarly.dragOffsetY = my - cpwEarly.rect.y;
                app.captureEditorUi(EditorRuntimeState::InputOwner::ColorPicker, false);
                return;
            }
            if (pointInRect(mx, my, cpwEarly.rect) && app.colorPickerWidget().handleMouseDown(mx, my))
            {
                app.syncColorPickerCommand();
                return;
            }
        }
        if (e.type == SDL_MOUSEMOTION)
        {
            if (cpwEarly.dragging)
            {
                cpwEarly.rect.x = mx - cpwEarly.dragOffsetX;
                cpwEarly.rect.y = my - cpwEarly.dragOffsetY;
                app.saveColorPickerWindowState();
                app.captureEditorUi(EditorRuntimeState::InputOwner::ColorPicker, false);
                return;
            }
            if (pointInRect(mx, my, cpwEarly.rect) && app.colorPickerWidget().handleMouseMotion(mx, my))
            {
                app.syncColorPickerCommand();
                return;
            }
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        {
            if (cpwEarly.dragging)
            {
                cpwEarly.dragging = false;
                app.saveColorPickerWindowState();
                app.captureEditorUi(EditorRuntimeState::InputOwner::ColorPicker, false);
                return;
            }
            if (pointInRect(mx, my, cpwEarly.rect))
            {
                app.colorPickerWidget().handleMouseUp(mx, my);
                app.syncColorPickerCommand();
                return;
            }
        }
    }

    bool dockLayoutChanged = false;
    if (app.dockManager().handleEvent(e, mx, my, workspace, dockLayoutChanged))
    {
        app.captureEditorUi(EditorRuntimeState::InputOwner::Dock, false);
        syncDockPanelsToLegacyLayout(app);
        if (dockLayoutChanged) app.saveDockLayoutForCurrentContext();
        return;
    }
    SDL_Rect toolsArea = app.dockManager().contentRect("Tools");
    g_toolGridExtraTop = 48;
    SDL_Rect brushArea = app.dockManager().contentRect("Brush");
    SDL_Rect previewArea = app.dockManager().contentRect("Preview");
    SDL_Rect framesArea = app.dockManager().contentRect("Frames");
    SDL_Rect layersArea = app.dockManager().contentRect("Layers");
    SDL_Rect timelineArea = app.dockManager().contentRect("Timeline");
    SDL_Rect colorArea = app.dockManager().contentRect("Color");






    if (g_exportSettingsOpen)
    {

        g_exportMenuOpen = false;

        SDL_Rect overlay{ 0,0,w,h };
        (void)overlay;

        SDL_Rect card{ w / 2 - 320, h / 2 - 240, 640, 480 };
        SDL_Rect closeR{ card.x + card.w - 44, card.y + 10, 34, 28 };
        SDL_Rect exportR{ card.x + card.w - 240, card.y + card.h - 56, 110, 38 };
        SDL_Rect cancelR{ card.x + card.w - 120, card.y + card.h - 56, 110, 38 };


        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
        {
            g_exportSettingsOpen = false;
            exportClearFocus();
            SDL_StopTextInput();
            return;
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {

            if (pointInRect(mx, my, closeR) || pointInRect(mx, my, cancelR))
            {
                g_exportSettingsOpen = false;
                exportClearFocus();
                SDL_StopTextInput();
                return;
            }


            if (pointInRect(mx, my, exportR))
            {
                clampApplyExportInputs(app);
                g_exportSettingsOpen = false;
                exportClearFocus();
                SDL_StopTextInput();

                const bool ok = runExportNow(app);
                SDL_Log(ok ? "Export complete." : "Export failed.");
                return;
            }
        }


        return;
    }

    SDL_FRect pageF = getPageRectF(app.getUILayout(), app.getProjectW(), app.getProjectH(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
    bool overPage = pointInRectF(mx, my, pageF);

    syncEditorModeFlags(app);
    TimelinePanelRects timelineRects = buildTimelinePanelRects(app, timelineArea);
    app.transportBarRect() = timelineRects.transport;
    app.timelineStripRect() = timelineRects.strip;

    SDL_Rect onionPanelRc = app.rightPanelOpenRef() ? g_onionPanelRectRight : SDL_Rect{ 0,0,0,0 };
    OnionPanelLayout onionLayout = buildOnionPanelLayout(onionPanelRc);

    SDL_Rect playR = timelineRects.play;
    SDL_Rect stopR = timelineRects.stop;
    SDL_Rect addR = timelineRects.add;
    SDL_Rect recordFlowR = timelineQuickButtonRect(timelineRects, 0);
    SDL_Rect stopRecordFlowR = timelineQuickButtonRect(timelineRects, 1);
    SDL_Rect flowLinkToggleR = timelineQuickButtonRect(timelineRects, 2);
    SDL_Rect addKeyManualR = timelineManualButtonRect(timelineRects, 0);
    SDL_Rect deleteKeyManualR = timelineManualButtonRect(timelineRects, 1);
    SDL_Rect startKeyManualR = timelineManualButtonRect(timelineRects, 2);
    SDL_Rect endKeyManualR = timelineManualButtonRect(timelineRects, 3);
    SDL_Rect linearInterpR = timelineManualButtonRect(timelineRects, 4);
    SDL_Rect easeInterpR = timelineManualButtonRect(timelineRects, 5);

    if (g_keyframePanelResizing && e.type == SDL_MOUSEMOTION)
    {
        const int dy = my - g_keyframeResizeStartY;
        const int availableH = std::max(0, timelineArea.h - 16);
        const int splitterH = 6;
        const int minPanelH = 120;
        const int maxKeyH = std::max(minPanelH, availableH - minPanelH - splitterH);
        g_keyframePanelHeight = clampi(g_keyframeResizeStartHeight + dy, minPanelH, maxKeyH);
        return;
    }
    if (g_keyframePanelResizing && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
    {
        g_keyframePanelResizing = false;
        return;
    }

    if (g_keyframeDragging && e.type == SDL_MOUSEMOTION)
    {
        const int newFrame = keyframeXToFrame(app, timelineRects.keyframes, mx);
        if (newFrame != g_selectedTransformKey.frameIndex)
        {
            if (app.getEngine().moveTransformKeyframe(g_selectedTransformKey.trackId, g_selectedTransformKey.channel, g_selectedTransformKey.frameIndex, newFrame))
            {
                g_selectedTransformKey.frameIndex = newFrame;
                g_keyframeTxnChanged = true;
            }
        }
        return;
    }
    if (g_keyframeDragging && e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
    {
        g_keyframeDragging = false;
        const bool committed = g_keyframeTxnChanged;
        finishTimelineTransaction(app, committed);
        g_keyframeTxnChanged = false;
        app.switchToFrameIndex((size_t)std::max(0, g_selectedTransformKey.frameIndex));
        if (committed)
            app.markCurrentFrameEditedAndSave();
        return;
    }

    if (g_keyframePanelFocused && g_selectedTransformKey.valid && e.type == SDL_KEYDOWN &&
        (e.key.keysym.sym == SDLK_DELETE || e.key.keysym.sym == SDLK_BACKSPACE))
    {
        app.getEngine().removeTransformKeyframe(g_selectedTransformKey.trackId, g_selectedTransformKey.channel, g_selectedTransformKey.frameIndex);
        g_selectedTransformKey.valid = false;
        app.markCurrentFrameEditedAndSave();
        return;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        if (g_keyframeActionModal.visible)
        {
            SDL_Rect card = keyframeModalRect(g_keyframeActionModal);
            if (g_keyframeActionModal.confirmDuplicate)
            {
                SDL_Rect cancelR = keyframeModalButtonRect(card, 0, true);
                SDL_Rect confirmR = keyframeModalButtonRect(card, 1, true);
                if (pointInRect(mx, my, cancelR))
                {
                    closeKeyframeActionModal();
                    return;
                }
                if (pointInRect(mx, my, confirmR))
                {
                    applyStackedKeyframeAction(app, g_keyframeActionModal.targetTrackId, g_keyframeActionModal.targetFrame);
                    closeKeyframeActionModal();
                    return;
                }
                if (!pointInRect(mx, my, card))
                {
                    closeKeyframeActionModal();
                    return;
                }
            }
            else
            {
                SDL_Rect addKeyR = keyframeModalButtonRect(card, 0, false);
                SDL_Rect startKeyR = keyframeModalButtonRect(card, 1, false);
                SDL_Rect endKeyR = keyframeModalButtonRect(card, 2, false);
                KeyframeModalAction action = KeyframeModalAction::None;
                if (pointInRect(mx, my, addKeyR)) action = KeyframeModalAction::Add;
                else if (pointInRect(mx, my, startKeyR)) action = KeyframeModalAction::Start;
                else if (pointInRect(mx, my, endKeyR)) action = KeyframeModalAction::End;
                if (action != KeyframeModalAction::None)
                {
                    if (action == KeyframeModalAction::Start)
                    {
                        applyVisibilityStartAction(app, g_keyframeActionModal.targetTrackId, g_keyframeActionModal.targetFrame);
                        closeKeyframeActionModal();
                    }
                    else if (action == KeyframeModalAction::End)
                    {
                        applyVisibilityEndAction(app, g_keyframeActionModal.targetTrackId, g_keyframeActionModal.targetFrame);
                        closeKeyframeActionModal();
                    }
                    else if (hasTransformKeyAtFrame(app, g_keyframeActionModal.targetTrackId, g_keyframeActionModal.targetFrame))
                    {
                        g_keyframeActionModal.confirmDuplicate = true;
                        g_keyframeActionModal.pendingAction = action;
                    }
                    else
                    {
                        applyStackedKeyframeAction(app, g_keyframeActionModal.targetTrackId, g_keyframeActionModal.targetFrame);
                        closeKeyframeActionModal();
                    }
                    return;
                }
                if (!pointInRect(mx, my, card))
                {
                    closeKeyframeActionModal();
                    return;
                }
            }
        }
        if (pointInRect(mx, my, playR))
        {
            app.playingRef() = !app.playingRef();
            if (app.playingRef())
            {
                app.resetPlaybackClock();
            }
            return;
        }
        if (pointInRect(mx, my, stopR))
        {
            app.playingRef() = false;
            app.resetPlaybackAccumulator();
            app.switchToFrameIndex(0);
            return;
        }
        if (pointInRect(mx, my, addR))
        {
            app.playingRef() = false;
            app.storeCurrentDrawFrameLayerTree();
            app.getEngine().addFrame();
            app.initFreshLayerTreeForFrame(app.getEngine().getFrameCount() - 1);
            app.switchToFrameIndex(app.getEngine().getFrameCount() - 1);
            app.rebuildAllThumbnailsNow();
            app.requestSaveProjectNow();
            return;
        }
        if (!g_keyframePanelFocused && pointInRect(mx, my, recordFlowR))
        {
            armFlowLink(app);
            return;
        }
        if (!g_keyframePanelFocused && pointInRect(mx, my, stopRecordFlowR))
        {
            app.flowCapturer().disarm();
            return;
        }
        if (!g_keyframePanelFocused && pointInRect(mx, my, flowLinkToggleR))
        {
            app.flowLinkEnabledRef() = !app.flowLinkEnabledValue();
            if (app.flowLinkEnabledValue() && app.flowCapturer().armed && activeLayerTree(app).primarySelectedTrackId() != 0)
            {
                app.activeToolModeRef() = ToolMode::Move;
                syncLogicalToolMode(app);
            }
            app.requestSaveProjectNow();
            return;
        }
        if (g_keyframePanelFocused && pointInRect(mx, my, addKeyManualR))
        {
            const int selectedTrackId = activeLayerTree(app).primarySelectedTrackId();
            if (selectedTrackId != 0)
                addTransformKeysAtCurrentFrame(app, selectedTrackId);
            return;
        }
        if (g_keyframePanelFocused && pointInRect(mx, my, deleteKeyManualR))
        {
            const int selectedTrackId = activeLayerTree(app).primarySelectedTrackId();
            if (selectedTrackId != 0)
                deleteTransformKeysAtCurrentFrame(app, selectedTrackId);
            return;
        }
        if (g_keyframePanelFocused && pointInRect(mx, my, startKeyManualR))
        {
            const int selectedTrackId = activeLayerTree(app).primarySelectedTrackId();
            if (selectedTrackId != 0)
                applyVisibilityStartAction(app, selectedTrackId, (int)app.getEngine().getCurrentFrameIndex());
            return;
        }
        if (g_keyframePanelFocused && pointInRect(mx, my, endKeyManualR))
        {
            const int selectedTrackId = activeLayerTree(app).primarySelectedTrackId();
            if (selectedTrackId != 0)
                applyVisibilityEndAction(app, selectedTrackId, (int)app.getEngine().getCurrentFrameIndex());
            return;
        }
        if (g_keyframePanelFocused && pointInRect(mx, my, linearInterpR))
        {
            const int selectedTrackId = activeLayerTree(app).primarySelectedTrackId();
            if (selectedTrackId != 0)
            {
                app.getEngine().setTransformInterpolationMode(selectedTrackId, DrawingEngine::TransformInterpolationMode::Linear);
                app.markCurrentFrameEditedAndSave();
            }
            return;
        }
        if (g_keyframePanelFocused && pointInRect(mx, my, easeInterpR))
        {
            const int selectedTrackId = activeLayerTree(app).primarySelectedTrackId();
            if (selectedTrackId != 0)
            {
                app.getEngine().setTransformInterpolationMode(selectedTrackId, DrawingEngine::TransformInterpolationMode::Ease);
                app.markCurrentFrameEditedAndSave();
            }
            return;
        }
        if (pointInRect(mx, my, timelineRects.splitter))
        {
            g_keyframePanelResizing = true;
            g_keyframeResizeStartY = my;
            g_keyframeResizeStartHeight = g_keyframePanelHeight;
            return;
        }
        if (pointInRect(mx, my, timelineRects.keyframes))
        {
            DrawingEngine::TransformChannel hitChannel = DrawingEngine::TransformChannel::PosX;
            int hitFrame = 0;
            if (findTransformKeyAtPoint(app, timelineRects.keyframes, mx, my, hitChannel, hitFrame))
            {
                g_selectedTransformKey.valid = true;
                g_selectedTransformKey.trackId = activeLayerTree(app).primarySelectedTrackId();
                g_selectedTransformKey.channel = hitChannel;
                g_selectedTransformKey.frameIndex = hitFrame;
                beginTimelineTransaction(app);
                g_keyframeTxnChanged = false;
                g_keyframeDragging = true;
                g_keyframeDragTrackId = g_selectedTransformKey.trackId;
                g_keyframeDragChannel = hitChannel;
                g_keyframeDragStartFrame = hitFrame;
                app.switchToFrameIndex((size_t)std::max(0, hitFrame));
                closeKeyframeActionModal();
                return;
            }

            g_selectedTransformKey.valid = false;
            return;
        }

        bool overTimeline = SDL_PointInRect(&mp, &app.timelineStripRect());
        if (g_keyframePanelFocused && overTimeline)
        {
            return;
        }
        if (overTimeline)
        {
            const int pad = 8;
            const int gap = 8;
            const int thumbW = 150;
            const int thumbH = 90;
            int startX = app.timelineStripRect().x + pad - app.timelineScrollRef();
            int y = app.timelineStripRect().y + (app.timelineStripRect().h - thumbH) / 2;
            size_t total = app.getEngine().getFrameCount();
            for (size_t i = 0; i < total; ++i)
            {
                SDL_Rect slot{ startX, y, thumbW, thumbH };
                if (pointInRect(mx, my, slot))
                {
                    app.playingRef() = false;
                    app.switchToFrameIndex(i);
                    return;
                }
                startX += thumbW + gap;
            }
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT && pointInRect(mx, my, timelineRects.keyframes))
    {
        DrawingEngine::TransformChannel hitChannel = DrawingEngine::TransformChannel::PosX;
        int hitFrame = 0;
        if (findTransformKeyAtPoint(app, timelineRects.keyframes, mx, my, hitChannel, hitFrame))
        {
            g_selectedTransformKey.valid = true;
            g_selectedTransformKey.trackId = activeLayerTree(app).primarySelectedTrackId();
            g_selectedTransformKey.channel = hitChannel;
            g_selectedTransformKey.frameIndex = hitFrame;
            return;
        }
    }

    app.timeline.setRect(app.timelineStripRect());
    app.timeline.setFont(app.getUiFont());
    app.timeline.setTotalFrames(std::max(1, (int)app.getEngine().getFrameCount()));
    app.timeline.setFps(app.getProjectFPS());
    app.timeline.setPlayheadFrame((int)app.getEngine().getCurrentFrameIndex());

    {
        int logicalW = 0, logicalH = 0;
        SDL_RenderGetLogicalSize(app.getRenderer(), &logicalW, &logicalH);
        if (logicalW == 0 && logicalH == 0)
        {
            int winW = 0, winH = 0;
            int outW = 0, outH = 0;
            SDL_GetWindowSize(app.windowHandle(), &winW, &winH);
            SDL_GetRendererOutputSize(app.getRenderer(), &outW, &outH);
            if (winW > 0 && winH > 0 && outW > 0 && outH > 0 && (outW != winW || outH != winH))
            {
                mx = (int)std::lround((double)mx * (double)outW / (double)winW);
                my = (int)std::lround((double)my * (double)outH / (double)winH);
            }
        }
    }


    LayerPanelLayout layerPanel = buildLayerPanelLayout(layersArea);
    bool overLayerPanel = pointInRect(mx, my, layerPanel.panel);
    bool overLayerRows = pointInRect(mx, my, layerPanel.rows);
    if (overLayerRows && e.type == SDL_MOUSEWHEEL)
    {
        const auto rows = activeLayerTree(app).buildRows();
        const int rowH = 36;
        const int contentH = (int)rows.size() * rowH;
        const int maxScroll = std::max(0, contentH - layerPanel.rows.h);
        g_layerPanelScroll = clampi(g_layerPanelScroll - e.wheel.y * 36, 0, maxScroll);
        app.captureEditorUi(EditorRuntimeState::InputOwner::LayerPanel, false);
        return;
    }

    if (overLayerPanel && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        if (pointInRect(mx, my, expandHitRect(layerPanel.addBtn, 6, 4))) { createLayerFromPanel(app, false); return; }
        if (pointInRect(mx, my, expandHitRect(layerPanel.childBtn, 6, 4))) { createLayerFromPanel(app, true); return; }
        if (pointInRect(mx, my, expandHitRect(layerPanel.groupBtn, 6, 4))) { groupSelectedLayersFromPanel(app); return; }
        if (pointInRect(mx, my, expandHitRect(layerPanel.upBtn, 6, 4))) { moveSelectedLayerFromPanel(app, -1); return; }
        if (pointInRect(mx, my, expandHitRect(layerPanel.downBtn, 6, 4))) { moveSelectedLayerFromPanel(app, +1); return; }
        if (pointInRect(mx, my, expandHitRect(layerPanel.deleteBtn, 6, 4))) { deleteSelectedLayersFromPanel(app); return; }
        if (pointInRect(mx, my, expandHitRect(layerPanel.copyBtn, 6, 4))) { copySelectedLayersToClipboard(app); return; }
        if (pointInRect(mx, my, expandHitRect(layerPanel.pasteBtn, 6, 4))) { pasteClipboardToSelectedLayers(app); return; }
        if (pointInRect(mx, my, expandHitRect(layerPanel.nextBtn, 6, 4))) { copySelectedLayersToNextFrame(app); return; }

        int nodeId = layerRowAtPoint(app, layerPanel, mx, my);
        if (nodeId != 0)
        {
            const auto rows = activeLayerTree(app).buildRows();
            const auto* node = activeLayerTree(app).findNode(nodeId);
            int localY = my - layerPanel.rows.y + g_layerPanelScroll;
            int rowIndex = localY / 36;
            if (rowIndex >= 0 && rowIndex < (int)rows.size() && node)
            {
                SDL_Rect rr = layerRowRect(layerPanel, rowIndex, std::max(0, (int)rows.size() * 36 - layerPanel.rows.h));
                int arrowX = layerThumbRect(rr).x + layerThumbRect(rr).w + 8 + rows[(size_t)rowIndex].depth * 14;
                SDL_Rect arrowHit{ arrowX - 2, rr.y + 8, 14, 20 };
                if (pointInRect(mx, my, arrowHit) && (node->isGroup || activeLayerTree(app).hasChild(node->id)))
                {
                    activeLayerTree(app).toggleExpanded(node->id);
                    app.refreshLayerPanelForActiveFrame();
                    app.requestSaveProjectNow(); return;
                }

                if (!node->isGroup && node->trackId != 0)
                {
                    if (pointInRect(mx, my, expandHitRect(layerEyeRect(rr), 6, 6)))
                    {
                        if (auto* tr = findUiTrackByEngineTrackId(app, node->trackId))
                        {
                            tr->visible = !tr->visible;
                            app.getEngine().setTrackVisible(node->trackId, tr->visible);
                            app.dirtyAllThumbs();
                            app.requestSaveProjectNow();
                        }
                        return;
                    }
                    if (pointInRect(mx, my, expandHitRect(layerLockRect(rr), 6, 6)))
                    {
                        if (auto* tr = findUiTrackByEngineTrackId(app, node->trackId))
                        {
                            tr->locked = !tr->locked;
                            app.getEngine().setTrackLocked(node->trackId, tr->locked);
                            app.requestSaveProjectNow();
                        }
                        return;
                    }
                    if (pointInRect(mx, my, expandHitRect(layerFocusRect(rr), 6, 6)))
                    {
                        app.setLayerFocusCommand(node->trackId);
                        return;
                    }
                }
            }

            SDL_Keymod mod = SDL_GetModState();
            bool ctrl = (mod & KMOD_CTRL) != 0;
            bool shift = (mod & KMOD_SHIFT) != 0;
            activeLayerTree(app).handleClick(nodeId, ctrl, shift);
            syncPrimaryLayerSelectionToEngine(app);
            return;
        }
    }

    if (overLayerPanel)
    {
        app.captureEditorUi(EditorRuntimeState::InputOwner::LayerPanel, false);
        return;
    }

    app.timeline.onFocusFrameLeftClick = [&](strova::TrackId uiTrackId, int frameIndex)
        {
            app.timeline.setPlayheadFrame(frameIndex);
            const auto* tr = app.timeline.findTrack(uiTrackId);
            if (tr && isFlowLinkUiTrack(tr))
                app.switchToFrameIndex((size_t)std::max(0, frameIndex));
        };

    app.timeline.onFocusFrameRightClick = [&](strova::TrackId, int) -> bool
        {
            return false;
        };

    if (app.timeline.handleEvent(e, mx, my))
    {
        app.captureEditorUi(EditorRuntimeState::InputOwner::Timeline, false);
        const int playhead = app.timeline.getPlayheadFrame();
        if (playhead != (int)app.getEngine().getCurrentFrameIndex())
            app.switchToFrameIndex((std::size_t)std::max(0, playhead));
        return;
    }

    SDL_Rect undoR{}, redoR{}, onionR{}, colorR{};
    buildTopBarButtonRects(app.getUILayout(), undoR, redoR, onionR, colorR);

    app.undoButton().setRect(undoR);
    app.redoButton().setRect(redoR);
    app.undoButton().setTexture(app.undoTextureHandle());
    app.redoButton().setTexture(app.redoTextureHandle());
    app.undoButton().setEnabled(app.getEngine().canUndo());
    app.redoButton().setEnabled(app.getEngine().canRedo());

    if (app.undoButton().handleEvent(e, mx, my))
    {
        app.getEngine().undo();
        app.markCurrentFrameEditedAndSave();
        return;
    }
    if (app.redoButton().handleEvent(e, mx, my))
    {
        app.getEngine().redo();
        app.markCurrentFrameEditedAndSave();
        return;
    }

    app.colorButton().setRect(colorR);
    app.colorButton().setTexture(app.colorTextureHandle());
    app.colorButton().setEnabled(true);
    if (app.colorButton().handleEvent(e, mx, my))
    {
        app.toggleRightPanelCommand();
        return;
    }

    g_windowBtnR = SDL_Rect{ colorR.x + colorR.w + 10, colorR.y, 96, colorR.h };
    g_importBtnR = SDL_Rect{ g_windowBtnR.x + g_windowBtnR.w + 10, colorR.y, 104, colorR.h };
    g_pluginBtnR = SDL_Rect{ g_importBtnR.x + g_importBtnR.w + 10, colorR.y, 104, colorR.h };
    g_exportBtnR = SDL_Rect{
        app.getUILayout().topBar.x + app.getUILayout().topBar.w - 140,
        app.getUILayout().topBar.y + 6,
        120,
        app.getUILayout().topBar.h - 12
    };
    const char* windowItems[] = { "Canvas", "Timeline", "Layers", "Tools", "Color", "Frames", "Brush", "Preview", "Flow Settings", "Color Picker", "Plugins" };
    const int windowItemCount = (int)(sizeof(windowItems) / sizeof(windowItems[0]));
    g_windowMenuR = SDL_Rect{ g_windowBtnR.x, g_windowBtnR.y + g_windowBtnR.h + 4, 160, windowItemCount * 32 };
    g_pluginMenuR = SDL_Rect{ g_pluginBtnR.x, g_pluginBtnR.y + g_pluginBtnR.h + 4, 190, 4 * 32 };

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        if (pointInRect(mx, my, g_windowBtnR))
        {
            g_windowMenuOpen = !g_windowMenuOpen;
            return;
        }
        if (g_windowMenuOpen)
        {
            for (int i = 0; i < windowItemCount; ++i)
            {
                SDL_Rect item{ g_windowMenuR.x, g_windowMenuR.y + i * 32, g_windowMenuR.w, 32 };
                if (!pointInRect(mx, my, item)) continue;
                if (std::string(windowItems[i]) == "Color Picker")
                {
                    app.colorPickerWindowState().visible = true;
                    app.saveColorPickerWindowState();
                }
                else
                {
                    app.dockManager().restorePanel(std::string(windowItems[i]) == "Flow Settings" ? "FlowSettings" : windowItems[i], workspace);
                    app.saveDockLayoutForCurrentContext();
                }
                g_windowMenuOpen = false;
                return;
            }
            if (!pointInRect(mx, my, g_windowMenuR) && !pointInRect(mx, my, g_windowBtnR))
            {
                g_windowMenuOpen = false;
            }
        }

        if (pointInRect(mx, my, g_importBtnR))
        {
            app.playingRef() = false;
            importImageIntoActiveFrame(app);
            return;
        }
        if (pointInRect(mx, my, g_pluginBtnR))
        {
            g_pluginMenuOpen = !g_pluginMenuOpen;
            g_pluginLoadedMenuOpen = false;
            g_windowMenuOpen = false;
            return;
        }
        if (g_pluginMenuOpen)
        {
            const auto loadedPlugins = pluginLoadedRuntimeRecords(app);
            g_pluginLoadedMenuR = pluginLoadedMenuRectForCount(g_pluginMenuR, (int)loadedPlugins.size());

            if (g_pluginLoadedMenuOpen && pointInRect(mx, my, g_pluginLoadedMenuR))
            {
                for (int i = 0; i < (int)loadedPlugins.size(); ++i)
                {
                    SDL_Rect item{ g_pluginLoadedMenuR.x, g_pluginLoadedMenuR.y + i * 32, g_pluginLoadedMenuR.w, 32 };
                    if (!pointInRect(mx, my, item)) continue;
                    openPluginFromLoadedSubmenu(app, *loadedPlugins[(size_t)i], workspace);
                    g_pluginMenuOpen = false;
                    g_pluginLoadedMenuOpen = false;
                    return;
                }
            }

            for (int i = 0; i < 4; ++i)
            {
                SDL_Rect item{ g_pluginMenuR.x, g_pluginMenuR.y + i * 32, g_pluginMenuR.w, 32 };
                if (!pointInRect(mx, my, item)) continue;
                std::string err;
                if (i == 0)
                {
                    app.dockManager().restorePanel("Plugins", workspace);
                    app.saveDockLayoutForCurrentContext();
                    g_pluginMenuOpen = false;
                    g_pluginLoadedMenuOpen = false;
                    return;
                }
                if (i == 1)
                {
                    g_pluginLoadedMenuOpen = !loadedPlugins.empty();
                    return;
                }
                if (i == 2)
                {
                    std::string path;
                    if (platform::pickOpenFile(path))
                    {
                        if (!app.pluginManager().installPackage(path, true, err))
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Plugin Install Failed", err.empty() ? "Unable to install plugin package." : err.c_str(), app.windowHandle());
                        else
                        {
                            app.dockManager().syncPluginPanels(app.pluginManager().registries().dockPanels, workspace);
                            app.dockManager().restorePanel("Plugins", workspace);
                            app.saveDockLayoutForCurrentContext();
                        }
                    }
                    g_pluginMenuOpen = false;
                    g_pluginLoadedMenuOpen = false;
                    return;
                }
                app.pluginManager().reload(err);
                app.dockManager().syncPluginPanels(app.pluginManager().registries().dockPanels, workspace);
                app.saveDockLayoutForCurrentContext();
                g_pluginMenuOpen = false;
                g_pluginLoadedMenuOpen = false;
                return;
            }
            if (!pluginMenuShouldStayOpen(mx, my, g_pluginBtnR, g_pluginMenuR, g_pluginLoadedMenuOpen, g_pluginLoadedMenuR))
            {
                g_pluginMenuOpen = false;
                g_pluginLoadedMenuOpen = false;
            }
        }
        if (pointInRect(mx, my, g_exportBtnR))
        {
            g_exportMenuOpen = !g_exportMenuOpen;
            return;
        }
        if (g_exportMenuOpen)
        {
            SDL_Rect menu = SDL_Rect{ g_exportBtnR.x, g_exportBtnR.y + g_exportBtnR.h + 4, g_exportBtnR.w, 3 * 36 };
            g_exportMenuR = menu;
            for (int i = 0; i < 3; i++)
            {
                SDL_Rect item{ menu.x, menu.y + i * 36, menu.w, 36 };
                if (pointInRect(mx, my, item))
                {
                    app.playingRef() = false;
                    if (i == 0) openExportSettingsDefault(app, ExportFormat::MP4);
                    if (i == 1) openExportSettingsDefault(app, ExportFormat::PNGSEQ);
                    if (i == 2) openExportSettingsDefault(app, ExportFormat::GIF);
                    return;
                }
            }
            if (!pointInRect(mx, my, menu) && !pointInRect(mx, my, g_exportBtnR))
            {
                g_exportMenuOpen = false;
                return;
            }
        }
    }

    bool hoverOnionPanel = pointInRect(mx, my, onionLayout.panel);
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        g_onionSliderDrag = OnionSliderDrag::None;

    if (e.type == SDL_MOUSEMOTION && g_onionSliderDrag != OnionSliderDrag::None)
    {
        if (g_onionSliderDrag == OnionSliderDrag::PrevAlpha)
            app.setOnionPrevAlphaCommand(alphaFromTrackX(onionLayout.prevTrack, mx));
        if (g_onionSliderDrag == OnionSliderDrag::NextAlpha)
            app.setOnionNextAlphaCommand(alphaFromTrackX(onionLayout.nextTrack, mx));
        return;
    }

    if (hoverOnionPanel && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        if (pointInRect(mx, my, onionLayout.enabledBtn))
        {
            app.setOnionEnabledCommand(!app.isOnionSkinEnabled());
            return;
        }
        if (pointInRect(mx, my, onionLayout.tintBtn))
        {
            app.setOnionTintCommand(!app.getEditorUiState().onionTint);
            return;
        }
        if (pointInRect(mx, my, onionLayout.stepMinus))
        {
            app.setOnionStepsCommand(std::max(1, app.getEditorUiState().onionSteps - 1));
            return;
        }
        if (pointInRect(mx, my, onionLayout.stepPlus))
        {
            app.setOnionStepsCommand(std::min(5, app.getEditorUiState().onionSteps + 1));
            return;
        }
        if (pointInRect(mx, my, onionLayout.prevTrack))
        {
            app.setOnionPrevAlphaCommand(alphaFromTrackX(onionLayout.prevTrack, mx));
            g_onionSliderDrag = OnionSliderDrag::PrevAlpha;
            return;
        }
        if (pointInRect(mx, my, onionLayout.nextTrack))
        {
            app.setOnionNextAlphaCommand(alphaFromTrackX(onionLayout.nextTrack, mx));
            g_onionSliderDrag = OnionSliderDrag::NextAlpha;
            return;
        }
    }

    if (hoverOnionPanel && e.type == SDL_MOUSEWHEEL)
    {
        SDL_Keymod mod = SDL_GetModState();
        bool shift = (mod & KMOD_SHIFT) != 0;
        bool ctrl = (mod & KMOD_CTRL) != 0;
        if (ctrl)
        {
            app.setOnionStepsCommand(std::clamp(app.getEditorUiState().onionSteps + ((e.wheel.y > 0) ? 1 : -1), 1, 5));
            return;
        }
        float delta = (e.wheel.y > 0) ? 0.05f : -0.05f;
        if (shift) app.setOnionNextAlphaCommand(fclamp(app.onionNextAlphaValue() + delta, 0.0f, 1.0f));
        else       app.setOnionPrevAlphaCommand(fclamp(app.onionPrevAlphaValue() + delta, 0.0f, 1.0f));
        return;
    }

    bool overCanvas = SDL_PointInRect(&mp, &app.getUILayout().canvas);
    SDL_Rect rightBarSafe = app.rightBarRectRef();
    int rightSlideOffset = (int)((1.0f - app.rightPanelAnimRef()) * rightBarSafe.w);
    rightBarSafe.x += rightSlideOffset;
    rightBarSafe.h = std::max(0, app.getUILayout().bottomBar.y - rightBarSafe.y);
    bool overRightBar = (rightBarSafe.w > 0 && rightBarSafe.h > 0) && SDL_PointInRect(&mp, &rightBarSafe);
    bool rightActive = app.rightPanelOpenRef() && rightBarSafe.w > 40 && rightBarSafe.h > 40;

    if (rightActive && overRightBar)
    {
        if (e.type == SDL_MOUSEWHEEL)
        {
            SDL_Rect inner = rightBarSafe;
            inner.x += 12;
            inner.y += 12;
            inner.w = (std::max)(0, inner.w - 24);
            inner.h = (std::max)(0, inner.h - 24);
            SDL_Rect header = inner;
            header.h = 44;
            SDL_Rect content = inner;
            content.y += header.h + 10;
            content.h = (std::max)(0, content.h - header.h - 10);
            int contentHeight = calcRightPanelContentHeight(app.getEditorUiState().activeTool);
            g_rightPanelContentH = std::max(0, contentHeight);
            int maxScroll = std::max(0, g_rightPanelContentH - std::max(0, content.h));
            g_rightPanelScrollTarget = fclamp(g_rightPanelScrollTarget - (float)(e.wheel.y * 52), 0.0f, (float)maxScroll);
            if (std::fabs(g_rightPanelScrollTarget - (float)g_rightPanelScroll) < 1.0f)
                g_rightPanelScroll = (int)std::lround(g_rightPanelScrollTarget);
            return;
        }

        SDL_Rect inner = rightBarSafe;
        inner.x += 12;
        inner.y += 12;
        inner.w = (std::max)(0, inner.w - 24);
        inner.h = (std::max)(0, inner.h - 24);
        SDL_Rect header = inner;
        header.h = 44;
        SDL_Rect content = inner;
        content.y += header.h + 10;
        content.h = (std::max)(0, content.h - header.h - 10);
        if (content.w <= 0 || content.h <= 0) return;

        int cursorY = content.y - g_rightPanelScroll;
        cursorY += 26;

        auto makeSliderTrack = [&](int& curY) -> SDL_Rect {
            SDL_Rect track{ content.x, curY + 18, content.w, 12 };
            curY += 52;
            return track;
            };

        struct SliderInfo {
            SDL_Rect track;
            RightSlider type;
        };

        std::vector<SliderInfo> sliders;
        ToolType activeTool = app.getEditorUiState().activeTool;
        ToolSettings& ts = app.toolBank.get(activeTool);

        sliders.push_back({ makeSliderTrack(cursorY), RightSlider::Size });
        sliders.push_back({ makeSliderTrack(cursorY), RightSlider::Opacity });
        sliders.push_back({ makeSliderTrack(cursorY), RightSlider::Stabilizer });
        sliders.push_back({ makeSliderTrack(cursorY), RightSlider::Hardness });
        sliders.push_back({ makeSliderTrack(cursorY), RightSlider::Spacing });
        sliders.push_back({ makeSliderTrack(cursorY), RightSlider::Flow });
        sliders.push_back({ makeSliderTrack(cursorY), RightSlider::Scatter });

        if (activeTool == ToolType::Smudge || activeTool == ToolType::Blur)
            sliders.push_back({ makeSliderTrack(cursorY), RightSlider::Strength });
        if (activeTool == ToolType::Calligraphy)
        {
            sliders.push_back({ makeSliderTrack(cursorY), RightSlider::AngleDeg });
            sliders.push_back({ makeSliderTrack(cursorY), RightSlider::Aspect });
        }
        if (activeTool == ToolType::Airbrush)
        {
            sliders.push_back({ makeSliderTrack(cursorY), RightSlider::AirRadius });
            sliders.push_back({ makeSliderTrack(cursorY), RightSlider::AirDensity });
        }
        if (activeTool == ToolType::Eraser || activeTool == ToolType::SoftEraser)
            sliders.push_back({ makeSliderTrack(cursorY), RightSlider::EraserStrength });
        if (activeTool == ToolType::Smudge)
            sliders.push_back({ makeSliderTrack(cursorY), RightSlider::SmudgeStrength });
        if (activeTool == ToolType::Blur)
            sliders.push_back({ makeSliderTrack(cursorY), RightSlider::BlurRadius });

        SDL_Rect tolTrack{}, gapTrack{};
        if (activeTool == ToolType::Fill)
        {
            cursorY += 10;
            cursorY += 26;
            tolTrack = makeSliderTrack(cursorY);
            gapTrack = makeSliderTrack(cursorY);
            cursorY += 10;
        }

        RightSlider hoveredSlider = RightSlider::None;
        SDL_Rect hoveredTrack{};

        for (const auto& s : sliders) {
            SDL_Rect hitArea = s.track;
            hitArea.y -= 10;
            hitArea.h += 20;
            if (mx >= hitArea.x && mx < hitArea.x + hitArea.w &&
                my >= hitArea.y && my < hitArea.y + hitArea.h) {
                hoveredSlider = s.type;
                hoveredTrack = s.track;
                break;
            }
        }

        if (activeTool == ToolType::Fill) {
            SDL_Rect tolHit = tolTrack; tolHit.y -= 10; tolHit.h += 20;
            SDL_Rect gapHit = gapTrack; gapHit.y -= 10; gapHit.h += 20;
            if (mx >= tolHit.x && mx < tolHit.x + tolHit.w &&
                my >= tolHit.y && my < tolHit.y + tolHit.h) {
                hoveredSlider = RightSlider::FillTolerance;
                hoveredTrack = tolTrack;
            }
            else if (mx >= gapHit.x && mx < gapHit.x + gapHit.w &&
                my >= gapHit.y && my < gapHit.y + gapHit.h) {
                hoveredSlider = RightSlider::FillGap;
                hoveredTrack = gapTrack;
            }
        }

        auto setFromTrackFloat = [&](const SDL_Rect& track, float minV, float maxV) -> float {
            if (track.w <= 1) return minV;
            int relX = mx - track.x;
            float t = (float)relX / (float)(track.w - 1);
            t = fclamp(t, 0.0f, 1.0f);
            return minV + t * (maxV - minV);
            };

        auto setFromTrackInt = [&](const SDL_Rect& track, int minV, int maxV) -> int {
            if (track.w <= 1) return minV;
            int relX = mx - track.x;
            float t = (float)relX / (float)(track.w - 1);
            t = fclamp(t, 0.0f, 1.0f);
            return (int)std::lround((float)minV + t * (float)(maxV - minV));
            };

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && hoveredSlider != RightSlider::None)
        {
            g_rightSliderDrag = hoveredSlider;
            g_rightPanelScrollTarget = (float)g_rightPanelScroll;

            switch (hoveredSlider) {
            case RightSlider::Size: ts.size = setFromTrackFloat(hoveredTrack, 1.0f, 200.0f); ts.clamp(); break;
            case RightSlider::Opacity: ts.opacity = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Stabilizer: ts.stabilizer = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Hardness: ts.hardness = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Spacing: ts.spacing = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Flow: ts.flow = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Scatter: ts.scatter = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Strength: ts.strength = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::AngleDeg: ts.angleDeg = setFromTrackFloat(hoveredTrack, 0.0f, 180.0f); ts.clamp(); break;
            case RightSlider::Aspect: ts.aspect = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::AirRadius: ts.airRadius = setFromTrackFloat(hoveredTrack, 2.0f, 120.0f); ts.clamp(); break;
            case RightSlider::AirDensity: ts.airDensity = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::EraserStrength: ts.eraserStrength = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::SmudgeStrength: ts.smudgeStrength = setFromTrackFloat(hoveredTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::BlurRadius: ts.blurRadius = setFromTrackFloat(hoveredTrack, 1.0f, 50.0f); ts.clamp(); break;
            case RightSlider::FillTolerance: ts.fillTolerance = setFromTrackInt(hoveredTrack, 0, 255); ts.clamp(); break;
            case RightSlider::FillGap: app.setFillGapCloseCommand(setFromTrackInt(hoveredTrack, 0, 12)); break;
            default: break;
            }
            app.replaceToolSettingsCommand(activeTool, ts);
            return;
        }

        if (e.type == SDL_MOUSEMOTION && g_rightSliderDrag != RightSlider::None)
        {
            SDL_Rect dragTrack{};
            bool found = false;
            for (const auto& s : sliders) {
                if (s.type == g_rightSliderDrag) {
                    dragTrack = s.track;
                    found = true;
                    break;
                }
            }
            if (!found && g_rightSliderDrag == RightSlider::FillTolerance) {
                dragTrack = tolTrack; found = true;
            }
            else if (!found && g_rightSliderDrag == RightSlider::FillGap) {
                dragTrack = gapTrack; found = true;
            }
            if (!found) {
                g_rightSliderDrag = RightSlider::None;
                g_rightPanelScrollTarget = (float)g_rightPanelScroll;
                return;
            }

            switch (g_rightSliderDrag) {
            case RightSlider::Size: ts.size = setFromTrackFloat(dragTrack, 1.0f, 200.0f); ts.clamp(); break;
            case RightSlider::Opacity: ts.opacity = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Stabilizer: ts.stabilizer = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Hardness: ts.hardness = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Spacing: ts.spacing = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Flow: ts.flow = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Scatter: ts.scatter = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::Strength: ts.strength = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::AngleDeg: ts.angleDeg = setFromTrackFloat(dragTrack, 0.0f, 180.0f); ts.clamp(); break;
            case RightSlider::Aspect: ts.aspect = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::AirRadius: ts.airRadius = setFromTrackFloat(dragTrack, 2.0f, 120.0f); ts.clamp(); break;
            case RightSlider::AirDensity: ts.airDensity = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::EraserStrength: ts.eraserStrength = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::SmudgeStrength: ts.smudgeStrength = setFromTrackFloat(dragTrack, 0.0f, 1.0f); ts.clamp(); break;
            case RightSlider::BlurRadius: ts.blurRadius = setFromTrackFloat(dragTrack, 1.0f, 50.0f); ts.clamp(); break;
            case RightSlider::FillTolerance: ts.fillTolerance = setFromTrackInt(dragTrack, 0, 255); ts.clamp(); break;
            case RightSlider::FillGap: app.setFillGapCloseCommand(setFromTrackInt(dragTrack, 0, 12)); break;
            default: break;
            }
            app.replaceToolSettingsCommand(activeTool, ts);
            return;
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        {
            if (g_rightSliderDrag != RightSlider::None) {
                g_rightSliderDrag = RightSlider::None;
                return;
            }
        }
    }

    if (colorArea.w > 0 && colorArea.h > 0 && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && pointInRect(mx, my, colorPanelOpenButtonRect(colorArea)))
    {
        auto& cpw = app.colorPickerWindowState();
        cpw.visible = true;
        app.saveColorPickerWindowState();
        return;
    }

    auto& cpw = app.colorPickerWindowState();
    if (cpw.visible)
    {
        SDL_Rect closeRc = floatingColorPickerCloseRect(app);
        SDL_Rect headerRc = floatingColorPickerHeaderRect(app);
        SDL_Rect innerRc = leftColorPopupInnerRect(SDL_Rect{ cpw.rect.x + 2, cpw.rect.y + 26, std::max(0, cpw.rect.w - 4), std::max(0, cpw.rect.h - 28) });
        app.colorPickerWidget().layout(innerRc);

        if (e.type == SDL_TEXTINPUT)
            if (app.colorPickerWidget().handleTextInput(e.text.text)) { app.syncColorPickerCommand(); return; }
        if (e.type == SDL_KEYDOWN)
        {
            if (e.key.keysym.sym == SDLK_ESCAPE && pointInRect(mx, my, cpw.rect))
            {
                cpw.visible = false;
                app.saveColorPickerWindowState();
                SDL_StopTextInput();
                return;
            }
            if (app.colorPickerWidget().handleKeyDown(e.key.keysym.sym)) { app.syncColorPickerCommand(); return; }
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            if (pointInRect(mx, my, closeRc))
            {
                cpw.visible = false;
                app.saveColorPickerWindowState();
                SDL_StopTextInput();
                return;
            }
            if (pointInRect(mx, my, headerRc))
            {
                cpw.dragging = true;
                cpw.dragOffsetX = mx - cpw.rect.x;
                cpw.dragOffsetY = my - cpw.rect.y;
                return;
            }
            if (pointInRect(mx, my, cpw.rect) && app.colorPickerWidget().handleMouseDown(mx, my))
            {
                app.syncColorPickerCommand();
                return;
            }
        }
        if (e.type == SDL_MOUSEMOTION)
        {
            if (cpw.dragging)
            {
                cpw.rect.x = mx - cpw.dragOffsetX;
                cpw.rect.y = my - cpw.dragOffsetY;
                app.saveColorPickerWindowState();
                return;
            }
            if (pointInRect(mx, my, cpw.rect) && app.colorPickerWidget().handleMouseMotion(mx, my))
            {
                app.syncColorPickerCommand();
                return;
            }
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        {
            if (cpw.dragging)
            {
                cpw.dragging = false;
                app.saveColorPickerWindowState();
                return;
            }
            if (pointInRect(mx, my, cpw.rect))
            {
                app.colorPickerWidget().handleMouseUp(mx, my);
                app.syncColorPickerCommand();
                return;
            }
        }
    }

    if (g_brushResourceManager.isOpen())
    {
        g_brushResourceManager.handleEvent(app, e);
        app.captureEditorUi(EditorRuntimeState::InputOwner::Modal, true);
        return;
    }


    syncLogicalToolMode(app);

    if (dispatchPluginDockPanelEvent(app, e, mx, my))
    {
        app.captureEditorUi(EditorRuntimeState::InputOwner::Dock, false);
        return;
    }

    if (handlePluginManagerPanelEvent(app, e, mx, my, workspace))
    {
        app.captureEditorUi(EditorRuntimeState::InputOwner::Dock, false);
        return;
    }

    ToolType activeTool = app.getEditorUiState().activeTool;
    if (brushArea.w > 0 && brushArea.h > 0)
    {
        auto& optionsPanel = sharedToolOptionsPanel();
        optionsPanel.layout(brushArea);
        ToolSettings optionSettings = app.toolBank.get(activeTool);
        if (optionsPanel.handleEvent(e, mx, my, activeTool, app.toolBank.get(activeTool), optionSettings))
        {
            app.replaceToolSettingsCommand(activeTool, optionSettings);
            if (activeTool == ToolType::Brush)
            {
                if (const auto* pkg = app.brushManager().findById(optionSettings.brushId))
                {
                    app.brushManager().select(pkg->manifest.id);
                    app.getEngine().setBrushSelection(pkg->manifest.id, pkg->manifest.version, pkg->manifest.name);
                }
            }
            handleBrushPanelActionImpl(app, optionsPanel);
            app.captureEditorUi(EditorRuntimeState::InputOwner::ToolPanel, false);
            return;
        }
    }

    if (framesArea.w > 0 && framesArea.h > 0 && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        if (pointInRect(mx, my, framesPrevButtonRect(framesArea)))
        {
            if (app.getEngine().getCurrentFrameIndex() > 0) app.switchToFrameIndex(app.getEngine().getCurrentFrameIndex() - 1);
            return;
        }
        if (pointInRect(mx, my, framesNextButtonRect(framesArea)))
        {
            if (app.getEngine().getCurrentFrameIndex() + 1 < app.getEngine().getFrameCount()) app.switchToFrameIndex(app.getEngine().getCurrentFrameIndex() + 1);
            return;
        }
        if (pointInRect(mx, my, framesAddButtonRect(framesArea)))
        {
            app.playingRef() = false;
            app.storeCurrentDrawFrameLayerTree();
            app.getEngine().addFrame();
            app.initFreshLayerTreeForFrame(app.getEngine().getFrameCount() - 1);
            app.switchToFrameIndex(app.getEngine().getFrameCount() - 1);
            app.rebuildAllThumbnailsNow();
            app.requestSaveProjectNow();
            return;
        }
    }

    if (keyframeTransformToolsVisible(app))
    {
        g_transformMoveBtnR = toolModeButtonRect(toolsArea, 1);
        g_transformRotateBtnR = toolModeButtonRect(toolsArea, 2);
        g_transformAutoBtnR = SDL_Rect{};
        g_transformAddKeyBtnR = SDL_Rect{};
        g_transformDeleteKeyBtnR = SDL_Rect{};
    }
    else if (!g_transformDragging)
    {
        g_transformMoveBtnR = SDL_Rect{};
        g_transformRotateBtnR = SDL_Rect{};
        g_transformAutoBtnR = SDL_Rect{};
        g_transformAddKeyBtnR = SDL_Rect{};
        g_transformDeleteKeyBtnR = SDL_Rect{};
        g_transformToolMode = TransformToolMode::None;
        g_editorInputMode = EditorInputMode::Draw;
    }

    const int toolsMaxScroll = toolsPanelMaxScroll(toolsArea);
    g_toolsPanelScroll = std::clamp(g_toolsPanelScroll, 0, toolsMaxScroll);
    bool overTools = SDL_PointInRect(&mp, &toolsArea);
    const SDL_Rect toolsScrollBody = toolsScrollBodyRect(toolsArea);
    bool overToolsScrollBody = SDL_PointInRect(&mp, &toolsScrollBody);
    if (overToolsScrollBody && e.type == SDL_MOUSEWHEEL)
    {
        g_toolsPanelScroll = std::clamp(g_toolsPanelScroll - e.wheel.y * 28, 0, toolsMaxScroll);
        app.captureEditorUi(EditorRuntimeState::InputOwner::ToolPanel, false);
        return;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && overTools)
    {
        if (pointInRect(mx, my, toolsColorPickerButtonRect(toolsArea)))
        {
            auto& cpw = app.colorPickerWindowState();
            cpw.visible = !cpw.visible;
            cpw.dragging = false;
            app.saveColorPickerWindowState();
            return;
        }
        if (pointInRect(mx, my, g_toolModeDrawBtnR))
        {
            app.activeToolModeRef() = ToolMode::Draw;
            syncLogicalToolMode(app);
            return;
        }
        if (pointInRect(mx, my, g_toolModeMoveBtnR))
        {
            app.activeToolModeRef() = ToolMode::Move;
            syncLogicalToolMode(app);
            return;
        }
        if (pointInRect(mx, my, g_toolModeRotateBtnR))
        {
            app.activeToolModeRef() = ToolMode::Rotate;
            syncLogicalToolMode(app);
            return;
        }
        if (pointInRect(mx, my, g_toolModeSelectBtnR))
        {
            app.activeToolModeRef() = ToolMode::Select;
            syncLogicalToolMode(app);
            return;
        }
        ToolType picked = app.getEditorUiState().activeTool;
        if (handleToolGridClick(toolsArea, mx, my, picked))
        {
            app.setToolCommand(picked);
            if (picked == ToolType::Ruler) app.setRulerVisibleCommand(true);
            g_actionActive = false;
            app.activeToolModeRef() = (picked == ToolType::Select) ? ToolMode::Select : ToolMode::Draw;
            syncLogicalToolMode(app);
            return;
        }
    }

    if (flowSettingsPanelVisible(app))
    {
        g_quickAnimPanelR = quickAnimPanelRect(app);
        const SDL_Rect quickAnimBody = quickAnimScrollBodyRect(g_quickAnimPanelR);
        g_quickAnimScroll = std::clamp(g_quickAnimScroll, 0, quickAnimPanelMaxScroll());
        if (pointInRect(mx, my, quickAnimBody) && e.type == SDL_MOUSEWHEEL)
        {
            g_quickAnimScroll = std::clamp(g_quickAnimScroll - e.wheel.y * 18, 0, quickAnimPanelMaxScroll());
            app.captureEditorUi(EditorRuntimeState::InputOwner::ToolPanel, false);
            return;
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT && pointInRect(mx, my, g_quickAnimPanelR))
        {
            auto& fs = app.flowCapturer().settings;
            SDL_Rect panelBody{ g_quickAnimPanelR.x + 6, g_quickAnimPanelR.y + 6, g_quickAnimPanelR.w - 12, g_quickAnimPanelR.h - 12 };
            for (int row = 0; row < 8; ++row)
            {
                SDL_Rect rr = quickAnimRowRect(panelBody, row, g_quickAnimScroll);
                if (!pointInRect(mx, my, rr)) continue;
                switch (row)
                {
                case 0: fs.projectFps = std::min(strova::limits::kMaxProjectFps, fs.projectFps + 5); app.setProjectFpsForFlow(fs.projectFps); break;
                case 1: fs.flowLinkSmoothing = (fs.flowLinkSmoothing == FlowLinkSmoothingMode::CatmullRom) ? FlowLinkSmoothingMode::Linear : (fs.flowLinkSmoothing == FlowLinkSmoothingMode::Linear ? FlowLinkSmoothingMode::None : FlowLinkSmoothingMode::CatmullRom); break;
                case 2: fs.catmullAlpha += 0.1f; if (fs.catmullAlpha > 1.0f) fs.catmullAlpha = 0.1f; break;
                case 3: fs.capturePosition = !fs.capturePosition; break;
                case 4: fs.captureRotation = !fs.captureRotation; break;
                case 5: fs.overlayMode = !fs.overlayMode; if (fs.overlayMode) fs.stitchMode = false; break;
                case 6: fs.stitchMode = !fs.stitchMode; if (fs.stitchMode) fs.overlayMode = false; break;
                case 7: fs.motionDampening += 0.1f; if (fs.motionDampening > 1.0f) fs.motionDampening = 0.0f; break;
                default: break;
                }
                fs.clampToSafeLimits();
                app.captureEditorUi(EditorRuntimeState::InputOwner::ToolPanel, false);
                return;
            }
        }
    }

    if ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_MOUSEWHEEL || e.type == SDL_MOUSEMOTION) &&
        app.dockManager().hitVisibleNonCanvasPanel(mx, my))
    {
        app.captureEditorUi(EditorRuntimeState::InputOwner::Dock, false);
        return;
    }

    if (!app.editorToolsCanUseMouse())
        return;

    ToolType tool = app.getEditorUiState().activeTool;

    int transformTrackId = activeLayerTree(app).primarySelectedTrackId();
    if (keyframeTransformToolsVisible(app) && g_transformToolMode != TransformToolMode::None)
    {
        const size_t transformFrame = app.getEngine().getCurrentFrameIndex();
        auto* layer = (transformTrackId != 0) ? app.getEngine().getFrameTrackLayerMutable(transformFrame, transformTrackId) : nullptr;
        const bool canAttemptTransformPick = overPage && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT;
        if (!layer && !canAttemptTransformPick)
        {
            g_transformDragging = false;
            g_transformDragTrackId = 0;
            g_transformPreviewActive = false;
            app.flowCapturer().clearFlowLinkResult();
            finishTimelineTransaction(app, false);
            g_transformTxnChanged = false;
        }
        else
        {
            const DrawingEngine::TrackLayer evalLayer = app.getEngine().getEvaluatedFrameTrackLayerCopy(transformFrame, transformTrackId);
            if (evalLayer.trackId != 0)
            {
                g_transformPreviewTransform = evalLayer.transform;
            }
            if (overPage && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
            {
                SDL_FPoint p = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
                const int pickedTrackId = pickTransformTrackAtWorldPoint(app, p);
                if (pickedTrackId != 0 && pickedTrackId != transformTrackId)
                {
                    transformTrackId = pickedTrackId;
                    if (const auto* pickedNode = activeLayerTree(app).findByTrackId(pickedTrackId))
                        activeLayerTree(app).handleClick(pickedNode->id, false, false);
                    syncPrimaryLayerSelectionToEngine(app);
                    layer = app.getEngine().getFrameTrackLayerMutable(transformFrame, transformTrackId);
                }

                if (transformTrackId != 0)
                    layer = app.getEngine().getFrameTrackLayerMutable(transformFrame, transformTrackId);

                if (layer)
                {
                    const DrawingEngine::TrackLayer beginEvalLayer = app.getEngine().getEvaluatedFrameTrackLayerCopy(transformFrame, transformTrackId);
                    beginTimelineTransaction(app);
                    g_transformTxnChanged = false;
                    app.captureEditorTool(EditorRuntimeState::InputOwner::Tool);
                    g_transformDragging = true;
                    g_transformDragTrackId = transformTrackId;
                    g_transformDragStartWorld = p;
                    g_transformDragLast = SDL_FPoint{ 0.0f, 0.0f };
                    g_selectionDragLast = p;
                    g_transformPreviewActive = true;
                    g_transformPreviewTransform = (beginEvalLayer.trackId != 0) ? beginEvalLayer.transform : layer->transform;
                    g_transformCommittedPreview = flowLinkDisplayToBaseTransform(app, (int)transformFrame, transformTrackId, g_transformPreviewTransform);
                    g_transformBaseRotation = g_transformPreviewTransform.rotation;
                    const SDL_FPoint center = layerTransformPivotWorld(beginEvalLayer.trackId != 0 ? beginEvalLayer : *layer);
                    g_transformStartMouseAngle = std::atan2(p.y - center.y, p.x - center.x);
                    g_transformStartTransform = g_transformCommittedPreview;
                    if (app.flowCapturer().armed && app.flowLinkEnabledValue())
                    {
                        g_flowLinkCaptureStartFrame = (int)transformFrame;
                        g_flowLinkConflictModalOpen = (transformFrame + 1 < app.getEngine().getFrameCount());
                        g_flowLinkStitchModePending = app.flowCapturer().settings.stitchMode;
                        if (g_flowLinkConflictModalOpen)
                        {
                            app.flowCapturer().settings.overlayMode = true;
                            app.flowCapturer().settings.stitchMode = false;
                        }
                        app.flowCapturer().onFlowLinkBegin(layer->transform.posX, layer->transform.posY, layer->transform.rotation, SDL_GetTicks() / 1000.0f);
                    }
                    return;
                }
            }
            if (e.type == SDL_MOUSEMOTION && g_transformDragging && g_transformDragTrackId == transformTrackId)
            {
                SDL_FPoint p = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
                DrawingEngine::LayerTransform tr = g_transformPreviewActive ? g_transformPreviewTransform : layer->transform;
                if (g_transformToolMode == TransformToolMode::Move)
                {
                    tr.posX = g_transformStartTransform.posX + (p.x - g_transformDragStartWorld.x);
                    tr.posY = g_transformStartTransform.posY + (p.y - g_transformDragStartWorld.y);
                }
                else if (g_transformToolMode == TransformToolMode::Rotate)
                {
                    const SDL_FPoint center = layerTransformPivotWorld(*layer);
                    const float nowAngle = std::atan2(p.y - center.y, p.x - center.x);
                    tr.rotation = g_transformBaseRotation + (nowAngle - g_transformStartMouseAngle) * 57.2957795f;
                }
                g_transformPreviewTransform = tr;
                g_transformCommittedPreview = flowLinkDisplayToBaseTransform(app, (int)transformFrame, transformTrackId, tr);
                g_transformTxnChanged = g_transformTxnChanged || transformsDiffer(g_transformStartTransform, g_transformCommittedPreview);
                if (app.flowCapturer().armed && app.flowLinkEnabledValue())
                {
                    app.flowCapturer().onFlowLinkMove(tr.posX, tr.posY, tr.rotation, SDL_GetTicks() / 1000.0f);
                    if (app.playingRef() && app.flowCapturer().settings.autoFrameGeneration)
                    {
                        if (app.getEngine().getCurrentFrameIndex() + 1 >= app.getEngine().getFrameCount())
                            app.getEngine().addFrame();
                        app.switchToFrameIndex(app.getEngine().getCurrentFrameIndex() + 1);
                    }
                }
                return;
            }
            if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && g_transformDragging && g_transformDragTrackId == transformTrackId)
            {
                g_transformDragging = false;
                g_transformDragTrackId = 0;
                bool committed = g_transformTxnChanged;
                bool committedFlowLinkClip = false;
                if (app.flowCapturer().armed && app.flowLinkEnabledValue())
                {
                    app.flowCapturer().onFlowLinkEnd(SDL_GetTicks() / 1000.0f);
                    if (app.flowCapturer().hasFlowLinkResult())
                    {
                        app.flowCapturer().setProjectFps(app.getProjectFPS());
                        FlowLinkClip clip = app.flowCapturer().buildFlowLinkClipProjectFps(std::min(g_flowLinkCaptureStartFrame, (int)transformFrame), transformTrackId, g_transformStartTransform.posX, g_transformStartTransform.posY, g_transformStartTransform.rotation, true);
                        if (!clip.empty())
                        {
                            committedFlowLinkClip = commitFlowLinkCaptureClip(app, transformTrackId, clip);
                            committed = committedFlowLinkClip || committed;
                        }
                    }
                    app.flowCapturer().clearFlowLinkResult();
                }
                const DrawingEngine::LayerTransform committedTransform = committedFlowLinkClip ? g_transformStartTransform : g_transformCommittedPreview;
                if (g_transformPreviewActive)
                    app.getEngine().setFrameTrackTransform(transformFrame, transformTrackId, committedTransform);
                committed = touchTransformKeyframe(app, transformTrackId, g_transformStartTransform, committedTransform) || committed;
                finishTimelineTransaction(app, committed);
                g_transformPreviewActive = false;
                g_transformTxnChanged = false;
                if (committed)
                    app.markCurrentFrameEditedAndSave();
                return;
            }
        }
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && g_transformDragging)
    {
        g_transformDragging = false;
        if (g_transformDragTrackId != 0)
        {
            bool committed = g_transformTxnChanged;
            bool committedFlowLinkClip = false;
            if (app.flowCapturer().armed && app.flowLinkEnabledValue())
            {
                app.flowCapturer().onFlowLinkEnd(SDL_GetTicks() / 1000.0f);
                if (app.flowCapturer().hasFlowLinkResult())
                {
                    app.flowCapturer().setProjectFps(app.getProjectFPS());
                    const int clipStartFrame = std::min(g_flowLinkCaptureStartFrame, (int)app.getEngine().getCurrentFrameIndex());
                    FlowLinkClip clip = app.flowCapturer().buildFlowLinkClipProjectFps(clipStartFrame, g_transformDragTrackId, g_transformStartTransform.posX, g_transformStartTransform.posY, g_transformStartTransform.rotation, true);
                    if (!clip.empty())
                    {
                        committedFlowLinkClip = commitFlowLinkCaptureClip(app, g_transformDragTrackId, clip);
                        committed = committedFlowLinkClip || committed;
                    }
                }
                app.flowCapturer().clearFlowLinkResult();
            }
            if (committedFlowLinkClip)
            {
                app.getEngine().setFrameTrackTransform(app.getEngine().getCurrentFrameIndex(), g_transformDragTrackId, g_transformStartTransform);
                committed = touchTransformKeyframe(app, g_transformDragTrackId, g_transformStartTransform, g_transformStartTransform) || committed;
            }
            else if (const auto* endLayer = app.getEngine().getFrameTrackLayer(app.getEngine().getCurrentFrameIndex(), g_transformDragTrackId))
            {
                committed = touchTransformKeyframe(app, g_transformDragTrackId, g_transformStartTransform, endLayer->transform) || committed;
            }
            finishTimelineTransaction(app, committed);
            g_transformTxnChanged = false;
            if (committed)
                app.markCurrentFrameEditedAndSave();
        }
        g_transformDragTrackId = 0;
        return;
    }

    if (g_editorInputMode == EditorInputMode::Transform && overPage)
        return;

    if (tool == ToolType::Ruler && overPage)
    {
        SDL_FPoint pt = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
        const float ang = g_rulerAngleDeg * 0.01745329252f;
        SDL_FPoint dir{ std::cos(ang), std::sin(ang) };
        SDL_FPoint nrm{ -dir.y, dir.x };
        SDL_FPoint a{ g_rulerCenter.x - dir.x * (g_rulerLength * 0.5f), g_rulerCenter.y - dir.y * (g_rulerLength * 0.5f) };
        SDL_FPoint b{ g_rulerCenter.x + dir.x * (g_rulerLength * 0.5f), g_rulerCenter.y + dir.y * (g_rulerLength * 0.5f) };
        auto distTo = [](SDL_FPoint p0, SDL_FPoint p1) -> float { float dx = p0.x - p1.x; float dy = p0.y - p1.y; return std::sqrt(dx * dx + dy * dy); };
        float along = (pt.x - g_rulerCenter.x) * dir.x + (pt.y - g_rulerCenter.y) * dir.y;
        float across = std::fabs((pt.x - g_rulerCenter.x) * nrm.x + (pt.y - g_rulerCenter.y) * nrm.y);

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            app.setRulerVisibleCommand(true);
            app.captureEditorTool(EditorRuntimeState::InputOwner::Tool);
            if (distTo(pt, b) <= 24.0f) g_rulerDragMode = RulerDragMode::Rotate;
            else if (std::fabs(along) <= g_rulerLength * 0.5f + 14.0f && across <= 26.0f) g_rulerDragMode = RulerDragMode::Body;
            else { g_rulerCenter = pt; g_rulerDragMode = RulerDragMode::Body; }
            g_selectionDragLast = pt;
            return;
        }
        if (e.type == SDL_MOUSEMOTION && g_rulerDragMode != RulerDragMode::None)
        {
            if (g_rulerDragMode == RulerDragMode::Body)
            {
                g_rulerCenter = pt;
            }
            else if (g_rulerDragMode == RulerDragMode::Rotate)
            {
                float dx = pt.x - g_rulerCenter.x;
                float dy = pt.y - g_rulerCenter.y;
                g_rulerAngleDeg = std::atan2(dy, dx) * 57.2957795f;
                float len = std::sqrt(dx * dx + dy * dy) * 2.0f;
                g_rulerLength = fclamp(len, 120.0f, 2400.0f);
            }
            return;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        {
            g_rulerDragMode = RulerDragMode::None;
            return;
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT)
        {
            app.setRulerVisibleCommand(false);
            g_rulerDragMode = RulerDragMode::None;
            return;
        }
    }

    if (tool == ToolType::Eyedropper && overPage && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        SDL_Color c = readPixel(app.getRenderer(), mx, my);
        app.getEngine().setBrushColor(c);
        return;
    }

    if (tool == ToolType::Fill && overPage && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        SDL_FPoint pt = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
        bucketFillAt(pt.x, pt.y);
        return;
    }

    if (tool == ToolType::Select && overPage)
    {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            SDL_FPoint p = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            if (pointHitsSelectedStrokeBounds(p.x, p.y))
            {
                app.captureEditorTool(EditorRuntimeState::InputOwner::Tool);
                g_draggingSelection = true;
                g_selectionDragLast = p;
                return;
            }

            app.captureEditorTool(EditorRuntimeState::InputOwner::Tool);
            g_actionActive = true;
            g_actionTool = tool;
            SDL_Keymod mod = SDL_GetModState();
            g_editorState.rectSelectMode = (mod & KMOD_SHIFT) != 0;
            g_actionA = p;
            g_actionB = g_actionA;
            g_hasSelection = true;
            g_selectedStrokeIndices.clear();

            if (g_editorState.rectSelectMode)
            {
                g_editorState.selPoly.clear();
                g_selA = g_actionA;
                g_selB = g_actionA;
            }
            else
            {
                g_editorState.selPoly.clear();
                g_editorState.selPoly.reserve(256);
                g_editorState.selPoly.push_back(g_actionA);
                g_selA = g_actionA;
                g_selB = g_actionA;
                g_editorState.lassoActive = true;
            }
            return;
        }

        if (e.type == SDL_MOUSEMOTION && g_draggingSelection)
        {
            SDL_FPoint p = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            float dx = p.x - g_selectionDragLast.x;
            float dy = p.y - g_selectionDragLast.y;
            g_selectionDragLast = p;
            moveSelectedStrokesBy(dx, dy);
            return;
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && g_draggingSelection)
        {
            g_draggingSelection = false;
            app.requestSaveProjectNow();
            return;
        }

        if (e.type == SDL_MOUSEMOTION && g_actionActive)
        {
            SDL_FPoint p = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            g_actionB = p;
            if (g_editorState.rectSelectMode)
                g_selB = p;
            else if (g_editorState.lassoActive)
            {
                if (!g_editorState.selPoly.empty())
                {
                    SDL_FPoint last = g_editorState.selPoly.back();
                    float dx = p.x - last.x;
                    float dy = p.y - last.y;
                    if ((dx * dx + dy * dy) >= 1.5f * 1.5f)
                        g_editorState.selPoly.push_back(p);
                }
                float minx = p.x, miny = p.y, maxx = p.x, maxy = p.y;
                for (const auto& q : g_editorState.selPoly)
                {
                    minx = std::min(minx, q.x); miny = std::min(miny, q.y);
                    maxx = std::max(maxx, q.x); maxy = std::max(maxy, q.y);
                }
                g_selA = SDL_FPoint{ minx, miny };
                g_selB = SDL_FPoint{ maxx, maxy };
            }
            return;
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && g_actionActive)
        {
            g_actionActive = false;
            if (g_editorState.rectSelectMode)
            {
                float dx = std::fabs(g_selB.x - g_selA.x);
                float dy = std::fabs(g_selB.y - g_selA.y);
                if (dx < 2.0f && dy < 2.0f)
                {
                    g_hasSelection = false;
                    g_editorState.selPoly.clear();
                    g_selectedStrokeIndices.clear();
                }
                else
                {
                    selectStrokesInCurrentSelection();
                }
            }
            else
            {
                g_editorState.lassoActive = false;
                if (g_editorState.selPoly.size() >= 3)
                {
                    SDL_FPoint a = g_editorState.selPoly.front();
                    SDL_FPoint b = g_editorState.selPoly.back();
                    float dx = a.x - b.x;
                    float dy = a.y - b.y;
                    if ((dx * dx + dy * dy) > 2.0f * 2.0f)
                        g_editorState.selPoly.push_back(a);
                    selectStrokesInCurrentSelection();
                }
                else
                {
                    g_hasSelection = false;
                    g_editorState.selPoly.clear();
                    g_selectedStrokeIndices.clear();
                }
            }
            return;
        }
    }

    if (isActionTool(tool) && tool != ToolType::Fill && tool != ToolType::Select && overPage)
    {
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            app.captureEditorTool(EditorRuntimeState::InputOwner::Tool);
            g_actionActive = true;
            g_actionTool = tool;
            g_actionA = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            if (app.getEditorUiState().rulerVisible) g_actionA = snapToRuler(g_actionA);
            g_actionB = g_actionA;
            return;
        }
        if (e.type == SDL_MOUSEMOTION && g_actionActive)
        {
            g_actionB = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            if (app.getEditorUiState().rulerVisible) g_actionB = snapToRuler(g_actionB);
            return;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && g_actionActive)
        {
            g_actionB = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            if (app.getEditorUiState().rulerVisible) g_actionB = snapToRuler(g_actionB);
            app.getEngine().setTool(g_actionTool);
            app.getEngine().beginStroke(g_actionA.x, g_actionA.y);
            app.getEngine().addPoint(g_actionB.x, g_actionB.y);
            app.getEngine().endStroke();
            app.markCurrentFrameEditedAndSave();
            g_actionActive = false;
            return;
        }
    }

    if (isStrokeTool(tool))
    {
        ToolSettings& ts = app.toolBank.get(tool);
        float radiusWorld = (ts.size * 0.5f);
        bool isEraser = (tool == ToolType::Eraser);

        if (overPage && e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            app.hasFilteredRef() = false;
            SDL_FPoint pt = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            if (app.getEditorUiState().rulerVisible && tool != ToolType::Ruler) pt = snapToRuler(pt);

            app.flowCapturer().colorSampler = [&](float x, float y) -> SDL_Color {
                return app.colorPickerWidget().sampleAtPosition(x, y);
                };

            if (!pointInSelection(pt.x, pt.y))
            {
                app.drawingRef() = false;
                return;
            }

            app.captureEditorTool(EditorRuntimeState::InputOwner::Tool);
            app.drawingRef() = true;

            if (isEraser)
            {
                app.getEngine().beginEraseSession();
                app.getEngine().eraseAt(pt.x, pt.y, radiusWorld);
                return;
            }

            app.getEngine().setTool(tool);
            app.getEngine().setToolSettings(ts);
            beginStrokePreview(app, tool, ts, pt);
            float now = SDL_GetTicks() / 1000.0f;
            app.flowCapturer().onStrokeBegin(pt.x, pt.y, now);
            return;
        }
        if (app.drawingRef() && e.type == SDL_MOUSEMOTION)
        {
            if (!overPage)
            {
                app.drawingRef() = false;
                if (isEraser) app.getEngine().endEraseSession();
                else
                {
                    clearStrokePreview();
                    float now = SDL_GetTicks() / 1000.0f;
                    app.flowCapturer().onStrokeEnd(now);
                    app.flowCapturer().clearResult();
                }
                return;
            }
            SDL_FPoint raw = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            if (app.getEditorUiState().rulerVisible && tool != ToolType::Ruler) raw = snapToRuler(raw);
            SDL_FPoint prevFiltered = app.filteredPointRef();
            if (!app.hasFilteredRef()) prevFiltered = raw;

            float alphaBase = fclamp(1.0f - app.stabilizerValue(), 0.02f, 1.0f);
            if (!app.hasFilteredRef()) { app.filteredPointRef() = raw; app.hasFilteredRef() = true; }
            else
            {
                float dx = raw.x - app.filteredPointRef().x;
                float dy = raw.y - app.filteredPointRef().y;
                float d = std::sqrt(dx * dx + dy * dy);
                float boost = fclamp(d / (radiusWorld * 3.0f + 0.001f), 0.0f, 1.0f);
                float alpha = fclamp(alphaBase * (0.35f + 0.65f * boost), 0.02f, 1.0f);
                app.filteredPointRef().x += dx * alpha;
                app.filteredPointRef().y += dy * alpha;
            }
            if (isEraser)
            {
                const float ex = app.filteredPointRef().x;
                const float ey = app.filteredPointRef().y;
                const float dx = ex - prevFiltered.x;
                const float dy = ey - prevFiltered.y;
                const float spacingWorld = std::max(0.05f, ts.size * std::max(0.01f, ts.spacing));
                const float dist = std::sqrt(dx * dx + dy * dy);
                const int steps = std::max(1, (int)std::ceil(dist / spacingWorld));
                for (int si = 1; si <= steps; ++si)
                {
                    const float t = (float)si / (float)steps;
                    const float px = prevFiltered.x + dx * t;
                    const float py = prevFiltered.y + dy * t;
                    if (pointInSelection(px, py))
                        app.getEngine().eraseAt(px, py, radiusWorld);
                }
                return;
            }
            if (pointInSelection(app.filteredPointRef().x, app.filteredPointRef().y))
                appendStrokePreviewPoint(app.filteredPointRef());
            float now = SDL_GetTicks() / 1000.0f;
            app.flowCapturer().onStrokeMove(app.filteredPointRef().x, app.filteredPointRef().y, now);
            return;
        }

        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        {
            if (app.drawingRef())
            {
                app.drawingRef() = false;
                if (isEraser)
                {
                    app.getEngine().endEraseSession();
                    app.markCurrentFrameEditedAndSave();
                    return;
                }
                float now = SDL_GetTicks() / 1000.0f;
                app.flowCapturer().onStrokeEnd(now);
                const bool capturedFlowStroke = app.flowCapturer().hasResult();

                bool committedStroke = false;
                if (!capturedFlowStroke)
                {
                    committedStroke = commitStrokePreview(app, ts);
                    if (committedStroke)
                        app.markCurrentFrameEdited();
                }
                else
                {
                    // Keep the preview alive until the Flow track is successfully committed.
                    // If Flow track creation fails, we fall back to a normal stroke commit.
                }

                if (capturedFlowStroke)
                {
                    app.flowCapturer().setProjectFps(app.getProjectFPS());
                    auto frames = app.flowCapturer().buildFramesProjectFps();
                    if (!frames.empty())
                    {
                        strova::TrackId flowTrackId = 0;
                        DrawingEngine::TrackId engFlowId = 0;
                        std::string flowName = "Flow";
                        {
                            auto* reusableBaseFlowTrack = static_cast<strova::TimelineTrack*>(nullptr);
                            auto& ts2 = app.timeline.state();
                            for (auto& tr : ts2.tracks)
                            {
                                if (tr.kind != strova::TrackKind::Flow || tr.name != "Flow")
                                    continue;
                                bool hasClip = false;
                                for (const auto& clip : ts2.clips)
                                {
                                    if (clip.trackId == tr.id && clip.lengthFrames > 0)
                                    {
                                        hasClip = true;
                                        break;
                                    }
                                }
                                bool hasContent = false;
                                if (tr.engineTrackId != 0)
                                {
                                    const size_t frameCount = std::max<size_t>(1, app.getEngine().getFrameCount());
                                    for (size_t fi = 0; fi < frameCount; ++fi)
                                    {
                                        if (!app.getEngine().getFrameTrackStrokes(fi, tr.engineTrackId).empty())
                                        {
                                            hasContent = true;
                                            break;
                                        }
                                    }
                                }
                                if (!hasClip && !hasContent)
                                {
                                    reusableBaseFlowTrack = &tr;
                                    break;
                                }
                            }
                            if (reusableBaseFlowTrack)
                            {
                                flowTrackId = reusableBaseFlowTrack->id;
                                engFlowId = reusableBaseFlowTrack->engineTrackId;
                                flowName = reusableBaseFlowTrack->name.empty() ? std::string("Flow") : reusableBaseFlowTrack->name;
                            }
                            else
                            {
                                int flowCount = 0;
                                for (const auto& tr : ts2.tracks)
                                    if (tr.kind == strova::TrackKind::Flow)
                                        flowCount++;
                                flowName = (flowCount <= 0) ? std::string("Flow") : (std::string("Flow ") + std::to_string(flowCount));
                            }
                        }
                        size_t usableFrameCount = 0;
                        const bool builtFlowTrack = runTimelineMutation(app, [&]() -> bool
                            {
                                if (engFlowId == 0)
                                {
                                    engFlowId = app.getEngine().createTrack(DrawingEngine::TrackKind::Flow, flowName);
                                    if (engFlowId == 0)
                                        return false;
                                }

                                size_t need = frames.size();
                                if (need > app.getEngine().getFrameCount())
                                {
                                    while (app.getEngine().getFrameCount() < need)
                                    {
                                        const size_t beforeGrow = app.getEngine().getFrameCount();
                                        app.getEngine().addFrame();
                                        if (app.getEngine().getFrameCount() == beforeGrow)
                                            break;
                                    }
                                }

                                usableFrameCount = std::min(frames.size(), app.getEngine().getFrameCount());
                                if (usableFrameCount == 0)
                                    return false;

                                float th = app.getEngine().getBrushSize();
                                for (size_t fi = 0; fi < usableFrameCount; ++fi)
                                {
                                    Stroke s{};
                                    s.tool = ToolType::Brush;
                                    s.thickness = th;

                                    const auto& pts = frames[fi];
                                    if (!pts.empty()) s.color = pts.back().color;
                                    else              s.color = app.getEngine().getBrushColor();

                                    s.gradient = app.getEngine().getGradientConfig();
                                    if (s.gradient.enabled)
                                        s.color = app.getEngine().sampleGradient(0.5f);

                                    s.points.reserve(pts.size());
                                    for (const auto& p : pts)
                                    {
                                        StrokePoint sp{};
                                        sp.x = p.x;
                                        sp.y = p.y;
                                        sp.pressure = p.pressure;
                                        s.points.push_back(sp);
                                    }
                                    std::vector<Stroke> flowStrokes;
                                    flowStrokes.push_back(std::move(s));
                                    app.getEngine().setFrameTrackStrokes(fi, engFlowId, flowStrokes);
                                }
                                return engFlowId != 0 && usableFrameCount > 0;
                            });
                        if (!builtFlowTrack || engFlowId == 0 || usableFrameCount == 0)
                        {
                            committedStroke = commitStrokePreview(app, ts);
                            if (committedStroke)
                                app.markCurrentFrameEdited();
                            app.flowCapturer().clearResult();
                            return;
                        }
                        {
                            auto& tl = app.timeline;
                            auto& ts2 = tl.state();
                            std::string trackName = flowName;
                            if (flowTrackId == 0)
                            {
                                flowTrackId = tl.addTrack(strova::TrackKind::Flow, trackName.c_str());
                                if (flowTrackId == 0)
                                {
                                    app.getEngine().removeTrack(engFlowId);
                                    committedStroke = commitStrokePreview(app, ts);
                                    if (committedStroke)
                                        app.markCurrentFrameEdited();
                                    app.flowCapturer().clearResult();
                                    return;
                                }
                            }
                            if (auto* uiTr = tl.findTrack(flowTrackId))
                            {
                                uiTr->name = trackName;
                                uiTr->engineTrackId = (int)engFlowId;
                            }
                            tl.addClip(flowTrackId, 0, (int)usableFrameCount, trackName.c_str());
                            ts2.scrollY = 9999999;
                        }
                        clearStrokePreview();
                        app.dirtyAllThumbs();
                    }
                    else
                    {
                        committedStroke = commitStrokePreview(app, ts);
                        if (committedStroke)
                            app.markCurrentFrameEdited();
                    }
                }
                app.flowCapturer().clearResult();
                app.requestSaveProjectNow();
            }
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_MIDDLE && overCanvas)
    {
        app.captureEditorUi(EditorRuntimeState::InputOwner::CanvasPan, false);
        app.panningRef() = true;
        app.panStartXRef() = mx - (int)app.canvasPanXRef();
        app.panStartYRef() = my - (int)app.canvasPanYRef();
        return;
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_MIDDLE)
    {
        app.panningRef() = false;
        return;
    }
    if (app.panningRef() && e.type == SDL_MOUSEMOTION)
    {
        app.canvasPanXRef() = (float)(mx - app.panStartXRef());
        app.canvasPanYRef() = (float)(my - app.panStartYRef());
        return;
    }
    if (overCanvas && e.type == SDL_MOUSEWHEEL)
    {
        SDL_FPoint before = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
        float zoom = (e.wheel.y > 0) ? 1.1f : 0.9f;
        float newScale = fclamp(app.canvasScaleRef() * zoom, app.minCanvasScaleValue(), app.maxCanvasScaleValue());
        if (std::abs(newScale - app.canvasScaleRef()) < 0.00001f) return;
        app.canvasScaleRef() = newScale;
        SDL_FPoint after = screenToWorldPoint(mx, my, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
        app.canvasPanXRef() += (after.x - before.x) * app.canvasScaleRef();
        app.canvasPanYRef() += (after.y - before.y) * app.canvasScaleRef();
        g_canvasProxyBoostFrames = std::max(g_canvasProxyBoostFrames, 3);
    }

    if (e.type == SDL_KEYDOWN)
    {
        SDL_Keymod mod = SDL_GetModState();
        bool ctrl = (mod & KMOD_CTRL) != 0;
        bool shift = (mod & KMOD_SHIFT) != 0;
        bool alt = (mod & KMOD_ALT) != 0;
        auto selectTool = [&](ToolType t) { app.setToolCommand(t); if (t == ToolType::Ruler) app.setRulerVisibleCommand(true); };

        if ((e.key.keysym.sym == SDLK_F11) || (alt && e.key.keysym.sym == SDLK_RETURN))
        {
            Uint32 flags = SDL_GetWindowFlags(app.windowHandle());
            bool fs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            SDL_SetWindowFullscreen(app.windowHandle(), fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            return;
        }

        if (!app.editorToolsCanUseKeyboard())
            return;

        if (!app.isFlowSettingsOpen())
        {
            bool shift2 = (SDL_GetModState() & KMOD_SHIFT) != 0;
            switch (e.key.keysym.sym)
            {
            case SDLK_b: selectTool(ToolType::Brush); break;
            case SDLK_p: selectTool(shift2 ? ToolType::Pen : ToolType::Pencil); break;
            case SDLK_n: selectTool(ToolType::Pen); break;
            case SDLK_m: selectTool(ToolType::Marker); break;
            case SDLK_a: selectTool(ToolType::Airbrush); break;
            case SDLK_c: selectTool(ToolType::Calligraphy); break;
            case SDLK_e: selectTool(shift2 ? ToolType::SoftEraser : ToolType::Eraser); break;
            case SDLK_s: selectTool(ToolType::Smudge); break;
            case SDLK_u: selectTool(ToolType::Blur); break;
            case SDLK_g: selectTool(shift2 ? ToolType::Glow : ToolType::Fill); break;
            case SDLK_l: selectTool(ToolType::Line); break;
            case SDLK_r: selectTool(shift2 ? ToolType::Ruler : ToolType::Rect); break;
            case SDLK_o: selectTool(ToolType::Ellipse); break;
            case SDLK_v: selectTool(ToolType::Select); break;
            case SDLK_i: selectTool(ToolType::Eyedropper); break;
            case SDLK_BACKQUOTE:
                app.setRulerVisibleCommand(!app.getEditorUiState().rulerVisible);
                if (app.getEditorUiState().rulerVisible)
                {
                    app.setToolCommand(ToolType::Ruler);
                }
                break;
            case SDLK_q:
                if (app.flowCapturer().armed) app.flowCapturer().disarm();
                else app.flowCapturer().arm();
                break;
            case SDLK_F1: app.setOnionEnabledCommand(!app.isOnionSkinEnabled()); break;
            case SDLK_F2:
                app.setOnionTintCommand(!app.getEditorUiState().onionTint);
                break;
            case SDLK_F3:
                app.runtimeStateRef().diagnostics.overlayEnabled = !app.runtimeStateRef().diagnostics.overlayEnabled;
                break;
            default: break;
            }
        }

        if (ctrl && e.key.keysym.sym == SDLK_z)
        {
            if (shift) app.getEngine().redo();
            else       app.getEngine().undo();
            app.requestSaveProjectNow();
            return;
        }
        if (ctrl && e.key.keysym.sym == SDLK_y)
        {
            app.getEngine().redo();
            app.requestSaveProjectNow();
            return;
        }
        if (ctrl && e.key.keysym.sym == SDLK_c)
        {
            copySelectedLayersToClipboard(app);
            return;
        }
        if (ctrl && e.key.keysym.sym == SDLK_v)
        {
            pasteClipboardToSelectedLayers(app);
            return;
        }
        if (ctrl && e.key.keysym.sym == SDLK_d)
        {
            copySelectedLayersToNextFrame(app);
            return;
        }
        if (e.key.keysym.sym == SDLK_ESCAPE)
        {
            if (g_hasSelection)
            {
                g_hasSelection = false;
                g_selectedStrokeIndices.clear();
                g_draggingSelection = false;
                g_editorState.clearSelection();
                return;
            }
            app.requestSaveProjectNow();
            app.returnToLauncher();
            app.launcherUi().refreshProjects();
            SDL_SetWindowTitle(app.windowHandle(), "Strova");
            return;
        }
    }
}

void Editor::update(App& app, double dt)
{
    Uint64 inputStart = SDL_GetPerformanceCounter();
    std::vector<SDL_Event> events = app.takeQueuedEditorEvents();
    for (auto& event : events)
    {
        app.runtimeStateRef().input.processedEventCount++;
        handleEvent(app, event);
        if (!app.isEditorMode())
            break;
    }
    app.noteInputProcessingTime(perfMs(inputStart, SDL_GetPerformanceCounter()));

    if (!app.isEditorMode())
        return;

    Uint64 playbackStart = SDL_GetPerformanceCounter();
    app.tickPlayback(dt);
    app.notePlaybackProcessingTime(perfMs(playbackStart, SDL_GetPerformanceCounter()));

    Uint64 uiStart = SDL_GetPerformanceCounter();
    float target = app.rightPanelOpenRef() ? 1.0f : 0.0f;
    float a = (float)dt * 10.0f;
    if (a > 1.0f) a = 1.0f;
    app.rightPanelAnimRef() = app.rightPanelAnimRef() + (target - app.rightPanelAnimRef()) * a;
    if (std::fabs(app.rightPanelAnimRef() - target) < 0.001f) app.rightPanelAnimRef() = target;
    if (!app.rightPanelOpenRef() && app.rightPanelAnimRef() <= 0.01f)
    {
        g_rightPanelScroll = 0;
        g_rightPanelScrollTarget = 0.0f;
    }
    app.noteUiProcessingTime(perfMs(uiStart, SDL_GetPerformanceCounter()));
    app.syncRuntimeStateFromEditor();
}

void Editor::render(App& app, int w, int h)
{
    SDL_Renderer* r = app.getRenderer();
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    app.validateEditorState();
    app.refreshUILayout(w, h);

    SDL_Rect workspace = dockWorkspace(app, w, h);
    app.dockManager().update(workspace);
    app.dockManager().updateHoverCursor(mx, my);

    SDL_Rect toolsArea = app.dockManager().contentRect("Tools");
    g_toolGridExtraTop = 48;
    SDL_Rect brushArea = app.dockManager().contentRect("Brush");
    SDL_Rect previewArea = app.dockManager().contentRect("Preview");
    SDL_Rect framesArea = app.dockManager().contentRect("Frames");
    SDL_Rect layersArea = app.dockManager().contentRect("Layers");
    SDL_Rect timelineArea = app.dockManager().contentRect("Timeline");
    SDL_Rect colorArea = app.dockManager().contentRect("Color");

    SDL_FRect pageF = getPageRectF(app.getUILayout(), app.getProjectW(), app.getProjectH(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
    bool overPage = pointInRectF(mx, my, pageF);

    SDL_SetRenderDrawColor(r, COL_BG_MAIN.r, COL_BG_MAIN.g, COL_BG_MAIN.b, 255);
    SDL_RenderClear(r);

    fillRect(r, app.getUILayout().topBar, COL_BG_PANEL);
    drawDividerH(r, app.getUILayout().topBar.x, app.getUILayout().topBar.x + app.getUILayout().topBar.w, app.getUILayout().topBar.y + app.getUILayout().topBar.h - 1, COL_BORDER_SOFT);

    app.dockManager().drawBackgrounds(r, workspace);


    SDL_RenderSetClipRect(r, nullptr);
    fillRect(r, app.getUILayout().canvas, COL_CANVAS_AREA);
    drawSubtleGrid(r, app.getUILayout().canvas);
    drawRect(r, app.getUILayout().canvas, SDL_Color{ 160, 64, 64, 180 });

    SDL_RenderSetClipRect(r, &app.getUILayout().canvas);
    SDL_FRect page;
    page.x = (float)app.getUILayout().canvas.x + app.canvasPanXRef();
    page.y = (float)app.getUILayout().canvas.y + app.canvasPanYRef();
    page.w = (float)app.getProjectW() * app.canvasScaleRef();
    page.h = (float)app.getProjectH() * app.canvasScaleRef();

    SDL_SetRenderDrawColor(r, COL_PAGE.r, COL_PAGE.g, COL_PAGE.b, 255);
    SDL_RenderFillRectF(r, &page);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 50);

    checkSDLVersion();
    if (sdlSupportsFloatLines)
        SDL_RenderDrawRectF(r, &page);
    else
    {
        SDL_Rect pageI{ (int)page.x, (int)page.y, (int)page.w, (int)page.h };
        SDL_RenderDrawRect(r, &pageI);
    }

    auto drawStrokesGhost = [&](const std::vector<Stroke>& src, float mul, SDL_Color tint, float tintStrength)
        {
            for (Stroke s : src)
            {
                s.color.a = (Uint8)std::clamp((int)(s.color.a * mul), 0, 255);
                if (app.getEditorUiState().onionTint) applyTint(s, tint, tintStrength);
                app.brushRendererHandle()->drawStroke(s, app.canvasScaleRef(), app.canvasPanXRef(), app.canvasPanYRef(), app.getUILayout().canvas.x, app.getUILayout().canvas.y);
            }
        };

    const int activeLayerTrackId = activeLayerTree(app).primarySelectedTrackId() != 0 ? activeLayerTree(app).primarySelectedTrackId() : app.getEngine().getActiveTrack();

    if (app.isOnionSkinEnabled())
    {
        size_t cur = app.getEngine().getCurrentFrameIndex();
        size_t total = app.getEngine().getFrameCount();
        int steps = std::clamp(app.getEditorUiState().onionSteps, 1, 5);
        if (total > 1)
        {
            for (int i = 1; i <= steps; ++i)
            {
                float fall = 1.0f - (float)(i - 1) / (float)steps;
                float prevMul = app.onionPrevAlphaValue() * fall;
                float nextMul = app.onionNextAlphaValue() * fall;
                SDL_Color drawOnionTint{ 150, 112, 72, 255 };
                if ((int)cur - i >= 0)
                {
                    if (activeLayerTrackId != 0)
                        drawStrokesGhost(app.getEngine().getFrameTrackStrokes(cur - (size_t)i, activeLayerTrackId), prevMul, drawOnionTint, 0.28f);
                    else
                        drawStrokesGhost(app.getEngine().getFrameStrokes(cur - (size_t)i), prevMul, drawOnionTint, 0.28f);
                }
                if (cur + (size_t)i < total)
                {
                    if (activeLayerTrackId != 0)
                        drawStrokesGhost(app.getEngine().getFrameTrackStrokes(cur + (size_t)i, activeLayerTrackId), nextMul, drawOnionTint, 0.28f);
                    else
                        drawStrokesGhost(app.getEngine().getFrameStrokes(cur + (size_t)i), nextMul, drawOnionTint, 0.28f);
                }
            }
        }
    }

    const size_t curFrameIndex = app.getEngine().getCurrentFrameIndex();
    const int primaryNodeId = activeLayerTree(app).primarySelectedNodeId();
    std::vector<int> selectedOrOwnedTrackIds;
    for (const auto& n : activeLayerTree(app).getNodes())
    {
        if (n.isGroup || n.trackId == 0) continue;
        if (n.id == primaryNodeId || activeLayerTree(app).isDescendantOf(n.id, primaryNodeId))
            selectedOrOwnedTrackIds.push_back(n.trackId);
    }
    auto isSelectedFamilyTrack = [&](int trackId) -> bool
        {
            for (int id : selectedOrOwnedTrackIds) if (id == trackId) return true;
            return false;
        };

    std::vector<int> drawOrder = orderedDrawTrackIdsFromLayerTree(app);
    for (const auto& tr : app.getEngine().getTracks())
    {
        if (tr.kind != DrawingEngine::TrackKind::Flow) continue;
        bool alreadyPresent = false;
        for (int id : drawOrder)
        {
            if (id == tr.id)
            {
                alreadyPresent = true;
                break;
            }
        }
        if (!alreadyPresent)
            drawOrder.push_back(tr.id);
    }
    if (drawOrder.empty())
    {
        for (const auto& tr : app.getEngine().getTracks())
            if (tr.kind == DrawingEngine::TrackKind::Draw || tr.kind == DrawingEngine::TrackKind::Flow) drawOrder.push_back(tr.id);
    }

    const bool showLayerGhosts = (app.getEditorUiState().isolatedLayerTrackId == 0 && activeLayerTrackId != 0);
    auto& frameImageCache = g_persistentFrameImageCache;
    const std::uint64_t cacheFrameOrdinal = app.runtimeStateRef().diagnostics.frameOrdinal;
    std::vector<std::string> usedFrameImageKeys;
    usedFrameImageKeys.reserve(drawOrder.size());

    auto drawVisibleTrackLayers = [&](SDL_Renderer* target,
        float renderScale,
        float panX,
        float panY,
        int canvasX,
        int canvasY,
        const char* cacheScope)
        {
            for (int trackId : drawOrder)
            {
                const auto* tr = app.getEngine().findTrack(trackId);
                if (!tr) continue;
                if (tr->kind != DrawingEngine::TrackKind::Draw && tr->kind != DrawingEngine::TrackKind::Flow) continue;
                if (!tr->visible || tr->muted) continue;
                if (app.getEditorUiState().isolatedLayerTrackId != 0 && tr->id != app.getEditorUiState().isolatedLayerTrackId) continue;

                DrawingEngine::TrackLayer layer = app.getEngine().getEvaluatedFrameTrackLayerCopy(curFrameIndex, tr->id);
                if (layer.trackId == 0 || !layer.visible) continue;

                const bool dimForLayerGhost = (tr->kind == DrawingEngine::TrackKind::Draw) && showLayerGhosts && tr->id != activeLayerTrackId && !isSelectedFamilyTrack(tr->id);
                const float alphaMul = dimForLayerGhost ? 0.18f : 1.0f;
                if (g_transformPreviewActive && tr->id == g_transformDragTrackId)
                    layer.transform = g_transformPreviewTransform;

                const std::string imageCacheKey = std::to_string((int)curFrameIndex) + ":" + std::to_string(tr->id) + ":" + std::to_string(layer.celId) + ":" + std::to_string(layer.imageRevision) + ":" + std::to_string(layer.image.surfaceRevision());
                std::string strokeCacheKey = std::to_string((int)curFrameIndex) + ":" + std::string(cacheScope) + ":" + std::to_string(tr->id) + ":" + std::to_string(layer.contentRevision) + ":" + std::to_string(layer.transformRevision);
                if (g_transformPreviewActive && tr->id == g_transformDragTrackId)
                {
                    strokeCacheKey += ":preview:" + std::to_string((int)std::lround(layer.transform.posX * 100.0f));
                    strokeCacheKey += ":" + std::to_string((int)std::lround(layer.transform.posY * 100.0f));
                    strokeCacheKey += ":" + std::to_string((int)std::lround(layer.transform.rotation * 100.0f));
                }

                usedFrameImageKeys.push_back(imageCacheKey);
                strova::layer_render::drawTrackLayer(
                    target,
                    *app.brushRendererHandle(),
                    layer,
                    renderScale,
                    panX,
                    panY,
                    canvasX,
                    canvasY,
                    alphaMul,
                    &frameImageCache,
                    imageCacheKey,
                    strokeCacheKey,
                    cacheFrameOrdinal);
            }
        };

    bool useCanvasProxy = false;
    int proxyRebuilds = 0;
    int proxyScalePercent = 100;
    const bool proxyInteraction = app.panningRef() || g_transformPreviewActive || (g_canvasProxyBoostFrames > 0);
    if (proxyInteraction && app.getUILayout().canvas.w >= 320 && app.getUILayout().canvas.h >= 220)
    {
        const float proxyFactor = computeCanvasProxyFactor(app.getUILayout().canvas, app.canvasScaleRef());
        if (ensureCanvasProxyTexture(r, app.getUILayout().canvas, proxyFactor))
        {
            SDL_Texture* prevTarget = SDL_GetRenderTarget(r);
            SDL_BlendMode prevBlend = SDL_BLENDMODE_NONE;
            SDL_GetRenderDrawBlendMode(r, &prevBlend);

            SDL_SetRenderTarget(r, g_canvasProxy.texture);
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
            SDL_RenderClear(r);

            SDL_FRect proxyPage;
            proxyPage.x = app.canvasPanXRef() * proxyFactor;
            proxyPage.y = app.canvasPanYRef() * proxyFactor;
            proxyPage.w = (float)app.getProjectW() * app.canvasScaleRef() * proxyFactor;
            proxyPage.h = (float)app.getProjectH() * app.canvasScaleRef() * proxyFactor;

            SDL_SetRenderDrawColor(r, COL_PAGE.r, COL_PAGE.g, COL_PAGE.b, 255);
            SDL_RenderFillRectF(r, &proxyPage);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 50);
            if (sdlSupportsFloatLines)
                SDL_RenderDrawRectF(r, &proxyPage);
            else
            {
                SDL_Rect proxyPageI{ (int)proxyPage.x, (int)proxyPage.y, (int)proxyPage.w, (int)proxyPage.h };
                SDL_RenderDrawRect(r, &proxyPageI);
            }

            drawVisibleTrackLayers(
                r,
                app.canvasScaleRef() * proxyFactor,
                app.canvasPanXRef() * proxyFactor,
                app.canvasPanYRef() * proxyFactor,
                0,
                0,
                "proxy");

            SDL_SetRenderTarget(r, prevTarget);
            SDL_SetRenderDrawBlendMode(r, prevBlend);

            SDL_Rect proxyDst = app.getUILayout().canvas;
#if SDL_VERSION_ATLEAST(2,0,12)
            SDL_SetTextureScaleMode(g_canvasProxy.texture, SDL_ScaleModeLinear);
#endif
            SDL_RenderCopy(r, g_canvasProxy.texture, nullptr, &proxyDst);
            g_canvasProxy.lastUseFrame = cacheFrameOrdinal;
            useCanvasProxy = true;
            proxyRebuilds = 1;
            proxyScalePercent = (int)std::lround(proxyFactor * 100.0f);
        }
    }

    if (!useCanvasProxy)
    {
        drawVisibleTrackLayers(
            r,
            app.canvasScaleRef(),
            app.canvasPanXRef(),
            app.canvasPanYRef(),
            app.getUILayout().canvas.x,
            app.getUILayout().canvas.y,
            "main");
    }

    app.runtimeStateRef().render.proxyActive = useCanvasProxy;
    app.runtimeStateRef().render.proxyScalePercent = proxyScalePercent;
    app.runtimeStateRef().render.proxyRebuildsThisFrame = proxyRebuilds;
    app.runtimeStateRef().diagnostics.proxyActive = useCanvasProxy;
    app.runtimeStateRef().diagnostics.proxyScalePercent = proxyScalePercent;
    app.runtimeStateRef().diagnostics.proxyRebuildCount = proxyRebuilds;

    for (const auto& key : usedFrameImageKeys)
        g_persistentFrameImageCacheLastUse[key] = cacheFrameOrdinal;
    for (auto it = g_persistentFrameImageCache.begin(); it != g_persistentFrameImageCache.end(); )
    {
        const auto found = g_persistentFrameImageCacheLastUse.find(it->first);
        const std::uint64_t lastUse = (found != g_persistentFrameImageCacheLastUse.end()) ? found->second : 0;
        if (cacheFrameOrdinal > lastUse + 180)
        {
            if (it->second) SDL_DestroyTexture(it->second);
            strova::layer_render::eraseImageTextureMeta(it->first);
            g_persistentFrameImageCacheLastUse.erase(it->first);
            it = g_persistentFrameImageCache.erase(it);
        }
        else ++it;
    }

    if (!proxyInteraction && g_canvasProxy.texture && cacheFrameOrdinal > g_canvasProxy.lastUseFrame + 180)
        destroyCanvasProxy();
    if (g_canvasProxyBoostFrames > 0)
        --g_canvasProxyBoostFrames;


    const auto& strokes = (activeLayerTrackId != 0)
        ? app.getEngine().getFrameTrackStrokes(curFrameIndex, activeLayerTrackId)
        : app.getEngine().getCurrentStrokes();

    for (size_t idx : g_selectedStrokeIndices)
    {
        if (idx >= strokes.size()) continue;
        Stroke hi = strokes[idx];
        if (hi.tool == ToolType::Glow)
        {
            continue;
        }
        else
        {
            hi.color = SDL_Color{ 80, 160, 255, 210 };
            hi.thickness += 2.0f;
        }
        app.brushRendererHandle()->drawStroke(hi, app.canvasScaleRef(), app.canvasPanXRef(), app.canvasPanYRef(), app.getUILayout().canvas.x, app.getUILayout().canvas.y);
    }

    if (app.getEditorUiState().rulerVisible)
    {
        const float ang = g_rulerAngleDeg * 0.01745329252f;
        SDL_FPoint dir{ std::cos(ang), std::sin(ang) };
        SDL_FPoint nrm{ -dir.y, dir.x };
        SDL_FPoint aW{ g_rulerCenter.x - dir.x * (g_rulerLength * 0.5f), g_rulerCenter.y - dir.y * (g_rulerLength * 0.5f) };
        SDL_FPoint bW{ g_rulerCenter.x + dir.x * (g_rulerLength * 0.5f), g_rulerCenter.y + dir.y * (g_rulerLength * 0.5f) };
        auto toScreen = [&](SDL_FPoint p) { return worldToScreenPoint(p.x, p.y, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef()); };
        SDL_FPoint a = toScreen(aW);
        SDL_FPoint b = toScreen(bW);
        const float screenLen = std::max(1.0f, SDL_sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)));
        const float halfW = fclamp(10.0f + app.canvasScaleRef() * 8.0f, 12.0f, 34.0f);
        const float bodyExtend = halfW * 0.55f;
        SDL_FPoint aExt{ a.x - dir.x * bodyExtend, a.y - dir.y * bodyExtend };
        SDL_FPoint bExt{ b.x + dir.x * bodyExtend, b.y + dir.y * bodyExtend };
        SDL_FPoint p1{ aExt.x + nrm.x * halfW, aExt.y + nrm.y * halfW };
        SDL_FPoint p2{ bExt.x + nrm.x * halfW, bExt.y + nrm.y * halfW };
        SDL_FPoint p3{ bExt.x - nrm.x * halfW, bExt.y - nrm.y * halfW };
        SDL_FPoint p4{ aExt.x - nrm.x * halfW, aExt.y - nrm.y * halfW };

        const int fillSteps = std::max(10, (int)std::lround(halfW * 2.0f));
        for (int s = -fillSteps; s <= fillSteps; ++s)
        {
            const float off = ((float)s / (float)fillSteps) * halfW;
            const float edgeT = 1.0f - std::fabs(off) / std::max(1.0f, halfW);
            const Uint8 alpha = (Uint8)std::clamp((int)(90.0f + edgeT * 54.0f), 0, 255);
            SDL_SetRenderDrawColor(r, 162, 167, 174, alpha);
            drawLineFCompat(r, aExt.x + nrm.x * off, aExt.y + nrm.y * off, bExt.x + nrm.x * off, bExt.y + nrm.y * off);
        }
        fillCircle(r, (int)std::lround(a.x), (int)std::lround(a.y), std::max(6, (int)std::lround(halfW)), SDL_Color{ 166, 170, 178, 124 });
        fillCircle(r, (int)std::lround(b.x), (int)std::lround(b.y), std::max(6, (int)std::lround(halfW)), SDL_Color{ 166, 170, 178, 124 });

        SDL_SetRenderDrawColor(r, 112, 118, 128, 220);
        drawLineFCompat(r, p1.x, p1.y, p2.x, p2.y);
        drawLineFCompat(r, p2.x, p2.y, p3.x, p3.y);
        drawLineFCompat(r, p3.x, p3.y, p4.x, p4.y);
        drawLineFCompat(r, p4.x, p4.y, p1.x, p1.y);

        SDL_SetRenderDrawColor(r, 208, 214, 222, 220);
        drawLineFCompat(r, aExt.x, aExt.y, bExt.x, bExt.y);

        const int majorCount = std::max(4, std::min(60, (int)std::lround(screenLen / 40.0f)));
        const int minorPerMajor = 5;
        const int minorCount = majorCount * minorPerMajor;
        for (int i = 0; i <= minorCount; ++i)
        {
            const float t = (minorCount > 0) ? ((float)i / (float)minorCount) : 0.0f;
            SDL_FPoint m{ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
            const bool major = (i % minorPerMajor) == 0;
            const float tickOut = major ? (halfW * 0.95f) : (halfW * 0.58f);
            const float tickIn = halfW * 0.10f;
            SDL_SetRenderDrawColor(r, 86, 92, 102, major ? 220 : 180);
            drawLineFCompat(r, m.x - nrm.x * tickOut, m.y - nrm.y * tickOut, m.x + nrm.x * tickIn, m.y + nrm.y * tickIn);

            if (major && app.getUiFont())
            {
                const int labelValue = (int)std::lround((g_rulerLength * t) / 10.0f);
                std::string label = std::to_string(labelValue);
                drawText(r, app.getUiFont(), label, (int)std::lround(m.x + nrm.x * (halfW * 0.30f)), (int)std::lround(m.y + nrm.y * (halfW * 0.30f)), SDL_Color{ 62, 68, 78, 230 });
            }
        }

        fillCircle(r, (int)std::lround(b.x), (int)std::lround(b.y), std::max(6, (int)std::lround(halfW * 0.38f)), SDL_Color{ 88, 145, 255, 220 });
        fillCircle(r, (int)std::lround((a.x + b.x) * 0.5f), (int)std::lround((a.y + b.y) * 0.5f), std::max(5, (int)std::lround(halfW * 0.30f)), SDL_Color{ 248,248,248,215 });
    }

    if (g_actionActive && (g_actionTool == ToolType::Line || g_actionTool == ToolType::Rect || g_actionTool == ToolType::Ellipse))
    {
        Stroke preview{};
        preview.tool = g_actionTool;
        preview.color = app.getEngine().getBrushColor();
        preview.thickness = app.getEngine().getBrushSize();
        preview.color.a = 140;
        StrokePoint a{}; a.x = g_actionA.x; a.y = g_actionA.y; a.pressure = 1.0f;
        StrokePoint b{}; b.x = g_actionB.x; b.y = g_actionB.y; b.pressure = 1.0f;
        preview.points.push_back(a);
        preview.points.push_back(b);
        app.brushRendererHandle()->drawStroke(preview, app.canvasScaleRef(), app.canvasPanXRef(), app.canvasPanYRef(), app.getUILayout().canvas.x, app.getUILayout().canvas.y);
    }

    if (g_strokePreviewActive && !g_strokePreviewPoints.empty())
    {
        Stroke preview{};
        preview.tool = g_strokePreviewTool;
        preview.color = g_strokePreviewColor;
        preview.gradient = g_strokePreviewGradient;
        preview.thickness = g_strokePreviewThickness;
        preview.color.a = (Uint8)std::clamp((int)preview.color.a, 64, 220);
        preview.points = g_strokePreviewPoints;
        if (preview.points.size() == 1)
            preview.points.push_back(preview.points.front());
        app.brushRendererHandle()->drawStroke(preview, app.canvasScaleRef(), app.canvasPanXRef(), app.canvasPanYRef(), app.getUILayout().canvas.x, app.getUILayout().canvas.y);
    }

    {
        std::vector<std::string> pluginOverlayCommands;
        std::string pluginOverlayErr;
        if (app.pluginManager().renderCanvasOverlays(app.getUILayout().canvas, mx, my, pluginOverlayCommands, pluginOverlayErr))
            renderPluginCanvasOverlayCommands(r, app.getUiFont(), pluginOverlayCommands);
        else if (!pluginOverlayErr.empty())
            SDL_Log("Plugin canvas overlay failed: %s", pluginOverlayErr.c_str());
    }

    if (keyframeTransformToolsVisible(app))
    {
        const int transformTrackIdRender = activeLayerTree(app).primarySelectedTrackId();
        strova::layer_render::Bounds bounds{};
        if (transformTrackIdRender != 0 && currentLayerWorldBounds(app, transformTrackIdRender, bounds))
        {
            SDL_FPoint aS = worldToScreenPoint(bounds.minX, bounds.minY, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            SDL_FPoint bS = worldToScreenPoint(bounds.maxX, bounds.maxY, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            SDL_Rect sel{ (int)std::floor(std::min(aS.x, bS.x)), (int)std::floor(std::min(aS.y, bS.y)),
                (int)std::ceil(std::fabs(bS.x - aS.x)), (int)std::ceil(std::fabs(bS.y - aS.y)) };
            SDL_SetRenderDrawColor(r, 120, 190, 255, 32);
            SDL_RenderFillRect(r, &sel);
            SDL_SetRenderDrawColor(r, 120, 190, 255, 220);
            SDL_RenderDrawRect(r, &sel);
            DrawingEngine::TrackLayer transformLayer = app.getEngine().getEvaluatedFrameTrackLayerCopy(curFrameIndex, transformTrackIdRender);
            if (g_transformPreviewActive && transformTrackIdRender == g_transformDragTrackId)
                transformLayer.transform = g_transformPreviewTransform;
            SDL_FPoint cW = (transformLayer.trackId != 0) ? layerTransformPivotWorld(transformLayer) : layerBoundsCenter(bounds);
            SDL_FPoint cS = worldToScreenPoint(cW.x, cW.y, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
            fillCircle(r, (int)std::lround(cS.x), (int)std::lround(cS.y), 5, SDL_Color{ 255,255,255,220 });
        }
    }

    if (g_hasSelection)
    {
        SDL_FPoint aS = worldToScreenPoint(g_selA.x, g_selA.y, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
        SDL_FPoint bS = worldToScreenPoint(g_selB.x, g_selB.y, app.getUILayout(), app.canvasPanXRef(), app.canvasPanYRef(), app.canvasScaleRef());
        float x1 = std::min(aS.x, bS.x);
        float y1 = std::min(aS.y, bS.y);
        float x2 = std::max(aS.x, bS.x);
        float y2 = std::max(aS.y, bS.y);
        SDL_Rect sel{ (int)x1, (int)y1, (int)(x2 - x1), (int)(y2 - y1) };
        SDL_SetRenderDrawColor(r, COL_ACCENT.r, COL_ACCENT.g, COL_ACCENT.b, 32);
        SDL_RenderFillRect(r, &sel);
        SDL_SetRenderDrawColor(r, COL_ACCENT.r, COL_ACCENT.g, COL_ACCENT.b, 220);
        SDL_RenderDrawRect(r, &sel);
        fillCircle(r, sel.x + sel.w / 2, sel.y + sel.h / 2, 5, SDL_Color{ 255,255,255,210 });
    }

    SDL_RenderSetClipRect(r, nullptr);

    if (overPage)
    {
        ToolType t = app.getEditorUiState().activeTool;
        ToolSettings& ts = app.toolBank.get(t);
        float radiusPx = (ts.size * 0.5f) * app.canvasScaleRef();
        radiusPx = std::max(2.0f, radiusPx);
        float cx = (float)mx;
        float cy = (float)my;
        if (t == ToolType::Glow)
        {

            fillCircle(r, (int)std::lround(cx), (int)std::lround(cy), std::max(2, (int)std::lround(radiusPx * 0.55f)), SDL_Color{ 255,255,255,12 });
        }
        else
        {
            SDL_SetRenderDrawColor(r, COL_TEXT_MAIN.r, COL_TEXT_MAIN.g, COL_TEXT_MAIN.b, 120);
            const int steps = 42;
            for (int i = 0; i < steps; ++i)
            {
                float a0 = (float)i / (float)steps * 6.2831853f;
                float a1 = (float)(i + 1) / (float)steps * 6.2831853f;
                float x0 = cx + SDL_cosf(a0) * radiusPx;
                float y0 = cy + SDL_sinf(a0) * radiusPx;
                float x1 = cx + SDL_cosf(a1) * radiusPx;
                float y1 = cy + SDL_sinf(a1) * radiusPx;
                drawLineFCompat(r, x0, y0, x1, y1);
            }
            if (t == ToolType::Calligraphy)
            {
                float ang = ts.angleDeg * 0.0174532925f;
                float lx = SDL_cosf(ang) * radiusPx;
                float ly = SDL_sinf(ang) * radiusPx;
                drawLineFCompat(r, cx - lx, cy - ly, cx + lx, cy + ly);
            }
        }
    }

    SDL_Rect undoR{}, redoR{}, onionR{}, colorR{};
    buildTopBarButtonRects(app.getUILayout(), undoR, redoR, onionR, colorR);

    app.undoButton().setRect(undoR);
    app.redoButton().setRect(redoR);
    app.undoButton().setTexture(app.undoTextureHandle());
    app.redoButton().setTexture(app.redoTextureHandle());
    bool canU = app.getEngine().canUndo();
    bool canR2 = app.getEngine().canRedo();
    app.undoButton().setEnabled(canU);
    app.redoButton().setEnabled(canR2);
    bool hoverUndo = canU && app.undoButton().hitTest(mx, my);
    bool hoverRedo = canR2 && app.redoButton().hitTest(mx, my);
    app.undoButton().draw(r, hoverUndo);
    app.redoButton().draw(r, hoverRedo);

    app.colorButton().setRect(colorR);
    app.colorButton().setTexture(app.colorTextureHandle());
    app.colorButton().setEnabled(true);
    bool hoverColor = app.colorButton().hitTest(mx, my);
    app.colorButton().draw(r, hoverColor);

    g_windowBtnR = SDL_Rect{ colorR.x + colorR.w + 10, colorR.y, 96, colorR.h };
    g_importBtnR = SDL_Rect{ g_windowBtnR.x + g_windowBtnR.w + 10, colorR.y, 104, colorR.h };
    g_pluginBtnR = SDL_Rect{ g_importBtnR.x + g_importBtnR.w + 10, colorR.y, 104, colorR.h };
    g_windowMenuR = SDL_Rect{ g_windowBtnR.x, g_windowBtnR.y + g_windowBtnR.h + 4, 160, 11 * 32 };
    g_pluginMenuR = SDL_Rect{ g_pluginBtnR.x, g_pluginBtnR.y + g_pluginBtnR.h + 4, 190, 4 * 32 };
    drawModernButton(r, g_windowBtnR, pointInRect(mx, my, g_windowBtnR), g_windowMenuOpen);
    drawText(r, app.getUiFont(), "Window", g_windowBtnR.x + 24, g_windowBtnR.y + 8, COL_TEXT_MAIN);
    drawModernButton(r, g_importBtnR, pointInRect(mx, my, g_importBtnR), false);
    drawText(r, app.getUiFont(), "Import", g_importBtnR.x + 18, g_importBtnR.y + 8, COL_TEXT_MAIN);
    drawModernButton(r, g_pluginBtnR, pointInRect(mx, my, g_pluginBtnR), g_pluginMenuOpen);
    drawText(r, app.getUiFont(), "Plugins", g_pluginBtnR.x + 14, g_pluginBtnR.y + 8, COL_TEXT_MAIN);

    g_exportBtnR = SDL_Rect{
        app.getUILayout().topBar.x + app.getUILayout().topBar.w - 140,
        app.getUILayout().topBar.y + 6,
        120,
        app.getUILayout().topBar.h - 12
    };
    g_exportMenuR = SDL_Rect{ g_exportBtnR.x, g_exportBtnR.y + g_exportBtnR.h + 4, g_exportBtnR.w, 3 * 36 };

    bool hoverExport = pointInRect(mx, my, g_exportBtnR);
    drawModernButton(r, g_exportBtnR, hoverExport, false);
    {
        std::string txt = "EXPORT";
        int tw = measureTextW(app.getUiFont(), txt);
        drawText(r, app.getUiFont(), txt, g_exportBtnR.x + (g_exportBtnR.w - tw) / 2,
            g_exportBtnR.y + (g_exportBtnR.h - 18) / 2, COL_TEXT_MAIN);
    }

    const int titleLeft = g_pluginBtnR.x + g_pluginBtnR.w + 18;
    const int titleRight = g_exportBtnR.x - 18;
    const int titleMaxW = std::max(0, titleRight - titleLeft);

    {
        std::string title = "Strova - " + app.getProjectNameStr();
        std::string drawTitle = title;
        while (!drawTitle.empty() && measureTextW(app.getUiFont(), drawTitle) > titleMaxW)
            drawTitle.pop_back();

        if (drawTitle != title && titleMaxW > measureTextW(app.getUiFont(), "..."))
            drawTitle += "...";

        if (!drawTitle.empty() && titleMaxW > 24)
            drawText(r, app.getUiFont(), drawTitle, titleLeft, app.getUILayout().topBar.y + 11, COL_TEXT_MAIN);
    }

    {
        std::string qa = app.flowCapturer().capturing ? "Flow: RECORDING" : (app.flowCapturer().armed ? "Flow: ARMED" : "Flow: OFF");
        qa += app.flowLinkEnabledValue() ? " | FlowLink: ON" : " | FlowLink: OFF";
        if (app.flowCapturer().lastStats.valid)
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "  |  %d frames | %.2fs | %d fps | %.0fpx",
                app.flowCapturer().lastStats.frames,
                app.flowCapturer().lastStats.duration,
                app.flowCapturer().lastStats.fpsUsed,
                app.flowCapturer().lastStats.totalDist);
            qa += buf;
        }

        const int qaW = measureTextW(app.getUiFont(), qa);
        const int qaX = g_exportBtnR.x - qaW - 18;
        const int minQaX = titleLeft + std::min(titleMaxW, 320);
        if (qaX > minQaX)
        {
            drawText(r, app.getUiFont(), qa, qaX, app.getUILayout().topBar.y + 11,
                app.flowCapturer().armed ? COL_TEXT_MAIN : COL_TEXT_DIM);
        }
    }


    if (toolsArea.w > 0 && toolsArea.h > 0)
    {
        SDL_RenderSetClipRect(r, &toolsArea);
        drawTransformToolStrip(app, r, app.getUiFont(), toolsArea);
        drawToolGrid(app, r, app.getUiFont(), toolsArea);
        SDL_RenderSetClipRect(r, nullptr);
    }

    if (!app.colorPickerWidget().isEditingText())
    {
        SDL_Color engCol = app.getEngine().getBrushColor();
        SDL_Color uiCol = app.colorPickerWidget().getColorRGBA();
        if (engCol.r != uiCol.r || engCol.g != uiCol.g || engCol.b != uiCol.b || engCol.a != uiCol.a)
            app.colorPickerWidget().setColor(engCol);
    }

    if (colorArea.w > 0 && colorArea.h > 0)
    {
        SDL_RenderSetClipRect(r, &colorArea);
        drawColorSummaryPanel(app, r, app.getUiFont(), colorArea, mx, my);
        SDL_RenderSetClipRect(r, nullptr);
    }

    ToolType activeToolPanel = app.getEditorUiState().activeTool;
    if (brushArea.w > 0 && brushArea.h > 0)
    {
        SDL_Rect brushClip = brushArea;
        SDL_Rect oldBrushClip{};
        const SDL_bool hadBrushClip = SDL_RenderIsClipEnabled(r);
        SDL_RenderGetClipRect(r, &oldBrushClip);
        SDL_RenderSetClipRect(r, &brushClip);
        auto& optionsPanel = sharedToolOptionsPanel();
        optionsPanel.layout(brushArea);
        optionsPanel.draw(r, app.getUiFont(), activeToolPanel, app.toolBank.get(activeToolPanel));
        SDL_RenderSetClipRect(r, &brushClip);
        drawRect(r, brushArea, COL_BORDER_SOFT);
        if (hadBrushClip) SDL_RenderSetClipRect(r, &oldBrushClip); else SDL_RenderSetClipRect(r, nullptr);
    }

    if (previewArea.w > 0 && previewArea.h > 0)
    {
        drawRect(r, previewArea, SDL_Color{ 80, 128, 220, 180 });
        SDL_RenderSetClipRect(r, &previewArea);
        drawPreviewPanel(app, r, app.getUiFont(), previewArea);
        SDL_RenderSetClipRect(r, nullptr);
    }

    if (g_brushResourceManager.isOpen())
        g_brushResourceManager.render(app, r, app.getUiFont(), w, h);
    if (framesArea.w > 0 && framesArea.h > 0)
    {
        SDL_RenderSetClipRect(r, &framesArea);
        drawFramesPanel(app, r, app.getUiFont(), framesArea, mx, my);
        SDL_RenderSetClipRect(r, nullptr);
    }

    syncEditorModeFlags(app);
    TimelinePanelRects timelineRects2 = buildTimelinePanelRects(app, timelineArea);
    app.transportBarRect() = timelineRects2.transport;
    app.timelineStripRect() = timelineRects2.strip;

    drawQuickAnimPanel(app, r, app.getUiFont());

    if (g_flowLinkConflictModalOpen)
    {
        SDL_Rect modal{ app.getUILayout().canvas.x + std::max(40, app.getUILayout().canvas.w / 2 - 140), app.getUILayout().canvas.y + 50, 280, 96 };
        fillRoundRect(r, modal, 10, SDL_Color{ 12, 16, 22, 240 });
        strokeRoundRect(r, modal, 10, SDL_Color{ 110, 120, 148, 220 });
        drawText(r, app.getUiFont(), "Frames already exist", modal.x + 12, modal.y + 12, COL_TEXT_MAIN);
        drawText(r, app.getUiFont(), "Overlay active. Click Stitch in Quick Anim to insert new frames.", modal.x + 12, modal.y + 42, COL_TEXT_DIM);
    }

    if (timelineArea.w > 0 && timelineArea.h > 0)
    {
        SDL_RenderSetClipRect(r, &timelineArea);

        fillRect(r, app.transportBarRect(), COL_BG_PANEL2);
        drawRect(r, app.transportBarRect(), COL_BORDER_SOFT);

        SDL_Rect playR2 = timelineRects2.play;
        SDL_Rect stopR2 = timelineRects2.stop;
        SDL_Rect addR2 = timelineRects2.add;

        auto drawBtn2 = [&](const SDL_Rect& rr, const char* label, bool active = false)
            {
                int lmx = 0, lmy = 0;
                SDL_GetMouseState(&lmx, &lmy);
                bool hover = pointInRect(lmx, lmy, rr);
                drawModernButton(r, rr, hover, active);
                SDL_Color c = hover ? COL_TEXT_MAIN : COL_TEXT_DIM;
                std::string shown = label ? std::string(label) : std::string();
                const int maxTextW = std::max(12, rr.w - 12);
                while (!shown.empty() && measureTextW(app.getUiFont(), shown) > maxTextW)
                    shown.pop_back();
                if (shown.empty()) shown = label ? std::string(label, label + std::min(2, (int)std::strlen(label))) : std::string();
                int tw = measureTextW(app.getUiFont(), shown);
                const int th = std::max(1, TTF_FontHeight(app.getUiFont()));
                drawText(r, app.getUiFont(), shown, rr.x + (rr.w - tw) / 2, rr.y + std::max(0, (rr.h - th) / 2), c);
            };

        drawBtn2(playR2, app.playingRef() ? "Pause" : "Play", app.playingRef());
        drawBtn2(stopR2, "Stop");
        drawBtn2(addR2, "Add");

        {
            SDL_Rect recordFlowR2 = timelineQuickButtonRect(timelineRects2, 0);
            SDL_Rect stopRecordFlowR2 = timelineQuickButtonRect(timelineRects2, 1);
            SDL_Rect flowLinkToggleR2 = timelineQuickButtonRect(timelineRects2, 2);
            drawText(r, app.getUiFont(), "Flow / FlowLink", recordFlowR2.x, recordFlowR2.y - 18, COL_TEXT_DIM);
            drawBtn2(recordFlowR2, app.flowCapturer().armed ? "Arm On" : "Arm Capture", app.flowCapturer().armed);
            drawBtn2(stopRecordFlowR2, "Disarm", false);
            drawBtn2(flowLinkToggleR2, "FlowLink", app.flowLinkEnabledValue());
            const int selectedTrackId = activeLayerTree(app).primarySelectedTrackId();
            std::string captureInfo = selectedTrackId != 0 ? (std::string("Selected Layer ") + std::to_string(selectedTrackId)) : std::string("Select a layer to capture transforms");
            drawText(r, app.getUiFont(), captureInfo, recordFlowR2.x, recordFlowR2.y + recordFlowR2.h + 6, selectedTrackId != 0 ? COL_TEXT_MAIN : COL_TEXT_DIM);
            drawText(r, app.getUiFont(), app.flowCapturer().armed ? "Move or rotate on canvas to record FlowLink" : "Arm capture to enable move / rotate recording", recordFlowR2.x, recordFlowR2.y + recordFlowR2.h + 26, app.flowCapturer().armed ? SDL_Color{ 140,205,160,255 } : COL_TEXT_DIM);
        }

        {
            SDL_Color c = COL_TEXT_DIM;
            size_t idx = app.getEngine().getCurrentFrameIndex();
            const int totalFrames = std::max(1, (int)app.getEngine().getFrameCount());
            std::string info = std::to_string(app.getProjectFPS()) + " FPS   " +
                "Frame " + std::to_string((int)idx + 1) + "/" + std::to_string((int)totalFrames);
            if (measureTextW(app.getUiFont(), info) > timelineRects2.status.w)
                info = std::to_string((int)idx + 1) + "/" + std::to_string((int)totalFrames) + " @ " + std::to_string(app.getProjectFPS()) + "fps";
            drawText(r, app.getUiFont(), info, timelineRects2.status.x, timelineRects2.status.y + 2, c);
        }

        if (timelineRects2.keyframes.w > 0 && timelineRects2.keyframes.h > 0)
        {
            drawTransformKeyframePanel(app, r, app.getUiFont(), timelineRects2.keyframes, mx, my);
            if (g_keyframeActionModal.visible)
            {
                SDL_Rect card = keyframeModalRect(g_keyframeActionModal);
                const SDL_Rect bounds = timelineRects2.keyframes;
                if (card.x + card.w > bounds.x + bounds.w) card.x = std::max(bounds.x + 8, bounds.x + bounds.w - card.w - 8);
                if (card.y + card.h > bounds.y + bounds.h) card.y = std::max(bounds.y + 8, bounds.y + bounds.h - card.h - 8);
                if (card.x < bounds.x + 8) card.x = bounds.x + 8;
                if (card.y < bounds.y + 8) card.y = bounds.y + 8;
                fillRect(r, card, SDL_Color{ 18, 20, 28, 248 });
                drawRect(r, card, SDL_Color{ 120, 132, 164, 220 });
                if (g_keyframeActionModal.confirmDuplicate)
                {
                    drawText(r, app.getUiFont(), "Capture sample already exists at this frame.", card.x + 12, card.y + 12, COL_TEXT_MAIN);
                    drawText(r, app.getUiFont(), "Capture another sample here?", card.x + 12, card.y + 34, COL_TEXT_DIM);
                    SDL_Rect cancelR = keyframeModalButtonRect(card, 0, true);
                    SDL_Rect confirmR = keyframeModalButtonRect(card, 1, true);
                    drawBtn2(cancelR, "Cancel", false);
                    drawBtn2(confirmR, "Confirm", false);
                }
                else
                {
                    drawText(r, app.getUiFont(), std::string("Frame ") + std::to_string(g_keyframeActionModal.targetFrame), card.x + 8, card.y - 18, COL_TEXT_DIM);
                    drawBtn2(keyframeModalButtonRect(card, 0, false), "New Sample", false);
                    drawBtn2(keyframeModalButtonRect(card, 1, false), "Show From Here", false);
                    drawBtn2(keyframeModalButtonRect(card, 2, false), "Hide After Here", false);
                }
            }
            if (timelineRects2.splitter.w > 0 && timelineRects2.splitter.h > 0)
            {
                fillRect(r, timelineRects2.splitter, SDL_Color{ 54, 60, 74, 255 });
                drawRect(r, timelineRects2.splitter, SDL_Color{ 92, 100, 122, 220 });
            }
        }

        app.timeline.setRect(app.timelineStripRect());
        app.timeline.setFont(app.getUiFont());
        app.timeline.setTotalFrames(std::max(1, (int)app.getEngine().getFrameCount()));
        app.timeline.setFps(app.getProjectFPS());
        app.timeline.setPlayheadFrame((int)app.getEngine().getCurrentFrameIndex());
        app.timeline.draw(r);

        SDL_RenderSetClipRect(r, nullptr);
    }

    drawPluginPanels(app, r, app.getUiFont());
    drawPluginWarningsOverlay(app, r, app.getUiFont(), workspace);
    app.dockManager().drawHeaders(r, app.getUiFont(), mx, my, workspace);

    std::string tooltipText;
    if (layersArea.w > 0 && layersArea.h > 0)
        drawLayerPanel(app, r, mx, my, layersArea, tooltipText);
    ToolType hoveredTool{};
    if (toolsArea.w > 0 && toolsArea.h > 0 && pointInRect(mx, my, toolsColorPickerButtonRect(toolsArea)))
        tooltipText = app.colorPickerWindowState().visible ? "Hide the floating color picker" : "Show the floating color picker";
    else if (toolsArea.w > 0 && toolsArea.h > 0 && pointInRect(mx, my, g_transformMoveBtnR))
        tooltipText = "Move selected layer";
    else if (toolsArea.w > 0 && toolsArea.h > 0 && pointInRect(mx, my, g_transformRotateBtnR))
        tooltipText = "Rotate selected layer";
    else if (timelineArea.w > 0 && pointInRect(mx, my, timelineRects2.keyframes))
        tooltipText = "Transform keyframes for the selected layer";
    else if (timelineArea.w > 0 && pointInRect(mx, my, timelineRects2.splitter))
        tooltipText = "Drag to resize keyframe and draw timelines";
    else if (toolsArea.w > 0 && toolsArea.h > 0 && hoveredToolInGrid(toolsArea, mx, my, hoveredTool))
        tooltipText = toolTooltipText(hoveredTool);
    else if (app.undoButton().hitTest(mx, my))
        tooltipText = "Undo (Ctrl+Z)";
    else if (app.redoButton().hitTest(mx, my))
        tooltipText = "Redo (Ctrl+Y)";
    else if (pointInRect(mx, my, colorR))
        tooltipText = "Settings / quick actions";
    else if (colorArea.w > 0 && pointInRect(mx, my, colorPanelOpenButtonRect(colorArea)))
        tooltipText = "Open the floating color selector";
    else if (pointInRect(mx, my, g_importBtnR))
        tooltipText = "Import image as a new layer";
    else if (pointInRect(mx, my, g_pluginBtnR))
        tooltipText = "Open plugin tools";
    else if (pointInRect(mx, my, g_exportBtnR))
        tooltipText = "Export project";
    else if (timelineArea.w > 0 && pointInRect(mx, my, timelineRects2.play))
        tooltipText = app.playingRef() ? "Pause playback" : "Play timeline";
    else if (timelineArea.w > 0 && pointInRect(mx, my, timelineRects2.stop))
        tooltipText = "Stop playback";
    else if (timelineArea.w > 0 && pointInRect(mx, my, timelineRects2.add))
        tooltipText = "Add a new frame";

    SDL_Rect rightBarBase = app.rightBarRectRef();
    int newH = app.getUILayout().bottomBar.y - rightBarBase.y;
    rightBarBase.h = newH > 0 ? newH : 0;

    drawFloatingColorPickerWindow(app, r, app.getUiFont(), mx, my);

    drawRightBarImpl(app, r, app.getUiFont(), rightBarBase,
        app.rightPanelOpenRef(), app.rightPanelAnimRef(),
        app.getEditorUiState().activeTool, app.toolBank);

    drawDiagnosticsOverlay(app, r, app.getUiFont());
    drawPluginManagerPanel(app, r, app.getUiFont(), mx, my);

    if (g_exportMenuOpen)
    {
        fillRect(r, g_exportMenuR, COL_BG_PANEL2);
        drawRect(r, g_exportMenuR, COL_BORDER_SOFT);
        const char* opts[3] = { "MP4", "PNG SEQ", "GIF" };
        for (int i = 0; i < 3; i++)
        {
            SDL_Rect item{ g_exportMenuR.x, g_exportMenuR.y + i * 36, g_exportMenuR.w, 36 };
            bool hover = pointInRect(mx, my, item);
            if (hover) fillRect(r, item, COL_BTN_HOVER);
            int tw = measureTextW(app.getUiFont(), opts[i]);
            drawText(r, app.getUiFont(), opts[i], item.x + (item.w - tw) / 2, item.y + (item.h - 18) / 2, COL_TEXT_MAIN);
        }
    }

    if (g_windowMenuOpen)
    {
        const char* items[] = { "Canvas", "Timeline", "Layers", "Tools", "Color", "Frames", "Brush", "Preview", "Flow Settings", "Color Picker", "Plugins" };
        const int itemCount = (int)(sizeof(items) / sizeof(items[0]));
        fillRect(r, g_windowMenuR, COL_BG_PANEL2);
        drawRect(r, g_windowMenuR, COL_BORDER_SOFT);
        for (int i = 0; i < itemCount; ++i)
        {
            SDL_Rect item{ g_windowMenuR.x, g_windowMenuR.y + i * 32, g_windowMenuR.w, 32 };
            bool hover = pointInRect(mx, my, item);
            if (hover) fillRect(r, item, COL_BTN_HOVER);
            drawText(r, app.getUiFont(), items[i], item.x + 10, item.y + 7, COL_TEXT_MAIN);
        }
    }

    if (g_pluginMenuOpen)
    {
        const char* items[] = { "Plugin Panel", "Loaded Plugins", "Install Plugin...", "Reload Plugins" };
        fillRect(r, g_pluginMenuR, COL_BG_PANEL2);
        drawRect(r, g_pluginMenuR, COL_BORDER_SOFT);
        for (int i = 0; i < 4; ++i)
        {
            SDL_Rect item{ g_pluginMenuR.x, g_pluginMenuR.y + i * 32, g_pluginMenuR.w, 32 };
            const bool hover = pointInRect(mx, my, item);
            const bool active = (i == 1 && g_pluginLoadedMenuOpen);
            if (hover || active) fillRect(r, item, COL_BTN_HOVER);
            drawText(r, app.getUiFont(), items[i], item.x + 10, item.y + 7, COL_TEXT_MAIN);
            if (i == 1)
                drawText(r, app.getUiFont(), ">", item.x + item.w - 16, item.y + 7, COL_TEXT_DIM);
        }

        if (g_pluginLoadedMenuOpen)
        {
            const auto loadedPlugins = pluginLoadedRuntimeRecords(app);
            g_pluginLoadedMenuR = pluginLoadedMenuRectForCount(g_pluginMenuR, (int)loadedPlugins.size());
            fillRect(r, g_pluginLoadedMenuR, COL_BG_PANEL2);
            drawRect(r, g_pluginLoadedMenuR, COL_BORDER_SOFT);
            if (loadedPlugins.empty())
            {
                drawText(r, app.getUiFont(), "No loaded plugins", g_pluginLoadedMenuR.x + 10, g_pluginLoadedMenuR.y + 7, COL_TEXT_DIM);
            }
            else
            {
                for (int i = 0; i < (int)loadedPlugins.size(); ++i)
                {
                    SDL_Rect item{ g_pluginLoadedMenuR.x, g_pluginLoadedMenuR.y + i * 32, g_pluginLoadedMenuR.w, 32 };
                    const bool hover = pointInRect(mx, my, item);
                    if (hover) fillRect(r, item, COL_BTN_HOVER);
                    const auto* record = loadedPlugins[(size_t)i];
                    const std::string label = !record->query.displayName.empty() ? record->query.displayName : (!record->package.manifest.name.empty() ? record->package.manifest.name : record->package.manifest.id);
                    drawText(r, app.getUiFont(), label, item.x + 10, item.y + 7, COL_TEXT_MAIN);
                }
            }
        }
    }

    if (g_exportSettingsOpen)
    {
        SDL_Rect overlay{ 0,0,w,h };
        SDL_SetRenderDrawColor(r, 0, 0, 0, 170);
        SDL_RenderFillRect(r, &overlay);

        SDL_Rect card{ w / 2 - 320, h / 2 - 240, 640, 480 };
        SDL_SetRenderDrawColor(r, COL_BG_PANEL2.r, COL_BG_PANEL2.g, COL_BG_PANEL2.b, 245);
        SDL_RenderFillRect(r, &card);
        SDL_SetRenderDrawColor(r, COL_BORDER_STRONG.r, COL_BORDER_STRONG.g, COL_BORDER_STRONG.b, COL_BORDER_STRONG.a);
        SDL_RenderDrawRect(r, &card);

        drawText(r, app.getUiFont(), std::string("Export Settings - ") + exportFmtLabel(g_exportFmt), card.x + 18, card.y + 16, COL_TEXT_MAIN);
        drawText(r, app.getUiFont(), "Tip: MP4 Preset min is 1 (0 breaks export).", card.x + 18, card.y + 40, COL_TEXT_DIM);

        SDL_Rect closeR{ card.x + card.w - 44, card.y + 10, 34, 28 };
        drawModernButton(r, closeR, pointInRect(mx, my, closeR), false);
        drawText(r, app.getUiFont(), "X", closeR.x + 11, closeR.y + 4, COL_TEXT_MAIN);

        SDL_Rect rowW{ card.x + 22, card.y + 90,  card.w - 44, 24 };
        SDL_Rect rowH{ card.x + 22, card.y + 130, card.w - 44, 24 };
        SDL_Rect rowF{ card.x + 22, card.y + 170, card.w - 44, 24 };
        SDL_Rect rowT{ card.x + 22, card.y + 210, card.w - 44, 24 };
        SDL_Rect rowS{ card.x + 22, card.y + 250, card.w - 44, 24 };
        SDL_Rect rowE{ card.x + 22, card.y + 290, card.w - 44, 24 };

        SDL_Rect boxW{}, boxH{}, boxF{}, tBox{}, boxS{}, boxE{};

        drawIntInputRow(r, app.getUiFont(), rowW, "Width", g_inW, (g_exportFocus == ExportField::Width), boxW);
        drawIntInputRow(r, app.getUiFont(), rowH, "Height", g_inH, (g_exportFocus == ExportField::Height), boxH);
        drawIntInputRow(r, app.getUiFont(), rowF, "FPS", g_inFps, (g_exportFocus == ExportField::FPS), boxF);
        drawToggle(r, app.getUiFont(), rowT, "Transparent BG", g_exportTransparent, tBox);
        drawIntInputRow(r, app.getUiFont(), rowS, "Start Frame", g_inStart, (g_exportFocus == ExportField::Start), boxS);
        drawIntInputRow(r, app.getUiFont(), rowE, "End Frame", g_inEnd, (g_exportFocus == ExportField::End), boxE);

        int totalFrames2 = (int)app.getEngine().getFrameCount();
        if (totalFrames2 < 1) totalFrames2 = 1;
        g_exportStart = std::clamp(g_exportStart, 0, totalFrames2 - 1);
        g_exportEnd = std::clamp(g_exportEnd, 0, totalFrames2 - 1);

        SDL_Rect rowA{ card.x + 22, card.y + 330, card.w - 44, 24 };
        SDL_Rect rowB{ card.x + 22, card.y + 370, card.w - 44, 24 };

        if (g_exportFmt == ExportFormat::MP4)
        {
            SDL_Rect boxA{}, boxB{};
            drawIntInputRow(r, app.getUiFont(), rowA, "CRF (0 best, 51 worst)", g_inMp4Crf, (g_exportFocus == ExportField::Mp4Crf), boxA);
            drawIntInputRow(r, app.getUiFont(), rowB, "Preset (1 fast, 8 slow)", g_inMp4Preset, (g_exportFocus == ExportField::Mp4Preset), boxB);
        }
        else if (g_exportFmt == ExportFormat::GIF)
        {
            SDL_Rect boxA{}, boxB{};
            drawIntInputRow(r, app.getUiFont(), rowA, "Colors (2-256)", g_inGifColors, (g_exportFocus == ExportField::GifColors), boxA);
            drawIntInputRow(r, app.getUiFont(), rowB, "Scale % (10-100)", g_inGifScale, (g_exportFocus == ExportField::GifScalePct), boxB);

            SDL_Rect dRow{ card.x + 22, card.y + 410, (card.w - 44) / 2 - 10, 24 };
            SDL_Rect lRow{ card.x + 22 + (card.w - 44) / 2 + 10, card.y + 410, (card.w - 44) / 2 - 10, 24 };
            SDL_Rect dBox{}, lBox{};
            drawToggle(r, app.getUiFont(), dRow, "Dither", g_gifDither, dBox);
            drawToggle(r, app.getUiFont(), lRow, "Loop", g_gifLoop, lBox);
        }
        else
        {
            SDL_Rect boxA{};
            drawIntInputRow(r, app.getUiFont(), rowA, "Compression (0-9)", g_inPngComp, (g_exportFocus == ExportField::PngCompression), boxA);
            SDL_Rect interlaceBox{};
            drawToggle(r, app.getUiFont(), rowB, "Interlace", g_pngInterlace, interlaceBox);
        }

        std::string base = (strova::paths::getExportsDir() / safeProjectFolderName(app.getProjectNameStr())).string();
        std::string out = (g_exportFmt == ExportFormat::PNGSEQ) ? (base + "/png_sequence/") :
            (g_exportFmt == ExportFormat::GIF) ? (base + "/animation.gif") : (base + "/animation.mp4");

        drawSmallLabel(r, app.getUiFont(), card.x + 22, card.y + 440, "Output");
        drawValue(r, app.getUiFont(), card.x + 22, card.y + 458, out);

        SDL_Rect exportR{ card.x + card.w - 240, card.y + card.h - 56, 110, 38 };
        SDL_Rect cancelR{ card.x + card.w - 120, card.y + card.h - 56, 110, 38 };

        drawModernButton(r, exportR, pointInRect(mx, my, exportR), true);
        drawModernButton(r, cancelR, pointInRect(mx, my, cancelR), false);

        drawText(r, app.getUiFont(), "Export", exportR.x + 30, exportR.y + 9, COL_TEXT_MAIN);
        drawText(r, app.getUiFont(), "Close", cancelR.x + 34, cancelR.y + 9, COL_TEXT_MAIN);
    }

    if (!tooltipText.empty() && !g_exportSettingsOpen)
        drawTooltipBubble(r, app.getUiFont(), tooltipText, mx, my, w, h);
}
