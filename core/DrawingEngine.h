/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/DrawingEngine.h
   Module:      Core
   Purpose:     Core drawing state, frame storage, and track helpers.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include <SDL.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <functional>
#include <memory>
#include <utility>

#include "../core/RasterSurface.h"

#include "../core/Stroke.h"
#include "../core/Tool.h"
#include "../core/Gradient.h"
#include "../core/FlowCapturer.h"
#include "../core/BrushSystem.h"

class DrawingEngine
{
public:
    using TrackId = int;

    enum class TrackKind : int
    {
        FlowLink = 0,
        Draw = 1,
        Flow = 2,
        Audio = 3
    };

    struct Track
    {
        TrackId id = 0;
        TrackKind kind = TrackKind::Draw;
        std::string name;

        
        bool visible = true;
        bool muted = false;
        bool locked = false;
        float opacity = 1.0f;
    };

    struct LayerTransform
    {
        float posX = 0.0f;
        float posY = 0.0f;
        float rotation = 0.0f;
        float pivotX = 0.0f;
        float pivotY = 0.0f;
    };

    struct TransformKeyframe
    {
        int frameIndex = 0;
        float value = 0.0f;
    };

    struct VisibilityKey
    {
        int frameIndex = 0;
        bool visible = true;
    };

    enum class TransformChannel : int
    {
        PosX = 0,
        PosY = 1,
        Rotation = 2
    };

    enum class TransformInterpolationMode : int
    {
        Linear = 0,
        Ease = 1
    };

    struct TransformKeyTrack
    {
        std::vector<TransformKeyframe> posX;
        std::vector<TransformKeyframe> posY;
        std::vector<TransformKeyframe> rotation;
        TransformInterpolationMode interpolation = TransformInterpolationMode::Linear;

        bool empty() const
        {
            return posX.empty() && posY.empty() && rotation.empty();
        }
    };

    enum class LayerContentType : int
    {
        Stroke = 0,
        Image = 1
    };

    struct LayerImage
    {
        std::shared_ptr<RasterSurface> surface;

        bool empty() const
        {
            return !surface || surface->empty();
        }

        int width() const
        {
            return surface ? surface->width() : 0;
        }

        int height() const
        {
            return surface ? surface->height() : 0;
        }

        const std::string& sourcePath() const
        {
            static const std::string kEmpty;
            return surface ? surface->sourcePath() : kEmpty;
        }

        void setSourcePath(const std::string& path)
        {
            ensureSurface();
            surface->setSourcePath(path);
        }

        const std::vector<std::uint8_t>& rgba() const
        {
            static const std::vector<std::uint8_t> kEmpty;
            return surface ? surface->pixels() : kEmpty;
        }

        std::vector<std::uint8_t>& rgbaMutable()
        {
            ensureSurface();
            return surface->pixelsMutable();
        }

        void setData(int width, int height, std::vector<std::uint8_t> pixels, const std::string& path = {})
        {
            ensureSurface();
            surface->setData(width, height, std::move(pixels), path);
        }

        void clear()
        {
            surface.reset();
        }

        std::size_t byteSize() const
        {
            return surface ? surface->byteSize() : 0;
        }

        LayerImage cloneDetached() const
        {
            LayerImage copy;
            if (surface)
                copy.surface = surface->clone();
            return copy;
        }

        std::uint64_t surfaceRevision() const
        {
            return surface ? surface->revision() : 0;
        }

        std::size_t dirtyTileCount() const
        {
            return surface ? surface->dirtyTileCount() : 0;
        }

        bool surfaceDirty() const
        {
            return surface ? surface->dirty() : false;
        }

        void markDirtyRect(int x, int y, int w, int h)
        {
            ensureSurface();
            surface->markDirtyRect(x, y, w, h);
        }

        std::shared_ptr<RasterSurface> surfacePtr() const
        {
            return surface;
        }

