/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        app/App.h
   Module:      App
   Purpose:     Main app state and runtime entry points.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once
#include <SDL.h>
#include <SDL_ttf.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include "../core/JobSystem.h"
#include "../core/ToolBank.h"
#include "../core/ToolRegistry.h"
#include "../core/FlowCapturer.h"
#include "../core/DrawingEngine.h"
#include "../core/ProjectIO.h"
#include "../core/AppSettings.h"
#include "../core/LayerTree.h"
#include "../core/BrushSystem.h"
#include "../plugin/PluginManager.h"
#include "../update/UpdateManager.h"
#include "../ui/UIButton.h"
#include "../ui/UILayout.h"

#include "../ui/ColorPicker.h"
#include "../ui/IconButton.h"
#include "../ui/Launcher.h"
#include "../ui/ToolBar.h"
#include "../ui/SplashScreen.h"
#include "../ui/Timeline.h"
#include "../ui/DockManager.h"

#include "../render/BrushRenderer.h"

class Editor;
class LauncherScreen;
class BrushCreatorScreen;

static inline int ToolTypeCount()
{
    return strova::tools::toolCount();
}


struct FloatingColorPickerState
{
    SDL_Rect rect{ 760, 180, 340, 540 };
    bool visible = false;
    bool dragging = false;
    int dragOffsetX = 0;
    int dragOffsetY = 0;
};

enum class ToolMode
{
    Draw = 0,
    Move,
    Rotate,
    Select
};

struct EditorRuntimeState
{
    struct ProjectState
    {
        int width = 1920;
        int height = 1080;
        int fps = 30;
        std::string name = "Untitled";
        std::string folderPath;
        std::size_t activeFrameIndex = 0;
    };

    struct UiState
    {
        bool rightPanelOpen = false;
        float rightPanelAnim = 0.0f;
        bool mouseCaptured = false;
        bool keyboardCaptured = false;
        bool modalActive = false;
        bool textInputActive = false;
        FloatingColorPickerState colorPickerWindow{};
    };

    enum class InputOwner
    {
        None = 0,
        Modal,
        Dock,
        Timeline,
        LayerPanel,
        ToolPanel,
        ColorPicker,
        Toolbar,
        CanvasPan,
        Tool
    };

    struct InputState
    {
        std::vector<SDL_Event> queuedEvents;
        int mouseX = 0;
        int mouseY = 0;
        Uint32 mouseButtons = 0;
        InputOwner mouseOwner = InputOwner::None;
        bool toolMouseAllowed = true;
        bool toolKeyboardAllowed = true;
        std::size_t queuedEventCount = 0;
        std::size_t processedEventCount = 0;

        void beginFrame(int mx, int my, Uint32 buttons)
        {
            mouseX = mx;
            mouseY = my;
            mouseButtons = buttons;
            mouseOwner = InputOwner::None;
            toolMouseAllowed = true;
            toolKeyboardAllowed = true;
            queuedEventCount = queuedEvents.size();
            processedEventCount = 0;
        }
    };

    struct ToolState
    {
        ToolMode mode = ToolMode::Draw;
        float stabilizer = 0.35f;
        bool transformAutoKey = false;
    };

    struct SelectionState
    {
        int activeLayerTrackId = 0;
        bool activeLayerValid = false;
        int isolatedLayerTrackId = 0;
    };

    struct PlaybackState
    {
        bool playing = false;
        double accumulator = 0.0;
        Uint64 lastCounter = 0;
        double lastStepMs = 0.0;
    };

    struct CaptureState
    {
        bool flowSettingsOpen = false;
        int flowSettingsField = 0;
        bool flowLinkEnabled = false;
        int lastFlowSampleCount = 0;
        int lastFlowLinkSampleCount = 0;
    };

    struct RenderState
    {
        int thumbRebuildsThisFrame = 0;
        int dirtyFrameCount = 0;
        int dirtyRegionCount = 0;
        std::size_t textureBytesEstimate = 0;
        bool proxyActive = false;
        int proxyScalePercent = 100;
        int proxyRebuildsThisFrame = 0;
        int backgroundJobsPending = 0;
        int backgroundJobsActive = 0;
        int thumbProbeHitsThisFrame = 0;
        int thumbProbeMissesThisFrame = 0;
    };

