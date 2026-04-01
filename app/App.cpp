/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        app/App.cpp
   Module:      App
   Purpose:     Application runtime, event flow, and project lifecycle handling.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "../app/App.h"
#include "../core/StrovaLimits.h"
#include "../core/DebugLog.h"
#include <SDL_image.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>
#include <unordered_map>
#include <string>
#include <cctype>
#include <ctime>
#include <cstdio>

#include "../core/Theme.h"
#include "../render/BrushRenderer.h"
#include "../core/ProjectIO.h"
#include "../core/SerializationUtils.h"
#include <thread>
#include "../core/DrawingEngine.h"
#include "../core/Stroke.h"
#include "../core/Tool.h"
#include "../core/Exporter.h"
#include "../core/LayerRenderUtil.h"
#include "../core/RenderCacheManager.h"
#include "../ui/BrushPreviewCache.h"
#include "../core/BrushSystem.h"
#include "../ui/Timeline.h"
#include "../editor/Editor.h"
#include "../launcher/LauncherScreen.h"
#include "../brush/BrushCreatorScreen.h"
#include "../platform/AppPaths.h"

using strova::TrackKind;
namespace fs = std::filesystem;


static std::string makeFrameImageAssetRelativePath(int frameIndex, int trackId)
{
    char name[96];
    sprintf_s(name, "frame_%03d_track_%d.rgba", frameIndex, trackId);
    return std::string("frame_assets/") + name;
}

static bool loadLayerImageAssetFromProject(const fs::path& projectRoot, const std::string& relPath, DrawingEngine::LayerImage& image)
{
    if (relPath.empty()) return false;
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgba;
    const fs::path assetPath = projectRoot / strova::iojson::normalizeProjectRelativePath(relPath);
    if (!strova::iojson::readRgbaAsset(assetPath, w, h, rgba))
        return false;
    image.setData(w, h, std::move(rgba));
    return true;
}

static bool commitStagedProjectFolder(const fs::path& stagingFolder, const fs::path& finalFolder, std::string& outErr)
{
    std::error_code ec;
    const fs::path parent = finalFolder.parent_path();
    if (!parent.empty())
        fs::create_directories(parent, ec);

    const fs::path backupFolder = parent / (std::string(".strova_backup_") + finalFolder.filename().string());
    fs::remove_all(backupFolder, ec);

    const bool hadExisting = fs::exists(finalFolder, ec);
    if (hadExisting)
    {
        fs::rename(finalFolder, backupFolder, ec);
        if (ec)
        {
            outErr = std::string("Failed to move existing project aside: ") + ec.message();
            return false;
        }
    }

    ec.clear();
    fs::rename(stagingFolder, finalFolder, ec);
    if (ec)
    {
        if (hadExisting)
        {
            std::error_code restoreEc;
            fs::rename(backupFolder, finalFolder, restoreEc);
        }
        outErr = std::string("Failed to commit staged project save: ") + ec.message();
        return false;
    }

    fs::remove_all(backupFolder, ec);
    return true;
}

static std::string trimCopy(const std::string& s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
static bool readTextFileSimple(const std::filesystem::path& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

static bool findJsonIntFieldSimple(const std::string& text, const std::string& key, int& outValue)
{
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = text.find(marker);
    if (pos == std::string::npos) return false;
    pos = text.find(':', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    const std::size_t start = pos;
    if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) ++pos;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos == start) return false;
    try { outValue = std::stoi(text.substr(start, pos - start)); return true; }
    catch (...) { return false; }
}

static bool findJsonBoolFieldSimple(const std::string& text, const std::string& key, bool& outValue)
{
    const std::string marker = "\"" + key + "\"";
    std::size_t pos = text.find(marker);
    if (pos == std::string::npos) return false;
    pos = text.find(':', pos);
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (text.compare(pos, 4, "true") == 0) { outValue = true; return true; }
    if (text.compare(pos, 5, "false") == 0) { outValue = false; return true; }
    return false;
}

static bool saveTextureToBmp(SDL_Renderer* renderer, SDL_Texture* tex, const std::string& path)
{
    if (!renderer || !tex) return false;

    int w = 0, h = 0;
    Uint32 format = SDL_PIXELFORMAT_RGBA8888;
    int access = 0;
    if (SDL_QueryTexture(tex, &format, &access, &w, &h) != 0 || w <= 0 || h <= 0) return false;

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!surf) return false;

    SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, tex);
    const int rc = SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA8888, surf->pixels, surf->pitch);
    SDL_SetRenderTarget(renderer, prevTarget);
    if (rc != 0)
    {
        SDL_FreeSurface(surf);
        return false;
    }

    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    const bool ok = (SDL_SaveBMP(surf, path.c_str()) == 0);
    SDL_FreeSurface(surf);
    return ok;
}

static SDL_Texture* loadBmpTexture(SDL_Renderer* renderer, const std::string& path, int* outW = nullptr, int* outH = nullptr)
{
    if (!renderer || path.empty() || !fs::exists(path)) return nullptr;
    SDL_Surface* surf = SDL_LoadBMP(path.c_str());
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex)
    {
        if (outW) *outW = surf->w;
        if (outH) *outH = surf->h;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    SDL_FreeSurface(surf);
    return tex;
}

static void fillRectA(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(r, &rc);
}

static void drawRectA(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(r, &rc);
}
static const char* kUpdateEndpoint = "https://broad-block-3e69.pjyot0191.workers.dev/";
static const char* kCurrentAppVersion = "1.0.5";

static float fclamp(float v, float a, float b)
{
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

static double perfMs(Uint64 beginCounter, Uint64 endCounter)
{
    const Uint64 freq = SDL_GetPerformanceFrequency();
    if (freq == 0 || endCounter < beginCounter)
        return 0.0;
    return (double)(endCounter - beginCounter) * 1000.0 / (double)freq;
}

static SDL_Rect clampLayoutRect(SDL_Rect rc, int windowW, int windowH)
{
    rc.w = std::max(rc.w, 0);
    rc.h = std::max(rc.h, 0);
    if (rc.w > windowW) rc.w = windowW;
    if (rc.h > windowH) rc.h = windowH;
    rc.x = std::clamp(rc.x, 0, std::max(0, windowW - rc.w));
    rc.y = std::clamp(rc.y, 0, std::max(0, windowH - rc.h));
    return rc;
}

static void setDefaultTimeline(strova::TimelineWidget& timeline, int frameCount)
{
    timeline.clearClips();
    timeline.clearTracks();

    auto& ts = timeline.state();
    ts.trackHeaderW = 150;
    ts.rulerH = 26;
    ts.trackH = 52;
    ts.pxPerFrame = 10.0f;

    const int safeFrameCount = strova::limits::clampTimelineFrames(std::max(1, frameCount));
    timeline.setTotalFrames(safeFrameCount);

    // New projects start with one Draw lane plus one base Flow and FlowLink lane.
    // Additional Flow / FlowLink lanes appear only when more captures are added.
    const int drawUi = timeline.addTrack(strova::TrackKind::Draw, "Draw");
    (void)timeline.addTrack(strova::TrackKind::Flow, "Flow");
    (void)timeline.addTrack(strova::TrackKind::FlowLink, "FlowLink");
    if (drawUi != 0)
        timeline.addClip(drawUi, 0, safeFrameCount, "Main");
}

static strova::TimelineTrack* findTimelineTrackByEngineTrackId(App& app, int engineTrackId)
{
    for (auto& tr : app.timeline.state().tracks)
    {
        if (tr.engineTrackId == engineTrackId)
            return &tr;
    }
    return nullptr;
}

static strova::TimelineTrack* findTimelineTrackByName(App& app, const std::string& name)
{
    for (auto& tr : app.timeline.state().tracks)
        if (tr.name == name)
            return &tr;
    return nullptr;
}
static bool timelineTrackHasAnyClip(const App& app, int uiTrackId)
{
    for (const auto& clip : app.timeline.state().clips)
    {
        if (clip.trackId == uiTrackId && clip.lengthFrames > 0)
            return true;
    }
    return false;
}

static bool engineTrackHasAnyDrawableContent(const App& app, int engineTrackId)
{
    if (engineTrackId == 0)
        return false;
    const size_t frameCount = std::max<size_t>(1, app.getEngine().getFrameCount());
    for (size_t fi = 0; fi < frameCount; ++fi)
    {
        const auto& strokes = app.getEngine().getFrameTrackStrokes(fi, engineTrackId);
        if (!strokes.empty())
            return true;
        const auto layer = app.getEngine().getEvaluatedFrameTrackLayerCopy(fi, engineTrackId);
        if (!layer.image.empty())
            return true;
    }
    return false;
}

static strova::TimelineTrack* findReusableBaseFlowTimelineTrack(App& app)
{
    auto* tr = findTimelineTrackByName(app, "Flow");
    if (!tr)
        return nullptr;
    if (timelineTrackHasAnyClip(app, tr->id))
        return nullptr;
    if (engineTrackHasAnyDrawableContent(app, tr->engineTrackId))
        return nullptr;
    return tr;
}


static int ensureFlowLinkTimelineTrack(App& app)
{
    if (auto* tr = findTimelineTrackByName(app, "FlowLink"))
        return tr->engineTrackId;

    const int uiId = app.timeline.addTrack(strova::TrackKind::FlowLink, "FlowLink");
    if (uiId == 0)
        return 0;
    auto* uiTr = app.timeline.findTrack(uiId);
    if (!uiTr)
        return 0;
    uiTr->engineTrackId = app.getEngine().createTrack(DrawingEngine::TrackKind::FlowLink, "FlowLink");
    if (uiTr->engineTrackId == 0)
    {
        auto& tracks = app.timeline.state().tracks;
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&](const strova::TimelineTrack& track)
        {
            return track.id == uiId;
        }), tracks.end());
        return 0;
    }
    return uiTr->engineTrackId;
}

static int ensureFlowLinkTimelineTrackForLane(App& app, int laneIndex)
{
    const std::string trackName = laneIndex > 0 ? (std::string("FlowLink ") + std::to_string(laneIndex)) : std::string("FlowLink");
    if (auto* tr = findTimelineTrackByName(app, trackName))
        return tr->id;
    const int uiId = app.timeline.addTrack(strova::TrackKind::FlowLink, trackName.c_str());
    if (auto* uiTr = app.timeline.findTrack(uiId))
        uiTr->engineTrackId = app.getEngine().createTrack(DrawingEngine::TrackKind::FlowLink, trackName.c_str());
    return uiId;
}

static void addFlowLinkUiClipOnLane(App& app, int laneIndex, int startFrame, int duration, const std::string& label)
{
    if (duration <= 0) return;
    const int uiTrackId = ensureFlowLinkTimelineTrackForLane(app, laneIndex);
    if (uiTrackId != 0)
        app.timeline.addClip(uiTrackId, startFrame, duration, label.c_str());
}

static void addFlowLinkUiClip(App& app, int startFrame, int duration, const std::string& label)
{
    if (duration <= 0) return;
    (void)ensureFlowLinkTimelineTrack(app);
    if (auto* tr = findTimelineTrackByName(app, "FlowLink"))
        app.timeline.addClip(tr->id, startFrame, duration, label.c_str());
}

static bool isDrawableTrackKind(DrawingEngine::TrackKind kind)
{
    return kind == DrawingEngine::TrackKind::Draw || kind == DrawingEngine::TrackKind::Flow;
}

static bool isDrawableUiTrackKind(strova::TrackKind kind)
{
    return kind == strova::TrackKind::Draw || kind == strova::TrackKind::Flow;
}

static int parseFlowLinkLaneIndex(const std::string& trackName)
{
    if (trackName.rfind("FlowLink", 0) != 0)
        return -1;
    const char* tail = trackName.c_str() + 8;
    while (*tail == ' ') ++tail;
    if (!*tail) return 0;
    return std::max(0, std::atoi(tail));
}

static bool flowLinkLaneHasContentAtFrame(const App& app, int laneIndex, int frameIndex)
{
    if (frameIndex < 0) return false;
    for (const auto& tr : app.getEngine().getTracks())
    {
        const auto& clips = app.getEngine().getFlowLinkClips(tr.id);
        for (const auto& clip : clips)
        {
            if (clip.samples.empty() || clip.duration <= 0) continue;
            if (clip.laneIndex != laneIndex) continue;
            if (frameIndex < clip.startFrame) continue;
            if (!clip.loop && frameIndex >= clip.startFrame + clip.duration) continue;
            return true;
        }
    }
    return false;
}

static bool trackHasVisibleContentAtFrame(const App& app, int uiTrackId, int frameIndex)
{
    if (frameIndex < 0) return false;
    const auto* uiTrack = app.timeline.findTrack(uiTrackId);
    if (!uiTrack || !uiTrack->visible || uiTrack->muted)
        return false;

    if (uiTrack->kind == strova::TrackKind::FlowLink)
    {
        const int laneIndex = parseFlowLinkLaneIndex(uiTrack->name);
        return laneIndex >= 0 && flowLinkLaneHasContentAtFrame(app, laneIndex, frameIndex);
    }

    if (uiTrack->engineTrackId == 0)
        return false;

    const DrawingEngine::TrackLayer layer = app.getEngine().getEvaluatedFrameTrackLayerCopy((size_t)frameIndex, uiTrack->engineTrackId);
    if (layer.trackId == 0 || !layer.visible)
        return false;
    return !layer.strokes.empty() || !layer.image.empty();
}

static bool moveFlowLinkEngineClipForUiMove(App& app, strova::ClipId clipId, int oldStart, int newStart)
{
    if (clipId == 0 || oldStart == newStart)
        return false;

    auto* uiClip = app.timeline.findClip(clipId);
    if (!uiClip)
        return false;
    const auto* uiTrack = app.timeline.findTrack(uiClip->trackId);
    if (!uiTrack || uiTrack->kind != strova::TrackKind::FlowLink)
        return false;

    const int laneIndex = parseFlowLinkLaneIndex(uiTrack->name);
    if (laneIndex < 0)
        return false;

    bool moved = false;
    const int clampedStart = std::max(0, newStart);
    for (const auto& track : app.getEngine().getTracks())
    {
        auto* clips = app.getEngine().getFlowLinkClipsMutable(track.id);
        if (!clips) continue;
        for (auto& clip : *clips)
        {
            if (clip.laneIndex != laneIndex) continue;
            if (clip.startFrame != oldStart) continue;
            if (clip.duration != uiClip->lengthFrames) continue;
            clip.startFrame = clampedStart;
            std::sort(clips->begin(), clips->end(), [](const FlowLinkClip& a, const FlowLinkClip& b)
            {
                return (a.laneIndex == b.laneIndex) ? (a.startFrame < b.startFrame) : (a.laneIndex < b.laneIndex);
            });
            moved = true;
            break;
        }
        if (moved)
            break;
    }

    if (!moved)
        uiClip->startFrame = std::max(0, oldStart);

    app.timeline.setTotalFrames(std::max(1, (int)app.getEngine().getFrameCount()));
    return moved;
}

static void applyActiveToolStateToEngine(App& app)
{
    auto& uiState = app.accessEditorUiState();
    ToolSettings& stored = app.toolBank.get(uiState.activeTool);
    stored.clamp();
    uiState.activeToolSettings = stored;

    app.getEngine().setTool(uiState.activeTool);
    app.getEngine().setToolSettings(stored);
    if (uiState.activeTool == ToolType::Brush)
        app.getEngine().setBrushSelection(stored.brushId, stored.brushVersion, stored.brushDisplayName);
    app.getEngine().setGradientConfig(uiState.currentGradient);
    app.getEngine().setColor(uiState.currentColor);
    app.stabilizerRef() = stored.stabilizer;
}

struct FrameTrackContentSnapshot
{
    bool exists = false;
    DrawingEngine::TrackLayer layer{};
};

static FrameTrackContentSnapshot captureFrameTrackContentSnapshot(const App& app, int frameIndex, DrawingEngine::TrackId trackId)
{
    FrameTrackContentSnapshot out{};
    if (frameIndex < 0 || trackId == 0)
        return out;

    if (const auto* layer = app.getEngine().getFrameTrackLayer((size_t)frameIndex, trackId))
    {
        out.exists = true;
        out.layer = *layer;
    }
    return out;
}

static void applyFrameTrackContentSnapshot(App& app, int frameIndex, DrawingEngine::TrackId trackId, const FrameTrackContentSnapshot& snapshot)
{
    if (frameIndex < 0 || trackId == 0)
        return;

    auto& engine = app.getEngine();

    if (!snapshot.exists)
    {
        engine.clearFrameTrackImage((size_t)frameIndex, trackId);
        engine.setFrameTrackStrokes((size_t)frameIndex, trackId, {});
        if (auto* layer = engine.getFrameTrackLayerMutable((size_t)frameIndex, trackId))
        {
            layer->baseTransform = {};
            layer->transform = {};
            layer->hasTransformEntry = false;
            ++layer->transformRevision;
        }
        return;
    }

    engine.clearFrameTrackImage((size_t)frameIndex, trackId);
    if (snapshot.layer.contentType == DrawingEngine::LayerContentType::Image && !snapshot.layer.image.empty())
        engine.setFrameTrackImage((size_t)frameIndex, trackId, snapshot.layer.image, snapshot.layer.baseTransform);
    else
        engine.setFrameTrackStrokes((size_t)frameIndex, trackId, snapshot.layer.strokes);

    if (snapshot.layer.hasTransformEntry)
    {
        engine.setFrameTrackTransform((size_t)frameIndex, trackId, snapshot.layer.baseTransform);
    }
    else if (auto* layer = engine.getFrameTrackLayerMutable((size_t)frameIndex, trackId))
    {
        layer->baseTransform = {};
        layer->transform = {};
        layer->hasTransformEntry = false;
        ++layer->transformRevision;
    }
}

static void saveEditorMutation(App& app)
{
    std::string err;
    app.saveProject(err);
}