    private:
        void ensureSurface()
        {
            if (!surface)
                surface = std::make_shared<RasterSurface>();
        }
    };

    struct TrackLayer
    {
        TrackId trackId = 0;
        TrackKind kind = TrackKind::Draw;
        std::vector<Stroke> strokes;
        LayerContentType contentType = LayerContentType::Stroke;
        LayerTransform transform{};
        LayerTransform baseTransform{};
        LayerImage image{};
        std::uint64_t imageRevision = 0;
        std::uint64_t contentRevision = 1;
        std::uint64_t transformRevision = 1;
        std::uint64_t visibilityRevision = 1;
        std::uint64_t celId = 0;
        int drawSourceFrame = -1;
        bool ownsDrawContent = false;
        bool visible = true;
        bool hasDrawEntry = false;
        bool hasTransformEntry = false;
    };

    struct Frame
    {
        std::vector<TrackLayer> layers;

        mutable bool compositeDirty = true;
        mutable std::vector<Stroke> composite;

        void markDirty() { compositeDirty = true; }

        const std::vector<Stroke>& getComposite(const std::vector<Track>& tracks) const
        {
            if (!compositeDirty) return composite;

            composite.clear();

            auto findTrackPtr = [&](TrackId id) -> const Track*
                {
                    for (const auto& t : tracks)
                        if (t.id == id) return &t;
                    return nullptr;
                };

            for (const auto& L : layers)
            {
                const Track* tr = findTrackPtr(L.trackId);
                if (!tr) continue;

                if (!tr->visible) continue;
                if (tr->muted) continue;

                float op = std::clamp(tr->opacity, 0.0f, 1.0f);

                if (op >= 0.999f)
                {
                    composite.insert(composite.end(), L.strokes.begin(), L.strokes.end());
                }
                else if (op <= 0.001f)
                {
                    continue;
                }
                else
                {
                    for (const auto& s : L.strokes)
                    {
                        Stroke copy = s;
                        int a = (int)copy.color.a;
                        a = (int)std::round((float)a * op);
                        copy.color.a = (Uint8)std::clamp(a, 0, 255);
                        composite.push_back(std::move(copy));
                    }
                }
            }

            compositeDirty = false;
            return composite;
        }
    };

    struct ToolState
    {
        ToolType type = ToolType::Brush;
        SDL_Color color{ 0,0,0,255 };
        float thickness = 2.0f;

        
        GradientConfig gradient{};
    };

public:
    DrawingEngine() = default;

    
    TrackId createTrack(TrackKind kind, const std::string& name);
    TrackId createTrackWithId(TrackId forcedId, TrackKind kind, const std::string& name);
    void    clearTracks();
    bool    removeTrack(TrackId id);
    void    setActiveTrack(TrackId id);
    TrackId getActiveTrack() const { return activeTrack; }

    const std::vector<Track>& getTracks() const { return tracks; }
    Track* findTrack(TrackId id);
    const Track* findTrack(TrackId id) const;

    
    bool setTrackVisible(TrackId id, bool v);
    bool setTrackMuted(TrackId id, bool m);
    bool setTrackLocked(TrackId id, bool l);
    bool setTrackOpacity(TrackId id, float opacity01);

    bool getTrackVisible(TrackId id) const
    {
        if (auto* t = findTrack(id)) return t->visible;
        return true;
    }

    bool getTrackMuted(TrackId id) const
    {
        if (auto* t = findTrack(id)) return t->muted;
        return false;
    }

    bool getTrackLocked(TrackId id) const
    {
        if (auto* t = findTrack(id)) return t->locked;
        return false;
    }

    float getTrackOpacity(TrackId id) const
    {
        if (auto* t = findTrack(id)) return t->opacity;
        return 1.0f;
    }

    
    void   clearAndResizeFrames(size_t count);
    void   setCurrentFrameIndex(size_t idx);
    size_t getCurrentFrameIndex() const { return currentFrame; }
    size_t getFrameCount() const { return frames.size(); }