    struct HistoryState
    {
        std::size_t undoBytesEstimate = 0;
        bool saveRequested = false;
    };

    struct DiagnosticsState
    {
        bool overlayEnabled = false;
        std::uint64_t frameOrdinal = 0;
        double frameMs = 0.0;
        double inputMs = 0.0;
        double uiMs = 0.0;
        double playbackMs = 0.0;
        double renderMs = 0.0;
        int compositeRebuildCount = 0;
        int layerRebuildCount = 0;
        int dirtyRegionCount = 0;
        int flowSampleCount = 0;
        int flowLinkSampleCount = 0;
        std::size_t textureBytesEstimate = 0;
        std::size_t undoBytesEstimate = 0;
        bool proxyActive = false;
        int proxyScalePercent = 100;
        int proxyRebuildCount = 0;

        void beginFrame(double dtSeconds)
        {
            ++frameOrdinal;
            frameMs = dtSeconds * 1000.0;
            inputMs = 0.0;
            uiMs = 0.0;
            playbackMs = 0.0;
            renderMs = 0.0;
            compositeRebuildCount = 0;
            layerRebuildCount = 0;
            dirtyRegionCount = 0;
            flowSampleCount = 0;
            flowLinkSampleCount = 0;
            textureBytesEstimate = 0;
            undoBytesEstimate = 0;
            proxyActive = false;
            proxyScalePercent = 100;
            proxyRebuildCount = 0;
        }
    };

    ProjectState project;
    UiState ui;
    InputState input;
    ToolState tool;
    SelectionState selection;
    PlaybackState playback;
    CaptureState capture;
    RenderState render;
    HistoryState history;
    DiagnosticsState diagnostics;
};


class App
{
public:
    enum class EditorCommandType
    {
        SelectTool,
        ReplaceToolSettings,
        SyncColorPicker,
        ToggleRightPanel,
        Undo,
        Redo,
        SetOnionEnabled,
        SetOnionTint,
        SetOnionSteps,
        SetOnionPrevAlpha,
        SetOnionNextAlpha,
        SetFillGapClose,
        SetActiveFrame,
        AddFrame,
        SelectLayerNode,
        SetLayerVisible,
        SetLayerLocked,
        SetLayerFocus,
        SetLayerOpacity,
        SelectTrack,
        SetTrackMuted,
        InsertFrame,
        DeleteFrame,
        DuplicateFrame,
        MoveFocusedFrame,
        RenameTrack,
        DeleteTrack,
        SetRulerVisible
    };

    struct EditorCommand
    {
        EditorCommandType type = EditorCommandType::SelectTool;
        ToolType tool = ToolType::Brush;
        ToolSettings toolSettings{};
        SDL_Color color{ 0, 0, 0, 255 };
        GradientConfig gradient{};
        float floatValue = 0.0f;
        int intValue = 0;
        int auxIntValue = 0;
        int trackId = 0;
        int nodeId = 0;
        bool boolValue = false;
        bool addToSelection = false;
        bool extendSelection = false;
    };

    struct EditorUiState
    {
        ToolType activeTool = ToolType::Brush;
        ToolSettings activeToolSettings{};
        SDL_Color currentColor{ 0, 0, 0, 255 };
        GradientConfig currentGradient{};

        std::size_t activeFrameIndex = 0;
        int activeLayerTrackId = 0;
        float activeLayerOpacity = 1.0f;
        bool activeLayerVisible = true;
        bool activeLayerLocked = false;

        bool rightPanelOpen = false;
        bool onionSkinEnabled = true;
        float onionPrevAlpha = 0.18f;
        float onionNextAlpha = 0.10f;
        int onionSteps = 2;
        bool onionTint = true;

        int fillGapClose = 0;
        bool rulerVisible = false;
        int isolatedLayerTrackId = 0;
    };