void App::syncUIFromState()
{
    editorUiState.activeToolSettings = toolBank.get(editorUiState.activeTool);
    editorUiState.currentColor = engine.getBrushColor();
    editorUiState.currentGradient = engine.getGradientConfig();
    editorUiState.activeFrameIndex = engine.getCurrentFrameIndex();
    editorUiState.rightPanelOpen = rightPanelOpenRef();
    editorUiState.onionSkinEnabled = onionSkinEnabled;
    editorUiState.onionPrevAlpha = onionPrevAlpha;
    editorUiState.onionNextAlpha = onionNextAlpha;
    editorUiState.onionSteps = std::clamp(editorUiState.onionSteps, 1, 5);

    int activeTrackId = layerTree.primarySelectedTrackId();
    if (activeTrackId == 0)
        activeTrackId = engine.getActiveTrack();

    editorUiState.activeLayerTrackId = activeTrackId;
    editorUiState.activeLayerOpacity = (activeTrackId != 0) ? engine.getTrackOpacity(activeTrackId) : 1.0f;
    editorUiState.activeLayerVisible = (activeTrackId != 0) ? engine.getTrackVisible(activeTrackId) : true;
    editorUiState.activeLayerLocked = (activeTrackId != 0) ? engine.getTrackLocked(activeTrackId) : false;

    if (!colorPicker.isEditingText())
        colorPicker.setColor(editorUiState.currentColor);

    if (editorUiState.activeTool == ToolType::Brush)
        brushManagerStore.select(toolBank.get(ToolType::Brush).brushId);

    syncRuntimeStateFromEditor();
}

void App::syncRuntimeStateFromEditor()
{
    runtimeState.project.width = projectW;
    runtimeState.project.height = projectH;
    runtimeState.project.fps = projectFPS;
    runtimeState.project.name = currentProject.name;
    runtimeState.project.folderPath = currentProject.folderPath;
    runtimeState.project.activeFrameIndex = engine.getCurrentFrameIndex();

    runtimeState.ui.rightPanelOpen = editorUiState.rightPanelOpen;
    runtimeState.ui.rightPanelAnim = rightPanelAnimRef();
    runtimeState.ui.textInputActive = colorPicker.isEditingText();

    runtimeState.tool.mode = activeToolModeRef();
    runtimeState.tool.stabilizer = stabilizerRef();

    runtimeState.selection.activeLayerTrackId = editorUiState.activeLayerTrackId;
    runtimeState.selection.activeLayerValid = (editorUiState.activeLayerTrackId != 0) &&
        (layerTree.findByTrackId(editorUiState.activeLayerTrackId) != nullptr);
    runtimeState.selection.isolatedLayerTrackId = editorUiState.isolatedLayerTrackId;

    runtimeState.capture.flowSettingsOpen = isFlowSettingsOpen();
    runtimeState.capture.flowLinkEnabled = flowLinkEnabledValue();
    runtimeState.capture.lastFlowSampleCount = (int)flow.getSamples().size();
    runtimeState.capture.lastFlowLinkSampleCount = (int)flow.getFlowLinkSamples().size();

    runtimeState.playback.playing = playingRef();
    runtimeState.playback.accumulator = playAccumulatorRef();
    runtimeState.playback.lastCounter = playLastCounterRef();

    runtimeState.render.dirtyFrameCount = engine.countDirtyFrameComposites();
    runtimeState.render.dirtyRegionCount = engine.countDirtyRasterTiles();
    runtimeState.render.textureBytesEstimate = strova::render_cache::totalBytes();
    for (const auto& thumb : thumbCache)
        runtimeState.render.textureBytesEstimate += (std::size_t)std::max(0, thumb.w) * (std::size_t)std::max(0, thumb.h) * 4u;
    runtimeState.render.textureBytesEstimate = (std::min)(runtimeState.render.textureBytesEstimate, strova::limits::kMaxTextureCacheBytes);
    runtimeState.history.undoBytesEstimate = engine.estimateUndoBytes();
    runtimeState.history.undoBytesEstimate = (std::min)(runtimeState.history.undoBytesEstimate, strova::limits::kMaxUndoBytes);

    runtimeState.diagnostics.flowSampleCount = runtimeState.capture.lastFlowSampleCount;
    runtimeState.diagnostics.flowLinkSampleCount = runtimeState.capture.lastFlowLinkSampleCount;
    runtimeState.diagnostics.dirtyRegionCount = runtimeState.render.dirtyRegionCount;
    runtimeState.diagnostics.textureBytesEstimate = runtimeState.render.textureBytesEstimate;
    runtimeState.diagnostics.undoBytesEstimate = runtimeState.history.undoBytesEstimate;
}

bool App::dispatchEditorCommand(const EditorCommand& cmd)
{
    auto clampFrameIndex = [&](int frameIndex) -> std::size_t
        {
            const int total = std::max(1, (int)engine.getFrameCount());
            return (std::size_t)std::clamp(frameIndex, 0, total - 1);
        };

    switch (cmd.type)
    {
    case EditorCommandType::SelectTool:
        editorUiState.activeTool = cmd.tool;
        if (cmd.tool == ToolType::Ruler)
            editorUiState.rulerVisible = true;
        applyActiveToolStateToEngine(*this);
        break;

    case EditorCommandType::ReplaceToolSettings:
    {
        ToolSettings settings = cmd.toolSettings;
        settings.clamp();
        toolBank.get(cmd.tool) = settings;
        if (editorUiState.activeTool == cmd.tool)
            applyActiveToolStateToEngine(*this);
        else if (cmd.tool == ToolType::Fill)
            editorUiState.activeToolSettings = toolBank.get(editorUiState.activeTool);
        break;
    }

    case EditorCommandType::SyncColorPicker:
        editorUiState.currentColor = colorPicker.getColorRGBA();
        editorUiState.currentGradient = colorPicker.buildGradientConfig();
        applyActiveToolStateToEngine(*this);
        break;

    case EditorCommandType::ToggleRightPanel:
        rightPanelOpenRef() = !rightPanelOpenRef();
        editorUiState.rightPanelOpen = rightPanelOpenRef();
        break;

    case EditorCommandType::Undo:
    {
        storeCurrentDrawFrameLayerTree();
        const std::size_t beforeFrame = engine.getCurrentFrameIndex();
        engine.undo();
        normalizeFrameLayerTrees();
        restoreLayerTreeForCurrentDrawFrame();
        if (beforeFrame != engine.getCurrentFrameIndex())
            dirtyAllThumbs();
        else
            dirtyThumb(engine.getCurrentFrameIndex());
        saveEditorMutation(*this);
        break;
    }

    case EditorCommandType::Redo:
    {
        storeCurrentDrawFrameLayerTree();
        const std::size_t beforeFrame = engine.getCurrentFrameIndex();
        engine.redo();
        normalizeFrameLayerTrees();
        restoreLayerTreeForCurrentDrawFrame();
        if (beforeFrame != engine.getCurrentFrameIndex())
            dirtyAllThumbs();
        else
            dirtyThumb(engine.getCurrentFrameIndex());
        saveEditorMutation(*this);
        break;
    }

    case EditorCommandType::SetOnionEnabled:
        onionSkinEnabled = cmd.boolValue;
        editorUiState.onionSkinEnabled = onionSkinEnabled;
        break;

    case EditorCommandType::SetOnionTint:
        editorUiState.onionTint = cmd.boolValue;
        break;

    case EditorCommandType::SetOnionSteps:
        editorUiState.onionSteps = std::clamp(cmd.intValue, 1, 5);
        break;

    case EditorCommandType::SetOnionPrevAlpha:
        onionPrevAlpha = fclamp(cmd.floatValue, 0.0f, 1.0f);
        editorUiState.onionPrevAlpha = onionPrevAlpha;
        break;

    case EditorCommandType::SetOnionNextAlpha:
        onionNextAlpha = fclamp(cmd.floatValue, 0.0f, 1.0f);
        editorUiState.onionNextAlpha = onionNextAlpha;
        break;

    case EditorCommandType::SetFillGapClose:
        editorUiState.fillGapClose = std::clamp(cmd.intValue, 0, 12);
        break;

    case EditorCommandType::SetActiveFrame:
        switchToFrameIndex(clampFrameIndex(cmd.intValue));
        break;

    case EditorCommandType::AddFrame:
        playingRef() = false;
        storeCurrentDrawFrameLayerTree();
        engine.addFrame();
        initFreshLayerTreeForFrame(engine.getFrameCount() - 1);
        switchToFrameIndex(engine.getFrameCount() - 1);
        markAllThumbsDirty();
        saveEditorMutation(*this);
        break;

    case EditorCommandType::SelectLayerNode:
        if (cmd.nodeId != 0)
        {
            layerTree.handleClick(cmd.nodeId, cmd.addToSelection, cmd.extendSelection);
            storeCurrentDrawFrameLayerTree();
            int trackId = layerTree.primarySelectedTrackId();
            if (trackId != 0)
                engine.setActiveTrack(trackId);
        }
        break;

    case EditorCommandType::SetLayerVisible:
        if (cmd.trackId != 0)
        {
            if (auto* tr = findTimelineTrackByEngineTrackId(*this, cmd.trackId))
                tr->visible = cmd.boolValue;
            engine.setTrackVisible(cmd.trackId, cmd.boolValue);
            dirtyAllThumbs();
            saveEditorMutation(*this);
        }
        break;

    case EditorCommandType::SetLayerLocked:
        if (cmd.trackId != 0)
        {
            if (auto* tr = findTimelineTrackByEngineTrackId(*this, cmd.trackId))
                tr->locked = cmd.boolValue;
            engine.setTrackLocked(cmd.trackId, cmd.boolValue);
            saveEditorMutation(*this);
        }
        break;

    case EditorCommandType::SetLayerFocus:
        editorUiState.isolatedLayerTrackId = (editorUiState.isolatedLayerTrackId == cmd.trackId) ? 0 : cmd.trackId;
        break;

    case EditorCommandType::SetLayerOpacity:
        if (cmd.trackId != 0)
        {
            engine.setTrackOpacity(cmd.trackId, fclamp(cmd.floatValue, 0.0f, 1.0f));
            dirtyAllThumbs();
            saveEditorMutation(*this);
        }
        break;

    case EditorCommandType::SelectTrack:
        if (auto* tr = timeline.findTrack(cmd.trackId))
        {
            if (tr->engineTrackId != 0 && tr->kind == TrackKind::Draw)
            {
                if (auto* node = layerTree.findByTrackId(tr->engineTrackId))
                    layerTree.handleClick(node->id, false, false);
                engine.setActiveTrack(tr->engineTrackId);
                storeCurrentDrawFrameLayerTree();
            }
        }
        break;

    case EditorCommandType::SetTrackMuted:
        if (auto* tr = timeline.findTrack(cmd.trackId))
        {
            tr->muted = cmd.boolValue;
            if (tr->engineTrackId != 0)
            {
                engine.setTrackMuted(tr->engineTrackId, cmd.boolValue);
                dirtyAllThumbs();
                saveEditorMutation(*this);
            }
        }
        break;

    case EditorCommandType::InsertFrame:
    {
        const std::size_t at = clampFrameIndex(cmd.intValue);
        const strova::TrackId preservedFocusTrackId = timeline.isFocused() ? timeline.focusedTrackId() : 0;
        storeCurrentDrawFrameLayerTree();
        ensureFrameLayerTreeSize();

        strova::LayerTree insertedTree;
        int defaultTrackId = 0;
        std::string defaultName = "Draw";
        if (engine.getActiveTrack() != 0)
            defaultTrackId = engine.getActiveTrack();
        for (const auto& tr : timeline.state().tracks)
        {
            if (tr.kind != strova::TrackKind::Draw || tr.engineTrackId == 0)
                continue;
            if (defaultTrackId == 0)
            {
                defaultTrackId = tr.engineTrackId;
                defaultName = tr.name.empty() ? "Draw" : tr.name;
                break;
            }
            if (tr.engineTrackId == defaultTrackId)
            {
                defaultName = tr.name.empty() ? "Draw" : tr.name;
                break;
            }
        }
        if (defaultTrackId != 0)
        {
            insertedTree.addLayerNode(defaultName, defaultTrackId, 0);
            const int firstLayer = insertedTree.firstLayerNodeId();
            if (firstLayer != 0)
                insertedTree.handleClick(firstLayer, false, false);
        }

        if (at <= frameLayerTrees.size())
            frameLayerTrees.insert(frameLayerTrees.begin() + (std::ptrdiff_t)at, insertedTree);
        engine.insertFrame(at);
        normalizeFrameLayerTrees();
        timeline.setTotalFrames((int)engine.getFrameCount());
        markAllThumbsDirty();
        switchToFrameIndex(at);
        if (preservedFocusTrackId != 0 && timeline.findTrack(preservedFocusTrackId))
            timeline.focusTrack(preservedFocusTrackId);
        syncRuntimeStateFromEditor();
        saveEditorMutation(*this);
        break;
    }

    case EditorCommandType::DeleteFrame:
    {
        const std::size_t at = clampFrameIndex(cmd.intValue);
        const strova::TrackId preservedFocusTrackId = timeline.isFocused() ? timeline.focusedTrackId() : 0;
        storeCurrentDrawFrameLayerTree();
        if (frameLayerTrees.size() > at && frameLayerTrees.size() > 1)
            frameLayerTrees.erase(frameLayerTrees.begin() + (std::ptrdiff_t)at);
        engine.deleteFrame(at);
        timeline.setTotalFrames((int)engine.getFrameCount());
        normalizeFrameLayerTrees();
        markAllThumbsDirty();
        switchToFrameIndex(std::min(at, engine.getFrameCount() - 1));
        if (preservedFocusTrackId != 0 && timeline.findTrack(preservedFocusTrackId))
            timeline.focusTrack(preservedFocusTrackId);
        syncRuntimeStateFromEditor();
        saveEditorMutation(*this);
        break;
    }

    case EditorCommandType::DuplicateFrame:
    {
        const std::size_t at = clampFrameIndex(cmd.intValue);
        const strova::TrackId preservedFocusTrackId = timeline.isFocused() ? timeline.focusedTrackId() : 0;
        storeCurrentDrawFrameLayerTree();
        strova::LayerTree duplicatedTree = (at < frameLayerTrees.size()) ? frameLayerTrees[at] : layerTree;
        engine.duplicateFrame(at);
        if (at + 1 <= frameLayerTrees.size())
            frameLayerTrees.insert(frameLayerTrees.begin() + (std::ptrdiff_t)(at + 1), duplicatedTree);
        normalizeFrameLayerTrees();
        timeline.setTotalFrames((int)engine.getFrameCount());
        markAllThumbsDirty();
        switchToFrameIndex(std::min(at + 1, engine.getFrameCount() - 1));
        if (preservedFocusTrackId != 0 && timeline.findTrack(preservedFocusTrackId))
            timeline.focusTrack(preservedFocusTrackId);
        syncRuntimeStateFromEditor();
        saveEditorMutation(*this);
        break;
    }

    case EditorCommandType::MoveFocusedFrame:
    {
        int fromFrame = cmd.intValue;
        int toFrame = cmd.auxIntValue;
        if (fromFrame == toFrame) break;

        const int total = (int)engine.getFrameCount();
        if (total <= 1) break;

        fromFrame = std::max(0, std::min(fromFrame, total - 1));
        toFrame = std::max(0, std::min(toFrame, total - 1));
        if (fromFrame == toFrame) break;

        auto* uiTr = timeline.findTrack(timeline.focusedTrackId());
        if (!uiTr || uiTr->engineTrackId == 0) break;

        const strova::TrackId engTrack = uiTr->engineTrackId;

        engine.beginTimelineTransaction();

        const FrameTrackContentSnapshot carried =
            captureFrameTrackContentSnapshot(*this, fromFrame, engTrack);

        if (fromFrame < toFrame)
        {
            for (int i = fromFrame; i < toFrame; ++i)
            {
                const FrameTrackContentSnapshot next =
                    captureFrameTrackContentSnapshot(*this, i + 1, engTrack);
                applyFrameTrackContentSnapshot(*this, i, engTrack, next);
            }
        }
        else
        {
            for (int i = fromFrame; i > toFrame; --i)
            {
                const FrameTrackContentSnapshot prev =
                    captureFrameTrackContentSnapshot(*this, i - 1, engTrack);
                applyFrameTrackContentSnapshot(*this, i, engTrack, prev);
            }
        }

        applyFrameTrackContentSnapshot(*this, toFrame, engTrack, carried);
        engine.commitTimelineTransaction();

        // Keep per-frame layer metadata attached to its frame index.
        // This command only moves content on the focused track between frames.
        // Reordering frameLayerTrees here causes cross-frame layer leakage.

        normalizeFrameLayerTrees();
        switchToFrameIndex((size_t)toFrame);
        markAllThumbsDirty();
        saveEditorMutation(*this);
        break;
    }

    case EditorCommandType::RenameTrack:
        if (auto* tr = timeline.findTrack(cmd.trackId))
        {
            std::string base = (tr->kind == TrackKind::Draw) ? "Draw" : (tr->kind == TrackKind::Flow ? "Flow" : "Track");
            std::string newName = base + std::string(" ") + std::to_string((int)cmd.trackId);
            tr->name = newName;
            engine.setTrackName(tr->engineTrackId, newName);
            saveEditorMutation(*this);
        }
        break;

    case EditorCommandType::DeleteTrack:
        if (auto* tr = timeline.findTrack(cmd.trackId))
        {
            if (tr->kind == TrackKind::Draw)
            {
                int drawCount = 0;
                for (const auto& item : timeline.state().tracks)
                    if (item.kind == TrackKind::Draw) ++drawCount;
                if (drawCount <= 1)
                    break;
            }

            if (tr->engineTrackId != 0)
                engine.removeTrack(tr->engineTrackId);

            auto& tracks = timeline.state().tracks;
            tracks.erase(
                std::remove_if(
                    tracks.begin(),
                    tracks.end(),
                    [&](const strova::TimelineTrack& item) { return item.id == cmd.trackId; }),
                tracks.end());

            if (timeline.isFocused() && timeline.focusedTrackId() == cmd.trackId)
                timeline.clearFocus();

            if (editorUiState.activeLayerTrackId == tr->engineTrackId)
                editorUiState.activeLayerTrackId = 0;
            if (editorUiState.isolatedLayerTrackId == tr->engineTrackId)
                editorUiState.isolatedLayerTrackId = 0;

            for (auto& frameTree : frameLayerTrees)
                frameTree.syncExistingFromTimeline(timeline);
            refreshLayerPanelForActiveFrame();

            int fallbackTrackId = layerTree.primarySelectedTrackId();
            if (fallbackTrackId == 0)
            {
                for (const auto& item : timeline.state().tracks)
                {
                    if (item.kind == TrackKind::Draw && item.engineTrackId != 0)
                    {
                        fallbackTrackId = item.engineTrackId;
                        break;
                    }
                }
            }
            if (fallbackTrackId != 0 && engine.findTrack(fallbackTrackId) != nullptr)
            {
                editorUiState.activeLayerTrackId = fallbackTrackId;
                editorUiState.activeLayerVisible = engine.getTrackVisible(fallbackTrackId);
                editorUiState.activeLayerLocked = engine.getTrackLocked(fallbackTrackId);
                editorUiState.activeLayerOpacity = engine.getTrackOpacity(fallbackTrackId);
                engine.setActiveTrack(fallbackTrackId);
            }
            else
            {
                editorUiState.activeLayerVisible = true;
                editorUiState.activeLayerLocked = false;
                editorUiState.activeLayerOpacity = 1.0f;
            }

            markAllThumbsDirty();
            syncRuntimeStateFromEditor();
            saveEditorMutation(*this);
        }
        break;

    case EditorCommandType::SetRulerVisible:
        editorUiState.rulerVisible = cmd.boolValue;
        break;
    }

    validateEditorState();
    syncUIFromState();
    return true;
}

