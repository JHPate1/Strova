#include "BrushCreatorScreen.h"

#include "../app/App.h"
#include "../core/Theme.h"
#include "../platform/FileDialog.h"
#include "../ui/TextRenderer.h"
#include "../ui/UiPrimitives.h"

#include <SDL_ttf.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace
{
    using strova::brush::BlendMode;
    using strova::brush::BrushType;
    using strova::brush::GeneratorType;
    using strova::brush::GradientMode;
    using strova::brush::MaskSource;
    using strova::brush::RotationMode;

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

    static std::string fitText(TTF_Font* font, const std::string& text, int maxW)
    {
        if (!font || maxW <= 8 || text.empty()) return std::string();
        int tw = 0, th = 0;
        if (TTF_SizeUTF8(font, text.c_str(), &tw, &th) == 0 && tw <= maxW) return text;
        const std::string ell = "...";
        std::string out = ell;
        for (size_t i = 0; i < text.size(); ++i)
        {
            std::string cand = text.substr(0, i + 1) + ell;
            if (TTF_SizeUTF8(font, cand.c_str(), &tw, &th) != 0 || tw > maxW)
                break;
            out = cand;
        }
        return out;
    }

    static void drawTextFit(SDL_Renderer* r, TTF_Font* font, const std::string& text, const SDL_Rect& rc, SDL_Color col, int padX = 0, int padY = 0)
    {
        if (rc.w <= 2 || rc.h <= 2) return;
        const SDL_Rect textRc{ rc.x + padX, rc.y + padY, std::max(0, rc.w - padX * 2), std::max(0, rc.h - padY * 2) };
        strova::ui_text::drawTextEllipsized(r, font, text, textRc, col);
    }

    static void drawButton(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const std::string& label, bool active = false)
    {
        strova::uix::drawButton(r, font, rc, label, false, active, false);
    }

    static void drawChecker(SDL_Renderer* r, const SDL_Rect& rc)
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

    static SDL_Color bgModeColor(int mode)
    {
        switch (mode)
        {
        case 0: return SDL_Color{ 244, 246, 250, 255 };
        case 1: return SDL_Color{ 30, 34, 42, 255 };
        default: return SDL_Color{ 64, 68, 76, 255 };
        }
    }

    static std::vector<std::uint8_t> stampMaskToRgba(const strova::brush::BrushStamp& stamp, float gamma = 1.0f)
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

    static void drawPixels(SDL_Renderer* r, const SDL_Rect& rc, int w, int h, const std::vector<std::uint8_t>& rgba, bool checkerBg, SDL_Color bg)
    {
        if (checkerBg) drawChecker(r, rc);
        else fillRect(r, rc, bg);
        drawRect(r, rc, COL_BORDER_SOFT);
        if (w <= 0 || h <= 0 || rgba.empty()) return;

        SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!tex) return;
        SDL_UpdateTexture(tex, nullptr, rgba.data(), w * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
#endif
        SDL_Rect dst = rc;
        const float sx = (float)(rc.w - 12) / (float)std::max(1, w);
        const float sy = (float)(rc.h - 12) / (float)std::max(1, h);
        const float fit = std::max(0.01f, std::min(sx, sy));
        dst.w = std::max(1, (int)std::lround((float)w * fit));
        dst.h = std::max(1, (int)std::lround((float)h * fit));
        dst.x = rc.x + (rc.w - dst.w) / 2;
        dst.y = rc.y + (rc.h - dst.h) / 2;
        SDL_RenderCopy(r, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }

    static const char* tabName(BrushCreatorScreen::Tab tab)
    {
        switch (tab)
        {
        case BrushCreatorScreen::Tab::Overview: return "Overview";
        case BrushCreatorScreen::Tab::Stamp: return "Stamp";
        case BrushCreatorScreen::Tab::Behavior: return "Behavior";
        case BrushCreatorScreen::Tab::Color: return "Color";
        case BrushCreatorScreen::Tab::Script: return "Script";
        case BrushCreatorScreen::Tab::TestCanvas: return "Test Canvas";
        case BrushCreatorScreen::Tab::Package: return "Package";
        default: return "Overview";
        }
    }

    static const char* bgModeLabel(int mode)
    {
        switch (mode)
        {
        case 0: return "White";
        case 1: return "Dark";
        default: return "Checker";
        }
    }

    static const char* previewModeLabel(BrushCreatorScreen::PreviewMode mode)
    {
        switch (mode)
        {
        case BrushCreatorScreen::PreviewMode::Mask: return "Mask";
        case BrushCreatorScreen::PreviewMode::FinalDab: return "Final Dab";
        case BrushCreatorScreen::PreviewMode::RawSource: return "Raw Source";
        case BrushCreatorScreen::PreviewMode::Strength: return "Strength";
        default: return "Mask";
        }
    }


    static const char* generatorLabel(GeneratorType type)
    {
        switch (type)
        {
        case GeneratorType::HardCircle: return "Hard Circle";
        case GeneratorType::SoftCircle: return "Soft Circle";
        case GeneratorType::Square: return "Square";
        case GeneratorType::SoftSquare: return "Soft Square";
        case GeneratorType::SpeckleCluster: return "Speckle";
        case GeneratorType::GrainyDisk: return "Grain Disk";
        case GeneratorType::OvalTip: return "Oval Tip";
        case GeneratorType::ChalkPatch: return "Chalk Patch";
        case GeneratorType::NoiseBlob: return "Noise Blob";
        default: return "Soft Circle";
        }
    }

    static const char* modalTitle(BrushCreatorScreen::Modal modal)
    {
        switch (modal)
        {
        case BrushCreatorScreen::Modal::NewBrush: return "New Brush";
        case BrushCreatorScreen::Modal::ImportStamp: return "Import Stamp";
        case BrushCreatorScreen::Modal::ExportBrush: return "Export Brush";
        case BrushCreatorScreen::Modal::InstallBrush: return "Install Brush";
        case BrushCreatorScreen::Modal::Validation: return "Validation Results";
        default: return "Dialog";
        }
    }

    static SDL_Color swatchColor(int index)
    {
        static const SDL_Color k[] = {
            SDL_Color{20,20,20,255}, SDL_Color{255,108,92,255}, SDL_Color{120,205,255,255},
            SDL_Color{120,216,150,255}, SDL_Color{255,212,88,255}, SDL_Color{189,140,255,255}
        };
        return k[index % (int)(sizeof(k) / sizeof(k[0]))];
    }

    static void cycleMaskSource(MaskSource& src, int dir)
    {
        int v = (int)src + dir;
        if (v < 0) v = 2;
        if (v > 2) v = 0;
        src = (MaskSource)v;
    }

    static void drawField(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const std::string& label, const std::string& value, bool focused)
    {
        fillRect(r, rc, COL_BG_PANEL2);
        drawRect(r, rc, focused ? SDL_Color{ 110, 160, 255, 255 } : COL_BORDER_SOFT);
        const int labelW = std::clamp(rc.w / 3, 88, 132);
        SDL_Rect labelRc{ rc.x + 8, rc.y + 5, std::max(24, labelW - 12), rc.h - 10 };
        SDL_Rect valueRc{ rc.x + labelW, rc.y + 5, std::max(24, rc.w - labelW - 10), rc.h - 10 };
        drawTextFit(r, font, label, labelRc, COL_TEXT_DIM);
        drawTextFit(r, font, value, valueRc, COL_TEXT_MAIN);
    }

    static void drawAdjustRow(SDL_Renderer* r, TTF_Font* font, const SDL_Rect& rc, const std::string& label, const std::string& value)
    {
        drawText(r, font, label, rc.x, rc.y + 2, COL_TEXT_DIM);
        SDL_Rect minusRc{ rc.x + rc.w - 66, rc.y - 2, 28, 24 };
        SDL_Rect plusRc{ rc.x + rc.w - 32, rc.y - 2, 28, 24 };
        drawButton(r, font, minusRc, "-");
        drawButton(r, font, plusRc, "+");
        drawText(r, font, value, rc.x + rc.w - 152, rc.y + 2, COL_TEXT_MAIN);
    }


    static SDL_Rect topActionRect(const SDL_Rect& panel, int index, int count)
    {
        const int pad = 12;
        const int gap = 8;
        const int btnH = 28;
        const bool compact = panel.w < 700;
        if (!compact)
        {
            const int btnW = std::max(88, std::min(120, (panel.w - pad * 2 - gap * (count - 1)) / std::max(1, count)));
            return SDL_Rect{ panel.x + pad + index * (btnW + gap), panel.y + 12, btnW, btnH };
        }
        const int cols = 2;
        const int rows = (count + cols - 1) / cols;
        (void)rows;
        const int btnW = std::max(90, (panel.w - pad * 2 - gap) / cols);
        const int col = index % cols;
        const int row = index / cols;
        return SDL_Rect{ panel.x + pad + col * (btnW + gap), panel.y + 12 + row * (btnH + 8), btnW, btnH };
    }


    static int topActionExtraY(const SDL_Rect& panel)
    {
        return panel.w < 700 ? 36 : 0;
    }

    static bool isCtrlS(const SDL_Event& e)
    {
        return e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_s) && ((SDL_GetModState() & KMOD_CTRL) != 0);
    }
}

struct BrushCreatorScreen::Layout
{
    SDL_Rect outer{};
    SDL_Rect header{};
    SDL_Rect left{};
    SDL_Rect center{};
    SDL_Rect right{};
    SDL_Rect dockLeft{};
    SDL_Rect dockCenter{};
    SDL_Rect dockRight{};
    SDL_Rect leftHeader{};
    SDL_Rect centerHeader{};
    SDL_Rect rightHeader{};
    SDL_Rect status{};
    SDL_Rect preview{};
    SDL_Rect canvas{};
    SDL_Rect closeBtn{};
    SDL_Rect backBtn{};
    SDL_Rect saveBtn{};
    SDL_Rect validateBtn{};
    SDL_Rect exportBtn{};
    SDL_Rect leftSplitter{};
    SDL_Rect rightSplitter{};
    std::array<SDL_Rect, (int)Tab::Count> tabs{};
    std::array<SDL_Rect, 7> overviewFields{};
};