    SDL_Texture* iconBrush = nullptr;
    SDL_Texture* iconPencil = nullptr;
    SDL_Texture* iconPen = nullptr;
    SDL_Texture* iconMarker = nullptr;
    SDL_Texture* iconAirbrush = nullptr;
    SDL_Texture* iconCalligraphy = nullptr;
    SDL_Texture* iconEraser = nullptr;
    SDL_Texture* iconSmudge = nullptr;
    SDL_Texture* iconBlur = nullptr;
    SDL_Texture* iconGlow = nullptr;
    SDL_Texture* iconRuler = nullptr;
    SDL_Texture* iconSoften = nullptr;
    SDL_Texture* iconBucket = nullptr;
    SDL_Texture* iconLine = nullptr;
    SDL_Texture* iconOval = nullptr;
    SDL_Texture* iconRectangle = nullptr;
    SDL_Texture* iconSelect = nullptr;
    SDL_Texture* iconDropper = nullptr;

    
    SDL_Texture* iconUndo = nullptr;
    SDL_Texture* iconRedo = nullptr;
    BrushRenderer* brushRenderer = nullptr;
    strova::brush::BrushManager brushManagerStore{};
    strova::plugin::Manager pluginManagerStore{};
    App() = default;
    ~App() = default;

    bool init();
    void run();
    void shutdown();
    bool isEditorMode() const { return mode == Mode::Editor; }
    bool isBrushCreatorMode() const { return mode == Mode::BrushCreator; }

    void setProjectFpsForFlow(int fps);

    ToolBank toolBank;

    bool onionSkinEnabled = true;
    float onionPrevAlpha = 0.18f;
    float onionNextAlpha = 0.10f;

    strova::TimelineWidget timeline;
    strova::LayerTree layerTree;

    SDL_Renderer* getRenderer() const { return sdlRenderer; }

    DrawingEngine& getEngine() { return engine; }
    const DrawingEngine& getEngine() const { return engine; }

    int getProjectW() const { return projectW; }
    int getProjectH() const { return projectH; }
    int getProjectFPS() const { return projectFPS; }
    const std::string& getProjectNameStr() const { return currentProject.name; }
    const char* getProjectNameCStr() const { return currentProject.name.c_str(); }
    SDL_Renderer* sdlRenderer = nullptr;

    AppSettings appSettings{};
    strova::UpdateManager updateManager;
    TTF_Font* getUiFont() const { return uiFont; }

    const UILayout& getUILayout() const { return ui; }
    UILayout& getUILayout() { return ui; }
    const UILayout& uiSystem() const { return ui; }
    UILayout& uiSystem() { return ui; }
    const SDL_Rect& rightBarRect() const { return rightBar; }
    SDL_Rect& rightBarRectRef() { return rightBar; }

    float leftBarRatioValue() const { return leftBarRatio; }
    float& leftBarRatioRef() { return leftBarRatio; }
    bool isResizingLeftBar() const { return resizingLeftBar; }
    bool& resizingLeftBarRef() { return resizingLeftBar; }
    EditorRuntimeState& runtimeStateRef() { return runtimeState; }
    const EditorRuntimeState& runtimeStateRef() const { return runtimeState; }

    ColorPicker& colorPickerWidget() { return colorPicker; }
    const ColorPicker& colorPickerWidget() const { return colorPicker; }
    BrushRenderer* brushRendererHandle() const { return brushRenderer; }
    strova::brush::BrushManager& brushManager() { return brushManagerStore; }
    const strova::brush::BrushManager& brushManager() const { return brushManagerStore; }
    strova::plugin::Manager& pluginManager() { return pluginManagerStore; }
    const strova::plugin::Manager& pluginManager() const { return pluginManagerStore; }
    strova::DockManager& dockManager() { return dockUi; }
    const strova::DockManager& dockManager() const { return dockUi; }
    FloatingColorPickerState& colorPickerWindowState() { return runtimeState.ui.colorPickerWindow; }
    const FloatingColorPickerState& colorPickerWindowState() const { return runtimeState.ui.colorPickerWindow; }

    float stabilizerValue() const { return runtimeState.tool.stabilizer; }
    float& stabilizerRef() { return runtimeState.tool.stabilizer; }