void App::ensureFrameLayerTreeSize()
{
    const size_t count = std::max<size_t>(1, engine.getFrameCount());
    if (frameLayerTrees.size() < count)
        frameLayerTrees.resize(count);
}

strova::LayerTree& App::activeFrameLayerTree()
{
    if (engine.getFrameCount() == 0)
        engine.clearAndResizeFrames(1);

    ensureFrameLayerTreeSize();

    if (frameLayerTrees.empty())
        frameLayerTrees.resize(1);

    const size_t fi = std::min(engine.getCurrentFrameIndex(), frameLayerTrees.size() - 1);
    return frameLayerTrees[fi];
}

const strova::LayerTree& App::activeFrameLayerTree() const
{
    return const_cast<App*>(this)->activeFrameLayerTree();
}

void App::refreshLayerPanelForActiveFrame()
{
    strova::LayerTree& tree = activeFrameLayerTree();
    tree.syncExistingFromTimeline(timeline);

    if (tree.primarySelectedTrackId() == 0)
    {
        const int firstLayer = tree.firstLayerNodeId();
        if (firstLayer != 0)
            tree.handleClick(firstLayer, false, false);
    }

    layerTree = tree;
}

void App::normalizeFrameLayerTrees()
{
    ensureFrameLayerTreeSize();
    if (frameLayerTrees.size() > engine.getFrameCount())
        frameLayerTrees.resize(engine.getFrameCount());

    auto buildFreshFrameTree = [this]()
    {
        strova::LayerTree fresh;
        int defaultTrackId = 0;
        std::string defaultName = "Draw";

        if (engine.getActiveTrack() != 0)
            defaultTrackId = engine.getActiveTrack();

        for (const auto& tr : timeline.state().tracks)
        {
            if (tr.kind != strova::TrackKind::Draw || tr.engineTrackId == 0)
                continue;

            if (defaultTrackId == 0)
            {
                defaultTrackId = tr.engineTrackId;
                defaultName = tr.name.empty() ? "Draw" : tr.name;
                break;
            }

            if (tr.engineTrackId == defaultTrackId)
            {
                defaultName = tr.name.empty() ? "Draw" : tr.name;
                break;
            }
        }

        if (defaultTrackId != 0)
        {
            fresh.addLayerNode(defaultName, defaultTrackId, 0);
            const int firstLayer = fresh.firstLayerNodeId();
            if (firstLayer != 0)
                fresh.handleClick(firstLayer, false, false);
        }
        return fresh;
    };

    for (size_t i = 0; i < frameLayerTrees.size(); ++i)
    {
        if (frameLayerTrees[i].getNodes().empty())
            frameLayerTrees[i] = buildFreshFrameTree();

        frameLayerTrees[i].syncExistingFromTimeline(timeline);
        if (frameLayerTrees[i].primarySelectedTrackId() == 0)
        {
            const int firstLayer = frameLayerTrees[i].firstLayerNodeId();
            if (firstLayer != 0)
                frameLayerTrees[i].handleClick(firstLayer, false, false);
        }
    }
}

void App::validateEditorState()
{
    if (engine.getFrameCount() == 0)
        engine.clearAndResizeFrames(1);

    timeline.setTotalFrames(std::max(1, (int)engine.getFrameCount()));

    if (timeline.state().tracks.empty())
        setDefaultTimeline(timeline, (int)engine.getFrameCount());

    if (timeline.isFocused())
    {
        const auto* focusedTrack = timeline.findTrack(timeline.focusedTrackId());
        if (!focusedTrack || (focusedTrack->engineTrackId != 0 && engine.findTrack(focusedTrack->engineTrackId) == nullptr))
            timeline.clearFocus();
    }

    bool haveDrawTrack = false;
    for (const auto& tr : timeline.state().tracks)
        if (tr.kind == strova::TrackKind::Draw) { haveDrawTrack = true; break; }
    if (!haveDrawTrack)
    {
        const int drawUi = timeline.addTrack(strova::TrackKind::Draw, "Draw");
        if (auto* drawTrack = timeline.findTrack(drawUi))
            drawTrack->engineTrackId = engine.createTrack(DrawingEngine::TrackKind::Draw, "Draw");
        if (drawUi != 0)
            timeline.addClip(drawUi, 0, std::max(1, (int)engine.getFrameCount()), "Main");
    }

    for (auto& tr : timeline.state().tracks)
    {
        if (tr.engineTrackId != 0)
            continue;
        DrawingEngine::TrackKind ek = DrawingEngine::TrackKind::Draw;
        if (tr.kind == strova::TrackKind::Flow) ek = DrawingEngine::TrackKind::Flow;
        else if (tr.kind == strova::TrackKind::FlowLink) ek = DrawingEngine::TrackKind::FlowLink;
        else if (tr.kind == strova::TrackKind::Audio) ek = DrawingEngine::TrackKind::Audio;
        tr.engineTrackId = engine.createTrack(ek, tr.name.empty() ? std::string("Track") : tr.name);
    }

    normalizeFrameLayerTrees();

    const size_t safeFrameCount = std::max<std::size_t>(1, engine.getFrameCount());
    const size_t activeFrame = std::min(engine.getCurrentFrameIndex(), safeFrameCount - 1);
    if (activeFrame != engine.getCurrentFrameIndex())
        engine.setCurrentFrameIndex(activeFrame);
    timeline.setPlayheadFrame((int)engine.getCurrentFrameIndex());
    editorUiState.activeFrameIndex = (int)engine.getCurrentFrameIndex();
    refreshLayerPanelForActiveFrame();

    if (projectW < 1) projectW = 1;
    if (projectH < 1) projectH = 1;
    if (projectFPS < 1) projectFPS = 1;
    projectW = strova::limits::clampCanvasWidth(projectW);
    projectH = strova::limits::clampCanvasHeight(projectH);
    projectFPS = strova::limits::clampProjectFps(projectFPS);
    flow.settings.clampToSafeLimits();
    setProjectFpsForFlow(projectFPS);

    leftBarRatio = fclamp(leftBarRatio, 0.05f, 0.28f);
    onionPrevAlpha = fclamp(onionPrevAlpha, 0.0f, 1.0f);
    onionNextAlpha = fclamp(onionNextAlpha, 0.0f, 1.0f);
    editorUiState.onionSteps = std::clamp(editorUiState.onionSteps, 1, 5);

    ToolSettings& settings = toolBank.get(editorUiState.activeTool);
    settings.clamp();
    editorUiState.activeToolSettings = settings;

    if (editorUiState.activeLayerTrackId != 0 && engine.findTrack(editorUiState.activeLayerTrackId) == nullptr)
        editorUiState.activeLayerTrackId = 0;
    if (editorUiState.isolatedLayerTrackId != 0 && engine.findTrack(editorUiState.isolatedLayerTrackId) == nullptr)
        editorUiState.isolatedLayerTrackId = 0;

    int primaryTrackId = layerTree.primarySelectedTrackId();
    if (primaryTrackId != 0 && engine.findTrack(primaryTrackId) == nullptr)
    {
        primaryTrackId = 0;
    }

    if (primaryTrackId == 0)
    {
        primaryTrackId = editorUiState.activeLayerTrackId;
        if (primaryTrackId == 0)
            primaryTrackId = engine.getActiveTrack();
    }

    if (primaryTrackId != 0 && engine.findTrack(primaryTrackId) != nullptr)
    {
        editorUiState.activeLayerTrackId = primaryTrackId;
        engine.setActiveTrack(primaryTrackId);
    }

    editorUiState.activeLayerVisible = (editorUiState.activeLayerTrackId != 0) ? engine.getTrackVisible(editorUiState.activeLayerTrackId) : true;
    editorUiState.activeLayerLocked = (editorUiState.activeLayerTrackId != 0) ? engine.getTrackLocked(editorUiState.activeLayerTrackId) : false;
    editorUiState.activeLayerOpacity = (editorUiState.activeLayerTrackId != 0) ? engine.getTrackOpacity(editorUiState.activeLayerTrackId) : 1.0f;

    runtimeState.capture.lastFlowSampleCount = (int)flow.getSamples().size();
    runtimeState.capture.lastFlowLinkSampleCount = (int)flow.getFlowLinkSamples().size();
    runtimeState.render.dirtyFrameCount = engine.countDirtyFrameComposites();
    runtimeState.render.dirtyRegionCount = std::max(engine.countDirtyRasterTiles(), runtimeState.render.dirtyFrameCount);
    runtimeState.history.undoBytesEstimate = engine.estimateUndoBytes();
    if (runtimeState.history.undoBytesEstimate > strova::limits::kMaxUndoBytes)
        runtimeState.history.undoBytesEstimate = strova::limits::kMaxUndoBytes;

    syncRuntimeStateFromEditor();
}

void App::enqueueEditorEvent(const SDL_Event& e)
{
    runtimeState.input.queuedEvents.push_back(e);
}

std::vector<SDL_Event> App::takeQueuedEditorEvents()
{
    std::vector<SDL_Event> events = std::move(runtimeState.input.queuedEvents);
    runtimeState.input.queuedEvents.clear();
    runtimeState.input.processedEventCount = 0;
    return events;
}

void App::beginEditorFrame(double dt)
{
    int mx = 0;
    int my = 0;
    const Uint32 buttons = SDL_GetMouseState(&mx, &my);

    runtimeState.diagnostics.beginFrame(dt);
    runtimeState.render.thumbRebuildsThisFrame = 0;
    runtimeState.render.dirtyRegionCount = 0;
    runtimeState.input.beginFrame(mx, my, buttons);
    runtimeState.ui.mouseCaptured = false;
    runtimeState.ui.keyboardCaptured = false;
    runtimeState.ui.modalActive = false;
    runtimeState.ui.textInputActive = colorPicker.isEditingText();
    syncRuntimeStateFromEditor();
}

void App::beginEditorInputRouting(int mouseX, int mouseY, Uint32 mouseButtons)
{
    runtimeState.input.mouseX = mouseX;
    runtimeState.input.mouseY = mouseY;
    runtimeState.input.mouseButtons = mouseButtons;
    runtimeState.input.mouseOwner = EditorRuntimeState::InputOwner::None;
    runtimeState.input.toolMouseAllowed = true;
    runtimeState.ui.mouseCaptured = false;
    runtimeState.ui.keyboardCaptured = runtimeState.ui.textInputActive;
    runtimeState.input.toolKeyboardAllowed = !runtimeState.ui.textInputActive;
}

void App::captureEditorUi(EditorRuntimeState::InputOwner owner, bool captureKeyboard)
{
    runtimeState.ui.mouseCaptured = true;
    runtimeState.input.mouseOwner = owner;
    runtimeState.input.toolMouseAllowed = false;
    if (captureKeyboard)
    {
        runtimeState.ui.keyboardCaptured = true;
        runtimeState.input.toolKeyboardAllowed = false;
    }
}

void App::captureEditorTool(EditorRuntimeState::InputOwner owner)
{
    if (runtimeState.input.mouseOwner == EditorRuntimeState::InputOwner::None)
        runtimeState.input.mouseOwner = owner;
}

bool App::editorToolsCanUseMouse() const
{
    return runtimeState.input.toolMouseAllowed && !runtimeState.ui.mouseCaptured;
}

bool App::editorToolsCanUseKeyboard() const
{
    return runtimeState.input.toolKeyboardAllowed && !runtimeState.ui.keyboardCaptured;
}

void App::noteInputProcessingTime(double ms)
{
    runtimeState.diagnostics.inputMs += std::max(0.0, ms);
}

void App::noteUiProcessingTime(double ms)
{
    runtimeState.diagnostics.uiMs += std::max(0.0, ms);
}

void App::notePlaybackProcessingTime(double ms)
{
    runtimeState.diagnostics.playbackMs += std::max(0.0, ms);
}

void App::noteRenderTime(double ms)
{
    runtimeState.diagnostics.renderMs = std::max(0.0, ms);
}

void App::noteCompositeRebuild(int count)
{
    runtimeState.diagnostics.compositeRebuildCount += std::max(0, count);
}

void App::noteLayerRebuild(int count)
{
    runtimeState.diagnostics.layerRebuildCount += std::max(0, count);
}

void App::noteThumbRebuild()
{
    ++runtimeState.render.thumbRebuildsThisFrame;
    noteLayerRebuild(1);
}

void App::noteDirtyRegionCount(int count)
{
    runtimeState.render.dirtyRegionCount = std::max(0, count);
    runtimeState.diagnostics.dirtyRegionCount = runtimeState.render.dirtyRegionCount;
}

void App::noteTextureBytesEstimate(std::size_t bytes)
{
    runtimeState.render.textureBytesEstimate = bytes;
    runtimeState.diagnostics.textureBytesEstimate = bytes;
}

void App::noteUndoBytesEstimate(std::size_t bytes)
{
    runtimeState.history.undoBytesEstimate = bytes;
    runtimeState.diagnostics.undoBytesEstimate = bytes;
}

#include "AppPersistenceUi.inl"

void App::returnToLauncher()
{
    mode = Mode::Launcher;
}

void App::openProjectPath(const std::string& path)
{
    openProject(path);
}

void App::createDefaultProject()
{
    createNewProjectDefault();
}

void App::openBrushCreatorWorkspace(const std::string& brushProjectPath, bool returnToEditor)
{
    brushCreatorReturnMode = returnToEditor ? Mode::Editor : Mode::Launcher;
    if (brushCreatorScreen)
        brushCreatorScreen->open(*this, brushProjectPath);
    mode = Mode::BrushCreator;
}

void App::openBrushCreatorWorkspace(const strova::brush::BrushProject& project, bool returnToEditor)
{
    brushCreatorReturnMode = returnToEditor ? Mode::Editor : Mode::Launcher;
    if (brushCreatorScreen)
        brushCreatorScreen->open(*this, project, returnToEditor ? "Brush Creator opened from editor" : "Brush Creator opened");
    mode = Mode::BrushCreator;
}

void App::closeBrushCreatorWorkspace()
{
    SDL_StopTextInput();
    mode = brushCreatorReturnMode;
    if (mode == Mode::Editor)
        syncRuntimeStateFromEditor();
}

void App::setToolCommand(ToolType tool)
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::SelectTool;
    cmd.tool = tool;
    dispatchEditorCommand(cmd);
}

void App::replaceToolSettingsCommand(ToolType tool, const ToolSettings& settings)
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::ReplaceToolSettings;
    cmd.tool = tool;
    cmd.toolSettings = settings;
    dispatchEditorCommand(cmd);
}

void App::syncColorPickerCommand()
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::SyncColorPicker;
    dispatchEditorCommand(cmd);
}

void App::toggleRightPanelCommand()
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::ToggleRightPanel;
    dispatchEditorCommand(cmd);
}

void App::setOnionEnabledCommand(bool enabled)
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::SetOnionEnabled;
    cmd.boolValue = enabled;
    dispatchEditorCommand(cmd);
}

void App::setOnionTintCommand(bool tinted)
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::SetOnionTint;
    cmd.boolValue = tinted;
    dispatchEditorCommand(cmd);
}

void App::setOnionStepsCommand(int steps)
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::SetOnionSteps;
    cmd.intValue = steps;
    dispatchEditorCommand(cmd);
}

void App::setOnionPrevAlphaCommand(float alpha)
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::SetOnionPrevAlpha;
    cmd.floatValue = alpha;
    dispatchEditorCommand(cmd);
}

void App::setOnionNextAlphaCommand(float alpha)
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::SetOnionNextAlpha;
    cmd.floatValue = alpha;
    dispatchEditorCommand(cmd);
}

void App::setFillGapCloseCommand(int gapClose)
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::SetFillGapClose;
    cmd.intValue = gapClose;
    dispatchEditorCommand(cmd);
}

void App::setRulerVisibleCommand(bool visible)
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::SetRulerVisible;
    cmd.boolValue = visible;
    dispatchEditorCommand(cmd);
}

void App::setLayerFocusCommand(int trackId)
{
    EditorCommand cmd{};
    cmd.type = EditorCommandType::SetLayerFocus;
    cmd.trackId = trackId;
    dispatchEditorCommand(cmd);
}

void App::markCurrentFrameEdited()
{
    markFrameEdited(engine.getCurrentFrameIndex());
}

void App::markFrameEdited(std::size_t frameIndex)
{
    markThumbDirty(frameIndex);
    validateEditorState();
    std::string recoveryErr;
    if (!saveRecoverySnapshot(recoveryErr) && !recoveryErr.empty())
        strova::debug::log("Recovery", recoveryErr);
    syncRuntimeStateFromEditor();
}

void App::markCurrentFrameEditedAndSave()
{
    markCurrentFrameEdited();
    requestSaveProjectNow();
}

void App::markFrameEditedAndSave(std::size_t frameIndex)
{
    markFrameEdited(frameIndex);
    requestSaveProjectNow();
}