static BrushCreatorScreen::Layout makeLayout(int w, int h, float leftRatio, float rightRatio,
    bool leftFloating, const SDL_Rect& leftFloatRect,
    bool centerFloating, const SDL_Rect& centerFloatRect,
    bool rightFloating, const SDL_Rect& rightFloatRect)
{
    BrushCreatorScreen::Layout l{};
    l.outer = SDL_Rect{ 18, 18, std::max(0, w - 36), std::max(0, h - 36) };
    l.header = SDL_Rect{ l.outer.x + 1, l.outer.y + 1, std::max(0, l.outer.w - 2), 48 };
    l.status = SDL_Rect{ l.outer.x + 12, l.outer.y + l.outer.h - 36, std::max(0, l.outer.w - 24), 22 };

    const int contentTop = l.outer.y + 60;
    const int contentH = std::max(120, l.outer.h - 108);
    const int gap = 12;
    const int splitterW = 6;
    const int minLeft = 200;
    const int minRight = 228;
    const int minCenter = 360;
    const int usableW = std::max(840, l.outer.w - 24);
    int leftW = std::clamp((int)std::lround((double)usableW * leftRatio), minLeft, std::max(minLeft, usableW - minCenter - minRight - splitterW * 2 - gap * 2));
    int rightW = std::clamp((int)std::lround((double)usableW * rightRatio), minRight, std::max(minRight, usableW - minCenter - leftW - splitterW * 2 - gap * 2));
    int centerW = usableW - leftW - rightW - splitterW * 2 - gap * 2;
    if (centerW < minCenter)
    {
        const int need = minCenter - centerW;
        const int takeRight = std::min(need / 2 + need % 2, std::max(0, rightW - minRight));
        rightW -= takeRight;
        const int takeLeft = std::min(need - takeRight, std::max(0, leftW - minLeft));
        leftW -= takeLeft;
        centerW = usableW - leftW - rightW - splitterW * 2 - gap * 2;
    }
    centerW = std::max(minCenter, centerW);

    l.dockLeft = SDL_Rect{ l.outer.x + 12, contentTop, leftW, contentH };
    l.leftSplitter = SDL_Rect{ l.dockLeft.x + l.dockLeft.w + gap / 2, contentTop, splitterW, contentH };
    l.dockCenter = SDL_Rect{ l.leftSplitter.x + splitterW + gap / 2, contentTop, centerW, contentH };
    l.rightSplitter = SDL_Rect{ l.dockCenter.x + l.dockCenter.w + gap / 2, contentTop, splitterW, contentH };
    l.dockRight = SDL_Rect{ l.rightSplitter.x + splitterW + gap / 2, contentTop, rightW, contentH };

    l.left = leftFloating && leftFloatRect.w > 0 && leftFloatRect.h > 0 ? leftFloatRect : l.dockLeft;
    l.center = centerFloating && centerFloatRect.w > 0 && centerFloatRect.h > 0 ? centerFloatRect : l.dockCenter;
    l.right = rightFloating && rightFloatRect.w > 0 && rightFloatRect.h > 0 ? rightFloatRect : l.dockRight;

    if (leftFloating || centerFloating || rightFloating)
    {
        l.leftSplitter = SDL_Rect{ 0, 0, 0, 0 };
        l.rightSplitter = SDL_Rect{ 0, 0, 0, 0 };
    }

    l.leftHeader = SDL_Rect{ l.left.x + 1, l.left.y + 1, std::max(0, l.left.w - 2), 24 };
    l.centerHeader = SDL_Rect{ l.center.x + 1, l.center.y + 1, std::max(0, l.center.w - 2), 24 };
    l.rightHeader = SDL_Rect{ l.right.x + 1, l.right.y + 1, std::max(0, l.right.w - 2), 24 };

    l.preview = SDL_Rect{ l.right.x + 12, l.right.y + 36, std::max(96, l.right.w - 24), std::min(220, std::max(132, l.right.h / 3)) };
    l.canvas = SDL_Rect{ l.center.x + 12, l.center.y + 76, std::max(80, l.center.w - 24), std::max(80, l.center.h - 88) };
    l.backBtn = SDL_Rect{ l.header.x + 12, l.header.y + 10, 88, 28 };
    l.saveBtn = SDL_Rect{ l.header.x + l.header.w - 320, l.header.y + 10, 90, 28 };
    l.validateBtn = SDL_Rect{ l.header.x + l.header.w - 222, l.header.y + 10, 100, 28 };
    l.exportBtn = SDL_Rect{ l.header.x + l.header.w - 114, l.header.y + 10, 72, 28 };
    l.closeBtn = SDL_Rect{ l.header.x + l.header.w - 34, l.header.y + 10, 22, 28 };

    for (int i = 0; i < (int)BrushCreatorScreen::Tab::Count; ++i)
        l.tabs[(size_t)i] = SDL_Rect{ l.left.x + 10, l.left.y + 170 + i * 34, std::max(96, l.left.w - 20), 28 };

    for (int i = 0; i < 7; ++i)
        l.overviewFields[(size_t)i] = SDL_Rect{ l.center.x + 12, l.center.y + 74 + i * 40, std::max(120, l.center.w - 24), 32 };

    return l;
}

bool BrushCreatorScreen::ensureLoadedProject(App& app, const std::string& brushProjectPath, const std::string& statusText)
{
    if (!brushProjectPath.empty())
    {
        strova::brush::BrushProject proj{};
        std::string err;
        if (!app.brushManager().loadProjectFile(brushProjectPath, proj, err))
        {
            projectData = app.brushManager().makeDefaultProject("New Brush");
            status = err;
            dirty = false;
            activeTab = Tab::Overview;
            modal = Modal::Validation;
            modalText = err;
            ensureDefaultScript();
            refreshDerived();
            return false;
        }
        projectData = proj;
        projectData.package.manifest.generator = projectData.generator;
        status = statusText;
    }
    else
    {
        projectData = app.brushManager().makeDefaultProject("New Brush");
        projectData.package.manifest.generator = projectData.generator;
        status = statusText;
    }

    inspectorBgMode = 0;
    testBgMode = 0;
    testUseGradient = false;
    testPressure = false;
    dirty = false;
    activeTab = Tab::Overview;
    modal = Modal::None;
    modalText.clear();
    overviewField = OverviewField::None;
    scriptFocused = false;
    pendingImportStamp = strova::brush::BrushStamp{};
    pendingImportPath.clear();
    pendingImportMaskSource = projectData.package.manifest.color.maskSource;
    pendingImportInvert = projectData.package.manifest.color.invertMask;
    pendingNewType = projectData.requestedType;
    pendingNewGenerator = projectData.generator;
    projectData.package.manifest.generator = projectData.generator;
    ensureDefaultScript();
    clampScriptCursor();
    resetSandbox();
    leftPaneFloating = false;
    centerPaneFloating = false;
    rightPaneFloating = false;
    leftPaneFloatRect = SDL_Rect{ 0, 0, 0, 0 };
    centerPaneFloatRect = SDL_Rect{ 0, 0, 0, 0 };
    rightPaneFloatRect = SDL_Rect{ 0, 0, 0, 0 };
    draggingPane = PaneId::None;
    paneDragOffsetX = 0;
    paneDragOffsetY = 0;
    refreshDerived();
    SDL_StartTextInput();
    return true;
}

void BrushCreatorScreen::open(App& app, const std::string& brushProjectPath)
{
    const std::string statusText = brushProjectPath.empty() ? "Brush Creator opened" : "Brush project opened";
    ensureLoadedProject(app, brushProjectPath, statusText);
}

void BrushCreatorScreen::open(App& app, const strova::brush::BrushProject& project, const std::string& statusText)
{
    projectData = project;
    dirty = false;
    activeTab = Tab::Overview;
    modal = Modal::None;
    modalText.clear();
    status = statusText;
    overviewField = OverviewField::None;
    scriptFocused = false;
    pendingImportStamp = strova::brush::BrushStamp{};
    pendingImportPath.clear();
    pendingImportMaskSource = projectData.package.manifest.color.maskSource;
    pendingImportInvert = projectData.package.manifest.color.invertMask;
    pendingNewType = projectData.requestedType;
    pendingNewGenerator = projectData.generator;
    projectData.package.manifest.generator = projectData.generator;
    inspectorBgMode = 0;
    testBgMode = 0;
    testUseGradient = false;
    testPressure = false;
    ensureDefaultScript();
    clampScriptCursor();
    resetSandbox();
    leftPaneFloating = false;
    centerPaneFloating = false;
    rightPaneFloating = false;
    leftPaneFloatRect = SDL_Rect{ 0, 0, 0, 0 };
    centerPaneFloatRect = SDL_Rect{ 0, 0, 0, 0 };
    rightPaneFloatRect = SDL_Rect{ 0, 0, 0, 0 };
    draggingPane = PaneId::None;
    paneDragOffsetX = 0;
    paneDragOffsetY = 0;
    refreshDerived();
    SDL_StartTextInput();
    (void)app;
}

void BrushCreatorScreen::markDirty(const std::string& statusText)
{
    dirty = true;
    status = statusText;
    refreshDerived();
}

void BrushCreatorScreen::refreshDerived()
{
    auto& col = projectData.package.manifest.color;
    if (!col.supportsGradient)
        col.gradientMode = GradientMode::None;
    if (!col.supportsUserColor && !col.supportsGradient && col.fixedColor.a == 0)
        col.fixedColor = col.previewTint;

    if (!projectData.package.stamp.empty())
    {
        auto& stamp = projectData.package.stamp;
        strova::brush::normalizeStamp(stamp, col.maskSource, col.invertMask, stamp.threshold, stamp.levelsClamp, stamp.edgeBoost);
    }
    projectData.package.manifest.generator = projectData.generator;
    projectData.package.preview = strova::brush::buildPackagePreview(projectData.package);
    projectData.package.validation = strova::brush::validate(projectData.package);
}

void BrushCreatorScreen::resetSandbox()
{
    testDrawing = false;
    liveTestStroke = Stroke{};
    testStrokes.clear();
}

Stroke BrushCreatorScreen::makeSandboxStroke() const
{
    Stroke s{};
    s.tool = ToolType::Brush;
    s.color = projectData.package.manifest.color.supportsUserColor ? testColor : projectData.package.manifest.color.fixedColor;
    s.thickness = projectData.package.manifest.params.sizeDefault;
    s.brushId = projectData.package.manifest.id;
    s.brushName = projectData.package.manifest.name;
    s.brushVersion = projectData.package.manifest.version;
    s.settings.brushId = projectData.package.manifest.id;
    s.settings.brushDisplayName = projectData.package.manifest.name;
    s.settings.brushVersion = projectData.package.manifest.version;
    s.settings.brushSupportsGradient = projectData.package.manifest.color.supportsGradient;
    s.settings.brushSupportsUserColor = projectData.package.manifest.color.supportsUserColor;
    s.settings.size = projectData.package.manifest.params.sizeDefault;
    s.settings.opacity = projectData.package.manifest.params.opacity;
    s.settings.hardness = projectData.package.manifest.params.hardness;
    s.settings.spacing = projectData.package.manifest.params.spacing;
    s.settings.flow = projectData.package.manifest.params.flow;
    s.settings.scatter = projectData.package.manifest.params.scatter;
    s.settings.jitterSize = projectData.package.manifest.params.jitterSize;
    s.settings.jitterOpacity = projectData.package.manifest.params.jitterOpacity;
    s.settings.jitterRotation = projectData.package.manifest.params.jitterRotation;
    s.settings.spacingJitter = projectData.package.manifest.params.spacingJitter;
    if (testUseGradient && projectData.package.manifest.color.supportsGradient)
    {
        GradientConfig g{};
        g.enabled = true;
        g.mode = 1;
        g.stopPos = { 0.0f, 0.33f, 0.66f, 1.0f };
        g.stopColor[0] = swatchColor(1);
        g.stopColor[1] = swatchColor(4);
        g.stopColor[2] = swatchColor(2);
        g.stopColor[3] = swatchColor(5);
        s.gradient = g;
    }
    return s;
}

void BrushCreatorScreen::appendSandboxPoint(const SDL_Rect& canvas, Stroke& stroke, int mx, int my)
{
    const float px = (float)(mx - canvas.x);
    const float py = (float)(my - canvas.y);
    if (!stroke.points.empty())
    {
        const StrokePoint& last = stroke.points.back();
        const float dx = px - last.x;
        const float dy = py - last.y;
        if ((dx * dx + dy * dy) < 4.0f) return;
    }
    StrokePoint p{};
    p.x = px;
    p.y = py;
    if (testPressure)
    {
        const float u = std::clamp(py / (float)std::max(1, canvas.h), 0.0f, 1.0f);
        p.pressure = 0.35f + (1.0f - u) * 0.65f;
    }
    stroke.points.push_back(p);
}

void BrushCreatorScreen::addReplayStroke(const SDL_Rect& canvas)
{
    Stroke s = makeSandboxStroke();
    const int left = canvas.x + 28;
    const int right = canvas.x + canvas.w - 28;
    const int midY = canvas.y + canvas.h / 2;
    const int count = 32;
    for (int i = 0; i < count; ++i)
    {
        const float t = (float)i / (float)(count - 1);
        const float x = (float)(left + (int)std::lround((right - left) * t));
        const float wave = std::sinf(t * 6.2831853f) * (float)std::max(12, canvas.h / 7);
        StrokePoint p{};
        p.x = x - (float)canvas.x;
        p.y = std::clamp((float)midY + wave - (float)canvas.y, 12.0f, (float)(canvas.h - 12));
        p.pressure = testPressure ? (0.35f + t * 0.65f) : 1.0f;
        s.points.push_back(p);
    }
    if (!s.points.empty()) testStrokes.push_back(s);
}