    void setFrameStrokes(size_t frameIndex, const std::vector<Stroke>& strokes);
    const std::vector<Stroke>& getFrameStrokes(size_t frameIndex) const;

    const std::vector<Stroke>& getFrameTrackStrokes(size_t frameIndex, TrackId trackId) const;
    void setFrameTrackStrokes(size_t frameIndex, TrackId trackId, const std::vector<Stroke>& strokes);
    const TrackLayer* getFrameTrackLayer(size_t frameIndex, TrackId trackId) const;
    TrackLayer* getFrameTrackLayerMutable(size_t frameIndex, TrackId trackId);
    std::uint64_t getEvaluatedDrawCelId(size_t frameIndex, TrackId trackId) const;
    int getResolvedDrawSourceFrame(size_t frameIndex, TrackId trackId) const;
    bool frameTrackOwnsDrawContent(size_t frameIndex, TrackId trackId) const;
    bool setFrameTrackTransform(size_t frameIndex, TrackId trackId, const LayerTransform& transform);
    LayerTransform getFrameTrackTransform(size_t frameIndex, TrackId trackId) const;
    bool setFrameTrackImage(size_t frameIndex, TrackId trackId, const LayerImage& image, const LayerTransform& transform);
    bool clearFrameTrackImage(size_t frameIndex, TrackId trackId);

    const TransformKeyTrack* getTransformKeyTrack(TrackId trackId) const;
    TransformKeyTrack* getTransformKeyTrackMutable(TrackId trackId);
    const std::vector<TransformKeyframe>* getTransformKeyframes(TrackId trackId, TransformChannel channel) const;
    std::vector<TransformKeyframe>* getTransformKeyframesMutable(TrackId trackId, TransformChannel channel);
    TransformInterpolationMode getTransformInterpolationMode(TrackId trackId) const;
    void setTransformInterpolationMode(TrackId trackId, TransformInterpolationMode mode);
    bool hasTransformKeys(TrackId trackId) const;
    void clearTransformKeys(TrackId trackId);
    void setTransformKeyframe(TrackId trackId, TransformChannel channel, int frameIndex, float value);
    bool removeTransformKeyframe(TrackId trackId, TransformChannel channel, int frameIndex);
    bool moveTransformKeyframe(TrackId trackId, TransformChannel channel, int fromFrame, int toFrame);

    const std::vector<VisibilityKey>* getVisibilityKeys(TrackId trackId) const;
    std::vector<VisibilityKey>* getVisibilityKeysMutable(TrackId trackId);
    bool evaluateTrackVisibility(size_t frameIndex, TrackId trackId) const;
    void setVisibilityKeyframe(TrackId trackId, int frameIndex, bool visible);
    bool removeVisibilityKeyframe(TrackId trackId, int frameIndex);
    bool moveVisibilityKeyframe(TrackId trackId, int fromFrame, int toFrame);
    void clearVisibilityKeys(TrackId trackId);
    bool hasVisibilityKeys(TrackId trackId) const;

    const std::vector<FlowLinkClip>& getFlowLinkClips(TrackId targetTrackId) const;
    std::vector<FlowLinkClip>* getFlowLinkClipsMutable(TrackId targetTrackId);
    void addFlowLinkClip(TrackId targetTrackId, const FlowLinkClip& clip);
    void clearFlowLinkClips(TrackId targetTrackId);
    LayerTransform evaluateFlowLinkTransform(size_t frameIndex, TrackId trackId, const LayerTransform& fallback) const;
    LayerTransform evaluateTrackTransform(size_t frameIndex, TrackId trackId, const LayerTransform& fallback) const;
    TrackLayer getEvaluatedFrameTrackLayerCopy(size_t frameIndex, TrackId trackId) const;
    void syncFrameTransformsFromKeyframes();

    
    void beginStroke(float x, float y);
    void addPoint(float x, float y);
    void endStroke();

    
    void beginEraseSession();
    void eraseAt(float x, float y, float radiusWorld);
    void endEraseSession();

    
    void setTool(ToolType t);
    void setColor(SDL_Color c);
    void setThickness(float th);