void App::storeCurrentDrawFrameLayerTree()
{
    normalizeFrameLayerTrees();
    strova::LayerTree& perFrame = activeFrameLayerTree();
    perFrame = layerTree;
    perFrame.syncExistingFromTimeline(timeline);
    layerTree = perFrame;
}

void App::restoreLayerTreeForCurrentDrawFrame()
{
    normalizeFrameLayerTrees();
    const size_t fi = std::min(engine.getCurrentFrameIndex(), frameLayerTrees.size() - 1);
    if (frameLayerTrees[fi].getNodes().empty())
        initFreshLayerTreeForFrame(fi);

    refreshLayerPanelForActiveFrame();

    int activeTrackId = layerTree.primarySelectedTrackId();
    if (activeTrackId == 0)
    {
        for (const auto& tr : timeline.state().tracks)
        {
            if (tr.kind == strova::TrackKind::Draw && tr.engineTrackId != 0)
            {
                activeTrackId = tr.engineTrackId;
                break;
            }
        }
    }

    editorUiState.activeLayerTrackId = 0;
    editorUiState.activeLayerVisible = true;
    editorUiState.activeLayerLocked = false;
    editorUiState.activeLayerOpacity = 1.0f;

    if (activeTrackId != 0)
    {
        engine.setActiveTrack(activeTrackId);
        editorUiState.activeLayerTrackId = activeTrackId;
        editorUiState.activeLayerVisible = engine.getTrackVisible(activeTrackId);
        editorUiState.activeLayerLocked = engine.getTrackLocked(activeTrackId);
        editorUiState.activeLayerOpacity = engine.getTrackOpacity(activeTrackId);
    }
}

void App::initFreshLayerTreeForFrame(std::size_t frameIndex)
{
    normalizeFrameLayerTrees();
    frameIndex = std::min(frameIndex, frameLayerTrees.size() - 1);

    strova::LayerTree fresh;
    int defaultTrackId = 0;
    std::string defaultName = "Draw";

    if (engine.getActiveTrack() != 0)
        defaultTrackId = engine.getActiveTrack();

    for (const auto& tr : timeline.state().tracks)
    {
        if (tr.kind != strova::TrackKind::Draw || tr.engineTrackId == 0)
            continue;

        if (defaultTrackId == 0)
        {
            defaultTrackId = tr.engineTrackId;
            defaultName = tr.name.empty() ? "Draw" : tr.name;
            break;
        }

        if (tr.engineTrackId == defaultTrackId)
        {
            defaultName = tr.name.empty() ? "Draw" : tr.name;
            break;
        }
    }

    if (defaultTrackId != 0)
    {
        fresh.addLayerNode(defaultName, defaultTrackId, 0);
        const int firstLayer = fresh.firstLayerNodeId();
        if (firstLayer != 0)
            fresh.handleClick(firstLayer, false, false);
    }

    frameLayerTrees[frameIndex] = fresh;
}

void App::onFrameChanged()
{
    validateEditorState();
    restoreLayerTreeForCurrentDrawFrame();

    if (timeline.isFocused())
    {
        const strova::TrackId focusedId = timeline.focusedTrackId();
        const auto* focusedTrack = timeline.findTrack(focusedId);
        const bool keepFocusedFlowLinkTrack = focusedTrack && focusedTrack->kind == strova::TrackKind::FlowLink;
        if (!keepFocusedFlowLinkTrack && (focusedId == 0 || layerTree.findByTrackId((int)focusedId) == nullptr))
            timeline.clearFocus();
    }

    if (editorUiState.isolatedLayerTrackId != 0 && layerTree.findByTrackId(editorUiState.isolatedLayerTrackId) == nullptr)
        editorUiState.isolatedLayerTrackId = 0;

    const int restoredActiveTrackId = layerTree.primarySelectedTrackId();
    if (restoredActiveTrackId != 0 && engine.findTrack(restoredActiveTrackId) != nullptr)
    {
        editorUiState.activeLayerTrackId = restoredActiveTrackId;
        editorUiState.activeLayerVisible = engine.getTrackVisible(restoredActiveTrackId);
        editorUiState.activeLayerLocked = engine.getTrackLocked(restoredActiveTrackId);
        editorUiState.activeLayerOpacity = engine.getTrackOpacity(restoredActiveTrackId);
        engine.setActiveTrack(restoredActiveTrackId);
    }
    else
    {
        editorUiState.activeLayerTrackId = 0;
        editorUiState.activeLayerVisible = true;
        editorUiState.activeLayerLocked = false;
        editorUiState.activeLayerOpacity = 1.0f;
    }

    timeline.setTotalFrames(std::max(1, (int)engine.getFrameCount()));
    timeline.setPlayheadFrame((int)engine.getCurrentFrameIndex());
    editorUiState.activeFrameIndex = (int)engine.getCurrentFrameIndex();
    syncRuntimeStateFromEditor();
}

void App::switchToFrameIndex(std::size_t frameIndex)
{
    validateEditorState();
    if (engine.getFrameCount() == 0)
        engine.clearAndResizeFrames(1);
    ensureFrameLayerTreeSize();
    storeCurrentDrawFrameLayerTree();
    frameIndex = std::min(frameIndex, engine.getFrameCount() - 1);
    engine.setCurrentFrameIndex(frameIndex);
    onFrameChanged();
}

void App::updatePlayback(double dt)
{
    if (!playingRef()) return;

    const int fps = (projectFPS > 0) ? projectFPS : 1;
    const double frameDt = 1.0 / (double)fps;
    const size_t total = engine.getFrameCount();
    if (total <= 1)
    {
        runtimeState.playback.lastStepMs = frameDt * 1000.0;
        return;
    }

    playAccumulatorRef() += dt;
    const int steps = (frameDt > 0.0) ? (int)std::floor(playAccumulatorRef() / frameDt) : 0;
    if (steps <= 0)
    {
        runtimeState.playback.lastStepMs = frameDt * 1000.0;
        return;
    }

    playAccumulatorRef() -= (double)steps * frameDt;

    // Avoid the heavy frame-switch persistence path during playback.
    // Playback should only advance the visible frame, not rewrite per-frame editor state.
    const size_t current = engine.getCurrentFrameIndex();
    const size_t idx = (current + (size_t)steps) % total;
    engine.setCurrentFrameIndex(idx);
    onFrameChanged();

    runtimeState.playback.lastStepMs = frameDt * 1000.0;
    syncRuntimeStateFromEditor();
}

static bool writeTextFile(const fs::path& p, const std::string& s)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << s;
    f.flush();
    return (bool)f;
}

static bool writeTextFileAtomic(const fs::path& p, const std::string& s)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    if (!writeTextFile(tmp, s)) return false;
    fs::remove(p, ec);
    fs::rename(tmp, p, ec);
    if (ec)
    {
        fs::copy_file(tmp, p, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
        if (ec) return false;
    }
    return true;
}

static bool copyFileAtomic(const fs::path& src, const fs::path& dst)
{
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    const fs::path tmp = dst.string() + ".tmp";
    fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::remove(dst, ec);
    fs::rename(tmp, dst, ec);
    if (ec)
    {
        fs::copy_file(tmp, dst, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp, ec);
        if (ec) return false;
    }
    return true;
}

static void clearDirectoryFiles(const fs::path& dir)
{
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    for (const auto& entry : fs::directory_iterator(dir, ec))
        fs::remove_all(entry.path(), ec);
}

static std::string sanitizeProjectKey(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
    {
        if (std::isalnum(c)) out.push_back((char)std::tolower(c));
        else if (c == '_' || c == '-' || c == '.') out.push_back((char)c);
        else out.push_back('_');
    }
    if (out.empty()) out = "untitled";
    return out;
}

static bool readTextFile(const fs::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;

    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}


static std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 16);

    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                out += buf;
            }
            else
            {
                out += static_cast<char>(c);
            }
            break;
        }
    }

    return out;
}

static bool parseFloat(const std::string& s, size_t& i, float& out)
{
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;

    size_t start = i;
    bool dot = false;

    if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;

    while (i < s.size())
    {
        char c = s[i];
        if (c >= '0' && c <= '9') { i++; continue; }
        if (c == '.' && !dot) { dot = true; i++; continue; }
        break;
    }

    if (i == start) return false;

    try { out = std::stof(s.substr(start, i - start)); }
    catch (...) { return false; }

    return true;
}

static bool parseInt(const std::string& s, size_t& i, int& out)
{
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;

    size_t start = i;

    if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
    while (i < s.size() && (s[i] >= '0' && s[i] <= '9')) i++;

    if (i == start) return false;

    try { out = std::stoi(s.substr(start, i - start)); }
    catch (...) { return false; }

    return true;
}

static bool parseString(const std::string& s, size_t& i, std::string& out)
{
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    std::string v;
    bool escape = false;
    while (i < s.size())
    {
        const char c = s[i++];
        if (escape)
        {
            switch (c)
            {
            case 'n': v.push_back('\n'); break;
            case 'r': v.push_back('\r'); break;
            case 't': v.push_back('\t'); break;
            case '\\': v.push_back('\\'); break;
            case '"': v.push_back('"'); break;
            default: v.push_back(c); break;
            }
            escape = false;
            continue;
        }
        if (c == '\\')
        {
            escape = true;
            continue;
        }
        if (c == '"')
        {
            out = std::move(v);
            return true;
        }
        v.push_back(c);
    }
    return false;
}

static void consume(const std::string& s, size_t& i, char ch)
{
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i < s.size() && s[i] == ch) ++i;
}