bool BrushCreatorScreen::saveProjectInteractive(App& app)
{
    std::string outPath = projectData.projectPath;
    if (outPath.empty())
    {
        if (!platform::pickSaveBrushProjectFile(outPath, projectData.package.manifest.name))
            return false;
    }
    refreshDerived();
    std::string err;
    if (!app.brushManager().saveProjectFile(projectData, outPath, err))
    {
        modal = Modal::Validation;
        modalText = err;
        status = err;
        return false;
    }
    projectData.projectPath = outPath;
    dirty = false;
    status = "Brush project saved";
    return true;
}

bool BrushCreatorScreen::exportBrushInteractive(App& app)
{
    std::string outPath;
    if (!platform::pickSaveBrushFile(outPath, projectData.package.manifest.name))
        return false;
    refreshDerived();
    std::string err;
    if (!app.brushManager().exportPackage(projectData.package, outPath, err))
    {
        modal = Modal::Validation;
        modalText = err;
        status = err;
        return false;
    }
    status = "Brush exported";
    return true;
}

bool BrushCreatorScreen::installBrushInteractive(App& app)
{
    const auto dst = strova::brush::userDir() / (strova::brush::sanitizeId(projectData.package.manifest.id) + ".sbrush");
    refreshDerived();
    std::string err;
    if (!app.brushManager().exportPackage(projectData.package, dst.string(), err))
    {
        modal = Modal::Validation;
        modalText = err;
        status = err;
        return false;
    }
    app.brushManager().refresh();
    app.brushManager().select(projectData.package.manifest.id);
    if (const auto* pkg = app.brushManager().findById(projectData.package.manifest.id))
    {
        auto& s = app.toolBank.get(ToolType::Brush);
        s.brushId = pkg->manifest.id;
        s.brushDisplayName = pkg->manifest.name;
        s.brushVersion = pkg->manifest.version;
        s.brushSupportsUserColor = pkg->manifest.color.supportsUserColor;
        s.brushSupportsGradient = pkg->manifest.color.supportsGradient;
        s.spacing = pkg->manifest.params.spacing;
        s.flow = pkg->manifest.params.flow;
        s.clamp();
        app.getEngine().setBrushSelection(s.brushId, s.brushVersion, s.brushDisplayName);
    }
    status = "Brush installed locally";
    return true;
}

void BrushCreatorScreen::createNewBrush(App& app)
{
    projectData = app.brushManager().makeDefaultProject("New Brush");
    projectData.requestedType = pendingNewType;
    projectData.package.manifest.type = pendingNewType;
    projectData.generator = pendingNewGenerator;
    strova::brush::applyFamilyPreset(projectData.package, pendingNewGenerator);
    projectData.package.manifest.type = pendingNewType;
    projectData.package.manifest.id = strova::brush::sanitizeId("strova.local.new_brush");
    inspectorBgMode = 0;
    testBgMode = 0;
    testUseGradient = false;
    testPressure = false;
    ensureDefaultScript();
    dirty = true;
    modal = Modal::None;
    status = "New brush project created";
    resetSandbox();
    refreshDerived();
}

void BrushCreatorScreen::chooseImportStamp()
{
    std::string path;
    if (!platform::pickOpenBrushOrProject(path)) return;
    const std::filesystem::path p(path);
    if (p.extension() != ".png")
    {
        modalText = "Pick a PNG stamp file.";
        return;
    }
    std::string err;
    strova::brush::BrushStamp stamp{};
    if (!strova::brush::loadRgbaFromImage(path, stamp, err))
    {
        modalText = err;
        return;
    }
    pendingImportStamp = stamp;
    pendingImportPath = path;
    modalText = "PNG loaded. Confirm to apply the interpretation settings.";
}

void BrushCreatorScreen::commitImportStamp()
{
    if (pendingImportStamp.empty())
    {
        modalText = "Pick a PNG first.";
        return;
    }
    projectData.package.stamp = pendingImportStamp;
    projectData.package.manifest.type = BrushType::RasterStamp;
    projectData.package.manifest.color.maskSource = pendingImportMaskSource;
    projectData.package.manifest.color.invertMask = pendingImportInvert;
    projectData.package.manifest.name = std::filesystem::path(pendingImportPath).stem().string();
    projectData.package.manifest.id = strova::brush::sanitizeId("strova.local." + std::filesystem::path(pendingImportPath).stem().string());
    projectData.package.manifest.stamp = "stamp.png";
    pendingImportStamp = strova::brush::BrushStamp{};
    pendingImportPath.clear();
    modal = Modal::None;
    activeTab = Tab::Stamp;
    markDirty("Stamp imported");
}

void BrushCreatorScreen::cycleBlendMode(int dir)
{
    int v = (int)projectData.package.manifest.params.blendMode + dir;
    if (v < 0) v = (int)BlendMode::Overlay;
    if (v > (int)BlendMode::Overlay) v = (int)BlendMode::Normal;
    projectData.package.manifest.params.blendMode = (BlendMode)v;
    markDirty("Blend mode updated");
}

void BrushCreatorScreen::cycleRotationMode(int dir)
{
    int v = (int)projectData.package.manifest.params.rotationMode + dir;
    if (v < 0) v = (int)RotationMode::Random;
    if (v > (int)RotationMode::Random) v = (int)RotationMode::Stroke;
    projectData.package.manifest.params.rotationMode = (RotationMode)v;
    markDirty("Rotation mode updated");
}

void BrushCreatorScreen::cycleGradientMode(int dir)
{
    int v = (int)projectData.package.manifest.color.gradientMode + dir;
    if (v < 0) v = (int)GradientMode::Fixed;
    if (v > (int)GradientMode::Fixed) v = (int)GradientMode::None;
    projectData.package.manifest.color.gradientMode = (GradientMode)v;
    markDirty("Gradient mode updated");
}

void BrushCreatorScreen::cyclePreviewMode()
{
    int v = (int)previewMode + 1;
    if (v > (int)PreviewMode::Strength) v = 0;
    previewMode = (PreviewMode)v;
}

void BrushCreatorScreen::handleOverviewTextInput(const SDL_Event& e)
{
    std::string* target = overviewFieldString(overviewField);
    if (!target) return;
    if (e.type == SDL_TEXTINPUT)
    {
        target->append(e.text.text);
        if (overviewField == OverviewField::Name && (projectData.package.manifest.id.empty() || projectData.package.manifest.id.rfind("strova.local.", 0) == 0))
            projectData.package.manifest.id = strova::brush::sanitizeId("strova.local." + projectData.package.manifest.name);
        markDirty("Overview updated");
    }
    else if (e.type == SDL_KEYDOWN)
    {
        if (e.key.keysym.sym == SDLK_BACKSPACE && !target->empty())
        {
            target->pop_back();
            markDirty("Overview updated");
        }
        else if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_ESCAPE)
        {
            setOverviewFieldFocus(OverviewField::None);
        }
    }
}

void BrushCreatorScreen::clampScriptCursor()
{
    scriptCursor = std::min<std::size_t>(scriptCursor, projectData.package.scriptSource.size());
}

void BrushCreatorScreen::ensureDefaultScript()
{
    if (strova::brush::trimCopy(projectData.package.scriptSource).empty())
        projectData.package.scriptSource = "-- Strova Brush Lua\nspacing_scale=1.0\nscatter_boost=0.0\nalpha_scale=1.0\nsize_scale=1.0\nrotation_bias_deg=0.0\n";
    scriptCursor = std::min<std::size_t>(scriptCursor, projectData.package.scriptSource.size());
}

void BrushCreatorScreen::handleScriptTextInput(const SDL_Event& e)
{
    if (!scriptFocused) return;
    clampScriptCursor();
    if (e.type == SDL_TEXTINPUT)
    {
        projectData.package.scriptSource.insert(scriptCursor, e.text.text);
        scriptCursor += SDL_strlen(e.text.text);
        markDirty("Script updated");
        return;
    }
    if (e.type != SDL_KEYDOWN) return;

    if (e.key.keysym.sym == SDLK_BACKSPACE)
    {
        if (scriptCursor > 0 && !projectData.package.scriptSource.empty())
        {
            projectData.package.scriptSource.erase(scriptCursor - 1, 1);
            --scriptCursor;
            markDirty("Script updated");
        }
    }
    else if (e.key.keysym.sym == SDLK_DELETE)
    {
        if (scriptCursor < projectData.package.scriptSource.size())
        {
            projectData.package.scriptSource.erase(scriptCursor, 1);
            markDirty("Script updated");
        }
    }
    else if (e.key.keysym.sym == SDLK_LEFT)
    {
        if (scriptCursor > 0) --scriptCursor;
    }
    else if (e.key.keysym.sym == SDLK_RIGHT)
    {
        if (scriptCursor < projectData.package.scriptSource.size()) ++scriptCursor;
    }
    else if (e.key.keysym.sym == SDLK_RETURN)
    {
        projectData.package.scriptSource.insert(scriptCursor, "\n");
        ++scriptCursor;
        markDirty("Script updated");
    }
    else if (e.key.keysym.sym == SDLK_TAB)
    {
        projectData.package.scriptSource.insert(scriptCursor, "    ");
        scriptCursor += 4;
        markDirty("Script updated");
    }
}

void BrushCreatorScreen::setOverviewFieldFocus(OverviewField field)
{
    overviewField = field;
    if (field != OverviewField::None) scriptFocused = false;
}

std::string* BrushCreatorScreen::overviewFieldString(OverviewField field)
{
    switch (field)
    {
    case OverviewField::Name: return &projectData.package.manifest.name;
    case OverviewField::InternalId: return &projectData.package.manifest.id;
    case OverviewField::Author: return &projectData.package.manifest.author;
    case OverviewField::Description: return &projectData.package.manifest.description;
    case OverviewField::Category: return &projectData.package.manifest.category;
    case OverviewField::Tags: return &projectData.package.manifest.tags;
    default: return nullptr;
    }
}

void BrushCreatorScreen::update(App& app, double dt)
{
    (void)app;
    (void)dt;
}

