#pragma once

#include <SDL.h>
#include <string>
#include <array>
#include <vector>

#include "../core/BrushSystem.h"
#include "../core/Stroke.h"

class App;

class BrushCreatorScreen
{
public:
    BrushCreatorScreen() = default;
    ~BrushCreatorScreen() = default;

    void open(App& app, const std::string& brushProjectPath);
    void open(App& app, const strova::brush::BrushProject& project, const std::string& statusText);
    void update(App& app, double dt);
    void handleEvent(App& app, SDL_Event& e);
    void render(App& app, int w, int h);

    bool isDirty() const { return dirty; }
    const strova::brush::BrushProject& project() const { return projectData; }

public:
    enum class Tab
    {
        Overview = 0,
        Stamp,
        Behavior,
        Color,
        Script,
        TestCanvas,
        Package,
        Count
    };

    enum class Modal
    {
        None = 0,
        NewBrush,
        ImportStamp,
        ExportBrush,
        InstallBrush,
        Validation
    };

    enum class PreviewMode
    {
        Mask = 0,
        FinalDab,
        RawSource,
        Strength
    };

    enum class OverviewField
    {
        None = 0,
        Name,
        InternalId,
        Author,
        Description,
        Category,
        Tags
    };

    struct Layout;

    enum class PaneId
    {
        None = 0,
        Left,
        Center,
        Right
    };

private:
    strova::brush::BrushProject projectData{};
    Tab activeTab = Tab::Overview;
    Modal modal = Modal::None;
    PreviewMode previewMode = PreviewMode::Mask;
    OverviewField overviewField = OverviewField::None;

    bool dirty = false;
    std::string status = "Brush Creator ready";
    std::string modalText;

    bool scriptFocused = false;
    std::size_t scriptCursor = 0;

    float leftPaneRatio = 0.17f;
    float rightPaneRatio = 0.19f;
    bool draggingLeftSplitter = false;
    bool draggingRightSplitter = false;

    bool leftPaneFloating = false;
    bool centerPaneFloating = false;
    bool rightPaneFloating = false;
    SDL_Rect leftPaneFloatRect{ 0, 0, 0, 0 };
    SDL_Rect centerPaneFloatRect{ 0, 0, 0, 0 };
    SDL_Rect rightPaneFloatRect{ 0, 0, 0, 0 };
    PaneId draggingPane = PaneId::None;
    int paneDragOffsetX = 0;
    int paneDragOffsetY = 0;

    strova::brush::BrushStamp pendingImportStamp{};
    std::string pendingImportPath;
    strova::brush::MaskSource pendingImportMaskSource = strova::brush::MaskSource::Darkness;
    bool pendingImportInvert = false;
    strova::brush::BrushType pendingNewType = strova::brush::BrushType::Procedural;
    strova::brush::GeneratorType pendingNewGenerator = strova::brush::GeneratorType::SoftCircle;

    int inspectorBgMode = 0;
    int testBgMode = 0;
    bool testUseGradient = false;
    bool testPressure = false;
    bool testDrawing = false;
    SDL_Color testColor{ 20, 20, 20, 255 };
    std::vector<Stroke> testStrokes;
    Stroke liveTestStroke{};

    bool ensureLoadedProject(App& app, const std::string& brushProjectPath, const std::string& statusText);
    void markDirty(const std::string& statusText);
    void refreshDerived();
    void resetSandbox();
    void addReplayStroke(const SDL_Rect& canvas);
    Stroke makeSandboxStroke() const;
    void appendSandboxPoint(const SDL_Rect& canvas, Stroke& stroke, int mx, int my);
    bool saveProjectInteractive(App& app);
    bool exportBrushInteractive(App& app);
    bool installBrushInteractive(App& app);
    void createNewBrush(App& app);
    void chooseImportStamp();
    void commitImportStamp();
    void cycleBlendMode(int dir);
    void cycleRotationMode(int dir);
    void cycleGradientMode(int dir);
    void cyclePreviewMode();
    void handleOverviewTextInput(const SDL_Event& e);
    void handleScriptTextInput(const SDL_Event& e);
    void clampScriptCursor();
    void ensureDefaultScript();
    void setOverviewFieldFocus(OverviewField field);
    std::string* overviewFieldString(OverviewField field);
};