    float canvasScaleValue() const { return canvasScale; }
    float& canvasScaleRef() { return canvasScale; }
    float canvasPanXValue() const { return canvasPanX; }
    float& canvasPanXRef() { return canvasPanX; }
    float canvasPanYValue() const { return canvasPanY; }
    float& canvasPanYRef() { return canvasPanY; }
    float minCanvasScaleValue() const { return minCanvasScale; }
    float maxCanvasScaleValue() const { return maxCanvasScale; }
    int& panStartXRef() { return panStartX; }
    int& panStartYRef() { return panStartY; }
    bool& drawingRef() { return drawing; }
    bool& panningRef() { return panning; }
    bool& hasFilteredRef() { return hasFiltered; }
    SDL_FPoint& filteredPointRef() { return filteredPt; }

    bool isPlaying() const { return runtimeState.playback.playing; }
    bool uiInputCaptured() const { return runtimeState.ui.mouseCaptured; }
    bool& uiInputCapturedRef() { return runtimeState.ui.mouseCaptured; }
    bool& playingRef() { return runtimeState.playback.playing; }
    double& playAccumulatorRef() { return runtimeState.playback.accumulator; }
    Uint64& playLastCounterRef() { return runtimeState.playback.lastCounter; }
    void resetPlaybackAccumulator() { runtimeState.playback.accumulator = 0.0; }
    void resetPlaybackClock() { runtimeState.playback.accumulator = 0.0; runtimeState.playback.lastCounter = SDL_GetPerformanceCounter(); }
    void tickPlayback(double dt) { updatePlayback(dt); }

    int& timelineScrollRef() { return timelineScroll; }
    SDL_Rect& timelineStripRect() { return timelineStrip; }
    const SDL_Rect& timelineStripRect() const { return timelineStrip; }
    SDL_Rect& transportBarRect() { return transportBar; }
    const SDL_Rect& transportBarRect() const { return transportBar; }

    bool isRightPanelOpen() const { return runtimeState.ui.rightPanelOpen; }
    bool& rightPanelOpenRef() { return runtimeState.ui.rightPanelOpen; }
    float rightPanelAnimValue() const { return runtimeState.ui.rightPanelAnim; }
    float& rightPanelAnimRef() { return runtimeState.ui.rightPanelAnim; }

    bool isOnionSkinEnabled() const { return onionSkinEnabled; }
    bool flowLinkEnabledValue() const { return runtimeState.capture.flowLinkEnabled; }
    bool& flowLinkEnabledRef() { return runtimeState.capture.flowLinkEnabled; }
    ToolMode activeToolModeValue() const { return runtimeState.tool.mode; }
    ToolMode& activeToolModeRef() { return runtimeState.tool.mode; }
    int timelineRangeStartValue() const { return 0; }
    int& timelineRangeStartRef() { return timelineRangeStart; }
    int timelineRangeEndValue() const { return std::max(0, (int)engine.getFrameCount() - 1); }
    int& timelineRangeEndRef() { return timelineRangeEnd; }
    bool& onionSkinEnabledRef() { return onionSkinEnabled; }
    float onionPrevAlphaValue() const { return onionPrevAlpha; }
    float& onionPrevAlphaRef() { return onionPrevAlpha; }
    float onionNextAlphaValue() const { return onionNextAlpha; }
    float& onionNextAlphaRef() { return onionNextAlpha; }

    FlowCapturer& flowCapturer() { return flow; }
    const FlowCapturer& flowCapturer() const { return flow; }
    bool isFlowSettingsOpen() const { return runtimeState.capture.flowSettingsOpen; }
    Launcher& launcherUi() { return launcher; }
    const Launcher& launcherUi() const { return launcher; }

    IconButton& undoButton() { return undoBtn; }
    IconButton& redoButton() { return redoBtn; }
    UIButton& colorButton() { return colorBtn; }
    SDL_Texture* undoTextureHandle() const { return undoTex; }
    SDL_Texture* redoTextureHandle() const { return redoTex; }
    SDL_Texture* colorTextureHandle() const { return colorTex; }