void BrushCreatorScreen::handleEvent(App& app, SDL_Event& e)
{
    int w = 0, h = 0;
    SDL_GetWindowSize(app.windowHandle(), &w, &h);
    const Layout layout = makeLayout(w, h, leftPaneRatio, rightPaneRatio,
        leftPaneFloating, leftPaneFloatRect,
        centerPaneFloating, centerPaneFloatRect,
        rightPaneFloating, rightPaneFloatRect);
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);

    if (isCtrlS(e))
    {
        saveProjectInteractive(app);
        return;
    }

    auto paneRectPtr = [&](PaneId pane) -> SDL_Rect*
    {
        switch (pane)
        {
        case PaneId::Left: return &leftPaneFloatRect;
        case PaneId::Center: return &centerPaneFloatRect;
        case PaneId::Right: return &rightPaneFloatRect;
        default: return nullptr;
        }
    };
    auto paneFloatingPtr = [&](PaneId pane) -> bool*
    {
        switch (pane)
        {
        case PaneId::Left: return &leftPaneFloating;
        case PaneId::Center: return &centerPaneFloating;
        case PaneId::Right: return &rightPaneFloating;
        default: return nullptr;
        }
    };
    auto paneCurrentRect = [&](PaneId pane) -> SDL_Rect
    {
        switch (pane)
        {
        case PaneId::Left: return layout.left;
        case PaneId::Center: return layout.center;
        case PaneId::Right: return layout.right;
        default: return SDL_Rect{ 0, 0, 0, 0 };
        }
    };
    auto paneHomeRect = [&](PaneId pane) -> SDL_Rect
    {
        switch (pane)
        {
        case PaneId::Left: return layout.dockLeft;
        case PaneId::Center: return layout.dockCenter;
        case PaneId::Right: return layout.dockRight;
        default: return SDL_Rect{ 0, 0, 0, 0 };
        }
    };
    auto clampPaneRect = [&](SDL_Rect& rc)
    {
        SDL_Rect ws{ layout.outer.x + 8, layout.outer.y + 56, std::max(220, layout.outer.w - 16), std::max(160, layout.outer.h - 64) };
        rc.w = std::max(180, std::min(ws.w, rc.w));
        rc.h = std::max(120, std::min(ws.h, rc.h));
        rc.x = std::clamp(rc.x, ws.x, ws.x + ws.w - rc.w);
        rc.y = std::clamp(rc.y, ws.y, ws.y + ws.h - rc.h);
    };
    auto startPaneDrag = [&](PaneId pane)
    {
        SDL_Rect current = paneCurrentRect(pane);
        if (SDL_Rect* rc = paneRectPtr(pane)) *rc = current;
        if (bool* floating = paneFloatingPtr(pane)) *floating = true;
        draggingPane = pane;
        paneDragOffsetX = mx - current.x;
        paneDragOffsetY = my - current.y;
        draggingLeftSplitter = false;
        draggingRightSplitter = false;
    };
    auto finishPaneDrag = [&]()
    {
        if (draggingPane == PaneId::None) return;
        SDL_Rect* rc = paneRectPtr(draggingPane);
        bool* floating = paneFloatingPtr(draggingPane);
        if (!rc || !floating)
        {
            draggingPane = PaneId::None;
            return;
        }

        const SDL_Rect home = paneHomeRect(draggingPane);
        const int rcCx = rc->x + rc->w / 2;
        const int rcCy = rc->y + rc->h / 2;
        const int homeCx = home.x + home.w / 2;
        const int homeCy = home.y + home.h / 2;
        if (std::abs(rcCx - homeCx) <= 72 && std::abs(rcCy - homeCy) <= 72)
        {
            *floating = false;
            *rc = SDL_Rect{ 0, 0, 0, 0 };
            draggingPane = PaneId::None;
            return;
        }

        SDL_Rect ws{ layout.outer.x + 12, layout.outer.y + 60, std::max(220, layout.outer.w - 24), std::max(160, layout.outer.h - 108) };
        const int cx = rc->x + rc->w / 2;
        const int cy = rc->y + rc->h / 2;
        const int wsCx = ws.x + ws.w / 2;
        const int wsCy = ws.y + ws.h / 2;
        const int edgeSnap = 72;
        const int centerSnap = 84;

        if (std::abs(cx - ws.x) <= edgeSnap) rc->x = ws.x;
        else if (std::abs(cx - (ws.x + ws.w)) <= edgeSnap) rc->x = ws.x + ws.w - rc->w;
        else if (std::abs(cx - wsCx) <= centerSnap) rc->x = ws.x + (ws.w - rc->w) / 2;

        if (std::abs(cy - ws.y) <= edgeSnap) rc->y = ws.y;
        else if (std::abs(cy - (ws.y + ws.h)) <= edgeSnap) rc->y = ws.y + ws.h - rc->h;
        else if (std::abs(cy - wsCy) <= centerSnap) rc->y = ws.y + (ws.h - rc->h) / 2;

        clampPaneRect(*rc);
        draggingPane = PaneId::None;
    };

    auto centerHeaderConsumesClick = [&]() -> bool
    {
        if (!inRect(layout.centerHeader, mx, my)) return false;
        if (activeTab == Tab::Overview || activeTab == Tab::Script || activeTab == Tab::Package)
        {
            for (int i = 0; i < 5; ++i)
                if (inRect(topActionRect(layout.center, i, 5), mx, my)) return true;
        }
        else if (activeTab == Tab::Stamp)
        {
            if (inRect(SDL_Rect{ layout.center.x + 12, layout.center.y + 12, 120, 28 }, mx, my)) return true;
            if (inRect(SDL_Rect{ layout.center.x + 140, layout.center.y + 12, 120, 28 }, mx, my)) return true;
        }
        else if (activeTab == Tab::TestCanvas)
        {
            if (inRect(SDL_Rect{ layout.center.x + 12, layout.center.y + 12, 100, 28 }, mx, my)) return true;
            if (inRect(SDL_Rect{ layout.center.x + 120, layout.center.y + 12, 130, 28 }, mx, my)) return true;
        }
        return false;
    };

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        if (!leftPaneFloating && !centerPaneFloating && !rightPaneFloating)
        {
            if (inRect(layout.leftSplitter, mx, my)) { draggingLeftSplitter = true; return; }
            if (inRect(layout.rightSplitter, mx, my)) { draggingRightSplitter = true; return; }
        }

        if (inRect(layout.leftHeader, mx, my)) { startPaneDrag(PaneId::Left); return; }
        if (inRect(layout.rightHeader, mx, my)) { startPaneDrag(PaneId::Right); return; }
        if (inRect(layout.centerHeader, mx, my) && !centerHeaderConsumesClick()) { startPaneDrag(PaneId::Center); return; }
    }
    if (e.type == SDL_MOUSEMOTION)
    {
        const int usableW = std::max(1, layout.outer.w - 24);
        if (draggingPane != PaneId::None)
        {
            if (SDL_Rect* rc = paneRectPtr(draggingPane))
            {
                rc->x = mx - paneDragOffsetX;
                rc->y = my - paneDragOffsetY;
                clampPaneRect(*rc);
            }
            return;
        }
        if (draggingLeftSplitter)
        {
            leftPaneRatio = std::clamp((float)(mx - (layout.outer.x + 12)) / (float)usableW, 0.16f, 0.30f);
            return;
        }
        if (draggingRightSplitter)
        {
            rightPaneRatio = std::clamp((float)((layout.outer.x + layout.outer.w - 12) - mx) / (float)usableW, 0.18f, 0.30f);
            return;
        }
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
    {
        draggingLeftSplitter = false;
        draggingRightSplitter = false;
        if (draggingPane != PaneId::None)
        {
            finishPaneDrag();
            return;
        }
    }

    if (modal != Modal::None)
    {
        SDL_Rect modalRc{ layout.outer.x + layout.outer.w / 2 - 240, layout.outer.y + layout.outer.h / 2 - 170, 480, 340 };
        SDL_Rect okR{ modalRc.x + modalRc.w - 224, modalRc.y + modalRc.h - 40, 96, 28 };
        SDL_Rect cancelR{ modalRc.x + modalRc.w - 116, modalRc.y + modalRc.h - 40, 96, 28 };

        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
        {
            modal = Modal::None;
            modalText.clear();
            return;
        }

        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            if (modal == Modal::Validation)
            {
                if (inRect(okR, mx, my)) { modal = Modal::None; modalText.clear(); }
                return;
            }
            if (modal == Modal::ImportStamp)
            {
                SDL_Rect pickR{ modalRc.x + 16, modalRc.y + 48, 140, 28 };
                SDL_Rect alphaR{ modalRc.x + 16, modalRc.y + 220, 96, 28 };
                SDL_Rect lumaR{ modalRc.x + 120, modalRc.y + 220, 110, 28 };
                SDL_Rect darkR{ modalRc.x + 238, modalRc.y + 220, 110, 28 };
                SDL_Rect invertR{ modalRc.x + 16, modalRc.y + 258, 140, 28 };
                if (inRect(pickR, mx, my)) { chooseImportStamp(); return; }
                if (inRect(alphaR, mx, my)) { pendingImportMaskSource = MaskSource::Alpha; return; }
                if (inRect(lumaR, mx, my)) { pendingImportMaskSource = MaskSource::Luminance; return; }
                if (inRect(darkR, mx, my)) { pendingImportMaskSource = MaskSource::Darkness; return; }
                if (inRect(invertR, mx, my)) { pendingImportInvert = !pendingImportInvert; return; }
                if (inRect(okR, mx, my)) { commitImportStamp(); return; }
                if (inRect(cancelR, mx, my)) { modal = Modal::None; modalText.clear(); return; }
                return;
            }
            if (modal == Modal::ExportBrush)
            {
                if (inRect(okR, mx, my)) { exportBrushInteractive(app); modal = Modal::None; return; }
                if (inRect(cancelR, mx, my)) { modal = Modal::None; return; }
                return;
            }
            if (modal == Modal::InstallBrush)
            {
                if (inRect(okR, mx, my)) { installBrushInteractive(app); modal = Modal::None; return; }
                if (inRect(cancelR, mx, my)) { modal = Modal::None; return; }
                return;
            }
            if (modal == Modal::NewBrush)
            {
                SDL_Rect typeR{ modalRc.x + 16, modalRc.y + 54, 180, 28 };
                SDL_Rect genR{ modalRc.x + 16, modalRc.y + 92, 180, 28 };
                if (inRect(typeR, mx, my))
                {
                    int v = (int)pendingNewType + 1;
                    if (v > (int)BrushType::ScriptedRaster) v = 0;
                    pendingNewType = (BrushType)v;
                    return;
                }
                if (inRect(genR, mx, my))
                {
                    int v = (int)pendingNewGenerator + 1;
                    if (v > (int)GeneratorType::NoiseBlob) v = 0;
                    pendingNewGenerator = (GeneratorType)v;
                    return;
                }
                if (inRect(okR, mx, my)) { createNewBrush(app); return; }
                if (inRect(cancelR, mx, my)) { modal = Modal::None; return; }
                return;
            }
        }
        return;
    }

    if (overviewField != OverviewField::None)
    {
        handleOverviewTextInput(e);
        if (e.type == SDL_TEXTINPUT || e.type == SDL_KEYDOWN) return;
    }
    if (scriptFocused)
    {
        handleScriptTextInput(e);
        if (e.type == SDL_TEXTINPUT || e.type == SDL_KEYDOWN) return;
    }

    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
    {
        app.closeBrushCreatorWorkspace();
        return;
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
    {
        if (inRect(layout.backBtn, mx, my)) { app.closeBrushCreatorWorkspace(); return; }
        if (inRect(layout.saveBtn, mx, my)) { saveProjectInteractive(app); return; }
        if (inRect(layout.validateBtn, mx, my)) { refreshDerived(); modal = Modal::Validation; modalText = projectData.package.validation.summary(); return; }
        if (inRect(layout.exportBtn, mx, my)) { modal = Modal::ExportBrush; return; }
        if (inRect(layout.closeBtn, mx, my)) { app.closeBrushCreatorWorkspace(); return; }

        for (int i = 0; i < (int)Tab::Count; ++i)
        {
            if (inRect(layout.tabs[(size_t)i], mx, my))
            {
                activeTab = (Tab)i;
                setOverviewFieldFocus(OverviewField::None);
                scriptFocused = (activeTab == Tab::Script);
                return;
            }
        }

        SDL_Rect previewModeBtn{ layout.right.x + 12, layout.right.y + layout.right.h - 34, layout.right.w - 24, 24 };
        if (inRect(previewModeBtn, mx, my)) { cyclePreviewMode(); return; }

        if (activeTab == Tab::Overview)
        {
            for (int i = 0; i < 6; ++i)
            {
                if (inRect(layout.overviewFields[(size_t)i], mx, my))
                {
                    setOverviewFieldFocus((OverviewField)(i + 1));
                    return;
                }
            }
            SDL_Rect newR = topActionRect(layout.center, 0, 5);
            SDL_Rect saveR = topActionRect(layout.center, 1, 5);
            SDL_Rect validateR = topActionRect(layout.center, 2, 5);
            SDL_Rect exportR = topActionRect(layout.center, 3, 5);
            SDL_Rect installR = topActionRect(layout.center, 4, 5);
            if (inRect(newR, mx, my)) { modal = Modal::NewBrush; return; }
            if (inRect(saveR, mx, my)) { saveProjectInteractive(app); return; }
            if (inRect(validateR, mx, my)) { refreshDerived(); modal = Modal::Validation; modalText = projectData.package.validation.summary(); return; }
            if (inRect(exportR, mx, my)) { modal = Modal::ExportBrush; return; }
            if (inRect(installR, mx, my)) { modal = Modal::InstallBrush; return; }
        }
        else if (activeTab == Tab::Stamp)
        {
            SDL_Rect importR{ layout.center.x + 12, layout.center.y + 12, 120, 28 };
            SDL_Rect clearR{ layout.center.x + 140, layout.center.y + 12, 120, 28 };
            if (inRect(importR, mx, my)) { modal = Modal::ImportStamp; return; }
            if (inRect(clearR, mx, my)) { projectData.package.stamp = strova::brush::BrushStamp{}; markDirty("Stamp cleared"); return; }
            for (int i = 0; i < 9; ++i)
            {
                SDL_Rect gr{ layout.center.x + 12 + (i % 3) * 128, layout.center.y + 54 + (i / 3) * 34, 120, 28 };
                if (inRect(gr, mx, my))
                {
                    projectData.generator = (GeneratorType)i;
                    strova::brush::applyFamilyPreset(projectData.package, projectData.generator);
                    projectData.package.manifest.type = BrushType::Procedural;
                    markDirty("Family preset applied");
                    return;
                }
            }

            SDL_Rect maskModeR{ layout.right.x + 12, layout.right.y + 222, layout.right.w - 24, 24 };
            SDL_Rect invertR{ layout.right.x + 12, layout.right.y + 252, layout.right.w - 24, 24 };
            SDL_Rect threshMinus{ layout.right.x + layout.right.w - 78, layout.right.y + 304, 28, 24 };
            SDL_Rect threshPlus{ layout.right.x + layout.right.w - 42, layout.right.y + 304, 28, 24 };
            SDL_Rect clampMinus{ layout.right.x + layout.right.w - 78, layout.right.y + 338, 28, 24 };
            SDL_Rect clampPlus{ layout.right.x + layout.right.w - 42, layout.right.y + 338, 28, 24 };
            SDL_Rect edgeMinus{ layout.right.x + layout.right.w - 78, layout.right.y + 372, 28, 24 };
            SDL_Rect edgePlus{ layout.right.x + layout.right.w - 42, layout.right.y + 372, 28, 24 };
            if (inRect(maskModeR, mx, my)) { cycleMaskSource(projectData.package.manifest.color.maskSource, 1); markDirty("Mask source updated"); return; }
            if (inRect(invertR, mx, my)) { projectData.package.manifest.color.invertMask = !projectData.package.manifest.color.invertMask; markDirty("Mask invert toggled"); return; }
            if (inRect(threshMinus, mx, my)) { projectData.package.stamp.threshold = std::max(0.0f, projectData.package.stamp.threshold - 0.05f); markDirty("Threshold updated"); return; }
            if (inRect(threshPlus, mx, my)) { projectData.package.stamp.threshold = std::min(1.0f, projectData.package.stamp.threshold + 0.05f); markDirty("Threshold updated"); return; }
            if (inRect(clampMinus, mx, my)) { projectData.package.stamp.levelsClamp = std::max(0.05f, projectData.package.stamp.levelsClamp - 0.05f); markDirty("Clamp updated"); return; }
            if (inRect(clampPlus, mx, my)) { projectData.package.stamp.levelsClamp = std::min(1.0f, projectData.package.stamp.levelsClamp + 0.05f); markDirty("Clamp updated"); return; }
            if (inRect(edgeMinus, mx, my)) { projectData.package.stamp.edgeBoost = std::max(0.0f, projectData.package.stamp.edgeBoost - 0.05f); markDirty("Edge boost updated"); return; }
            if (inRect(edgePlus, mx, my)) { projectData.package.stamp.edgeBoost = std::min(1.0f, projectData.package.stamp.edgeBoost + 0.05f); markDirty("Edge boost updated"); return; }
        }
        else if (activeTab == Tab::Behavior)
        {
            auto& p = projectData.package.manifest.params;
            SDL_Rect rotR{ layout.right.x + 12, layout.right.y + 222, layout.right.w - 24, 24 };
            SDL_Rect blendR{ layout.right.x + 12, layout.right.y + 252, layout.right.w - 24, 24 };
            SDL_Rect accR{ layout.right.x + 12, layout.right.y + 282, layout.right.w - 24, 24 };
            if (inRect(rotR, mx, my)) { cycleRotationMode(1); return; }
            if (inRect(blendR, mx, my)) { cycleBlendMode(1); return; }
            if (inRect(accR, mx, my)) { p.accumulate = !p.accumulate; markDirty("Accumulate toggled"); return; }
            struct Row { const char* name; float* value; float step; float minv; float maxv; } rows[] = {
                { "Default Size", &p.sizeDefault, 2.0f, 1.0f, 512.0f },
                { "Spacing", &p.spacing, 0.02f, 0.01f, 1.0f },
                { "Opacity", &p.opacity, 0.05f, 0.01f, 1.0f },
                { "Flow", &p.flow, 0.05f, 0.01f, 1.0f },
                { "Scatter", &p.scatter, 0.05f, 0.0f, 1.0f },
                { "Smoothing", &p.smoothing, 0.05f, 0.0f, 1.0f }
            };
            for (int i = 0; i < 6; ++i)
            {
                SDL_Rect minusR{ layout.center.x + layout.center.w - 78, layout.center.y + 84 + i * 40, 28, 24 };
                SDL_Rect plusR{ layout.center.x + layout.center.w - 42, layout.center.y + 84 + i * 40, 28, 24 };
                if (inRect(minusR, mx, my)) { *rows[i].value = std::max(rows[i].minv, *rows[i].value - rows[i].step); markDirty("Behavior updated"); return; }
                if (inRect(plusR, mx, my)) { *rows[i].value = std::min(rows[i].maxv, *rows[i].value + rows[i].step); markDirty("Behavior updated"); return; }
            }
        }
        else if (activeTab == Tab::Color)
        {
            auto& c = projectData.package.manifest.color;
            SDL_Rect userR{ layout.right.x + 12, layout.right.y + 222, layout.right.w - 24, 24 };
            SDL_Rect gradR{ layout.right.x + 12, layout.right.y + 252, layout.right.w - 24, 24 };
            SDL_Rect gradModeR{ layout.right.x + 12, layout.right.y + 282, layout.right.w - 24, 24 };
            SDL_Rect fixedR{ layout.right.x + 12, layout.right.y + 312, layout.right.w - 24, 24 };
            SDL_Rect previewTintR{ layout.right.x + 12, layout.right.y + 342, layout.right.w - 24, 24 };
            if (inRect(userR, mx, my))
            {
                c.supportsUserColor = !c.supportsUserColor;
                if (!c.supportsUserColor && c.fixedColor.a == 0) c.fixedColor = testColor;
                markDirty("User color support updated");
                return;
            }
            if (inRect(gradR, mx, my))
            {
                c.supportsGradient = !c.supportsGradient;
                if (!c.supportsGradient) c.gradientMode = GradientMode::None;
                markDirty("Gradient support updated");
                return;
            }
            if (inRect(gradModeR, mx, my))
            {
                if (!c.supportsGradient) { status = "Gradient unavailable: brush family does not support package gradients"; return; }
                cycleGradientMode(1); return;
            }
            if (inRect(fixedR, mx, my)) { c.fixedColor = testColor; markDirty("Fixed color sampled from test color"); return; }
            if (inRect(previewTintR, mx, my)) { c.previewTint = testColor; markDirty("Preview tint sampled from test color"); return; }
            for (int i = 0; i < 4; ++i)
            {
                SDL_Rect sw{ layout.center.x + 12 + i * 44, layout.center.y + 96, 32, 32 };
                if (inRect(sw, mx, my))
                {
                    c.stops[(size_t)i].color = swatchColor(i + 1);
                    c.stops[(size_t)i].pos = (float)i / 3.0f;
                    markDirty("Gradient stop updated");
                    return;
                }
            }
        }
        else if (activeTab == Tab::Script)
        {
            SDL_Rect scriptedR = topActionRect(layout.center, 0, 5);
            SDL_Rect loadR = topActionRect(layout.center, 1, 5);
            SDL_Rect saveR = topActionRect(layout.center, 2, 5);
            SDL_Rect templateR = topActionRect(layout.center, 3, 5);
            SDL_Rect clearR = topActionRect(layout.center, 4, 5);
            SDL_Rect editorR{ layout.center.x + 12, layout.center.y + 84, layout.center.w - 24, layout.center.h - 96 };
            if (inRect(scriptedR, mx, my))
            {
                projectData.package.manifest.type = (projectData.package.manifest.type == BrushType::ScriptedRaster) ? BrushType::RasterStamp : BrushType::ScriptedRaster;
                ensureDefaultScript();
                markDirty("Brush type updated");
                return;
            }
            if (inRect(loadR, mx, my))
            {
                std::string path;
                if (platform::pickOpenLuaFile(path, projectData.projectPath))
                {
                    std::ifstream in(path, std::ios::binary);
                    if (in)
                    {
                        projectData.package.scriptSource.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        scriptCursor = projectData.package.scriptSource.size();
                        markDirty("Lua loaded");
                    }
                    else status = "Failed to load Lua";
                }
                return;
            }
            if (inRect(saveR, mx, my))
            {
                std::string path;
                const std::string defPath = projectData.projectPath.empty() ? std::string() : (std::filesystem::path(projectData.projectPath).parent_path() / "behavior.lua").string();
                if (platform::pickSaveLuaFile(path, defPath))
                {
                    std::ofstream out(path, std::ios::binary | std::ios::trunc);
                    out << projectData.package.scriptSource;
                    status = out ? "behavior.lua saved" : "Failed to save behavior.lua";
                }
                return;
            }
            if (inRect(templateR, mx, my))
            {
                ensureDefaultScript();
                projectData.package.scriptSource += "\n-- template\nspacing_scale=1.0\nscatter_boost=0.1\nalpha_scale=1.0\n";
                scriptCursor = projectData.package.scriptSource.size();
                markDirty("Lua template inserted");
                return;
            }
            if (inRect(clearR, mx, my))
            {
                projectData.package.scriptSource.clear();
                scriptCursor = 0;
                ensureDefaultScript();
                markDirty("Script cleared");
                return;
            }
            if (inRect(editorR, mx, my))
            {
                scriptFocused = true;
                setOverviewFieldFocus(OverviewField::None);
                return;
            }
        }
        else if (activeTab == Tab::TestCanvas)
        {
            SDL_Rect clearR{ layout.center.x + 12, layout.center.y + 12, 100, 28 };
            SDL_Rect replayR{ layout.center.x + 120, layout.center.y + 12, 130, 28 };
            SDL_Rect bgR{ layout.right.x + 12, layout.right.y + 222, layout.right.w - 24, 24 };
            SDL_Rect gradR{ layout.right.x + 12, layout.right.y + 252, layout.right.w - 24, 24 };
            SDL_Rect pressureR{ layout.right.x + 12, layout.right.y + 282, layout.right.w - 24, 24 };
            if (inRect(clearR, mx, my)) { resetSandbox(); status = "Test canvas cleared"; return; }
            if (inRect(replayR, mx, my)) { addReplayStroke(layout.canvas); status = "Sample stroke replayed"; return; }
            if (inRect(bgR, mx, my)) { testBgMode = (testBgMode + 1) % 3; return; }
            if (inRect(gradR, mx, my))
            {
                if (!projectData.package.manifest.color.supportsGradient) { status = "Gradient unavailable: brush family does not support package gradients"; return; }
                testUseGradient = !testUseGradient; return;
            }
            if (inRect(pressureR, mx, my)) { testPressure = !testPressure; return; }
            for (int i = 0; i < 6; ++i)
            {
                SDL_Rect sw{ layout.right.x + 12 + (i % 3) * 40, layout.right.y + 324 + (i / 3) * 40, 28, 28 };
                if (inRect(sw, mx, my)) { testColor = swatchColor(i); status = "Test color updated"; return; }
            }
            if (inRect(layout.canvas, mx, my))
            {
                liveTestStroke = makeSandboxStroke();
                appendSandboxPoint(layout.canvas, liveTestStroke, mx, my);
                testDrawing = true;
                return;
            }
        }
        else if (activeTab == Tab::Package)
        {
            SDL_Rect saveR = topActionRect(layout.center, 1, 5);
            SDL_Rect validateR = topActionRect(layout.center, 2, 5);
            SDL_Rect exportR = topActionRect(layout.center, 3, 5);
            SDL_Rect installR = topActionRect(layout.center, 4, 5);
            if (inRect(saveR, mx, my)) { saveProjectInteractive(app); return; }
            if (inRect(validateR, mx, my)) { refreshDerived(); modal = Modal::Validation; modalText = projectData.package.validation.summary(); return; }
            if (inRect(exportR, mx, my)) { modal = Modal::ExportBrush; return; }
            if (inRect(installR, mx, my)) { modal = Modal::InstallBrush; return; }
        }
    }
    else if (e.type == SDL_MOUSEMOTION && testDrawing && activeTab == Tab::TestCanvas)
    {
        if (inRect(layout.canvas, mx, my)) appendSandboxPoint(layout.canvas, liveTestStroke, mx, my);
    }
    else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT && activeTab == Tab::TestCanvas)
    {
        if (testDrawing)
        {
            if (!liveTestStroke.points.empty()) testStrokes.push_back(liveTestStroke);
            liveTestStroke = Stroke{};
            testDrawing = false;
            return;
        }
    }
}