    void setToolSettings(const ToolSettings& settings);
    const ToolSettings& getToolSettings() const { return currentToolSettings; }

    void setBrushSelection(const std::string& brushId, int version = 1, const std::string& displayName = "");
    const std::string& getBrushSelectionId() const { return selectedBrushId; }
    int getBrushSelectionVersion() const { return selectedBrushVersion; }

    
    void setGradientConfig(const GradientConfig& cfg);
    const GradientConfig& getGradientConfig() const;

    
    SDL_Color sampleGradient(float t) const;

    void setBrushSize(float px) { setThickness(px); }
    void setBrushColor(SDL_Color c) { setColor(c); }

    SDL_Color getBrushColor() const { return currentTool.color; }
    float     getBrushSize()  const { return currentTool.thickness; }

    
    void nextFrame();
    void prevFrame();
    void addFrame();

    
    
    void insertFrame(size_t at);
    
    void deleteFrame(size_t at);
    
    void duplicateFrame(size_t at);

    
    bool setTrackName(TrackId id, const std::string& name);
    bool setTrackOrder(const std::vector<TrackId>& orderedIds);

    
    const std::vector<Stroke>& getCurrentStrokes() const;
    int countDirtyFrameComposites() const;
    int countDirtyRasterTiles() const;
    std::size_t estimateUndoBytes() const;

    
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    
    void beginTimelineTransaction();
    void commitTimelineTransaction();
    void rollbackTimelineTransaction();

    void shiftTrackRange(TrackId trackId, int srcStartFrame, int lengthFrames, int dstStartFrame);

    // Public so lightweight file-local helpers in DrawingEngine.cpp can inspect
    // edit records without tripping access errors. This does not change engine
    // behavior; it only fixes type visibility for compilation.
    struct TimelineSnapshot
    {
        std::vector<Frame> frames;
        size_t currentFrame = 0;
        std::unordered_map<TrackId, TransformKeyTrack> transformKeyTracks;
        std::unordered_map<TrackId, std::vector<VisibilityKey>> visibilityKeyTracks;
        std::unordered_map<TrackId, std::vector<FlowLinkClip>> flowLinkClips;
    };

    struct LayerEditRecord
    {
        size_t frameIndex = 0;
        TrackId trackId = 0;
        bool hadBefore = false;
        bool hadAfter = false;
        TrackLayer before{};
        TrackLayer after{};
    };

    struct TrackEditRecord
    {
        TrackId trackId = 0;
        Track before{};
        Track after{};
    };

    struct EditRecord
    {
        enum class Kind : int
        {
            Layer = 0,
            Track = 1
        };

        Kind kind = Kind::Layer;
        LayerEditRecord layer{};
        TrackEditRecord track{};
    };

    TrackLayer* layerFor(Frame& f, TrackId id, TrackKind kindFallback);
    const TrackLayer* layerFor(const Frame& f, TrackId id) const;
    void restoreLayerState(Frame& f, const LayerEditRecord& rec, bool useAfterState);
    bool captureLayerEdit(size_t frameIndex, TrackId trackId, LayerEditRecord& outBefore) const;
    void pushFrameEdit(const EditRecord& rec);
    void clearFrameRedo();
    bool applyTrackMutation(TrackId id, const std::function<bool(Track&)>& mutator);
    static bool trackLayerHasRenderableEntry(const TrackLayer& layer);
    bool trackLayerHasOwnDrawContent(const TrackLayer& layer) const;
    std::uint64_t allocateCelId();
    void assignOwnContentIdentity(TrackLayer& layer);
    TrackLayer* ensureOwnDrawLayerMutable(size_t frameIndex, TrackId trackId);
    int resolveDrawSourceFrameIndex(size_t frameIndex, TrackId trackId) const;
    const TrackLayer* resolveDrawSourceLayer(size_t frameIndex, TrackId trackId) const;
    const TrackLayer* resolveTransformSourceLayer(size_t frameIndex, TrackId trackId) const;
    LayerTransform resolveBaseTransformForFrame(size_t frameIndex, TrackId trackId, const LayerTransform& fallback) const;