    SDL_Window* windowHandle() const { return window; }
    void refreshUILayout(int windowW, int windowH);
    void refreshUILayout();
    void validateEditorState();
    void syncRuntimeStateFromEditor();
    void enqueueEditorEvent(const SDL_Event& e);
    std::vector<SDL_Event> takeQueuedEditorEvents();
    void beginEditorFrame(double dt);
    void beginEditorInputRouting(int mouseX, int mouseY, Uint32 mouseButtons);
    void captureEditorUi(EditorRuntimeState::InputOwner owner, bool captureKeyboard);
    void captureEditorTool(EditorRuntimeState::InputOwner owner);
    bool editorToolsCanUseMouse() const;
    bool editorToolsCanUseKeyboard() const;
    void noteInputProcessingTime(double ms);
    void noteUiProcessingTime(double ms);
    void notePlaybackProcessingTime(double ms);
    void noteRenderTime(double ms);
    void noteCompositeRebuild(int count);
    void noteLayerRebuild(int count);
    void noteThumbRebuild();
    void noteDirtyRegionCount(int count);
    void noteTextureBytesEstimate(std::size_t bytes);
    void noteUndoBytesEstimate(std::size_t bytes);
    void requestSaveProjectNow();
    void requestQuit() { running = false; }
    void returnToLauncher();
    void openProjectPath(const std::string& path);
    void createDefaultProject();
    void openBrushCreatorWorkspace(const std::string& brushProjectPath = "", bool returnToEditor = false);
    void openBrushCreatorWorkspace(const strova::brush::BrushProject& project, bool returnToEditor = false);
    void closeBrushCreatorWorkspace();
    void initializeDockingUiState(int windowW, int windowH);
    void loadDockLayoutForCurrentContext(int windowW, int windowH);
    void saveDockLayoutForCurrentContext() const;
    void loadColorPickerWindowState();
    void saveColorPickerWindowState() const;
    std::filesystem::path currentDockLayoutPath() const;
    void setToolCommand(ToolType tool);
    void replaceToolSettingsCommand(ToolType tool, const ToolSettings& settings);
    void syncColorPickerCommand();
    void toggleRightPanelCommand();
    void setOnionEnabledCommand(bool enabled);
    void setOnionTintCommand(bool tinted);
    void setOnionStepsCommand(int steps);
    void setOnionPrevAlphaCommand(float alpha);
    void setOnionNextAlphaCommand(float alpha);
    void setFillGapCloseCommand(int gapClose);
    void setRulerVisibleCommand(bool visible);
    void setLayerFocusCommand(int trackId);
    void markCurrentFrameEdited();
    void markFrameEdited(std::size_t frameIndex);
    void markCurrentFrameEditedAndSave();
    void markFrameEditedAndSave(std::size_t frameIndex);

    bool saveProject(std::string& outErr) { return saveProjectNow(outErr); }

    void dirtyThumb(std::size_t frameIndex) { markThumbDirty(frameIndex); }
    void dirtyAllThumbs() { markAllThumbsDirty(); }
    void rebuildAllThumbnailsNow() { rebuildAllThumbsIfNeeded(); }
    const EditorUiState& getEditorUiState() const { return editorUiState; }
    EditorUiState& accessEditorUiState() { return editorUiState; }
    void syncUIFromState();
    bool dispatchEditorCommand(const EditorCommand& cmd);
    void switchToFrameIndex(std::size_t frameIndex);
    void onFrameChanged();
    strova::LayerTree& activeFrameLayerTree();
    const strova::LayerTree& activeFrameLayerTree() const;
    void refreshLayerPanelForActiveFrame();
    void storeCurrentDrawFrameLayerTree();
    void restoreLayerTreeForCurrentDrawFrame();
    void ensureFrameLayerTreeSize();
    void initFreshLayerTreeForFrame(std::size_t frameIndex);
    void normalizeFrameLayerTrees();
private:
    bool saveProjectFolderInternal(const std::filesystem::path& folder, std::string& outErr, bool recoverySnapshot);
    bool saveRecoverySnapshot(std::string& outErr);
    void clearRecoverySnapshot();
    std::filesystem::path recoveryFolderForProject(const std::string& folderPath) const;
    std::filesystem::path resolveOpenFolderWithRecovery(const std::string& folderPath, bool* outUsedRecovery = nullptr) const;
    enum class Mode
    {
        Splash,
        Launcher,
        Editor,
        BrushCreator
    };

    Editor* editor = nullptr;
    LauncherScreen* launcherScreen = nullptr;
    BrushCreatorScreen* brushCreatorScreen = nullptr;

    SDL_Window* window = nullptr;
    TTF_Font* uiFont = nullptr;