void BrushCreatorScreen::render(App& app, int w, int h)
{
    SDL_Renderer* r = app.getRenderer();
    TTF_Font* font = app.getUiFont();
    const Layout layout = makeLayout(w, h, leftPaneRatio, rightPaneRatio,
        leftPaneFloating, leftPaneFloatRect,
        centerPaneFloating, centerPaneFloatRect,
        rightPaneFloating, rightPaneFloatRect);

    fillRect(r, SDL_Rect{ 0, 0, w, h }, SDL_Color{ 9, 12, 18, 255 });
    fillRect(r, layout.outer, SDL_Color{ 10, 14, 20, 246 });
    drawRect(r, layout.outer, SDL_Color{ 110, 122, 152, 230 });
    fillRect(r, layout.header, SDL_Color{ 14, 18, 26, 250 });
    fillRect(r, layout.left, COL_BG_PANEL);
    fillRect(r, layout.center, COL_BG_PANEL);
    fillRect(r, layout.right, COL_BG_PANEL);
    fillRect(r, layout.leftHeader, leftPaneFloating ? SDL_Color{ 28, 40, 62, 255 } : SDL_Color{ 18, 24, 34, 255 });
    fillRect(r, layout.centerHeader, centerPaneFloating ? SDL_Color{ 28, 40, 62, 255 } : SDL_Color{ 18, 24, 34, 255 });
    fillRect(r, layout.rightHeader, rightPaneFloating ? SDL_Color{ 28, 40, 62, 255 } : SDL_Color{ 18, 24, 34, 255 });
    fillRect(r, layout.status, COL_BG_PANEL2);
    drawRect(r, layout.left, COL_BORDER_SOFT);
    drawRect(r, layout.center, COL_BORDER_SOFT);
    drawRect(r, layout.right, COL_BORDER_SOFT);
    drawRect(r, layout.leftHeader, SDL_Color{ 92, 104, 132, 220 });
    drawRect(r, layout.centerHeader, SDL_Color{ 92, 104, 132, 220 });
    drawRect(r, layout.rightHeader, SDL_Color{ 92, 104, 132, 220 });
    drawRect(r, layout.status, COL_BORDER_SOFT);
    if (layout.leftSplitter.w > 0)
    {
        fillRect(r, layout.leftSplitter, SDL_Color{ 36, 44, 58, 255 });
        drawRect(r, layout.leftSplitter, SDL_Color{ 86, 98, 122, 220 });
    }
    if (layout.rightSplitter.w > 0)
    {
        fillRect(r, layout.rightSplitter, SDL_Color{ 36, 44, 58, 255 });
        drawRect(r, layout.rightSplitter, SDL_Color{ 86, 98, 122, 220 });
    }

    drawButton(r, font, layout.backBtn, "Back");
    drawButton(r, font, layout.saveBtn, "Save");
    drawButton(r, font, layout.validateBtn, "Validate");
    drawButton(r, font, layout.exportBtn, "Export");
    drawButton(r, font, layout.closeBtn, "X");

    drawText(r, font, "Brush Creator", layout.header.x + 110, layout.header.y + 12, COL_TEXT_MAIN);
    drawTextFit(r, font, projectData.package.manifest.name, SDL_Rect{ layout.header.x + 252, layout.header.y + 12, std::max(60, layout.header.w - 680), 20 }, COL_TEXT_DIM);
    drawText(r, font, dirty ? "Dirty" : "Saved", layout.header.x + layout.header.w - 420, layout.header.y + 12,
        dirty ? SDL_Color{ 255, 196, 96, 255 } : SDL_Color{ 118, 218, 154, 255 });

    drawTextFit(r, font, leftPaneFloating ? "Floating" : "Drag Header", SDL_Rect{ layout.leftHeader.x + layout.leftHeader.w - 112, layout.leftHeader.y + 4, 104, 16 }, SDL_Color{ 118, 132, 160, 255 });
    drawTextFit(r, font, centerPaneFloating ? "Floating" : "Drag Header", SDL_Rect{ layout.centerHeader.x + layout.centerHeader.w - 112, layout.centerHeader.y + 4, 104, 16 }, SDL_Color{ 118, 132, 160, 255 });
    drawTextFit(r, font, rightPaneFloating ? "Floating" : "Drag Header", SDL_Rect{ layout.rightHeader.x + layout.rightHeader.w - 112, layout.rightHeader.y + 4, 104, 16 }, SDL_Color{ 118, 132, 160, 255 });
    drawText(r, font, "Brush Project", layout.left.x + 10, layout.left.y + 10, COL_TEXT_MAIN);
    SDL_Rect thumb{ layout.left.x + 10, layout.left.y + 34, 72, 72 };
    drawPixels(r, thumb, projectData.package.preview.width, projectData.package.preview.height, projectData.package.preview.rgba, false, bgModeColor(0));
    drawTextFit(r, font, projectData.package.manifest.name, SDL_Rect{ layout.left.x + 92, layout.left.y + 38, std::max(40, layout.left.w - 104), 18 }, COL_TEXT_DIM);
    drawTextFit(r, font, "Id: " + projectData.package.manifest.id, SDL_Rect{ layout.left.x + 92, layout.left.y + 60, std::max(40, layout.left.w - 104), 18 }, COL_TEXT_DIM);
    drawTextFit(r, font, "Type: " + std::string(strova::brush::brushTypeName(projectData.package.manifest.type)), SDL_Rect{ layout.left.x + 92, layout.left.y + 82, std::max(40, layout.left.w - 104), 18 }, COL_TEXT_DIM);
    drawText(r, font, projectData.package.validation.ok ? "Validation: OK" : "Validation: Needs fixes", layout.left.x + 10, layout.left.y + 122,
        projectData.package.validation.ok ? SDL_Color{ 118,218,154,255 } : SDL_Color{ 255,148,120,255 });
    drawText(r, font, "Tabs", layout.left.x + 10, layout.left.y + 154, COL_TEXT_MAIN);
    for (int i = 0; i < (int)Tab::Count; ++i)
        drawButton(r, font, layout.tabs[(size_t)i], tabName((Tab)i), (int)activeTab == i);

    drawText(r, font, "Preview", layout.right.x + 10, layout.right.y + 10, COL_TEXT_MAIN);
    std::vector<std::uint8_t> previewRgba;
    int previewW = projectData.package.stamp.width;
    int previewH = projectData.package.stamp.height;
    bool checker = false;
    SDL_Color bg = bgModeColor(inspectorBgMode);
    switch (previewMode)
    {
    case PreviewMode::Mask:
        previewRgba = stampMaskToRgba(projectData.package.stamp, 1.0f);
        break;
    case PreviewMode::FinalDab:
        previewRgba = projectData.package.preview.rgba;
        previewW = projectData.package.preview.width;
        previewH = projectData.package.preview.height;
        checker = false;
        bg = bgModeColor(0);
        break;
    case PreviewMode::RawSource:
        previewRgba = projectData.package.stamp.rgba;
        checker = true;
        break;
    case PreviewMode::Strength:
        previewRgba = stampMaskToRgba(projectData.package.stamp, 0.7f);
        bg = SDL_Color{ 10, 10, 10, 255 };
        break;
    }
    drawPixels(r, layout.preview, previewW, previewH, previewRgba, checker, bg);
    drawText(r, font, "Package-derived preview. Test canvas swatches and background do not change this image.", layout.right.x + 12, layout.right.y + 186, COL_TEXT_DIM);
    drawText(r, font, std::string("Effective Color Mode: ") + strova::brush::effectiveColorModeName(projectData.package.manifest.color), layout.right.x + 12, layout.right.y + 204, COL_TEXT_DIM);
    drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + layout.right.h - 34, layout.right.w - 24, 24 },
        std::string("Preview: ") + previewModeLabel(previewMode));

    if (activeTab == Tab::Overview)
    {
        drawText(r, font, "Overview", layout.center.x + 12, layout.center.y + 12, COL_TEXT_MAIN);
        drawButton(r, font, topActionRect(layout.center, 0, 5), "New Brush...");
        drawButton(r, font, topActionRect(layout.center, 1, 5), "Save Project");
        drawButton(r, font, topActionRect(layout.center, 2, 5), "Validate");
        drawButton(r, font, topActionRect(layout.center, 3, 5), "Export...");
        drawButton(r, font, topActionRect(layout.center, 4, 5), "Install...");

        drawField(r, font, layout.overviewFields[0], "Brush Name", projectData.package.manifest.name, overviewField == OverviewField::Name);
        drawField(r, font, layout.overviewFields[1], "Internal Id", projectData.package.manifest.id, overviewField == OverviewField::InternalId);
        drawField(r, font, layout.overviewFields[2], "Author", projectData.package.manifest.author, overviewField == OverviewField::Author);
        drawField(r, font, layout.overviewFields[3], "Description", projectData.package.manifest.description, overviewField == OverviewField::Description);
        drawField(r, font, layout.overviewFields[4], "Category", projectData.package.manifest.category, overviewField == OverviewField::Category);
        drawField(r, font, layout.overviewFields[5], "Tags", projectData.package.manifest.tags, overviewField == OverviewField::Tags);
        drawText(r, font, "Click any field to edit. This workspace is separate from the editor and stays focused on brush authoring.", layout.center.x + 12, layout.center.y + 340, COL_TEXT_DIM);
        drawText(r, font, projectData.package.validation.summary(), layout.center.x + 12, layout.center.y + 372,
            projectData.package.validation.ok ? SDL_Color{ 118,218,154,255 } : SDL_Color{ 255,148,120,255 });

        drawText(r, font, "Quick Settings", layout.right.x + 12, layout.right.y + 220, COL_TEXT_MAIN);
        drawText(r, font, std::string("Mask: ") + strova::brush::maskSourceName(projectData.package.manifest.color.maskSource), layout.right.x + 12, layout.right.y + 248, COL_TEXT_DIM);
        drawText(r, font, std::string("Blend: ") + strova::brush::blendModeName(projectData.package.manifest.params.blendMode), layout.right.x + 12, layout.right.y + 270, COL_TEXT_DIM);
        drawText(r, font, std::string("Rotation: ") + strova::brush::rotationModeName(projectData.package.manifest.params.rotationMode), layout.right.x + 12, layout.right.y + 292, COL_TEXT_DIM);
        drawText(r, font, std::string("Save: ") + (projectData.projectPath.empty() ? "Unsaved .sbrushproj" : projectData.projectPath), layout.right.x + 12, layout.right.y + 326, COL_TEXT_DIM);
    }
    else if (activeTab == Tab::Stamp)
    {
        drawText(r, font, "Stamp", layout.center.x + 12, layout.center.y + 12, COL_TEXT_MAIN);
        drawButton(r, font, SDL_Rect{ layout.center.x + 12, layout.center.y + 12, 120, 28 }, "Import PNG...");
        drawButton(r, font, SDL_Rect{ layout.center.x + 140, layout.center.y + 12, 120, 28 }, "Clear Stamp");
        drawText(r, font, "Generators", layout.center.x + 12, layout.center.y + 56, COL_TEXT_MAIN);
        static const char* genNames[9] = { "Hard Circle", "Soft Circle", "Square", "Soft Square", "Speckle", "Grain Disk", "Oval Tip", "Chalk Patch", "Noise Blob" };
        for (int i = 0; i < 9; ++i)
        {
            SDL_Rect gr{ layout.center.x + 12 + (i % 3) * 128, layout.center.y + 54 + (i / 3) * 34, 120, 28 };
            drawButton(r, font, gr, genNames[i], (int)projectData.generator == i);
        }
        drawText(r, font, "Darkness mode is the default authoring path here: darker = more paint, lighter = more transparent, transparent = none.", layout.center.x + 12, layout.center.y + 184, COL_TEXT_DIM);
        SDL_Rect sourceInfo{ layout.center.x + 12, layout.center.y + 220, layout.center.w - 24, 170 };
        fillRect(r, sourceInfo, COL_BG_PANEL2);
        drawRect(r, sourceInfo, COL_BORDER_SOFT);
        drawText(r, font, pendingImportPath.empty() ? "Source" : ("Source: " + pendingImportPath), sourceInfo.x + 10, sourceInfo.y + 10, COL_TEXT_MAIN);
        drawText(r, font, projectData.package.stamp.empty() ? "No stamp loaded yet." : ("Stamp: " + std::to_string(projectData.package.stamp.width) + " x " + std::to_string(projectData.package.stamp.height)), sourceInfo.x + 10, sourceInfo.y + 36, COL_TEXT_DIM);
        drawText(r, font, "Mask truth stays neutral grayscale in the preview panel. Final paint preview is separate from mask truth.", sourceInfo.x + 10, sourceInfo.y + 62, COL_TEXT_DIM);
        drawText(r, font, "Use the right bar to change mask mode, invert, threshold, clamp, and edge boost.", sourceInfo.x + 10, sourceInfo.y + 88, COL_TEXT_DIM);

        drawText(r, font, "Stamp Settings", layout.right.x + 12, layout.right.y + 220, COL_TEXT_MAIN);
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 222, layout.right.w - 24, 24 }, std::string("Mask Source: ") + strova::brush::maskSourceName(projectData.package.manifest.color.maskSource));
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 252, layout.right.w - 24, 24 }, projectData.package.manifest.color.invertMask ? "Invert Mask: On" : "Invert Mask: Off", projectData.package.manifest.color.invertMask);
        drawAdjustRow(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 308, layout.right.w - 24, 24 }, "Threshold", std::to_string((int)std::lround(projectData.package.stamp.threshold * 100.0f)) + "%");
        drawAdjustRow(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 342, layout.right.w - 24, 24 }, "Clamp", std::to_string((int)std::lround(projectData.package.stamp.levelsClamp * 100.0f)) + "%");
        drawAdjustRow(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 376, layout.right.w - 24, 24 }, "Edge Boost", std::to_string((int)std::lround(projectData.package.stamp.edgeBoost * 100.0f)) + "%");
    }
    else if (activeTab == Tab::Behavior)
    {
        const auto& p = projectData.package.manifest.params;
        drawText(r, font, "Behavior", layout.center.x + 12, layout.center.y + 12, COL_TEXT_MAIN);
        drawText(r, font, "These values feed the same brush runtime path used by the real Brush Tool.", layout.center.x + 12, layout.center.y + 44, COL_TEXT_DIM);
        auto valueText = [&](float value, bool pct) -> std::string
        {
            if (pct) return std::to_string((int)std::lround(value * 100.0f)) + "%";
            return std::to_string((int)std::lround(value));
        };
        drawAdjustRow(r, font, SDL_Rect{ layout.center.x + 12, layout.center.y + 86, layout.center.w - 24, 24 }, "Default Size", valueText(p.sizeDefault, false));
        drawAdjustRow(r, font, SDL_Rect{ layout.center.x + 12, layout.center.y + 126, layout.center.w - 24, 24 }, "Spacing", valueText(p.spacing, true));
        drawAdjustRow(r, font, SDL_Rect{ layout.center.x + 12, layout.center.y + 166, layout.center.w - 24, 24 }, "Opacity", valueText(p.opacity, true));
        drawAdjustRow(r, font, SDL_Rect{ layout.center.x + 12, layout.center.y + 206, layout.center.w - 24, 24 }, "Flow", valueText(p.flow, true));
        drawAdjustRow(r, font, SDL_Rect{ layout.center.x + 12, layout.center.y + 246, layout.center.w - 24, 24 }, "Scatter", valueText(p.scatter, true));
        drawAdjustRow(r, font, SDL_Rect{ layout.center.x + 12, layout.center.y + 286, layout.center.w - 24, 24 }, "Smoothing", valueText(p.smoothing, true));

        drawText(r, font, "Behavior Settings", layout.right.x + 12, layout.right.y + 220, COL_TEXT_MAIN);
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 222, layout.right.w - 24, 24 }, std::string("Rotation: ") + strova::brush::rotationModeName(p.rotationMode));
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 252, layout.right.w - 24, 24 }, std::string("Blend: ") + strova::brush::blendModeName(p.blendMode));
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 282, layout.right.w - 24, 24 }, p.accumulate ? "Accumulate: On" : "Accumulate: Off", p.accumulate);
        drawText(r, font, "Pressure Size: " + valueText(p.pressureSize, true), layout.right.x + 12, layout.right.y + 320, COL_TEXT_DIM);
        drawText(r, font, "Pressure Opacity: " + valueText(p.pressureOpacity, true), layout.right.x + 12, layout.right.y + 342, COL_TEXT_DIM);
        drawText(r, font, "Pressure Flow: " + valueText(p.pressureFlow, true), layout.right.x + 12, layout.right.y + 364, COL_TEXT_DIM);
    }
    else if (activeTab == Tab::Color)
    {
        const auto& c = projectData.package.manifest.color;
        drawText(r, font, "Color", layout.center.x + 12, layout.center.y + 12, COL_TEXT_MAIN);
        drawText(r, font, std::string("Effective Color Mode: ") + strova::brush::effectiveColorModeName(c), layout.center.x + 12, layout.center.y + 44, COL_TEXT_DIM);
        drawText(r, font, "Preview Tint is package-owned. Test Color is sandbox-only.", layout.center.x + 12, layout.center.y + 64, COL_TEXT_DIM);
        for (int i = 0; i < 4; ++i)
        {
            SDL_Rect sw{ layout.center.x + 12 + i * 44, layout.center.y + 96, 32, 32 };
            fillRect(r, sw, c.stops[(size_t)i].color);
            drawRect(r, sw, COL_BORDER_SOFT);
            drawText(r, font, std::to_string(i + 1), sw.x + 10, sw.y + 7, SDL_Color{ 20,20,20,255 });
        }
        SDL_Rect ramp{ layout.center.x + 12, layout.center.y + 148, layout.center.w - 24, 70 };
        fillRect(r, ramp, SDL_Color{ 22,26,30,255 });
        drawRect(r, ramp, COL_BORDER_SOFT);
        for (int x = 0; x < ramp.w; ++x)
        {
            const float t = (float)x / (float)std::max(1, ramp.w - 1);
            SDL_Color col = strova::brush::sampleGradient(projectData.package.manifest.color, t);
            SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
            SDL_RenderDrawLine(r, ramp.x + x, ramp.y + 10, ramp.x + x, ramp.y + ramp.h - 10);
        }
        drawText(r, font, "Test Color", layout.center.x + 12, layout.center.y + 234, COL_TEXT_DIM);
        SDL_Rect testSw{ layout.center.x + 12, layout.center.y + 256, 42, 24 };
        fillRect(r, testSw, testColor);
        drawRect(r, testSw, COL_BORDER_SOFT);
        drawText(r, font, "Brush Fixed Color", layout.center.x + 76, layout.center.y + 234, COL_TEXT_DIM);
        SDL_Rect fixedSwCenter{ layout.center.x + 76, layout.center.y + 256, 42, 24 };
        fillRect(r, fixedSwCenter, c.fixedColor);
        drawRect(r, fixedSwCenter, COL_BORDER_SOFT);
        drawText(r, font, "Preview Tint", layout.center.x + 140, layout.center.y + 234, COL_TEXT_DIM);
        SDL_Rect previewSwCenter{ layout.center.x + 140, layout.center.y + 256, 42, 24 };
        fillRect(r, previewSwCenter, c.previewTint);
        drawRect(r, previewSwCenter, COL_BORDER_SOFT);
        drawText(r, font, c.supportsGradient ? "Brush Gradient" : "Brush Gradient (disabled)", layout.center.x + 204, layout.center.y + 234, COL_TEXT_DIM);

        drawText(r, font, "Color Settings", layout.right.x + 12, layout.right.y + 220, COL_TEXT_MAIN);
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 222, layout.right.w - 24, 24 }, c.supportsUserColor ? "Supports User Color: On" : "Supports User Color: Off", c.supportsUserColor);
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 252, layout.right.w - 24, 24 }, c.supportsGradient ? "Supports Gradient: On" : "Supports Gradient: Off", c.supportsGradient);
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 282, layout.right.w - 24, 24 }, std::string("Gradient Mode: ") + strova::brush::gradientModeName(c.gradientMode));
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 312, layout.right.w - 24, 24 }, "Set Fixed Color = Test Color");
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 342, layout.right.w - 24, 24 }, "Set Preview Tint = Test Color");
        drawText(r, font, c.supportsGradient ? "Gradient preview is only shown when package gradient is the real brush mode." : "Gradient unavailable: brush family does not support package gradients", layout.right.x + 12, layout.right.y + 376, COL_TEXT_DIM);
        drawText(r, font, c.supportsUserColor ? "User tinting is allowed in sandbox/runtime." : "User color locked: fixed-color brush", layout.right.x + 12, layout.right.y + 398, COL_TEXT_DIM);
    }
    else if (activeTab == Tab::Script)
    {
        drawText(r, font, "Script", layout.center.x + 12, layout.center.y + 12, COL_TEXT_MAIN);
        drawButton(r, font, topActionRect(layout.center, 0, 5),
            projectData.package.manifest.type == BrushType::ScriptedRaster ? "Scripted Raster: On" : "Enable Scripted Raster",
            projectData.package.manifest.type == BrushType::ScriptedRaster);
        drawButton(r, font, topActionRect(layout.center, 1, 5), "Load Lua");
        drawButton(r, font, topActionRect(layout.center, 2, 5), "Save Lua");
        drawButton(r, font, topActionRect(layout.center, 3, 5), "Insert Template");
        drawButton(r, font, topActionRect(layout.center, 4, 5), "Clear");
        const int topExtra = topActionExtraY(layout.center);
        drawText(r, font, projectData.package.manifest.type == BrushType::ScriptedRaster ? "Safe brush script editing is enabled." : "Script stays quiet for non-scripted brushes.", layout.center.x + 12, layout.center.y + 52 + topExtra, COL_TEXT_DIM);
        SDL_Rect textR{ layout.center.x + 12, layout.center.y + 84 + topExtra, layout.center.w - 24, layout.center.h - (96 + topExtra) };
        fillRect(r, textR, SDL_Color{ 18, 22, 30, 255 });
        drawRect(r, textR, scriptFocused ? SDL_Color{ 110,160,255,255 } : COL_BORDER_SOFT);
        std::vector<std::string> lines;
        {
            std::string cur;
            for (char cch : projectData.package.scriptSource)
            {
                if (cch == '\n') { lines.push_back(cur); cur.clear(); }
                else if (cch != '\r') cur.push_back(cch);
            }
            lines.push_back(cur);
        }
        const int lineH = 18;
        int drawY = textR.y + 8;
        for (int i = 0; i < (int)lines.size() && drawY + lineH <= textR.y + textR.h - 6; ++i)
        {
            drawText(r, font, std::to_string(i + 1), textR.x + 8, drawY, SDL_Color{ 118,126,146,255 });
            drawText(r, font, lines[(size_t)i], textR.x + 40, drawY, COL_TEXT_DIM);
            drawY += lineH;
        }
    }
    else if (activeTab == Tab::TestCanvas)
    {
        drawText(r, font, "Test Canvas", layout.center.x + 12, layout.center.y + 12, COL_TEXT_MAIN);
        drawButton(r, font, SDL_Rect{ layout.center.x + 12, layout.center.y + 12, 100, 28 }, "Clear Canvas");
        drawButton(r, font, SDL_Rect{ layout.center.x + 120, layout.center.y + 12, 130, 28 }, "Replay Sample");
        if (testBgMode == 2) drawChecker(r, layout.canvas);
        else fillRect(r, layout.canvas, bgModeColor(testBgMode));
        drawRect(r, layout.canvas, SDL_Color{ 100,112,144,255 });
        if (app.brushRendererHandle())
        {
            SDL_RenderSetClipRect(r, &layout.canvas);
            for (const Stroke& s : testStrokes)
                app.brushRendererHandle()->drawStrokeWithPackage(s, &projectData.package, 1.0f, 0.0f, 0.0f, layout.canvas.x, layout.canvas.y);
            if (testDrawing && !liveTestStroke.points.empty())
                app.brushRendererHandle()->drawStrokeWithPackage(liveTestStroke, &projectData.package, 1.0f, 0.0f, 0.0f, layout.canvas.x, layout.canvas.y);
            SDL_RenderSetClipRect(r, nullptr);
        }
        drawText(r, font, "Sandbox only. It does not touch project layers, Flow, FlowLink, or playback.", layout.center.x + 12, layout.center.y + layout.center.h - 22, COL_TEXT_DIM);
        drawText(r, font, "Default surface is White. Checker is inspection-only.", layout.center.x + 270, layout.center.y + 18, COL_TEXT_DIM);

        drawText(r, font, "Test Controls", layout.right.x + 12, layout.right.y + 220, COL_TEXT_MAIN);
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 222, layout.right.w - 24, 24 }, std::string("Background: ") + bgModeLabel(testBgMode));
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 252, layout.right.w - 24, 24 }, projectData.package.manifest.color.supportsGradient ? (testUseGradient ? "Gradient: On" : "Gradient: Off") : "Gradient: Locked Off", testUseGradient && projectData.package.manifest.color.supportsGradient);
        drawButton(r, font, SDL_Rect{ layout.right.x + 12, layout.right.y + 282, layout.right.w - 24, 24 }, testPressure ? "Pressure Sim: On" : "Pressure Sim: Off", testPressure);
        drawText(r, font, "Test Color", layout.right.x + 12, layout.right.y + 318, COL_TEXT_DIM);
        for (int i = 0; i < 6; ++i)
        {
            SDL_Rect sw{ layout.right.x + 12 + (i % 3) * 40, layout.right.y + 324 + (i / 3) * 40, 28, 28 };
            fillRect(r, sw, swatchColor(i));
            drawRect(r, sw, COL_BORDER_SOFT);
        }
    }
    else if (activeTab == Tab::Package)
    {
        drawText(r, font, "Package", layout.center.x + 12, layout.center.y + 12, COL_TEXT_MAIN);
        drawButton(r, font, topActionRect(layout.center, 1, 5), "Save Project");
        drawButton(r, font, topActionRect(layout.center, 2, 5), "Validate");
        drawButton(r, font, topActionRect(layout.center, 3, 5), "Export...");
        drawButton(r, font, topActionRect(layout.center, 4, 5), "Install...");
        const int topExtra = topActionExtraY(layout.center);
        drawText(r, font, "Manifest Summary", layout.center.x + 12, layout.center.y + 62 + topExtra, COL_TEXT_MAIN);
        drawText(r, font, "Brush Type: " + std::string(strova::brush::brushTypeName(projectData.package.manifest.type)), layout.center.x + 12, layout.center.y + 88 + topExtra, COL_TEXT_DIM);
        drawText(r, font, "Stamp: " + std::to_string(projectData.package.stamp.width) + " x " + std::to_string(projectData.package.stamp.height), layout.center.x + 12, layout.center.y + 110 + topExtra, COL_TEXT_DIM);
        drawTextFit(r, font, "Project File: " + (projectData.projectPath.empty() ? std::string("unsaved") : projectData.projectPath), SDL_Rect{ layout.center.x + 12, layout.center.y + 132 + topExtra, layout.center.w - 24, 18 }, COL_TEXT_DIM);
        drawText(r, font, "Validation", layout.center.x + 12, layout.center.y + 172 + topExtra, COL_TEXT_MAIN);
        drawTextFit(r, font, projectData.package.validation.summary(), SDL_Rect{ layout.center.x + 12, layout.center.y + 198 + topExtra, layout.center.w - 24, 18 },
            projectData.package.validation.ok ? SDL_Color{ 118,218,154,255 } : SDL_Color{ 255,148,120,255 });
    }

    drawTextFit(r, font, std::string("Tab: ") + tabName(activeTab) + "  |  Status: " + status, SDL_Rect{ layout.status.x + 8, layout.status.y + 2, layout.status.w - 16, layout.status.h - 4 }, COL_TEXT_DIM);

    if (modal != Modal::None)
    {
        fillRect(r, layout.outer, SDL_Color{ 0, 0, 0, 178 });
        SDL_Rect modalRc{ layout.outer.x + layout.outer.w / 2 - 240, layout.outer.y + layout.outer.h / 2 - 170, 480, 340 };
        fillRect(r, modalRc, SDL_Color{ 16, 18, 26, 255 });
        drawRect(r, modalRc, SDL_Color{ 118, 130, 160, 240 });
        SDL_Rect okR{ modalRc.x + modalRc.w - 224, modalRc.y + modalRc.h - 40, 96, 28 };
        SDL_Rect cancelR{ modalRc.x + modalRc.w - 116, modalRc.y + modalRc.h - 40, 96, 28 };
        drawText(r, font, modalTitle(modal), modalRc.x + 16, modalRc.y + 12, COL_TEXT_MAIN);
        if (modal == Modal::Validation)
        {
            drawText(r, font, modalText, modalRc.x + 16, modalRc.y + 48, COL_TEXT_DIM);
            drawButton(r, font, okR, "OK");
        }
        else if (modal == Modal::ImportStamp)
        {
            drawButton(r, font, SDL_Rect{ modalRc.x + 16, modalRc.y + 48, 140, 28 }, "Choose PNG...");
            drawText(r, font, pendingImportPath.empty() ? "No file selected" : pendingImportPath, modalRc.x + 168, modalRc.y + 54, COL_TEXT_DIM);
            SDL_Rect previewRc{ modalRc.x + 16, modalRc.y + 86, 180, 116 };
            drawPixels(r, previewRc, pendingImportStamp.width, pendingImportStamp.height, pendingImportStamp.rgba, true, bgModeColor(2));
            drawButton(r, font, SDL_Rect{ modalRc.x + 16, modalRc.y + 220, 96, 28 }, "Alpha", pendingImportMaskSource == MaskSource::Alpha);
            drawButton(r, font, SDL_Rect{ modalRc.x + 120, modalRc.y + 220, 110, 28 }, "Luminance", pendingImportMaskSource == MaskSource::Luminance);
            drawButton(r, font, SDL_Rect{ modalRc.x + 238, modalRc.y + 220, 110, 28 }, "Darkness", pendingImportMaskSource == MaskSource::Darkness);
            drawButton(r, font, SDL_Rect{ modalRc.x + 16, modalRc.y + 258, 140, 28 }, pendingImportInvert ? "Invert: On" : "Invert: Off", pendingImportInvert);
            drawText(r, font, modalText, modalRc.x + 208, modalRc.y + 96, COL_TEXT_DIM);
            drawButton(r, font, okR, "Import");
            drawButton(r, font, cancelR, "Cancel");
        }
        else if (modal == Modal::ExportBrush)
        {
            drawText(r, font, "Package summary", modalRc.x + 16, modalRc.y + 48, COL_TEXT_MAIN);
            drawText(r, font, projectData.package.manifest.name, modalRc.x + 16, modalRc.y + 74, COL_TEXT_DIM);
            drawText(r, font, projectData.package.validation.summary(), modalRc.x + 16, modalRc.y + 98, COL_TEXT_DIM);
            drawText(r, font, "Choose a destination and write the .sbrush package.", modalRc.x + 16, modalRc.y + 132, COL_TEXT_DIM);
            drawButton(r, font, okR, "Export");
            drawButton(r, font, cancelR, "Cancel");
        }
        else if (modal == Modal::InstallBrush)
        {
            drawText(r, font, "Install target", modalRc.x + 16, modalRc.y + 48, COL_TEXT_MAIN);
            drawText(r, font, strova::brush::userDir().string(), modalRc.x + 16, modalRc.y + 74, COL_TEXT_DIM);
            drawText(r, font, projectData.package.validation.summary(), modalRc.x + 16, modalRc.y + 106, COL_TEXT_DIM);
            drawButton(r, font, okR, "Install");
            drawButton(r, font, cancelR, "Cancel");
        }
        else if (modal == Modal::NewBrush)
        {
            drawButton(r, font, SDL_Rect{ modalRc.x + 16, modalRc.y + 54, 180, 28 }, std::string("Type: ") + strova::brush::brushTypeName(pendingNewType));
            drawButton(r, font, SDL_Rect{ modalRc.x + 16, modalRc.y + 92, 180, 28 }, std::string("Generator: ") + generatorLabel(pendingNewGenerator));
            drawText(r, font, "Create a fresh .sbrushproj with a built-in generator seed. You can then refine it across the tabs and test it on the sandbox canvas.", modalRc.x + 16, modalRc.y + 142, COL_TEXT_DIM);
            drawButton(r, font, okR, "Create");
            drawButton(r, font, cancelR, "Cancel");
        }
    }
}