static size_t findMatchingBrace(const std::string& s, size_t openPos)
{
    if (openPos >= s.size() || s[openPos] != '{') return std::string::npos;
    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t i = openPos; i < s.size(); ++i)
    {
        const char c = s[i];
        if (inString)
        {
            if (escape) { escape = false; continue; }
            if (c == '\\') { escape = true; continue; }
            if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}')
        {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

static bool findKeyPos(const std::string& s, const std::string& key, size_t& outPos)
{
    const std::string marker = "\"" + key + "\"";
    size_t p = s.find(marker);
    if (p == std::string::npos) return false;
    p = s.find(':', p + marker.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
    outPos = p;
    return true;
}

static bool loadOneFrameJson(const fs::path& framePath, std::vector<Stroke>& outStrokes)
{
    outStrokes.clear();

    std::string j;
    if (!readTextFile(framePath, j)) return false;

    size_t i = 0;

    while (true)
    {
        size_t sPos = j.find("\"color\"", i);
        if (sPos == std::string::npos) break;
        i = sPos;

        Stroke s{};
        s.color = SDL_Color{ 0, 0, 0, 255 };
        s.thickness = 2.0f;

        size_t arr = j.find('[', i);
        if (arr == std::string::npos) break;

        i = arr + 1;

        int r = 0, g = 0, b = 0, a = 255;

        if (!parseInt(j, i, r)) break;
        if (j.find(',', i) != std::string::npos) i = j.find(',', i) + 1;
        if (!parseInt(j, i, g)) break;
        if (j.find(',', i) != std::string::npos) i = j.find(',', i) + 1;
        if (!parseInt(j, i, b)) break;
        if (j.find(',', i) != std::string::npos) i = j.find(',', i) + 1;
        if (!parseInt(j, i, a)) a = 255;

        s.color = SDL_Color{
            (Uint8)std::clamp(r, 0, 255),
            (Uint8)std::clamp(g, 0, 255),
            (Uint8)std::clamp(b, 0, 255),
            (Uint8)std::clamp(a, 0, 255)
        };

        size_t tPos = j.find("\"thickness\"", i);
        if (tPos != std::string::npos)
        {
            size_t colon = j.find(':', tPos);
            if (colon != std::string::npos)
            {
                i = colon + 1;
                float th = 2.0f;
                if (parseFloat(j, i, th)) s.thickness = th;
            }
        }

        size_t pPos = j.find("\"points\"", i);
        if (pPos == std::string::npos)
        {
            outStrokes.push_back(s);
            continue;
        }

        size_t pArr = j.find('[', pPos);
        if (pArr == std::string::npos)
        {
            outStrokes.push_back(s);
            continue;
        }

        i = pArr + 1;

        while (i < j.size())
        {
            size_t ptOpen = j.find('[', i);
            size_t ptClose = j.find(']', i);

            if (ptClose == std::string::npos) break;

            if (ptOpen == std::string::npos || ptOpen > ptClose)
            {
                i = ptClose + 1;
                break;
            }

            i = ptOpen + 1;

            float x = 0, y = 0, pr = 1.0f;

            if (!parseFloat(j, i, x)) break;

            size_t c1 = j.find(',', i);
            if (c1 == std::string::npos) break;
            i = c1 + 1;

            if (!parseFloat(j, i, y)) break;

            size_t c2 = j.find(',', i);
            size_t endBracket = j.find(']', i);

            if (c2 != std::string::npos && endBracket != std::string::npos && c2 < endBracket)
            {
                i = c2 + 1;
                (void)parseFloat(j, i, pr);
            }

            size_t endPt = j.find(']', i);
            if (endPt == std::string::npos) break;
            i = endPt + 1;

            StrokePoint sp{};
            sp.x = x;
            sp.y = y;
            sp.pressure = pr;

            s.points.push_back(sp);
        }

        outStrokes.push_back(s);
    }

    return true;
}


struct LoadedFrameLayer
{
    int trackId = 0;
    DrawingEngine::LayerContentType contentType = DrawingEngine::LayerContentType::Stroke;
    DrawingEngine::LayerTransform transform{};
    DrawingEngine::LayerImage image{};
    std::vector<Stroke> strokes;
    std::vector<DrawingEngine::TransformKeyframe> posXKeys;
    std::vector<DrawingEngine::TransformKeyframe> posYKeys;
    std::vector<DrawingEngine::TransformKeyframe> rotationKeys;
    std::vector<DrawingEngine::VisibilityKey> visibilityKeys;
    DrawingEngine::TransformInterpolationMode interpolationMode = DrawingEngine::TransformInterpolationMode::Linear;
    bool hasDrawEntry = false;
    bool hasTransformEntry = false;
    std::uint64_t celId = 0;
    int drawSourceFrame = -1;
    bool ownsDrawContent = false;
};

static void extractJsonObjects(const std::string& arrText, std::vector<std::string>& outObjs)
{
    outObjs.clear();
    size_t i = 0;
    while (i < arrText.size())
    {
        size_t a = arrText.find('{', i);
        if (a == std::string::npos) break;
        int depth = 0;
        size_t b = std::string::npos;
        for (size_t k = a; k < arrText.size(); ++k)
        {
            if (arrText[k] == '{') depth++;
            else if (arrText[k] == '}')
            {
                depth--;
                if (depth == 0) { b = k; break; }
            }
        }
        if (b == std::string::npos) break;
        outObjs.push_back(arrText.substr(a, b - a + 1));
        i = b + 1;
    }
}

static bool loadLayeredFrameJson(const fs::path& framePath, std::vector<LoadedFrameLayer>& outLayers, std::vector<Stroke>& outLegacyComposite)
{
    outLayers.clear();
    outLegacyComposite.clear();

    std::string j;
    if (!readTextFile(framePath, j)) return false;

    size_t layersKey = j.find("\"layers\"");
    if (layersKey == std::string::npos)
        return loadOneFrameJson(framePath, outLegacyComposite);

    size_t arr0 = j.find('[', layersKey);
    if (arr0 == std::string::npos)
        return false;

    int depth = 0;
    size_t arr1 = std::string::npos;
    for (size_t i = arr0; i < j.size(); ++i)
    {
        if (j[i] == '[') depth++;
        else if (j[i] == ']')
        {
            depth--;
            if (depth == 0) { arr1 = i; break; }
        }
    }
    if (arr1 == std::string::npos)
        return false;

    std::string arrText = j.substr(arr0, arr1 - arr0 + 1);
    std::vector<std::string> objs;
    extractJsonObjects(arrText, objs);

    auto parseImageObject = [&](const std::string& obj, DrawingEngine::LayerImage& image)
    {
        size_t p = obj.find("\"image\"");
        if (p == std::string::npos) return;
        p = obj.find('{', p);
        if (p == std::string::npos) return;
        size_t endObj = findMatchingBrace(obj, p);
        if (endObj == std::string::npos) return;
        std::string iobj = obj.substr(p, endObj - p + 1);

        size_t q = 0;
        int parsedWidth = 0;
        int parsedHeight = 0;
        if (findKeyPos(iobj, "width", q)) { size_t i = q; parseInt(iobj, i, parsedWidth); }
        if (findKeyPos(iobj, "height", q)) { size_t i = q; parseInt(iobj, i, parsedHeight); }

        std::string parsedSourcePath;
        if (findKeyPos(iobj, "sourcePath", q))
        {
            size_t i = q;
            (void)parseString(iobj, i, parsedSourcePath);
        }

        std::string rgbaAssetRel;
        if (findKeyPos(iobj, "rgbaAsset", q))
        {
            size_t i = q;
            (void)parseString(iobj, i, rgbaAssetRel);
        }
        if (!rgbaAssetRel.empty())
        {
            (void)loadLayerImageAssetFromProject(framePath.parent_path().parent_path(), rgbaAssetRel, image);
            if (!parsedSourcePath.empty())
                image.setSourcePath(parsedSourcePath);
        }

        size_t pixPos = iobj.find("\"pixels\"");
        if (pixPos != std::string::npos)
        {
            std::vector<std::uint8_t> parsedPixels;
            size_t a0 = iobj.find('[', pixPos);
            if (a0 != std::string::npos)
            {
                int d = 0;
                size_t a1 = std::string::npos;
                for (size_t i = a0; i < iobj.size(); ++i)
                {
                    if (iobj[i] == '[') d++;
                    else if (iobj[i] == ']')
                    {
                        d--;
                        if (d == 0) { a1 = i; break; }
                    }
                }
                if (a1 != std::string::npos)
                {
                    size_t i = a0 + 1;
                    while (i < a1)
                    {
                        int v = 0;
                        if (!parseInt(iobj, i, v))
                        {
                            ++i;
                            continue;
                        }
                        parsedPixels.push_back((std::uint8_t)std::clamp(v, 0, 255));
                        consume(iobj, i, ',');
                    }
                }
            }
            if (!parsedPixels.empty())
                image.setData(parsedWidth, parsedHeight, std::move(parsedPixels), parsedSourcePath);
        }
        else if (!image.empty() && !parsedSourcePath.empty())
        {
            image.setSourcePath(parsedSourcePath);
        }
    };

    auto parseBoolField = [&](const std::string& textObj, const char* keyName, bool& outValue) -> bool
    {
        if (!keyName) return false;
        size_t pos = 0;
        if (!findKeyPos(textObj, keyName, pos)) return false;
        while (pos < textObj.size() && std::isspace((unsigned char)textObj[pos])) ++pos;
        if (textObj.compare(pos, 4, "true") == 0) { outValue = true; return true; }
        if (textObj.compare(pos, 5, "false") == 0) { outValue = false; return true; }
        int iv = 0;
        size_t i = pos;
        if (parseInt(textObj, i, iv)) { outValue = (iv != 0); return true; }
        return false;
    };

    auto parseTransformKeyArray = [&](const std::string& textObj, const char* keyName, std::vector<DrawingEngine::TransformKeyframe>& outKeys)
    {
        outKeys.clear();
        if (!keyName) return;

        const std::string marker = std::string("\"") + keyName + "\"";
        size_t keyPos = textObj.find(marker);
        if (keyPos == std::string::npos) return;
        size_t a0 = textObj.find('[', keyPos);
        if (a0 == std::string::npos) return;

        int depthArr = 0;
        size_t a1 = std::string::npos;
        for (size_t i = a0; i < textObj.size(); ++i)
        {
            if (textObj[i] == '[') depthArr++;
            else if (textObj[i] == ']')
            {
                depthArr--;
                if (depthArr == 0) { a1 = i; break; }
            }
        }
        if (a1 == std::string::npos) return;

        std::vector<std::string> keyObjs;
        extractJsonObjects(textObj.substr(a0, a1 - a0 + 1), keyObjs);
        for (const auto& kobj : keyObjs)
        {
            DrawingEngine::TransformKeyframe key{};
            size_t p = 0;
            if (findKeyPos(kobj, "frame", p))
            {
                size_t i = p;
                (void)parseInt(kobj, i, key.frameIndex);
            }
            if (findKeyPos(kobj, "value", p))
            {
                size_t i = p;
                (void)parseFloat(kobj, i, key.value);
            }
            outKeys.push_back(key);
        }
    };

    auto parseVisibilityKeyArray = [&](const std::string& textObj, const char* keyName, std::vector<DrawingEngine::VisibilityKey>& outKeys)
    {
        outKeys.clear();
        if (!keyName) return;

        const std::string marker = std::string("\"") + keyName + "\"";
        size_t keyPos = textObj.find(marker);
        if (keyPos == std::string::npos) return;
        size_t a0 = textObj.find('[', keyPos);
        if (a0 == std::string::npos) return;

        int depthArr = 0;
        size_t a1 = std::string::npos;
        for (size_t i = a0; i < textObj.size(); ++i)
        {
            if (textObj[i] == '[') depthArr++;
            else if (textObj[i] == ']')
            {
                depthArr--;
                if (depthArr == 0) { a1 = i; break; }
            }
        }
        if (a1 == std::string::npos) return;

        std::vector<std::string> keyObjs;
        extractJsonObjects(textObj.substr(a0, a1 - a0 + 1), keyObjs);
        for (const auto& kobj : keyObjs)
        {
            DrawingEngine::VisibilityKey key{};
            size_t p = 0;
            if (findKeyPos(kobj, "frame", p))
            {
                size_t i = p;
                (void)parseInt(kobj, i, key.frameIndex);
            }
            bool visible = true;
            if (parseBoolField(kobj, "visible", visible))
                key.visible = visible;
            outKeys.push_back(key);
        }
    };

    for (const auto& obj : objs)
    {
        LoadedFrameLayer layer{};
        size_t p = obj.find("\"trackId\"");
        if (p != std::string::npos)
        {
            p = obj.find(':', p);
            if (p != std::string::npos)
            {
                ++p;
                (void)parseInt(obj, p, layer.trackId);
            }
        }

        if (findKeyPos(obj, "contentType", p))
        {
            int typeInt = 0;
            size_t i = p;
            if (parseInt(obj, i, typeInt))
                layer.contentType = (typeInt == (int)DrawingEngine::LayerContentType::Image)
                    ? DrawingEngine::LayerContentType::Image
                    : DrawingEngine::LayerContentType::Stroke;
        }

        size_t tfPos = obj.find("\"transform\"");
        if (tfPos != std::string::npos)
        {
            size_t t0 = obj.find('{', tfPos);
            if (t0 != std::string::npos)
            {
                size_t t1 = findMatchingBrace(obj, t0);
                if (t1 != std::string::npos)
                {
                    std::string tobj = obj.substr(t0, t1 - t0 + 1);
                    size_t q = 0;
                    if (findKeyPos(tobj, "x", q)) { size_t i = q; parseFloat(tobj, i, layer.transform.posX); }
                    if (findKeyPos(tobj, "y", q)) { size_t i = q; parseFloat(tobj, i, layer.transform.posY); }
                    if (findKeyPos(tobj, "rotation", q)) { size_t i = q; parseFloat(tobj, i, layer.transform.rotation); }
                    if (findKeyPos(tobj, "pivotX", q)) { size_t i = q; parseFloat(tobj, i, layer.transform.pivotX); }
                    if (findKeyPos(tobj, "pivotY", q)) { size_t i = q; parseFloat(tobj, i, layer.transform.pivotY); }
                }
            }
        }

        size_t tKeysPos = obj.find("\"transformKeys\"");
        if (tKeysPos != std::string::npos)
        {
            size_t k0 = obj.find('{', tKeysPos);
            if (k0 != std::string::npos)
            {
                size_t k1 = findMatchingBrace(obj, k0);
                if (k1 != std::string::npos)
                {
                    const std::string kobj = obj.substr(k0, k1 - k0 + 1);
                    parseTransformKeyArray(kobj, "x", layer.posXKeys);
                    parseTransformKeyArray(kobj, "y", layer.posYKeys);
                    parseTransformKeyArray(kobj, "rotation", layer.rotationKeys);
                    parseVisibilityKeyArray(kobj, "visibility", layer.visibilityKeys);
                }
            }
        }

        size_t interpPos = obj.find("\"transformInterpolation\"");
        if (interpPos == std::string::npos)
            interpPos = obj.find("\"interpolation\"");
        if (interpPos != std::string::npos)
        {
            size_t colonPos = obj.find(':', interpPos);
            if (colonPos != std::string::npos)
            {
                size_t firstQuote = obj.find('"', colonPos + 1);
                if (firstQuote != std::string::npos)
                {
                    size_t secondQuote = obj.find('"', firstQuote + 1);
                    if (secondQuote != std::string::npos)
                    {
                        const std::string mode = obj.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                        layer.interpolationMode = (mode == "ease" || mode == "Ease")
                            ? DrawingEngine::TransformInterpolationMode::Ease
                            : DrawingEngine::TransformInterpolationMode::Linear;
                    }
                }
            }
        }

        (void)parseBoolField(obj, "hasDrawEntry", layer.hasDrawEntry);
        (void)parseBoolField(obj, "hasTransformEntry", layer.hasTransformEntry);
        {
            size_t q = 0;
            if (findKeyPos(obj, "drawSourceFrame", q))
            {
                int src = -1;
                size_t i = q;
                if (parseInt(obj, i, src))
                    layer.drawSourceFrame = src;
            }
        }
        {
            size_t q = 0;
            if (findKeyPos(obj, "celId", q))
            {
                int cel = 0;
                size_t i = q;
                if (parseInt(obj, i, cel))
                    layer.celId = (std::uint64_t)std::max(0, cel);
            }
        }
        (void)parseBoolField(obj, "ownsDrawContent", layer.ownsDrawContent);

        parseImageObject(obj, layer.image);
        if (!layer.image.empty())
            layer.contentType = DrawingEngine::LayerContentType::Image;

        size_t sPos = obj.find("\"strokes\"");
        if (sPos != std::string::npos)
        {
            size_t a0 = obj.find('[', sPos);
            if (a0 != std::string::npos)
            {
                int d = 0;
                size_t a1 = std::string::npos;
                for (size_t i = a0; i < obj.size(); ++i)
                {
                    if (obj[i] == '[') d++;
                    else if (obj[i] == ']')
                    {
                        d--;
                        if (d == 0) { a1 = i; break; }
                    }
                }
                if (a1 != std::string::npos)
                {
                    std::string fakeFrame = "{\n  \"strokes\": " + obj.substr(a0, a1 - a0 + 1) + "\n}\n";
                    fs::path tempPath = framePath;
                    tempPath += ".tmp_parse";
                    if (writeTextFile(tempPath, fakeFrame))
                    {
                        std::vector<Stroke> tmp;
                        if (loadOneFrameJson(tempPath, tmp))
                            layer.strokes = std::move(tmp);
                        std::error_code ec;
                        fs::remove(tempPath, ec);
                    }
                }
            }
        }

        if (!layer.hasDrawEntry)
            layer.hasDrawEntry = !layer.image.empty() || !layer.strokes.empty();
        if (!layer.ownsDrawContent)
            layer.ownsDrawContent = !layer.image.empty() || !layer.strokes.empty();
        if (!layer.hasTransformEntry)
        {
            layer.hasTransformEntry = !layer.posXKeys.empty() || !layer.posYKeys.empty() || !layer.rotationKeys.empty()
                || std::fabs(layer.transform.posX) > 0.0001f
                || std::fabs(layer.transform.posY) > 0.0001f
                || std::fabs(layer.transform.rotation) > 0.0001f
                || layer.hasDrawEntry;
        }

        if (layer.trackId != 0)
            outLayers.push_back(std::move(layer));
    }

    return true;
}

static size_t countFrameFiles(const fs::path& framesDir)
{
    if (!fs::exists(framesDir) || !fs::is_directory(framesDir)) return 0;

    size_t count = 0;

    for (auto& e : fs::directory_iterator(framesDir))
    {
        if (!e.is_regular_file()) continue;

        auto p = e.path().filename().string();
        if (p.rfind("frame_", 0) == 0 && e.path().extension() == ".json") count++;
    }

    return strova::limits::clampTimelineFrameCount(count);
}


static void writeStrokeListJson(std::ostringstream& j, const std::vector<Stroke>& strokes, const char* indent)
{
    j << "[\n";
    for (size_t si = 0; si < strokes.size(); ++si)
    {
        const Stroke& s = strokes[si];
        j << indent << "{\n";
        j << indent << "  \"color\": [" << (int)s.color.r << "," << (int)s.color.g << "," << (int)s.color.b << "," << (int)s.color.a << "],\n";
        j << indent << "  \"thickness\": " << s.thickness << ",\n";
        j << indent << "  \"tool\": " << (int)s.tool << ",\n";
        j << indent << "  \"brushId\": \"" << jsonEscape(s.brushId) << "\",\n";
        j << indent << "  \"brushName\": \"" << jsonEscape(s.brushName) << "\",\n";
        j << indent << "  \"brushVersion\": " << s.brushVersion << ",\n";
        j << indent << "  \"brushMissing\": " << (s.brushMissing ? "true" : "false") << ",\n";
        j << indent << "  \"gradient\": {\n";
        j << indent << "    \"enabled\": " << (s.gradient.enabled ? 1 : 0) << ",\n";
        j << indent << "    \"mode\": " << s.gradient.mode << ",\n";
        j << indent << "    \"stopPos\": [" << s.gradient.stopPos[0] << "," << s.gradient.stopPos[1] << "," << s.gradient.stopPos[2] << "," << s.gradient.stopPos[3] << "],\n";
        j << indent << "    \"stopColor\": ["
            << "[" << (int)s.gradient.stopColor[0].r << "," << (int)s.gradient.stopColor[0].g << "," << (int)s.gradient.stopColor[0].b << "," << (int)s.gradient.stopColor[0].a << "],"
            << "[" << (int)s.gradient.stopColor[1].r << "," << (int)s.gradient.stopColor[1].g << "," << (int)s.gradient.stopColor[1].b << "," << (int)s.gradient.stopColor[1].a << "],"
            << "[" << (int)s.gradient.stopColor[2].r << "," << (int)s.gradient.stopColor[2].g << "," << (int)s.gradient.stopColor[2].b << "," << (int)s.gradient.stopColor[2].a << "],"
            << "[" << (int)s.gradient.stopColor[3].r << "," << (int)s.gradient.stopColor[3].g << "," << (int)s.gradient.stopColor[3].b << "," << (int)s.gradient.stopColor[3].a << "]"
            << "]\n";
        j << indent << "  },\n";
        j << indent << "  \"settings\": {\n";
        j << indent << "    \"size\": " << s.settings.size << ",\n";
        j << indent << "    \"opacity\": " << s.settings.opacity << ",\n";
        j << indent << "    \"spacing\": " << s.settings.spacing << ",\n";
        j << indent << "    \"flow\": " << s.settings.flow << ",\n";
        j << indent << "    \"scatter\": " << s.settings.scatter << ",\n";
        j << indent << "    \"hardness\": " << s.settings.hardness << ",\n";
        j << indent << "    \"stabilizer\": " << s.settings.stabilizer << ",\n";
        j << indent << "    \"airRadius\": " << s.settings.airRadius << ",\n";
        j << indent << "    \"airDensity\": " << s.settings.airDensity << ",\n";
        j << indent << "    \"strength\": " << s.settings.strength << ",\n";
        j << indent << "    \"smudgeStrength\": " << s.settings.smudgeStrength << ",\n";
        j << indent << "    \"blurRadius\": " << s.settings.blurRadius << ",\n";
        j << indent << "    \"jitterSize\": " << s.settings.jitterSize << ",\n";
        j << indent << "    \"jitterOpacity\": " << s.settings.jitterOpacity << ",\n";
        j << indent << "    \"jitterRotation\": " << s.settings.jitterRotation << ",\n";
        j << indent << "    \"spacingJitter\": " << s.settings.spacingJitter << ",\n";
        j << indent << "    \"brushId\": \"" << jsonEscape(s.settings.brushId) << "\",\n";
        j << indent << "    \"brushDisplayName\": \"" << jsonEscape(s.settings.brushDisplayName) << "\",\n";
        j << indent << "    \"brushVersion\": " << s.settings.brushVersion << ",\n";
        j << indent << "    \"brushSupportsUserColor\": " << (s.settings.brushSupportsUserColor ? "true" : "false") << ",\n";
        j << indent << "    \"brushSupportsGradient\": " << (s.settings.brushSupportsGradient ? "true" : "false") << "\n";
        j << indent << "  },\n";
        j << indent << "  \"fillTolerance\": " << s.fillTolerance << ",\n";
        j << indent << "  \"fillGapClose\": " << s.fillGapClose << ",\n";
        j << indent << "  \"points\": [";
        for (size_t pi = 0; pi < s.points.size(); ++pi)
        {
            j << "[" << s.points[pi].x << "," << s.points[pi].y << "," << s.points[pi].pressure << "]";
            if (pi + 1 < s.points.size()) j << ",";
        }
        j << "]\n";
        j << indent << "}";
        if (si + 1 < strokes.size()) j << ",";
        j << "\n";
    }
    j << indent << "]";
}

static void writeTransformKeyArrayJson(
    std::ostringstream& j,
    const std::vector<DrawingEngine::TransformKeyframe>* keys,
    int frameIndex)
{
    j << "[";
    bool first = true;
    if (keys)
    {
        for (const auto& key : *keys)
        {
            if (key.frameIndex != frameIndex)
                continue;
            if (!first) j << ",";
            first = false;
            j << "{\"frame\":" << key.frameIndex << ",\"value\":" << key.value << "}";
        }
    }
    j << "]";
}


static void writeVisibilityKeyArrayJson(
    std::ostringstream& j,
    const std::vector<DrawingEngine::VisibilityKey>* keys,
    int frameIndex)
{
    j << "[";
    bool first = true;
    if (keys)
    {
        for (const auto& key : *keys)
        {
            if (key.frameIndex != frameIndex)
                continue;
            if (!first) j << ",";
            first = false;
            j << "{\"frame\":" << key.frameIndex << ",\"visible\":" << (key.visible ? "true" : "false") << "}";
        }
    }
    j << "]";
}

static bool saveEngineFramesToFolder(const std::string& projectFolder, const DrawingEngine& engine, std::string& outErr)
{
    outErr.clear();

    try
    {
        fs::create_directories(projectFolder);
        fs::path framesDir = fs::path(projectFolder) / "frames";
        fs::path frameAssetsDir = fs::path(projectFolder) / "frame_assets";
        fs::create_directories(framesDir);
        fs::create_directories(frameAssetsDir);
        clearDirectoryFiles(framesDir);
        clearDirectoryFiles(frameAssetsDir);

        const size_t frameCount = engine.getFrameCount();
        const auto& tracks = engine.getTracks();

        for (size_t fi = 0; fi < frameCount; ++fi)
        {
            std::ostringstream j;
            j << "{\n";
            j << "  \"layers\": [\n";

            bool firstLayer = true;
            for (const auto& tr : tracks)
            {
                if (tr.id == 0) continue;
                const auto* layer = engine.getFrameTrackLayer(fi, tr.id);
                const DrawingEngine::TrackLayer evaluatedLayer = engine.getEvaluatedFrameTrackLayerCopy(fi, tr.id);
                const auto& strokes = engine.getFrameTrackStrokes(fi, tr.id);
                if (!firstLayer) j << ",\n";
                firstLayer = false;

                j << "    {\n";
                j << "      \"trackId\": " << tr.id << ",\n";
                j << "      \"kind\": " << (int)tr.kind << ",\n";
                j << "      \"name\": \"" << jsonEscape(tr.name) << "\",\n";
                j << "      \"contentType\": " << (int)(layer ? layer->contentType : DrawingEngine::LayerContentType::Stroke) << ",\n";
                j << "      \"hasDrawEntry\": " << ((layer && layer->hasDrawEntry) ? "true" : "false") << ",\n";
                j << "      \"hasTransformEntry\": " << ((layer && layer->hasTransformEntry) ? "true" : "false") << ",\n";
                j << "      \"ownsDrawContent\": " << ((layer && engine.frameTrackOwnsDrawContent(fi, tr.id)) ? "true" : "false") << ",\n";
                j << "      \"drawSourceFrame\": " << engine.getResolvedDrawSourceFrame(fi, tr.id) << ",\n";
                j << "      \"celId\": " << (unsigned long long)engine.getEvaluatedDrawCelId(fi, tr.id) << ",\n";

                const auto transform = layer ? layer->baseTransform : DrawingEngine::LayerTransform{};
                j << "      \"transform\": {\"x\":" << transform.posX
                  << ",\"y\":" << transform.posY
                  << ",\"rotation\":" << transform.rotation << "},\n";
                j << "      \"transformKeys\": {\"x\":[],\"y\":[],\"rotation\":[],\"visibility\":";
                writeVisibilityKeyArrayJson(j, engine.getVisibilityKeys(tr.id), (int)fi);
                j << "},\n";
                j << "      \"transformInterpolation\": \"capture\",\n";
                if (layer && !layer->image.empty())
                {
                    const std::string assetRel = makeFrameImageAssetRelativePath((int)fi, tr.id);
                    const fs::path assetPath = fs::path(projectFolder) / assetRel;
                    if (!strova::iojson::writeRgbaAssetAtomic(assetPath, layer->image.width(), layer->image.height(), layer->image.rgba()))
                    {
                        outErr = "Failed to write layer image asset: " + assetPath.string();
                        return false;
                    }

                    j << "      \"image\": {\n";
                    j << "        \"width\": " << layer->image.width() << ",\n";
                    j << "        \"height\": " << layer->image.height() << ",\n";
                    j << "        \"sourcePath\": \"" << jsonEscape(layer->image.sourcePath()) << "\",\n";
                    j << "        \"rgbaAsset\": \"" << jsonEscape(strova::iojson::normalizeProjectRelativePath(assetRel)) << "\"\n";
                    j << "      },\n";
                }

                j << "      \"strokes\": ";
                writeStrokeListJson(j, strokes, "      ");
                j << "\n";
                j << "    }";
            }

            j << "\n  ],\n";
            j << "  \"strokes\": ";
            writeStrokeListJson(j, engine.getFrameStrokes(fi), "  ");
            j << "\n}\n";

            char name[64];
            sprintf_s(name, "frame_%03d.json", (int)fi);

            fs::path fp = framesDir / name;
            if (!writeTextFileAtomic(fp, j.str()))
            {
                outErr = "Failed to write frame file: " + fp.string();
                return false;
            }
        }

        return true;
    }
    catch (const std::exception& e)
    {
        outErr = e.what();
        return false;
    }
}

void App::syncProjectFromEngine()
{
    currentProject.width = projectW;
    currentProject.height = projectH;
    currentProject.fps = projectFPS;
    runtimeState.project.width = projectW;
    runtimeState.project.height = projectH;
    runtimeState.project.fps = projectFPS;
}

#include "AppProjectPersistence.inl"

SDL_Texture* App::loadIconTexture(const char* path)
{
    const std::string resolved = strova::paths::resolveAssetPath(path).string();
    SDL_Surface* surf = IMG_Load(resolved.c_str());
    if (!surf) return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(sdlRenderer, surf);
    SDL_FreeSurface(surf);

    if (tex)
    {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2,0,12)
        SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
#endif
    }

    return tex;
}

bool App::createWindow(const char* title, int w, int h)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");

    window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) return false;

    sdlRenderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdlRenderer)
    {
        sdlRenderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!sdlRenderer) return false;
    }

    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);
    return true;
}