    void ensureHistorySize();
    void initHistory();

    void pushUndoSnapshot();
    void clearRedo();

    void markAllFramesDirty()
    {
        for (auto& f : frames) f.markDirty();
    }

    void pushTimelineUndoSnapshot(const TimelineSnapshot& snap);

private:
    static constexpr int MAX_HISTORY = 80;

    std::vector<Frame> frames;
    size_t currentFrame = 0;

    std::vector<Track> tracks;
    std::unordered_map<TrackId, TransformKeyTrack> transformKeyTracks;
    std::unordered_map<TrackId, std::vector<VisibilityKey>> visibilityKeyTracks;
    std::unordered_map<TrackId, std::vector<FlowLinkClip>> flowLinkClips;
    TrackId nextTrackId = 1;
    TrackId activeTrack = 0;
    std::uint64_t nextCelId = 1;

    Stroke* currentStroke = nullptr;

    ToolState currentTool;
    ToolSettings currentToolSettings{};
    std::string selectedBrushId = "strova.builtin.soft_round";
    std::string selectedBrushName = "Soft Round";
    int selectedBrushVersion = 1;

    std::vector<std::vector<EditRecord>> undoStack;
    std::vector<std::vector<EditRecord>> redoStack;

    bool inTimelineTxn = false;
    TimelineSnapshot txnBefore;
    std::vector<TimelineSnapshot> timelineUndo;
    std::vector<TimelineSnapshot> timelineRedo;

    bool strokeEditPending = false;
    LayerEditRecord pendingStrokeEdit{};
    bool eraseEditPending = false;
    LayerEditRecord pendingEraseEdit{};
    bool erasingSession = false;
};


static inline float strova_clamp01f(float v)
{
    return (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline SDL_Color strova_lerpColor(SDL_Color a, SDL_Color b, float t)
{
    t = strova_clamp01f(t);
    SDL_Color o{};
    o.r = (Uint8)std::lround(a.r * (1.0f - t) + b.r * t);
    o.g = (Uint8)std::lround(a.g * (1.0f - t) + b.g * t);
    o.b = (Uint8)std::lround(a.b * (1.0f - t) + b.b * t);
    o.a = (Uint8)std::lround(a.a * (1.0f - t) + b.a * t);
    return o;
}

static inline SDL_Color strova_sampleStops4(const GradientConfig& g, float t)
{
    t = strova_clamp01f(t);

    int hi = 1;
    while (hi < STROVA_MAX_GRADIENT_STOPS && g.stopPos[(size_t)hi] < t)
        ++hi;

    if (hi >= STROVA_MAX_GRADIENT_STOPS)
        return g.stopColor[STROVA_MAX_GRADIENT_STOPS - 1];

    int lo = (hi > 0) ? (hi - 1) : 0;

    float a = g.stopPos[(size_t)lo];
    float b = g.stopPos[(size_t)hi];
    float f = (b > a) ? (t - a) / (b - a) : 0.0f;

    return strova_lerpColor(g.stopColor[(size_t)lo], g.stopColor[(size_t)hi], f);
}

inline const GradientConfig& DrawingEngine::getGradientConfig() const
{
    return currentTool.gradient;
}

inline SDL_Color DrawingEngine::sampleGradient(float t) const
{
    if (!currentTool.gradient.enabled || currentTool.gradient.mode == 0)
        return currentTool.color;

    SDL_Color c = strova_sampleStops4(currentTool.gradient, t);
    c.a = currentTool.color.a; 
    return c;
}