    bool running = true;
    Mode mode = Mode::Launcher;
    UIButton colorBtn;
    UILayout ui{};
    float leftBarRatio = 0.16f;
    bool resizingLeftBar = false;
    EditorRuntimeState runtimeState{};

    Project currentProject{};
    DrawingEngine engine;

    int projectW = 1920;
    int projectH = 1080;
    int projectFPS = 30;

    int rightPanelW = 330;
    SDL_Rect rightBar{};
    SDL_Texture* colorTex = nullptr;

    int compModeIndex = 0;
    float fillOpacity = 1.0f;

    ToolBar toolBar;
    ColorPicker colorPicker;
    strova::DockManager dockUi;
    SplashScreen splash;

    IconButton undoBtn;
    IconButton redoBtn;
    SDL_Texture* undoTex = nullptr;
    SDL_Texture* redoTex = nullptr;

    float canvasScale = 1.0f;
    float canvasPanX = 0.0f;
    float canvasPanY = 0.0f;

    float minCanvasScale = 0.10f;
    float maxCanvasScale = 8.00f;

    bool drawing = false;
    bool panning = false;

    int panStartX = 0;
    int panStartY = 0;

    bool hasFiltered = false;
    SDL_FPoint filteredPt{ 0.0f, 0.0f };

    Launcher launcher;

    FlowCapturer flow;
    int timelineRangeStart = 0;
    int timelineRangeEnd = 0;

    Uint64 lastTicks = 0;

    int timelineScroll = 0;
    SDL_Rect timelineStrip{};
    SDL_Rect transportBar{};

    struct ThumbProbeResult
    {
        std::size_t frameIndex = 0;
        std::uint64_t expectedKey = 0;
        bool valid = false;
    };

    struct Thumb
    {
        SDL_Texture* tex = nullptr;
        int w = 0;
        int h = 0;
        std::uint64_t dirtyKey = 0;
        std::string diskPath;
        std::string keyPath;
        bool diskReady = false;
        bool loadedFromDisk = false;
        bool probeQueued = false;
        bool probeReady = false;
        bool probedValid = false;
        std::uint64_t probedKey = 0;
    };

    std::vector<Thumb> thumbCache;
    std::size_t thumbRebuildCursor = 0;
    strova::jobs::JobSystem backgroundJobs{};
    std::mutex thumbProbeMutex;
    std::deque<ThumbProbeResult> thumbProbeResults;
    std::vector<strova::LayerTree> frameLayerTrees;
    EditorUiState editorUiState{};
    Mode brushCreatorReturnMode = Mode::Launcher;

    bool createWindow(const char* title, int w, int h);
    SDL_Texture* loadIconTexture(const char* path);

    UILayout calculateUILayout(int w, int h, float leftRatio);
    void fitProjectToCanvasView();

    void openProject(const std::string& path);
    void createNewProjectDefault();
    void syncProjectFromEngine();
    bool saveProjectNow(std::string& outErr);

    void handleEvent(SDL_Event& e);
    void render();

    void updatePlayback(double dt);

    void ensureThumbCacheSize();
    std::uint64_t calcFrameDirtyKey(std::size_t frameIndex) const;

    void rebuildThumb(std::size_t frameIndex);
    void rebuildAllThumbsIfNeeded();
    void prepareThumbDiskCache();
    bool loadThumbFromDisk(std::size_t frameIndex);

    void markThumbDirty(std::size_t frameIndex);
    void markAllThumbsDirty();

    void processThumbJobs(int budget);
    void queueThumbDiskProbe(std::size_t frameIndex, std::uint64_t expectedKey);
    void drainThumbProbeResults();

    SDL_Texture* makeUiText(const std::string& text, SDL_Color col, int* outW, int* outH);
    void drawSplashOverlay(int w, int h);
    void handleSplashOverlayEvent(const SDL_Event& e);
    void maybeFinishSplashOrLaunchUpdater();

    SDL_Rect splashPrimaryBtn{};
    SDL_Rect splashSecondaryBtn{};
    std::string splashOverlayMessage;
    bool pendingUpdaterLaunch = false;
    Uint32 pendingUpdaterLaunchAtMs = 0;
    std::filesystem::path pendingUpdaterExe;
    std::string pendingUpdaterArgs;
};