bool App::init()
{
    strova::debug::log("App", "init() entered.");
    editor = new Editor();
    launcherScreen = new LauncherScreen();
    brushCreatorScreen = new BrushCreatorScreen();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { strova::debug::log("App", std::string("SDL_Init failed: ") + SDL_GetError()); return false; }
    strova::debug::log("App", "SDL initialized.");
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) { strova::debug::log("App", std::string("IMG_Init failed: ") + IMG_GetError()); return false; }
    strova::debug::log("App", "SDL_image initialized.");
    if (TTF_Init() != 0) { strova::debug::log("App", std::string("TTF_Init failed: ") + TTF_GetError()); return false; }
    strova::debug::log("App", "SDL_ttf initialized.");

    if (!createWindow("Strova 1.0.5", 1080, 640)) { strova::debug::log("App", "createWindow failed."); return false; }
    strova::debug::log("App", "Window created.");
    splash.setStatusText("Loading configuration...", "Bootstrapping runtime");
    const std::string iconPath = strova::paths::resolveAssetPath("favicon.png").string();
    SDL_Surface* icon = IMG_Load(iconPath.c_str());
    if (icon)
    {
        SDL_SetWindowIcon(window, icon);
        SDL_FreeSurface(icon);
    }
    if (!icon) { strova::debug::log("App", std::string("Window Icon Failed to Load: ") + IMG_GetError()); }
    const std::string fontPath = strova::paths::resolveAssetPath("font/main.otf").string();
    uiFont = TTF_OpenFont(fontPath.c_str(), 18);
    if (!uiFont) { strova::debug::log("App", std::string("TTF_OpenFont failed: ") + TTF_GetError()); return false; }
    strova::debug::log("App", "UI font loaded.");

    timeline.setFont(uiFont);
    splash.setStatusText("Loading interface fonts...", "Preparing editor chrome");


    colorPicker.setFont(uiFont);
    timeline.setTotalFrames((int)engine.getFrameCount());
    timeline.setFps(projectFPS);

    auto& ts = timeline.state();
    ts.trackHeaderW = 150;
    ts.rulerH = 26;
    ts.trackH = 52;
    ts.pxPerFrame = 10.0f;

    if (ts.tracks.empty())
    {
        setDefaultTimeline(timeline, (int)engine.getFrameCount());
    }

    mode = Mode::Splash;
    splash.start(SDL_GetTicks());
    SDL_SetWindowBordered(window, SDL_FALSE);

    std::string settingsErr;
    splash.setStatusText("Loading configuration...", "Reading local settings");
    if (!AppSettingsIO::load(appSettings, settingsErr)) { strova::debug::log("App", std::string("AppSettingsIO::load failed: ") + settingsErr); }
    else { strova::debug::log("App", std::string("App settings loaded. updateCheckDaily=") + (appSettings.updateCheckDaily ? "true" : "false")); }

    int startupW = 0, startupH = 0;
    SDL_GetWindowSize(window, &startupW, &startupH);
    refreshUILayout(startupW, startupH);
    initializeDockingUiState(startupW, startupH);

    running = true;

    splash.setStatusText("Initializing plugin runtime...", "Scanning installed plugins");
    pluginManagerStore.initialize(kCurrentAppVersion, strova::paths::getPlatformKey());
    pluginManagerStore.bindApp(this);
    pluginManagerStore.bindProject(nullptr);
    std::string pluginErr;
    pluginManagerStore.discoverInstalled(pluginErr);
    if (!pluginErr.empty())
        strova::debug::log("App", std::string("Plugin discovery warning: ") + pluginErr);
    for (const auto& missingPlugin : pluginManagerStore.missingRecords())
    {
        strova::debug::log("App", std::string("Plugin missing from indexed location: ") + missingPlugin.pluginId +
            " expected=" + missingPlugin.expectedPath +
            " action=" + (missingPlugin.ignored ? std::string("ignored") : std::string("replace_or_ignore")));
    }
    pluginErr.clear();
    if (!pluginManagerStore.loadEnabledPlugins(pluginErr) && !pluginErr.empty())
        strova::debug::log("App", std::string("Plugin load warning: ") + pluginErr);
    {
        const SDL_Rect& top = getUILayout().topBar;
        const int topY = top.y + top.h;
        SDL_Rect workspace{ 0, topY, std::max(1, startupW), std::max(1, startupH - topY) };
        dockUi.syncPluginPanels(pluginManagerStore.registries().dockPanels, workspace);
    }

    launcher.init(uiFont);
    strova::debug::log("App", "Launcher initialized.");
    launcher.setUpdateChecksEnabled(appSettings.updateCheckDaily);
    splash.setStatusText("Scanning projects...", "Refreshing launcher content");
    launcher.refreshProjects();
    splash.setStatusText("Checking for updates...", "Pinging release channel");
    strova::debug::log("App", std::string("Beginning startup update check against ") + kUpdateEndpoint);
    updateManager.beginSplashCheck(kUpdateEndpoint, kCurrentAppVersion, appSettings);

    splash.setStatusText("Caching brush resources...", "Preparing renderer");
    brushManagerStore.initialize();
    strova::brush::setGlobalManager(&brushManagerStore);
    if (const auto* selectedBrush = brushManagerStore.selected())
    {
        toolBank.get(ToolType::Brush).brushId = selectedBrush->manifest.id;
        toolBank.get(ToolType::Brush).brushDisplayName = selectedBrush->manifest.name;
        toolBank.get(ToolType::Brush).brushVersion = selectedBrush->manifest.version;
        engine.setBrushSelection(selectedBrush->manifest.id, selectedBrush->manifest.version, selectedBrush->manifest.name);
    }
    brushRenderer = new BrushRenderer(sdlRenderer);
    strova::debug::log("App", "BrushRenderer created. init() complete.");


    timeline.onFocusFrameHasContent = [this](strova::TrackId uiTrackId, int frameIndex) -> bool
        {
            return trackHasVisibleContentAtFrame(*this, uiTrackId, frameIndex);
        };

    timeline.onFocusFrameThumbnail = [this](strova::TrackId uiTrackId, int frameIndex) -> SDL_Texture*
        {
            if (!trackHasVisibleContentAtFrame(*this, uiTrackId, frameIndex))
                return nullptr;
            if (frameIndex < 0) return nullptr;
            const size_t fi = (size_t)frameIndex;
            if (fi >= engine.getFrameCount()) return nullptr;

            ensureThumbCacheSize();

            Thumb& t = thumbCache[fi];
            const uint64_t k = calcFrameDirtyKey(fi);

            if (!t.tex || t.dirtyKey != k)
                rebuildThumb(fi);

            return t.tex;
        };


    timeline.onFocusInsertFrame = [this](int atFrame)
        {
            EditorCommand cmd{};
            cmd.type = EditorCommandType::InsertFrame;
            cmd.intValue = atFrame;
            dispatchEditorCommand(cmd);
        };

    timeline.onFocusDeleteFrame = [this](int atFrame)
        {
            EditorCommand cmd{};
            cmd.type = EditorCommandType::DeleteFrame;
            cmd.intValue = atFrame;
            dispatchEditorCommand(cmd);
        };

    timeline.onFocusDuplicateFrame = [this](int atFrame)
        {
            EditorCommand cmd{};
            cmd.type = EditorCommandType::DuplicateFrame;
            cmd.intValue = atFrame;
            dispatchEditorCommand(cmd);
        };


    timeline.onFocusMoveFrame = [this](int fromFrame, int toFrame)
        {
            EditorCommand cmd{};
            cmd.type = EditorCommandType::MoveFocusedFrame;
            cmd.intValue = fromFrame;
            cmd.auxIntValue = toFrame;
            dispatchEditorCommand(cmd);
        };




    timeline.onTrackDeleteRequested = [this](strova::TrackId uiTrackId)
        {
            EditorCommand cmd{};
            cmd.type = EditorCommandType::DeleteTrack;
            cmd.trackId = uiTrackId;
            dispatchEditorCommand(cmd);
        };

    timeline.onTrackRenameRequested = [this](strova::TrackId uiTrackId)
        {
            EditorCommand cmd{};
            cmd.type = EditorCommandType::RenameTrack;
            cmd.trackId = uiTrackId;
            dispatchEditorCommand(cmd);
        };


    timeline.onTrackSelected = [this](strova::TrackId uiTrackId)
        {
            EditorCommand cmd{};
            cmd.type = EditorCommandType::SelectTrack;
            cmd.trackId = uiTrackId;
            dispatchEditorCommand(cmd);
        };

    timeline.onTrackVisibilityToggled = [this](strova::TrackId uiTrackId, bool visible)
        {
            auto* tr = timeline.findTrack(uiTrackId);
            if (!tr || tr->engineTrackId == 0) return;
            EditorCommand cmd{};
            cmd.type = EditorCommandType::SetLayerVisible;
            cmd.trackId = tr->engineTrackId;
            cmd.boolValue = visible;
            dispatchEditorCommand(cmd);
        };

    timeline.onTrackLockToggled = [this](strova::TrackId uiTrackId, bool locked)
        {
            auto* tr = timeline.findTrack(uiTrackId);
            if (!tr || tr->engineTrackId == 0) return;
            EditorCommand cmd{};
            cmd.type = EditorCommandType::SetLayerLocked;
            cmd.trackId = tr->engineTrackId;
            cmd.boolValue = locked;
            dispatchEditorCommand(cmd);
        };

    timeline.onTrackMuteToggled = [this](strova::TrackId uiTrackId, bool muted)
        {
            EditorCommand cmd{};
            cmd.type = EditorCommandType::SetTrackMuted;
            cmd.trackId = uiTrackId;
            cmd.boolValue = muted;
            dispatchEditorCommand(cmd);
        };

    timeline.onClipMoved = [this](strova::ClipId clipId, int oldStart, int newStart)
        {
            if (moveFlowLinkEngineClipForUiMove(*this, clipId, oldStart, newStart))
            {
                markAllThumbsDirty();
                syncRuntimeStateFromEditor();
                requestSaveProjectNow();
            }
        };

    engine.setTool(ToolType::Brush);
    editorUiState.activeTool = ToolType::Brush;
    editorUiState.currentColor = engine.getBrushColor();
    editorUiState.currentGradient = engine.getGradientConfig();
    colorPicker.setColor(editorUiState.currentColor);

    iconBrush = loadIconTexture("assets/icons/brush.png");
    iconPencil = loadIconTexture("assets/icons/pencil.png");
    iconPen = loadIconTexture("assets/icons/pen.png");
    iconMarker = loadIconTexture("assets/icons/marker.png");
    iconAirbrush = loadIconTexture("assets/icons/airbrush.png");
    iconCalligraphy = loadIconTexture("assets/icons/caligraphy.png");
    iconEraser = loadIconTexture("assets/icons/eraser.png");
    iconSmudge = loadIconTexture("assets/icons/smudge.png");
    iconBlur = loadIconTexture("assets/icons/blur.png");
    iconGlow = loadIconTexture("assets/icons/glow.png");
    iconRuler = loadIconTexture("assets/icons/ruler.png");
    iconSoften = loadIconTexture("assets/icons/soften.png");
    iconBucket = loadIconTexture("assets/icons/bucket.png");
    iconLine = loadIconTexture("assets/icons/line.png");
    iconOval = loadIconTexture("assets/icons/oval.png");
    iconRectangle = loadIconTexture("assets/icons/rectangle.png");
    iconSelect = loadIconTexture("assets/icons/select.png");
    iconDropper = loadIconTexture("assets/icons/dropper.png");


    colorTex = loadIconTexture("assets/icons/settings.png");
    if (!colorTex) colorTex = loadIconTexture("assets/icons/gear.png");
    if (!colorTex) colorTex = loadIconTexture("assets/icons/options.png");


    iconUndo = loadIconTexture("assets/icons/undo.png");
    iconRedo = loadIconTexture("assets/icons/redo.png");


    undoTex = iconUndo;
    redoTex = iconRedo;
    canvasScale = 1.0f;
    canvasPanX = 0.0f;
    canvasPanY = 0.0f;

    stabilizerRef() = 0.35f;
    drawing = false;
    hasFiltered = false;

    flow.setProjectFps(projectFPS > 0 ? projectFPS : 30);
    syncRuntimeStateFromEditor();

    return true;
}

UILayout App::calculateUILayout(int w, int h, float leftRatio)
{
    UILayout layout;

    const int topH = 40;
    const int bottomH = 220;

    int leftW = (int)std::lround((float)w * fclamp(leftRatio, 0.05f, 0.28f));


    const int rightW = std::max(0, std::min(rightPanelW, w));

    int canvasW = w - leftW;
    if (canvasW < 1) canvasW = 1;

    layout.topBar = { 0, 0, w, topH };
    layout.bottomBar = { 0, h - bottomH, w, bottomH };

    layout.leftBar = { 0, topH, leftW, h - topH - bottomH };
    layout.canvas = { leftW, topH, canvasW, h - topH - bottomH };

    rightBar = { w - rightW, topH, rightW, h - topH - bottomH };

    return layout;
}

void App::refreshUILayout()
{
    int w = 0;
    int h = 0;
    if (window) SDL_GetWindowSize(window, &w, &h);
    if (w <= 0) w = 1600;
    if (h <= 0) h = 900;
    refreshUILayout(w, h);
}

void App::refreshUILayout(int windowW, int windowH)
{
    const int safeW = std::max(windowW, 900);
    const int safeH = std::max(windowH, 650);

    ui = calculateUILayout(safeW, safeH, leftBarRatio);
    ui.topBar = clampLayoutRect(ui.topBar, safeW, safeH);
    ui.leftBar = clampLayoutRect(ui.leftBar, safeW, safeH);
    ui.bottomBar = clampLayoutRect(ui.bottomBar, safeW, safeH);
    ui.canvas = clampLayoutRect(ui.canvas, safeW, safeH);
    rightBar = clampLayoutRect(rightBar, safeW, safeH);

    if (ui.bottomBar.y < ui.topBar.y + ui.topBar.h)
        ui.bottomBar.y = ui.topBar.y + ui.topBar.h;

    const int contentBottom = ui.bottomBar.y;
    if (ui.leftBar.y + ui.leftBar.h > contentBottom)
        ui.leftBar.h = std::max(0, contentBottom - ui.leftBar.y);
    if (ui.canvas.y + ui.canvas.h > contentBottom)
        ui.canvas.h = std::max(0, contentBottom - ui.canvas.y);
    if (rightBar.y + rightBar.h > contentBottom)
        rightBar.h = std::max(0, contentBottom - rightBar.y);
}

void App::fitProjectToCanvasView()
{
    if (ui.canvas.w <= 0 || ui.canvas.h <= 0) return;

    float sx = (float)ui.canvas.w / (float)std::max(1, projectW);
    float sy = (float)ui.canvas.h / (float)std::max(1, projectH);

    float fit = std::min(sx, sy) * 0.92f;
    fit = std::clamp(fit, minCanvasScale, maxCanvasScale);

    canvasScale = fit;

    float projPxW = projectW * canvasScale;
    float projPxH = projectH * canvasScale;

    canvasPanX = (ui.canvas.w - projPxW) * 0.5f;
    canvasPanY = (ui.canvas.h - projPxH) * 0.5f;
}


void App::openProject(const std::string& path)
{
    std::string err;
    Project proj;

    bool usedRecovery = false;
    const fs::path resolvedOpenFolder = resolveOpenFolderWithRecovery(path, &usedRecovery);

    if (!ProjectIO::loadFromFolder(resolvedOpenFolder.string(), proj, err))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Strova - Open Project", err.c_str(), window);
        return;
    }

    currentProject = proj;
    currentProject.folderPath = path;
    pluginManagerStore.bindProject(&currentProject);
    {
        const SDL_Rect& top = getUILayout().topBar;
        const int topY = top.y + top.h;
        int winW = 0;
        int winH = 0;
        if (window)
            SDL_GetWindowSize(window, &winW, &winH);
        winW = std::max(1, winW);
        winH = std::max(1, winH);
        SDL_Rect workspace{ 0, topY, winW, std::max(1, winH - topY) };
        dockUi.syncPluginPanels(pluginManagerStore.registries().dockPanels, workspace);
    }
    pluginManagerStore.evaluateProjectDependencies(currentProject);
    for (const auto& issue : pluginManagerStore.lastDependencyIssues())
        strova::debug::log("Plugin", issue.pluginId + ": " + issue.message);
    loadEditorRuntimeStateFromFolder(resolvedOpenFolder, *this);
    if (usedRecovery)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Strova Recovery", "Recovered newer unsaved work snapshot for this project.", window);

    size_t fc = 1;
    fs::path framesDir = resolvedOpenFolder / "frames";
    fc = countFrameFiles(framesDir);
    if (fc == 0) fc = 1;

    timeline.clearClips();
    timeline.clearTracks();

    bool haveTimeline = ProjectIO::loadTimelineFromFolder(resolvedOpenFolder.string(), timeline, err);
    if (!haveTimeline)
    {
        auto& ts = timeline.state();
        ts.trackHeaderW = 150;
        ts.rulerH = 26;
        ts.trackH = 52;
        ts.pxPerFrame = 10.0f;

        const int tDraw = timeline.addTrack(strova::TrackKind::Draw, "Draw");
        (void)timeline.addTrack(strova::TrackKind::Flow, "Flow");
        (void)timeline.addTrack(strova::TrackKind::FlowLink, "FlowLink");
        if (tDraw != 0)
            timeline.addClip(tDraw, 0, std::max(1, (int)fc), "Main");
    }

    engine.clearTracks();
    engine.clearAndResizeFrames(fc);
    frameLayerTrees.clear();
    frameLayerTrees.resize(std::max<size_t>(1, fc));

    for (auto& tr : timeline.state().tracks)
    {
        DrawingEngine::TrackKind ek = DrawingEngine::TrackKind::Draw;
        if ((int)tr.kind == (int)strova::TrackKind::Flow) ek = DrawingEngine::TrackKind::Flow;
        else if ((int)tr.kind == (int)strova::TrackKind::FlowLink) ek = DrawingEngine::TrackKind::FlowLink;
        else if ((int)tr.kind == (int)strova::TrackKind::Audio) ek = DrawingEngine::TrackKind::Audio;

        if (tr.engineTrackId == 0)
            tr.engineTrackId = engine.createTrack(ek, tr.name);
        else
            engine.createTrackWithId(tr.engineTrackId, ek, tr.name);
    }

    if (!findTimelineTrackByName(*this, "FlowLink"))
    {
        if (auto* legacy = findTimelineTrackByName(*this, "Keyframe"))
        {
            legacy->name = "FlowLink";
            legacy->kind = strova::TrackKind::FlowLink;
            if (legacy->engineTrackId == 0)
                legacy->engineTrackId = engine.createTrack(DrawingEngine::TrackKind::FlowLink, "FlowLink");
        }
    }

    if (timeline.state().tracks.empty())
    {
        const int drawUi = timeline.addTrack(strova::TrackKind::Draw, "Draw");
        const int flowUi = timeline.addTrack(strova::TrackKind::Flow, "Flow");
        const int flowLinkUi = timeline.addTrack(strova::TrackKind::FlowLink, "FlowLink");
        if (auto* drawTrack = timeline.findTrack(drawUi)) drawTrack->engineTrackId = engine.createTrack(DrawingEngine::TrackKind::Draw, "Draw");
        if (auto* flowTrack = timeline.findTrack(flowUi)) flowTrack->engineTrackId = engine.createTrack(DrawingEngine::TrackKind::Flow, "Flow");
        if (auto* flowLinkTrack = timeline.findTrack(flowLinkUi)) flowLinkTrack->engineTrackId = engine.createTrack(DrawingEngine::TrackKind::FlowLink, "FlowLink");

        if (drawUi != 0)
            timeline.addClip(drawUi, 0, std::max(1, (int)fc), "Main");
    }

    std::vector<LoadedFrameLayer> loadedLayers;
    std::vector<Stroke> legacyComposite;
    for (size_t fi = 0; fi < fc; ++fi)
    {
        char name[64];
        sprintf_s(name, "frame_%03d.json", (int)fi);
        fs::path fp = framesDir / name;

        loadedLayers.clear();
        legacyComposite.clear();

        if (!loadLayeredFrameJson(fp, loadedLayers, legacyComposite))
            continue;

        if (!loadedLayers.empty())
        {
            for (const auto& L : loadedLayers)
            {
                engine.setFrameTrackStrokes(fi, L.trackId, L.strokes);
                engine.setFrameTrackTransform(fi, L.trackId, L.transform);
                if (!L.image.empty())
                    engine.setFrameTrackImage(fi, L.trackId, L.image, L.transform);
                if (auto* rawLayer = engine.getFrameTrackLayerMutable(fi, L.trackId))
                {
                    rawLayer->contentType = L.contentType;
                    rawLayer->hasDrawEntry = L.hasDrawEntry;
                    rawLayer->hasTransformEntry = L.hasTransformEntry;
                    rawLayer->celId = L.celId;
                    rawLayer->drawSourceFrame = L.drawSourceFrame;
                    rawLayer->ownsDrawContent = L.ownsDrawContent;
                }
                for (const auto& key : L.visibilityKeys)
                    engine.setVisibilityKeyframe(L.trackId, key.frameIndex, key.visible);
            }
        }
        else if (!legacyComposite.empty())
        {
            int fallbackTrack = 0;
            for (const auto& tr : timeline.state().tracks)
            {
                if (tr.kind == strova::TrackKind::Draw && tr.engineTrackId != 0)
                {
                    fallbackTrack = tr.engineTrackId;
                    break;
                }
            }
            if (fallbackTrack == 0)
            {
                fallbackTrack = engine.createTrack(DrawingEngine::TrackKind::Draw, "Draw");
            }
            if (fallbackTrack != 0)
                engine.setFrameTrackStrokes(fi, fallbackTrack, legacyComposite);
        }
    }

    loadFlowLinkClipsFromFolder(resolvedOpenFolder, *this);
    engine.syncFrameTransformsFromKeyframes();

    layerTree.loadFromFolder(resolvedOpenFolder.string());
    layerTree.syncFromTimeline(timeline);
    frameLayerTrees.clear();
    frameLayerTrees.resize(fc);
    fs::path frameLayersDir = resolvedOpenFolder / "frame_layers";
    for (size_t fi = 0; fi < fc; ++fi)
    {
        char name[64];
        sprintf_s(name, "layers_%03d.json", (int)fi);
        fs::path lp = frameLayersDir / name;
        if (fs::exists(lp))
        {
            frameLayerTrees[fi].loadFromPath(lp);
            frameLayerTrees[fi].syncExistingFromTimeline(timeline);
        }
    }
    if (!frameLayerTrees.empty())
    {
        if (frameLayerTrees[0].getNodes().empty()) frameLayerTrees[0] = layerTree;
        layerTree = frameLayerTrees[0];
    }

    {
        std::vector<DrawingEngine::TrackId> engineOrder;
        for (const auto& row : layerTree.buildRows())
        {
            const auto* node = layerTree.findNode(row.nodeId);
            if (node && !node->isGroup && node->trackId != 0)
                engineOrder.push_back(node->trackId);
        }
        for (const auto& tr : engine.getTracks())
        {
            bool found = false;
            for (auto id : engineOrder) if (id == tr.id) { found = true; break; }
            if (!found) engineOrder.push_back(tr.id);
        }
        engine.setTrackOrder(engineOrder);
    }

    int activeTrackId = layerTree.primarySelectedTrackId();
    if (activeTrackId == 0)
    {
        for (const auto& tr : timeline.state().tracks)
        {
            if (tr.kind == strova::TrackKind::Draw && tr.engineTrackId != 0)
            {
                activeTrackId = tr.engineTrackId;
                break;
            }
        }
    }
    if (activeTrackId != 0)
        engine.setActiveTrack(activeTrackId);

    switchToFrameIndex(0);
    syncUIFromState();

    projectW = proj.width;
    projectH = proj.height;
    projectFPS = proj.fps;

    flow.setProjectFps(projectFPS);
    timeline.setTotalFrames((int)fc);
    timeline.setFps(projectFPS);

    std::string title = "Strova - " + proj.name;
    SDL_SetWindowTitle(window, title.c_str());

    engine.setTool(toolBar.getSelectedTool());
    engine.setToolSettings(toolBank.get(toolBar.getSelectedTool()));
    if (toolBar.getSelectedTool() == ToolType::Brush)
        engine.setBrushSelection(toolBank.get(ToolType::Brush).brushId, toolBank.get(ToolType::Brush).brushVersion, toolBank.get(ToolType::Brush).brushDisplayName);

    mode = Mode::Editor;
    drawing = false;
    hasFiltered = false;
    panning = false;

    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    validateEditorState();
    refreshUILayout(w, h);
    loadDockLayoutForCurrentContext(w, h);
    fitProjectToCanvasView();

    markAllThumbsDirty();
    prepareThumbDiskCache();

    for (const auto& tr : timeline.state().tracks)
    {
        if (tr.engineTrackId == 0) continue;
        engine.setTrackVisible(tr.engineTrackId, tr.visible);
        engine.setTrackMuted(tr.engineTrackId, tr.muted);
        engine.setTrackLocked(tr.engineTrackId, tr.locked);
    }
}

void App::createNewProjectDefault()
{
    const fs::path projectsRoot = strova::paths::getProjectsDir();
    std::error_code projectsEc;
    fs::create_directories(projectsRoot, projectsEc);

    int idx = 1;
    std::string folder;

    while (true)
    {
        folder = (projectsRoot / ("NewProject_" + std::to_string(idx) + ".strova")).string();
        if (!fs::exists(folder)) break;
        idx++;
    }

    fs::create_directories(folder);

    Project p;
    p.folderPath = folder;
    p.name = "NewProject_" + std::to_string(idx);
    p.width = 1920;
    p.height = 1080;
    p.fps = 30;

    std::string perr;

    if (!ProjectIO::saveToFolder(p, perr))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Strova - New Project", perr.c_str(), window);
        return;
    }

    {
        fs::create_directories(fs::path(folder) / "frames");
        fs::path fp = fs::path(folder) / "frames" / "frame_000.json";
        if (!fs::exists(fp)) writeTextFile(fp, "{\n  \"strokes\": []\n}\n");
    }

    timeline.clearClips();
    timeline.clearTracks();

    timeline.setFps(p.fps);
    timeline.setTotalFrames(1);

    auto& ts = timeline.state();
    ts.trackHeaderW = 150;
    ts.rulerH = 26;
    ts.trackH = 52;
    ts.pxPerFrame = 10.0f;

    const int tDraw = timeline.addTrack(strova::TrackKind::Draw, "Draw");
    const int tFlow = timeline.addTrack(strova::TrackKind::Flow, "Flow");
    const int tFlowLink = timeline.addTrack(strova::TrackKind::FlowLink, "FlowLink");

    engine.clearTracks();
    engine.clearAndResizeFrames(1);

    if (auto* tr = timeline.findTrack(tDraw)) tr->engineTrackId = engine.createTrack(DrawingEngine::TrackKind::Draw, "Draw");
    if (auto* tr = timeline.findTrack(tFlow)) tr->engineTrackId = engine.createTrack(DrawingEngine::TrackKind::Flow, "Flow");
    if (auto* tr = timeline.findTrack(tFlowLink)) tr->engineTrackId = engine.createTrack(DrawingEngine::TrackKind::FlowLink, "FlowLink");

    layerTree.clear();
    if (auto* tr = timeline.findTrack(tDraw))
    {
        layerTree.addLayerNode("Draw", tr->engineTrackId, 0);
        layerTree.handleClick(layerTree.firstLayerNodeId(), false, false);
        engine.setActiveTrack(tr->engineTrackId);
        engine.setTrackOrder(std::vector<DrawingEngine::TrackId>{ tr->engineTrackId });
    }

    if (tDraw != 0)
        timeline.addClip(tDraw, 0, 1, "Main");

    if (!ProjectIO::saveTimelineToFolder(folder, timeline, perr))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Strova - New Project", perr.c_str(), window);
        return;
    }

    launcher.refreshProjects();
    openProject(folder);
}
#include "AppThumbnailCache.inl"

void App::queueThumbDiskProbe(std::size_t fi, std::uint64_t expectedKey)
{
    if (fi >= thumbCache.size()) return;

    Thumb& t = thumbCache[fi];
    if (t.probeQueued && t.probedKey == expectedKey) return;

    t.probeQueued = true;
    t.probeReady = false;
    t.probedValid = false;
    t.probedKey = expectedKey;

    const std::string diskPath = t.diskPath;
    const std::string keyPath = t.keyPath;

    backgroundJobs.enqueue([this, fi, expectedKey, diskPath, keyPath]()
    {
        bool valid = false;
        try
        {
            if (!diskPath.empty() && !keyPath.empty() && fs::exists(diskPath) && fs::exists(keyPath))
            {
                std::string keyStr;
                if (readTextFile(keyPath, keyStr))
                {
                    std::uint64_t diskKey = 0;
                    try { diskKey = (std::uint64_t)std::stoull(trimCopy(keyStr)); }
                    catch (...) { diskKey = 0; }
                    valid = (diskKey == expectedKey);
                }
            }
        }
        catch (...)
        {
            valid = false;
        }

        std::lock_guard<std::mutex> lk(thumbProbeMutex);
        thumbProbeResults.push_back(ThumbProbeResult{ fi, expectedKey, valid });
    });
}

void App::drainThumbProbeResults()
{
    std::deque<ThumbProbeResult> ready;
    {
        std::lock_guard<std::mutex> lk(thumbProbeMutex);
        ready.swap(thumbProbeResults);
    }

    for (const ThumbProbeResult& r : ready)
    {
        if (r.frameIndex >= thumbCache.size()) continue;
        Thumb& t = thumbCache[r.frameIndex];
        if (t.probedKey != r.expectedKey) continue;
        t.probeQueued = false;
        t.probeReady = true;
        t.probedValid = r.valid;
        t.probedKey = r.expectedKey;
        if (r.valid) runtimeState.render.thumbProbeHitsThisFrame++;
        else runtimeState.render.thumbProbeMissesThisFrame++;
    }
}


void App::processThumbJobs(int budget)
{
    if (!sdlRenderer) return;
    if (mode != Mode::Editor) return;

    ensureThumbCacheSize();
    drainThumbProbeResults();

    runtimeState.render.backgroundJobsPending = backgroundJobs.pendingCount();
    runtimeState.render.backgroundJobsActive = backgroundJobs.activeCount();

    const size_t n = thumbCache.size();
    if (n == 0) return;

    bool haveTimeline = (timelineStrip.w > 0 && timelineStrip.h > 0);

    const int pad = 8;
    const int gap = 8;
    const int thumbW = 120;

    size_t visStart = 0;
    size_t visEnd = n ? (n - 1) : 0;

    if (haveTimeline)
    {
        int viewL = timelineScroll;
        int viewR = timelineScroll + timelineStrip.w;

        int step = thumbW + gap;

        int first = (viewL - pad) / step;
        int last = (viewR - pad) / step;

        if (first < 0) first = 0;
        if (last < 0) last = 0;

        if (first >= (int)n) first = (int)n - 1;
        if (last >= (int)n) last = (int)n - 1;

        visStart = (size_t)first;
        visEnd = (size_t)last;

        if (visStart > visEnd) std::swap(visStart, visEnd);
    }

    auto considerAsyncProbe = [&](size_t i, uint64_t curKey) -> bool
    {
        if (i >= n) return false;
        Thumb& t = thumbCache[i];
        if (t.tex) return false;
        if (t.dirtyKey == 0 || t.dirtyKey != curKey) return false;
        if (t.probeReady && t.probedKey == curKey) return true;
        if (!t.probeQueued || t.probedKey != curKey)
            queueThumbDiskProbe(i, curKey);
        return false;
    };

    auto needsRebuild = [&](size_t i) -> bool
    {
        if (i >= n) return false;

        uint64_t curKey = calcFrameDirtyKey(i);
        Thumb& t = thumbCache[i];

        if (t.dirtyKey == 0) return true;
        if (t.dirtyKey != curKey) return true;
        if (t.tex) return false;

        if (considerAsyncProbe(i, curKey))
        {
            if (t.probedValid && loadThumbFromDisk(i))
            {
                t.probeReady = false;
                return false;
            }
            t.probeReady = false;
            return true;
        }

        return false;
    };

    int done = 0;

    for (size_t i = visStart; i <= visEnd && done < budget; ++i)
    {
        if (needsRebuild(i))
        {
            rebuildThumb(i);
            done++;
        }
    }

    if (done < budget)
    {
        for (size_t k = 0; k < n && done < budget; ++k)
        {
            size_t i = (thumbRebuildCursor + k) % n;

            if (i >= visStart && i <= visEnd) continue;

            if (needsRebuild(i))
            {
                rebuildThumb(i);
                done++;
            }
        }
    }

    const size_t keepPad = 8;
    size_t keepStart = (visStart > keepPad) ? (visStart - keepPad) : 0;
    size_t keepEnd = std::min(n - 1, visEnd + keepPad);
    for (size_t i = 0; i < n; ++i)
    {
        if (i >= keepStart && i <= keepEnd) continue;
        if (thumbCache[i].tex)
        {
            SDL_DestroyTexture(thumbCache[i].tex);
            thumbCache[i].tex = nullptr;
        }
    }

    runtimeState.render.backgroundJobsPending = backgroundJobs.pendingCount();
    runtimeState.render.backgroundJobsActive = backgroundJobs.activeCount();
    thumbRebuildCursor = (thumbRebuildCursor + 1) % n;
}

void App::setProjectFpsForFlow(int fps)
{
    flow.setProjectFps(fps);
    flow.settings.clampToSafeLimits();
}

SDL_Texture* App::makeUiText(const std::string& text, SDL_Color col, int* outW, int* outH)
{
    if (!uiFont || !sdlRenderer) return nullptr;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(uiFont, text.c_str(), col);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(sdlRenderer, surf);
    if (outW) *outW = surf->w;
    if (outH) *outH = surf->h;
    SDL_FreeSurface(surf);
    return tex;
}

void App::handleSplashOverlayEvent(const SDL_Event& e)
{
    if (e.type != SDL_MOUSEBUTTONDOWN || e.button.button != SDL_BUTTON_LEFT) return;

    const int mx = e.button.x;
    const int my = e.button.y;
    auto hit = [&](const SDL_Rect& rc) { return mx >= rc.x && mx < rc.x + rc.w && my >= rc.y && my < rc.y + rc.h; };

    const auto state = updateManager.getState();
    if (state == strova::UpdateManager::State::UpdateAvailable)
    {
        if (hit(splashPrimaryBtn))
        {
            strova::debug::log("App", "Splash overlay: user chose Update now.");
            splash.setStatusText("Downloading update...", "Preparing new build package");
            updateManager.startApplyUpdate();
            return;
        }
        if (hit(splashSecondaryBtn))
        {
            strova::debug::log("App", "Splash overlay: user chose Skip or Continue.");
            updateManager.declineUpdate();
            return;
        }
    }
    else if (state == strova::UpdateManager::State::Failed)
    {
        if (hit(splashPrimaryBtn) || hit(splashSecondaryBtn))
        {
            updateManager.declineUpdate();
            return;
        }
    }
}

void App::drawSplashOverlay(int w, int h)
{
    const auto state = updateManager.getState();
    splashPrimaryBtn = SDL_Rect{};
    splashSecondaryBtn = SDL_Rect{};

    if (state != strova::UpdateManager::State::UpdateAvailable &&
        state != strova::UpdateManager::State::Failed)
    {
        return;
    }

    SDL_Rect overlay{ 0,0,w,h };
    fillRectA(sdlRenderer, overlay, SDL_Color{ 2, 5, 12, 148 });

    SDL_Rect shadow{ w / 2 - 252, h / 2 - 136, 504, 292 };
    fillRectA(sdlRenderer, shadow, SDL_Color{ 0, 0, 0, 96 });

    SDL_Rect card{ w / 2 - 240, h / 2 - 124, 480, 268 };
    fillRectA(sdlRenderer, card, SDL_Color{ 11, 17, 28, 244 });
    drawRectA(sdlRenderer, card, SDL_Color{ 88, 126, 220, 160 });

    SDL_Rect accent{ card.x + 24, card.y + 24, 64, 5 };
    fillRectA(sdlRenderer, accent, SDL_Color{ 86, 136, 255, 255 });

    auto drawTextAt = [&](const std::string& txt, int x, int y, SDL_Color col, int* outW = nullptr, int* outH = nullptr, int wrap = 0)
        {
            int tw = 0, th = 0;
            SDL_Texture* tex = nullptr;
            if (wrap > 0)
            {
                SDL_Surface* surf = TTF_RenderUTF8_Blended_Wrapped(uiFont, txt.c_str(), col, wrap);
                if (!surf) return;
                tex = SDL_CreateTextureFromSurface(sdlRenderer, surf);
                tw = surf->w; th = surf->h;
                SDL_FreeSurface(surf);
            }
            else
            {
                tex = makeUiText(txt, col, &tw, &th);
            }
            if (!tex) return;
            SDL_Rect dst{ x, y, tw, th };
            SDL_RenderCopy(sdlRenderer, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
            if (outW) *outW = tw;
            if (outH) *outH = th;
        };

    auto drawButton = [&](const SDL_Rect& rc, SDL_Color fill, SDL_Color border, const std::string& label)
        {
            fillRectA(sdlRenderer, rc, fill);
            drawRectA(sdlRenderer, rc, border);
            int tw = 0, th = 0;
            SDL_Texture* tex = makeUiText(label, SDL_Color{ 250, 252, 255, 255 }, &tw, &th);
            if (!tex) return;
            SDL_Rect dst{ rc.x + (rc.w - tw) / 2, rc.y + (rc.h - th) / 2, tw, th };
            SDL_RenderCopy(sdlRenderer, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        };

    const strova::UpdateInfo info = updateManager.getInfo();
    std::string eyebrow = "Startup update check";
    std::string title = "Checking for updates";
    std::string body = updateManager.getStatusLine();
    std::string foot = "";

    if (state == strova::UpdateManager::State::UpdateAvailable)
    {
        eyebrow = "New build ready";
        title = info.latestVersion.empty() ? "Update available" : ("Strova " + info.latestVersion + " is ready");
        body = "A new build was found for this device. Download and prepare the update now?";
        foot = "Your app data and projects stay in place.";
    }
    else if (state == strova::UpdateManager::State::Downloading)
    {
        eyebrow = "Downloading package";
        title = "Retrieving update files";
        body = "Please wait while Strova downloads the correct package for this platform.";
        foot = updateManager.getStatusLine();
    }
    else if (state == strova::UpdateManager::State::Extracting)
    {
        eyebrow = "Preparing install";
        title = "Unzipping update into app root";
        body = "Files are being extracted into the app folder so the updater can swap the executable safely.";
        foot = "root/update/update_replace.exe will be launched next.";
    }
    else if (state == strova::UpdateManager::State::ReadyToLaunchUpdater)
    {
        eyebrow = "Restarting to finish";
        title = "Update staged successfully";
        const Uint32 nowMs = SDL_GetTicks();
        int remain = 5;
        if (pendingUpdaterLaunch && pendingUpdaterLaunchAtMs > nowMs)
            remain = (int)((pendingUpdaterLaunchAtMs - nowMs + 999) / 1000);
        else if (pendingUpdaterLaunch)
            remain = 0;
        body = "The app will restart in " + std::to_string(remain) + " second" + (remain == 1 ? std::string() : std::string("s")) + " to complete the update.";
        foot = "Please do not reopen Strova until the updater finishes.";
    }
    else if (state == strova::UpdateManager::State::Failed)
    {
        eyebrow = "Update problem";
        title = "Unable to prepare update";
        if (!info.errorMessage.empty()) body = info.errorMessage;
        foot = "You can continue into Strova and try again later.";
    }

    drawTextAt(eyebrow, card.x + 28, card.y + 44, SDL_Color{ 126, 170, 255, 255 });
    drawTextAt(title, card.x + 28, card.y + 76, SDL_Color{ 242, 247, 255, 255 });
    drawTextAt(body, card.x + 28, card.y + 116, SDL_Color{ 197, 206, 224, 255 }, nullptr, nullptr, card.w - 56);
    if (!foot.empty())
        drawTextAt(foot, card.x + 28, card.y + 176, SDL_Color{ 140, 152, 176, 255 }, nullptr, nullptr, card.w - 56);

    if (state == strova::UpdateManager::State::Checking || state == strova::UpdateManager::State::Downloading || state == strova::UpdateManager::State::Extracting || state == strova::UpdateManager::State::ReadyToLaunchUpdater)
    {
        SDL_Rect bar{ card.x + 28, card.y + card.h - 34, card.w - 56, 8 };
        fillRectA(sdlRenderer, bar, SDL_Color{ 26, 35, 52, 255 });
        SDL_Rect fill = bar;
        Uint32 pulse = SDL_GetTicks() % 1600;
        float t = (float)pulse / 1600.0f;
        if (state == strova::UpdateManager::State::ReadyToLaunchUpdater) t = 1.0f;
        fill.w = (int)((float)bar.w * (0.28f + 0.72f * t));
        fillRectA(sdlRenderer, fill, SDL_Color{ 74, 126, 255, 255 });
    }

    if (state == strova::UpdateManager::State::UpdateAvailable)
    {
        splashPrimaryBtn = SDL_Rect{ card.x + 28, card.y + 212, 178, 40 };
        splashSecondaryBtn = SDL_Rect{ card.x + 218, card.y + 212, 120, 40 };
        drawButton(splashPrimaryBtn, SDL_Color{ 54, 112, 255, 255 }, SDL_Color{ 110, 156, 255, 255 }, "Update now");
        drawButton(splashSecondaryBtn, SDL_Color{ 28, 36, 52, 255 }, SDL_Color{ 72, 88, 118, 255 }, "Skip");
    }
    else if (state == strova::UpdateManager::State::Failed)
    {
        splashPrimaryBtn = SDL_Rect{ card.x + 28, card.y + 212, 146, 40 };
        drawButton(splashPrimaryBtn, SDL_Color{ 28, 36, 52, 255 }, SDL_Color{ 72, 88, 118, 255 }, "Continue");
    }
}

void App::maybeFinishSplashOrLaunchUpdater()
{
    if (mode != Mode::Splash) return;

    std::filesystem::path updaterExe;
    std::string updaterArgs;
    if (updateManager.takeLaunchRequest(updaterExe, updaterArgs))
    {
        strova::debug::logPath("App", "Launching updater executable", updaterExe);
        strova::debug::log("App", std::string("Updater arguments: ") + updaterArgs);
        pendingUpdaterLaunch = true;
        pendingUpdaterLaunchAtMs = SDL_GetTicks() + 5000u;
        pendingUpdaterExe = updaterExe;
        pendingUpdaterArgs = updaterArgs;
        return;
    }

    if (pendingUpdaterLaunch)
    {
        if (SDL_GetTicks() >= pendingUpdaterLaunchAtMs)
        {
            std::string launchErr;
            if (strova::UpdateManager::launchDetached(pendingUpdaterExe, pendingUpdaterArgs, launchErr))
            {
                strova::debug::log("App", "Updater launched successfully. Main app will exit.");
                running = false;
                return;
            }
            strova::debug::log("App", std::string("Updater launch failed: ") + launchErr);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Strova Update", launchErr.c_str(), window);
            pendingUpdaterLaunch = false;
            pendingUpdaterArgs.clear();
            pendingUpdaterExe.clear();
            updateManager.declineUpdate();
        }
        return;
    }

    const auto state = updateManager.getState();
    if (state == strova::UpdateManager::State::Disabled ||
        state == strova::UpdateManager::State::WaitingForDailyWindow ||
        state == strova::UpdateManager::State::NoUpdate ||
        state == strova::UpdateManager::State::Declined)
    {
        strova::debug::log("App", "Splash complete. Transitioning to launcher.");
        SDL_SetWindowBordered(window, SDL_TRUE);
        mode = Mode::Launcher;
    }
}

void App::handleEvent(SDL_Event& e)
{
    if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE))
    {
        if (mode == Mode::Editor)
            requestSaveProjectNow();
        running = false;
        return;
    }

    if (mode == Mode::Splash)
    {
        const auto state = updateManager.getState();

        if (state == strova::UpdateManager::State::UpdateAvailable ||
            state == strova::UpdateManager::State::Failed)
        {
            handleSplashOverlayEvent(e);
        }
        else
        {
            splash.handleEvent(e);
        }
        return;
    }

    if (mode == Mode::Launcher) launcherScreen->handleEvent(*this, e);
    else if (mode == Mode::BrushCreator) { if (brushCreatorScreen) brushCreatorScreen->handleEvent(*this, e); }
    else enqueueEditorEvent(e);
}

void App::render()
{
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);

    if (mode == Mode::Splash)
    {
        SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
        SDL_RenderClear(sdlRenderer);

        splash.draw(sdlRenderer, w, h, uiFont);
        drawSplashOverlay(w, h);
        SDL_RenderPresent(sdlRenderer);
        return;
    }

    const Uint64 renderStart = SDL_GetPerformanceCounter();
    if (mode == Mode::Launcher) launcherScreen->render(*this, w, h);
    else if (mode == Mode::BrushCreator) { if (brushCreatorScreen) brushCreatorScreen->render(*this, w, h); }
    else editor->render(*this, w, h);
    if (mode == Mode::Editor)
    {
        noteRenderTime(perfMs(renderStart, SDL_GetPerformanceCounter()));
        syncRuntimeStateFromEditor();
    }

    SDL_RenderPresent(sdlRenderer);
}

void App::run()
{
    strova::debug::log("App", "run() entered.");
    SDL_Event e;

    lastTicks = SDL_GetPerformanceCounter();
    playLastCounterRef() = 0;

    while (running)
    {
        while (SDL_PollEvent(&e))
        {
            handleEvent(e);
        }

        Uint64 now = SDL_GetPerformanceCounter();

        double dt =
            (double)(now - lastTicks) /
            (double)SDL_GetPerformanceFrequency();

        lastTicks = now;

        if (mode == Mode::Editor)
        {
            beginEditorFrame(dt);
            editor->update(*this, dt);
            if (!playingRef()) processThumbJobs(1);
            syncRuntimeStateFromEditor();
        }
        else if (mode == Mode::BrushCreator)
        {
            if (brushCreatorScreen) brushCreatorScreen->update(*this, dt);
        }
        if (mode == Mode::Splash)
        {
            Uint32 nowMs = SDL_GetTicks();
            const auto updateState = updateManager.getState();
            if (updateState == strova::UpdateManager::State::Checking) splash.setStatusText("Checking for updates...", "Pinging release channel");
            else if (updateState == strova::UpdateManager::State::Downloading) splash.setStatusText("Downloading update...", "Retrieving package from server");
            else if (updateState == strova::UpdateManager::State::Extracting) splash.setStatusText("Preparing update...", "Unzipping files into app root");
            else if (updateState == strova::UpdateManager::State::ReadyToLaunchUpdater) splash.setStatusText("Restarting to install update...", "Handing off to update_replace.exe");
            else processThumbJobs(1);
            splash.update(nowMs);

            const std::int64_t checked = updateManager.getCompletedCheckEpoch();
            if (checked > 0 && checked != appSettings.lastUpdateCheckEpoch)
            {
                appSettings.lastUpdateCheckEpoch = checked;
                std::string settingsErr;
                if (!AppSettingsIO::save(appSettings, settingsErr)) strova::debug::log("App", std::string("Failed to save lastUpdateCheckEpoch: ") + settingsErr);
                else strova::debug::log("App", std::string("Saved completed update check epoch: ") + std::to_string(checked));
            }

            if (splash.finished())
            {
                maybeFinishSplashOrLaunchUpdater();
            }
        }

        render();
    }
    strova::debug::log("App", "run() exiting.");
}

void App::shutdown()
{
    strova::debug::log("App", "shutdown() entered.");
    if (mode == Mode::Editor)
    {
        std::string err;
        if (!saveProjectNow(err)) strova::debug::log("App", std::string("saveProjectNow during shutdown failed: ") + err);
        else strova::debug::log("App", "Project saved during shutdown.");
    }

    saveDockLayoutForCurrentContext();
    pluginManagerStore.bindProject(nullptr);
    pluginManagerStore.bindApp(nullptr);
    pluginManagerStore.shutdown();
    saveColorPickerWindowState();

    if (brushRenderer)
    {
        delete brushRenderer;
        brushRenderer = nullptr;
        strova::debug::log("App", "BrushRenderer destroyed.");
    }
    if (brushCreatorScreen) { delete brushCreatorScreen; brushCreatorScreen = nullptr; }
    if (launcherScreen) { delete launcherScreen; launcherScreen = nullptr; }
    if (editor) { delete editor; editor = nullptr; }
    strova::brush::setGlobalManager(nullptr);
    backgroundJobs.stop();

    for (auto& t : thumbCache)
    {
        if (t.tex) { SDL_DestroyTexture(t.tex); t.tex = nullptr; }
    }
    thumbCache.clear();
#define DESTROY_ICON(x) if (x) { SDL_DestroyTexture(x); x = nullptr; }

    DESTROY_ICON(iconBrush);
    DESTROY_ICON(iconPencil);
    DESTROY_ICON(iconPen);
    DESTROY_ICON(iconMarker);
    DESTROY_ICON(iconAirbrush);
    DESTROY_ICON(iconCalligraphy);
    DESTROY_ICON(iconEraser);
    DESTROY_ICON(iconSmudge);
    DESTROY_ICON(iconBlur);
    DESTROY_ICON(iconGlow);
    DESTROY_ICON(iconRuler);
    DESTROY_ICON(iconSoften);
    DESTROY_ICON(iconBucket);
    DESTROY_ICON(iconLine);
    DESTROY_ICON(iconOval);
    DESTROY_ICON(iconRectangle);
    DESTROY_ICON(iconSelect);
    DESTROY_ICON(iconDropper);
    DESTROY_ICON(iconUndo);
    DESTROY_ICON(iconRedo);

#undef DESTROY_ICON
    if (colorTex)
    {
        SDL_DestroyTexture(colorTex);
        colorTex = nullptr;
    }

    if (uiFont)
    {
        TTF_CloseFont(uiFont);
        uiFont = nullptr;
    }

    IMG_Quit();
    TTF_Quit();

    if (sdlRenderer)
    {
        strova::ui_brush_preview::purgePreviewCache();
        strova::layer_render::purgeAllImageTextureCache();
        strova::render_cache::clearAll();
        BrushRenderer::purgeCache(sdlRenderer);
        SDL_DestroyRenderer(sdlRenderer);
        sdlRenderer = nullptr;
    }

    if (window)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    SDL_Quit();
}
