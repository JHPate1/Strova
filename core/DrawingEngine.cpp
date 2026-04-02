/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/DrawingEngine.cpp
   Module:      Core
   Purpose:     Frame, track, and stroke state management.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "../core/DrawingEngine.h"
#include "../core/Gradient.h"
#include "../core/StrovaLimits.h"
#include <algorithm>
#include <cstddef>
#include <cmath>

static inline size_t clampIndex(size_t v, size_t hiInclusive)
{
    if (hiInclusive == 0) return 0;
    return (v > hiInclusive) ? hiInclusive : v;
}

static inline float dist2f(float ax, float ay, float bx, float by)
{
    float dx = ax - bx;
    float dy = ay - by;
    return dx * dx + dy * dy;
}

static std::size_t estimateStrokeBytes(const Stroke& stroke)
{
    return sizeof(Stroke) + stroke.points.capacity() * sizeof(StrokePoint);
}

static std::size_t estimateLayerBytes(const DrawingEngine::TrackLayer& layer)
{
    std::size_t bytes = sizeof(DrawingEngine::TrackLayer);
    if (layer.ownsDrawContent || layer.drawSourceFrame < 0)
    {
        for (const auto& stroke : layer.strokes)
            bytes += estimateStrokeBytes(stroke);
        bytes += layer.image.byteSize();
    }
    return bytes;
}

static std::size_t estimateFrameBytes(const DrawingEngine::Frame& frame)
{
    std::size_t bytes = sizeof(DrawingEngine::Frame);
    for (const auto& layer : frame.layers)
        bytes += estimateLayerBytes(layer);
    for (const auto& stroke : frame.composite)
        bytes += estimateStrokeBytes(stroke);
    return bytes;
}


static std::size_t estimateEditRecordBytes(const DrawingEngine::EditRecord& rec)
{
    std::size_t bytes = sizeof(DrawingEngine::EditRecord);
    if (rec.kind == DrawingEngine::EditRecord::Kind::Layer)
    {
        if (rec.layer.hadBefore)
            bytes += estimateLayerBytes(rec.layer.before);
        if (rec.layer.hadAfter)
            bytes += estimateLayerBytes(rec.layer.after);
    }
    else
    {
        bytes += sizeof(DrawingEngine::TrackEditRecord);
    }
    return bytes;
}

static int maxTracksForKind(DrawingEngine::TrackKind kind)
{
    switch (kind)
    {
    case DrawingEngine::TrackKind::Draw: return strova::limits::kMaxDrawTracks;
    case DrawingEngine::TrackKind::Flow: return strova::limits::kMaxFlowTracks;
    case DrawingEngine::TrackKind::FlowLink: return strova::limits::kMaxFlowLinkTracks;
    case DrawingEngine::TrackKind::Audio: return strova::limits::kMaxAudioTracks;
    default: return strova::limits::kMaxTimelineTracks;
    }
}

static int countTracksOfKind(const std::vector<DrawingEngine::Track>& tracks, DrawingEngine::TrackKind kind)
{
    int count = 0;
    for (const auto& track : tracks)
        if (track.kind == kind)
            ++count;
    return count;
}


static inline float clamp01f(float v)
{
    return (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static inline SDL_Color lerpColor(SDL_Color a, SDL_Color b, float t)
{
    t = clamp01f(t);
    SDL_Color o{};
    o.r = (Uint8)std::lround(a.r * (1.0f - t) + b.r * t);
    o.g = (Uint8)std::lround(a.g * (1.0f - t) + b.g * t);
    o.b = (Uint8)std::lround(a.b * (1.0f - t) + b.b * t);
    o.a = (Uint8)std::lround(a.a * (1.0f - t) + b.a * t);
    return o;
}


static inline SDL_Color sampleStops4(const GradientConfig& g, float t)
{
    t = clamp01f(t);

    int hi = 1;
    while (hi < STROVA_MAX_GRADIENT_STOPS && g.stopPos[(size_t)hi] < t)
        ++hi;

    if (hi >= STROVA_MAX_GRADIENT_STOPS)
        return g.stopColor[STROVA_MAX_GRADIENT_STOPS - 1];

    int lo = (hi > 0) ? (hi - 1) : 0;

    float a = g.stopPos[(size_t)lo];
    float b = g.stopPos[(size_t)hi];
    float f = (b > a) ? (t - a) / (b - a) : 0.0f;

    return lerpColor(g.stopColor[(size_t)lo], g.stopColor[(size_t)hi], f);
}

static inline SDL_Color sampleGradientCfg(const GradientConfig& g, float t)
{
    
    
    if (!g.enabled || g.mode == 0)
        return g.stopColor[0];

    return sampleStops4(g, t);
}

static std::vector<DrawingEngine::TransformKeyframe>* keyChannelMutable(
    DrawingEngine::TransformKeyTrack& keys,
    DrawingEngine::TransformChannel channel)
{
    switch (channel)
    {
    case DrawingEngine::TransformChannel::PosX: return &keys.posX;
    case DrawingEngine::TransformChannel::PosY: return &keys.posY;
    case DrawingEngine::TransformChannel::Rotation: return &keys.rotation;
    default: return &keys.posX;
    }
}

static const std::vector<DrawingEngine::TransformKeyframe>* keyChannelConst(
    const DrawingEngine::TransformKeyTrack& keys,
    DrawingEngine::TransformChannel channel)
{
    switch (channel)
    {
    case DrawingEngine::TransformChannel::PosX: return &keys.posX;
    case DrawingEngine::TransformChannel::PosY: return &keys.posY;
    case DrawingEngine::TransformChannel::Rotation: return &keys.rotation;
    default: return &keys.posX;
    }
}

static void sortAndDeduplicateKeys(std::vector<DrawingEngine::TransformKeyframe>& keys)
{
    std::stable_sort(keys.begin(), keys.end(), [](const DrawingEngine::TransformKeyframe& a, const DrawingEngine::TransformKeyframe& b)
    {
        if (a.frameIndex != b.frameIndex) return a.frameIndex < b.frameIndex;
        return false;
    });
}

static float evaluateChannelKeys(
    const std::vector<DrawingEngine::TransformKeyframe>& keys,
    int frameIndex,
    float fallback,
    DrawingEngine::TransformInterpolationMode interpolation)
{
    if (keys.empty())
        return fallback;

    auto it = std::lower_bound(keys.begin(), keys.end(), frameIndex, [](const DrawingEngine::TransformKeyframe& key, int frame)
    {
        return key.frameIndex < frame;
    });

    if (it != keys.end() && it->frameIndex == frameIndex)
    {
        auto jt = it;
        while ((jt + 1) != keys.end() && (jt + 1)->frameIndex == frameIndex)
            ++jt;
        return jt->value;
    }
    if (it == keys.begin())
        return it->value;
    if (it == keys.end())
        return keys.back().value;

    const auto& b = *it;
    const auto& a = *(it - 1);
    const float denom = (float)(b.frameIndex - a.frameIndex);
    if (denom <= 0.0001f)
        return b.value;

    float t = (float)(frameIndex - a.frameIndex) / denom;
    t = std::clamp(t, 0.0f, 1.0f);
    if (interpolation == DrawingEngine::TransformInterpolationMode::Ease)
        t = t * t * (3.0f - 2.0f * t);
    return a.value + (b.value - a.value) * t;
}

static void shiftKeysAtOrAfter(std::vector<DrawingEngine::TransformKeyframe>& keys, int frameIndex, int delta)
{
    for (auto& key : keys)
        if (key.frameIndex >= frameIndex)
            key.frameIndex += delta;
    sortAndDeduplicateKeys(keys);
}

static void shiftKeysAfter(std::vector<DrawingEngine::TransformKeyframe>& keys, int frameIndex, int delta)
{
    for (auto& key : keys)
        if (key.frameIndex > frameIndex)
            key.frameIndex += delta;
    sortAndDeduplicateKeys(keys);
}

static void removeKeyAtFrame(std::vector<DrawingEngine::TransformKeyframe>& keys, int frameIndex)
{
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const DrawingEngine::TransformKeyframe& key)
    {
        return key.frameIndex == frameIndex;
    }), keys.end());
}

static void sortVisibilityKeys(std::vector<DrawingEngine::VisibilityKey>& keys)
{
    std::stable_sort(keys.begin(), keys.end(), [](const DrawingEngine::VisibilityKey& a, const DrawingEngine::VisibilityKey& b)
    {
        if (a.frameIndex != b.frameIndex) return a.frameIndex < b.frameIndex;
        return false;
    });
}

static void removeVisibilityKeyAtFrame(std::vector<DrawingEngine::VisibilityKey>& keys, int frameIndex)
{
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const DrawingEngine::VisibilityKey& key)
    {
        return key.frameIndex == frameIndex;
    }), keys.end());
}

static void shiftVisibilityKeysAtOrAfter(std::vector<DrawingEngine::VisibilityKey>& keys, int frameIndex, int delta)
{
    for (auto& key : keys)
        if (key.frameIndex >= frameIndex)
            key.frameIndex += delta;
    sortVisibilityKeys(keys);
}

static void shiftVisibilityKeysAfter(std::vector<DrawingEngine::VisibilityKey>& keys, int frameIndex, int delta)
{
    for (auto& key : keys)
        if (key.frameIndex > frameIndex)
            key.frameIndex += delta;
    sortVisibilityKeys(keys);
}

static void shiftVisibilityTrackForInsert(std::vector<DrawingEngine::VisibilityKey>& keys, int frameIndex)
{
    shiftVisibilityKeysAtOrAfter(keys, frameIndex, 1);
}

static void shiftVisibilityTrackForDelete(std::vector<DrawingEngine::VisibilityKey>& keys, int frameIndex)
{
    removeVisibilityKeyAtFrame(keys, frameIndex);
    shiftVisibilityKeysAfter(keys, frameIndex, -1);
}

static void shiftVisibilityTrackForDuplicate(std::vector<DrawingEngine::VisibilityKey>& keys, int frameIndex)
{
    const auto copy = keys;
    shiftVisibilityKeysAfter(keys, frameIndex, 1);
    for (const auto& key : copy)
        if (key.frameIndex == frameIndex)
            keys.push_back({ frameIndex + 1, key.visible });
    sortVisibilityKeys(keys);
}

static void shiftKeyTrackForInsert(DrawingEngine::TransformKeyTrack& keys, int frameIndex)
{
    shiftKeysAtOrAfter(keys.posX, frameIndex, 1);
    shiftKeysAtOrAfter(keys.posY, frameIndex, 1);
    shiftKeysAtOrAfter(keys.rotation, frameIndex, 1);
}

static void shiftKeyTrackForDelete(DrawingEngine::TransformKeyTrack& keys, int frameIndex)
{
    removeKeyAtFrame(keys.posX, frameIndex);
    removeKeyAtFrame(keys.posY, frameIndex);
    removeKeyAtFrame(keys.rotation, frameIndex);
    shiftKeysAfter(keys.posX, frameIndex, -1);
    shiftKeysAfter(keys.posY, frameIndex, -1);
    shiftKeysAfter(keys.rotation, frameIndex, -1);
}

static void shiftKeyTrackForDuplicate(DrawingEngine::TransformKeyTrack& keys, int frameIndex)
{
    const auto copyX = keys.posX;
    const auto copyY = keys.posY;
    const auto copyR = keys.rotation;
    shiftKeysAfter(keys.posX, frameIndex, 1);
    shiftKeysAfter(keys.posY, frameIndex, 1);
    shiftKeysAfter(keys.rotation, frameIndex, 1);
    for (const auto& key : copyX) if (key.frameIndex == frameIndex) keys.posX.push_back({ frameIndex + 1, key.value });
    for (const auto& key : copyY) if (key.frameIndex == frameIndex) keys.posY.push_back({ frameIndex + 1, key.value });
    for (const auto& key : copyR) if (key.frameIndex == frameIndex) keys.rotation.push_back({ frameIndex + 1, key.value });
    sortAndDeduplicateKeys(keys.posX);
    sortAndDeduplicateKeys(keys.posY);
    sortAndDeduplicateKeys(keys.rotation);
}

static void shiftFlowLinkClipsForInsert(std::vector<FlowLinkClip>& clips, int frameIndex)
{
    for (auto& clip : clips)
        if (clip.startFrame >= frameIndex)
            clip.startFrame += 1;
}

static void shiftFlowLinkClipsForDelete(std::vector<FlowLinkClip>& clips, int frameIndex)
{
    for (auto& clip : clips)
    {
        if (clip.startFrame > frameIndex)
            clip.startFrame -= 1;
        else if (frameIndex >= clip.startFrame && frameIndex < clip.startFrame + clip.duration && clip.duration > 1)
            clip.duration -= 1;
    }
    clips.erase(std::remove_if(clips.begin(), clips.end(), [](const FlowLinkClip& clip){ return clip.duration <= 0 || clip.samples.empty(); }), clips.end());
}

static void shiftFlowLinkClipsForDuplicate(std::vector<FlowLinkClip>& clips, int frameIndex)
{
    for (auto& clip : clips)
        if (clip.startFrame > frameIndex)
            clip.startFrame += 1;
}



DrawingEngine::Track* DrawingEngine::findTrack(TrackId id)
{
    for (auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}

const DrawingEngine::Track* DrawingEngine::findTrack(TrackId id) const
{
    for (auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}

DrawingEngine::TrackLayer* DrawingEngine::layerFor(Frame& f, TrackId id, TrackKind kindFallback)
{
    for (auto& L : f.layers)
        if (L.trackId == id) return &L;

    TrackLayer L;
    L.trackId = id;
    L.kind = kindFallback;
    f.layers.push_back(std::move(L));
    f.markDirty();
    return &f.layers.back();
}

const DrawingEngine::TrackLayer* DrawingEngine::layerFor(const Frame& f, TrackId id) const
{
    for (auto& L : f.layers)
        if (L.trackId == id) return &L;
    return nullptr;
}

bool DrawingEngine::captureLayerEdit(size_t frameIndex, TrackId trackId, LayerEditRecord& outBefore) const
{
    if (frameIndex >= frames.size())
        return false;

    outBefore = LayerEditRecord{};
    outBefore.frameIndex = frameIndex;
    outBefore.trackId = trackId;

    if (const TrackLayer* existing = layerFor(frames[frameIndex], trackId))
    {
        outBefore.hadBefore = true;
        outBefore.before = *existing;
    }
    return true;
}

void DrawingEngine::restoreLayerState(Frame& f, const LayerEditRecord& rec, bool useAfterState)
{
    const bool shouldExist = useAfterState ? rec.hadAfter : rec.hadBefore;
    const TrackLayer& source = useAfterState ? rec.after : rec.before;

    for (auto it = f.layers.begin(); it != f.layers.end(); ++it)
    {
        if (it->trackId != rec.trackId)
            continue;

        if (shouldExist)
            *it = source;
        else
            f.layers.erase(it);

        f.markDirty();
        return;
    }

    if (shouldExist)
    {
        f.layers.push_back(source);
        f.markDirty();
    }
}

void DrawingEngine::pushFrameEdit(const EditRecord& rec)
{
    ensureHistorySize();
    if (rec.kind != EditRecord::Kind::Layer)
        return;
    if (rec.layer.frameIndex >= undoStack.size())
        return;

    auto& u = undoStack[rec.layer.frameIndex];
    u.push_back(rec);
    if ((int)u.size() > MAX_HISTORY)
        u.erase(u.begin());
}

void DrawingEngine::clearFrameRedo()
{
    ensureHistorySize();
    if (currentFrame < redoStack.size())
        redoStack[currentFrame].clear();
}

bool DrawingEngine::applyTrackMutation(TrackId id, const std::function<bool(Track&)>& mutator)
{
    if (auto* t = findTrack(id))
    {
        Track before = *t;
        if (!mutator(*t))
            return false;

        if (before.visible != t->visible || before.muted != t->muted ||
            before.locked != t->locked || std::fabs(before.opacity - t->opacity) > 0.0001f ||
            before.name != t->name)
        {
            TrackEditRecord trackRec{};
            trackRec.trackId = id;
            trackRec.before = before;
            trackRec.after = *t;

            EditRecord rec{};
            rec.kind = EditRecord::Kind::Track;
            rec.track = trackRec;

            ensureHistorySize();
            if (currentFrame >= undoStack.size())
                undoStack.resize(currentFrame + 1);
            undoStack[currentFrame].push_back(rec);
            if ((int)undoStack[currentFrame].size() > MAX_HISTORY)
                undoStack[currentFrame].erase(undoStack[currentFrame].begin());
            clearFrameRedo();
            markAllFramesDirty();
        }
        return true;
    }
    return false;
}

bool DrawingEngine::trackLayerHasRenderableEntry(const TrackLayer& layer)
{
    return layer.hasDrawEntry || !layer.image.empty() || !layer.strokes.empty() || layer.drawSourceFrame >= 0;
}

bool DrawingEngine::trackLayerHasOwnDrawContent(const TrackLayer& layer) const
{
    return layer.ownsDrawContent || !layer.image.empty() || !layer.strokes.empty();
}

std::uint64_t DrawingEngine::allocateCelId()
{
    return nextCelId++;
}

void DrawingEngine::assignOwnContentIdentity(TrackLayer& layer)
{
    layer.ownsDrawContent = true;
    layer.drawSourceFrame = -1;
    if (layer.celId == 0)
        layer.celId = allocateCelId();
}

int DrawingEngine::resolveDrawSourceFrameIndex(size_t frameIndex, TrackId trackId) const
{
    if (frames.empty())
        return -1;
    if (frameIndex >= frames.size())
        frameIndex = frames.size() - 1;

    const TrackLayer* direct = layerFor(frames[frameIndex], trackId);
    if (direct)
    {
        size_t cursor = frameIndex;
        const TrackLayer* layer = direct;
        int safety = 0;
        while (layer && !trackLayerHasOwnDrawContent(*layer) && layer->drawSourceFrame >= 0 &&
               static_cast<size_t>(layer->drawSourceFrame) < frames.size() && safety++ < 4096)
        {
            cursor = static_cast<size_t>(layer->drawSourceFrame);
            layer = layerFor(frames[cursor], trackId);
        }
        if (layer && trackLayerHasOwnDrawContent(*layer))
            return static_cast<int>(cursor);
    }

    for (size_t i = frameIndex + 1; i > 0; --i)
    {
        const TrackLayer* layer = layerFor(frames[i - 1], trackId);
        if (layer && trackLayerHasOwnDrawContent(*layer))
            return static_cast<int>(i - 1);
    }
    return static_cast<int>(frameIndex);
}

DrawingEngine::TrackLayer* DrawingEngine::ensureOwnDrawLayerMutable(size_t frameIndex, TrackId trackId)
{
    TrackLayer* layer = getFrameTrackLayerMutable(frameIndex, trackId);
    if (!layer)
        return nullptr;

    if (trackLayerHasOwnDrawContent(*layer))
    {
        assignOwnContentIdentity(*layer);
        return layer;
    }

    const TrackLayer* source = resolveDrawSourceLayer(frameIndex, trackId);
    if (source)
    {
        layer->strokes = source->strokes;
        layer->contentType = source->contentType;
        layer->image = source->image.cloneDetached();
        layer->imageRevision = source->imageRevision;
        layer->contentRevision = source->contentRevision;
        layer->hasDrawEntry = source->hasDrawEntry;
    }
    else
    {
        layer->strokes.clear();
        layer->image.clear();
        layer->contentType = LayerContentType::Stroke;
        layer->hasDrawEntry = false;
    }

    assignOwnContentIdentity(*layer);
    return layer;
}

const DrawingEngine::TrackLayer* DrawingEngine::resolveDrawSourceLayer(size_t frameIndex, TrackId trackId) const
{
    const int resolvedIndex = resolveDrawSourceFrameIndex(frameIndex, trackId);
    if (resolvedIndex < 0 || static_cast<size_t>(resolvedIndex) >= frames.size())
        return nullptr;
    return layerFor(frames[static_cast<size_t>(resolvedIndex)], trackId);
}

const DrawingEngine::TrackLayer* DrawingEngine::resolveTransformSourceLayer(size_t frameIndex, TrackId trackId) const
{
    if (frames.empty())
        return nullptr;

    if (frameIndex >= frames.size())
        frameIndex = frames.size() - 1;

    for (size_t i = frameIndex + 1; i > 0; --i)
    {
        const TrackLayer* layer = layerFor(frames[i - 1], trackId);
        if (!layer)
            continue;
        if (layer->hasTransformEntry || trackLayerHasRenderableEntry(*layer))
            return layer;
    }

    return layerFor(frames[frameIndex], trackId);
}

DrawingEngine::LayerTransform DrawingEngine::resolveBaseTransformForFrame(size_t frameIndex, TrackId trackId, const LayerTransform& fallback) const
{
    const TrackLayer* source = resolveTransformSourceLayer(frameIndex, trackId);
    return source ? source->baseTransform : fallback;
}

DrawingEngine::TrackId DrawingEngine::createTrack(TrackKind kind, const std::string& name)
{
    if ((int)tracks.size() >= strova::limits::kMaxTimelineTracks)
        return 0;
    if (countTracksOfKind(tracks, kind) >= maxTracksForKind(kind))
        return 0;

    Track t;
    t.id = nextTrackId++;
    t.kind = kind;
    t.name = name;

    t.muted = false;
    t.locked = false;
    t.visible = true;
    t.opacity = 1.0f;

    tracks.push_back(t);

    if (activeTrack == 0)
        activeTrack = t.id;

    for (auto& f : frames)
    {
        (void)layerFor(f, t.id, t.kind);
        f.markDirty();
    }

    ensureHistorySize();
    initHistory();
    return t.id;
}


DrawingEngine::TrackId DrawingEngine::createTrackWithId(TrackId forcedId, TrackKind kind, const std::string& name)
{
    if (forcedId <= 0)
        return createTrack(kind, name);

    if (findTrack(forcedId))
        return forcedId;

    if ((int)tracks.size() >= strova::limits::kMaxTimelineTracks)
        return 0;
    if (countTracksOfKind(tracks, kind) >= maxTracksForKind(kind))
        return 0;

    Track t;
    t.id = forcedId;
    t.kind = kind;
    t.name = name;
    t.muted = false;
    t.locked = false;
    t.visible = true;
    t.opacity = 1.0f;

    tracks.push_back(t);
    nextTrackId = std::max(nextTrackId, forcedId + 1);

    if (activeTrack == 0)
        activeTrack = t.id;

    for (auto& f : frames)
    {
        (void)layerFor(f, t.id, t.kind);
        f.markDirty();
    }

    ensureHistorySize();
    initHistory();
    return t.id;
}

void DrawingEngine::clearTracks()
{
    tracks.clear();
    transformKeyTracks.clear();
    visibilityKeyTracks.clear();
    flowLinkClips.clear();
    nextTrackId = 1;
    activeTrack = 0;
    currentStroke = nullptr;

    for (auto& f : frames)
    {
        f.layers.clear();
        f.markDirty();
    }

    ensureHistorySize();
    initHistory();
}

bool DrawingEngine::removeTrack(TrackId id)
{
    auto it = std::remove_if(tracks.begin(), tracks.end(),
        [&](const Track& t) { return t.id == id; });
    if (it == tracks.end()) return false;
    tracks.erase(it, tracks.end());
    transformKeyTracks.erase(id);
    visibilityKeyTracks.erase(id);
    flowLinkClips.erase(id);

    for (auto& f : frames)
    {
        auto itL = std::remove_if(f.layers.begin(), f.layers.end(),
            [&](const TrackLayer& L) { return L.trackId == id; });
        if (itL != f.layers.end())
        {
            f.layers.erase(itL, f.layers.end());
            f.markDirty();
        }
    }

    if (activeTrack == id)
        activeTrack = tracks.empty() ? 0 : tracks.front().id;

    currentStroke = nullptr;

    ensureHistorySize();
    initHistory();
    return true;
}

void DrawingEngine::setActiveTrack(TrackId id)
{
    if (findTrack(id))
    {
        activeTrack = id;
        currentStroke = nullptr;
    }
}


void DrawingEngine::clearAndResizeFrames(size_t count)
{
    count = (std::max<std::size_t>)(1, strova::limits::clampTimelineFrameCount(count));
    frames.clear();
    frames.resize(count);

    for (auto& f : frames)
    {
        for (const auto& t : tracks)
            (void)layerFor(f, t.id, t.kind);
        f.markDirty();
    }

    currentFrame = 0;
    currentStroke = nullptr;

    initHistory();
}

void DrawingEngine::setCurrentFrameIndex(size_t idx)
{
    if (frames.empty())
        frames.resize(1);

    currentFrame = (std::min)(strova::limits::clampTimelineFrameCount(idx), frames.size() - 1);
    currentStroke = nullptr;
}

const std::vector<Stroke>& DrawingEngine::getFrameStrokes(size_t frameIndex) const
{
    static const std::vector<Stroke> empty;
    if (frameIndex >= frames.size()) return empty;
    return frames[frameIndex].getComposite(tracks);
}

const std::vector<Stroke>& DrawingEngine::getCurrentStrokes() const
{
    return getFrameStrokes(currentFrame);
}

int DrawingEngine::countDirtyFrameComposites() const
{
    int dirty = 0;
    for (const auto& frame : frames)
        dirty += frame.compositeDirty ? 1 : 0;
    return dirty;
}

int DrawingEngine::countDirtyRasterTiles() const
{
    int dirty = 0;
    for (const auto& frame : frames)
    {
        for (const auto& layer : frame.layers)
            dirty += static_cast<int>(layer.image.dirtyTileCount());
    }
    return dirty;
}

std::size_t DrawingEngine::estimateUndoBytes() const
{
    std::size_t bytes = 0;

    for (const auto& frameHistory : undoStack)
        for (const auto& rec : frameHistory)
            bytes += estimateEditRecordBytes(rec);

    for (const auto& frameHistory : redoStack)
        for (const auto& rec : frameHistory)
            bytes += estimateEditRecordBytes(rec);

    auto addKeyTrackBytes = [&](const TransformKeyTrack& keys)
    {
        bytes += sizeof(TransformKeyTrack);
        bytes += keys.posX.capacity() * sizeof(TransformKeyframe);
        bytes += keys.posY.capacity() * sizeof(TransformKeyframe);
        bytes += keys.rotation.capacity() * sizeof(TransformKeyframe);
    };

    for (const auto& snap : timelineUndo)
    {
        for (const auto& frame : snap.frames)
            bytes += estimateFrameBytes(frame);
        for (const auto& item : snap.transformKeyTracks)
            addKeyTrackBytes(item.second);
        for (const auto& item : snap.visibilityKeyTracks)
            bytes += item.second.capacity() * sizeof(VisibilityKey);
        for (const auto& item : snap.flowLinkClips)
        {
            for (const auto& clip : item.second)
                bytes += sizeof(FlowLinkClip) + clip.samples.capacity() * sizeof(FlowLinkFrameSample);
        }
    }

    for (const auto& snap : timelineRedo)
    {
        for (const auto& frame : snap.frames)
            bytes += estimateFrameBytes(frame);
        for (const auto& item : snap.transformKeyTracks)
            addKeyTrackBytes(item.second);
        for (const auto& item : snap.visibilityKeyTracks)
            bytes += item.second.capacity() * sizeof(VisibilityKey);
        for (const auto& item : snap.flowLinkClips)
        {
            for (const auto& clip : item.second)
                bytes += sizeof(FlowLinkClip) + clip.samples.capacity() * sizeof(FlowLinkFrameSample);
        }
    }

    return bytes;
}

const std::vector<Stroke>& DrawingEngine::getFrameTrackStrokes(size_t frameIndex, TrackId trackId) const
{
    static const std::vector<Stroke> empty;
    if (frameIndex >= frames.size()) return empty;

    const TrackLayer* L = resolveDrawSourceLayer(frameIndex, trackId);
    return L ? L->strokes : empty;
}

void DrawingEngine::setFrameTrackStrokes(size_t frameIndex, TrackId trackId, const std::vector<Stroke>& strokes)
{
    if (frames.empty())
        frames.resize(1);

    const size_t clampedFrameIndex = (std::min)(strova::limits::clampTimelineFrameCount(frameIndex), static_cast<std::size_t>(strova::limits::kMaxTimelineFrames - 1));
    if (clampedFrameIndex >= frames.size())
        frames.resize(clampedFrameIndex + 1);

    LayerEditRecord recBefore{};
    captureLayerEdit(clampedFrameIndex, trackId, recBefore);

    for (auto& f : frames)
    {
        for (const auto& t : tracks)
            (void)layerFor(f, t.id, t.kind);
    }

    TrackKind k = TrackKind::Draw;
    if (auto* t = findTrack(trackId))
        k = t->kind;

    TrackLayer* L = ensureOwnDrawLayerMutable(clampedFrameIndex, trackId);
    if (!L) return;
    L->strokes = strokes;
    L->contentType = LayerContentType::Stroke;
    assignOwnContentIdentity(*L);
    L->hasDrawEntry = true;
    ++L->contentRevision;

    frames[clampedFrameIndex].markDirty();
    currentStroke = nullptr;

    LayerEditRecord rec = recBefore;
    if (const TrackLayer* after = layerFor(frames[clampedFrameIndex], trackId))
    {
        rec.hadAfter = true;
        rec.after = *after;
    }

    EditRecord edit{};
    edit.kind = EditRecord::Kind::Layer;
    edit.layer = rec;
    pushFrameEdit(edit);
    if (clampedFrameIndex < redoStack.size())
        redoStack[clampedFrameIndex].clear();
}

const DrawingEngine::TrackLayer* DrawingEngine::getFrameTrackLayer(size_t frameIndex, TrackId trackId) const
{
    if (frameIndex >= frames.size())
        return nullptr;
    return layerFor(frames[frameIndex], trackId);
}

DrawingEngine::TrackLayer* DrawingEngine::getFrameTrackLayerMutable(size_t frameIndex, TrackId trackId)
{
    if (frames.empty())
        frames.resize(1);

    const size_t clampedFrameIndex = (std::min)(strova::limits::clampTimelineFrameCount(frameIndex), static_cast<std::size_t>(strova::limits::kMaxTimelineFrames - 1));
    if (clampedFrameIndex >= frames.size())
        frames.resize(clampedFrameIndex + 1);

    TrackKind k = TrackKind::Draw;
    if (auto* t = findTrack(trackId))
        k = t->kind;
    return layerFor(frames[clampedFrameIndex], trackId, k);
}

std::uint64_t DrawingEngine::getEvaluatedDrawCelId(size_t frameIndex, TrackId trackId) const
{
    const TrackLayer* src = resolveDrawSourceLayer(frameIndex, trackId);
    return src ? src->celId : 0;
}

int DrawingEngine::getResolvedDrawSourceFrame(size_t frameIndex, TrackId trackId) const
{
    return resolveDrawSourceFrameIndex(frameIndex, trackId);
}

bool DrawingEngine::frameTrackOwnsDrawContent(size_t frameIndex, TrackId trackId) const
{
    if (const TrackLayer* layer = getFrameTrackLayer(frameIndex, trackId))
        return trackLayerHasOwnDrawContent(*layer);
    return false;
}

bool DrawingEngine::setFrameTrackTransform(size_t frameIndex, TrackId trackId, const LayerTransform& transform)
{
    if (frames.empty())
        frames.resize(1);
    const size_t clampedFrameIndex = (std::min)(strova::limits::clampTimelineFrameCount(frameIndex), static_cast<std::size_t>(strova::limits::kMaxTimelineFrames - 1));
    if (clampedFrameIndex >= frames.size())
        frames.resize(clampedFrameIndex + 1);
    LayerEditRecord recBefore{};
    captureLayerEdit(clampedFrameIndex, trackId, recBefore);

    TrackLayer* layer = getFrameTrackLayerMutable(clampedFrameIndex, trackId);
    if (!layer)
        return false;

    if (layer->baseTransform.posX == transform.posX &&
        layer->baseTransform.posY == transform.posY &&
        layer->baseTransform.rotation == transform.rotation &&
        layer->baseTransform.pivotX == transform.pivotX &&
        layer->baseTransform.pivotY == transform.pivotY &&
        layer->hasTransformEntry)
        return true;

    layer->baseTransform = transform;
    layer->transform = transform;
    layer->hasTransformEntry = true;
    ++layer->transformRevision;
    frames[clampedFrameIndex].markDirty();

    LayerEditRecord rec = recBefore;
    if (const TrackLayer* after = layerFor(frames[clampedFrameIndex], trackId))
    {
        rec.hadAfter = true;
        rec.after = *after;
    }
    EditRecord edit{};
    edit.kind = EditRecord::Kind::Layer;
    edit.layer = rec;
    pushFrameEdit(edit);
    if (clampedFrameIndex < redoStack.size())
        redoStack[clampedFrameIndex].clear();
    return true;
}

DrawingEngine::LayerTransform DrawingEngine::getFrameTrackTransform(size_t frameIndex, TrackId trackId) const
{
    LayerTransform out{};
    if (const TrackLayer* layer = getFrameTrackLayer(frameIndex, trackId))
        out = layer->transform;
    return out;
}

bool DrawingEngine::setFrameTrackImage(size_t frameIndex, TrackId trackId, const LayerImage& image, const LayerTransform& transform)
{
    if (frames.empty())
        frames.resize(1);
    const size_t clampedFrameIndex = (std::min)(strova::limits::clampTimelineFrameCount(frameIndex), static_cast<std::size_t>(strova::limits::kMaxTimelineFrames - 1));
    if (clampedFrameIndex >= frames.size())
        frames.resize(clampedFrameIndex + 1);
    LayerEditRecord recBefore{};
    captureLayerEdit(clampedFrameIndex, trackId, recBefore);

    TrackLayer* layer = ensureOwnDrawLayerMutable(clampedFrameIndex, trackId);
    if (!layer)
        return false;

    layer->contentType = LayerContentType::Image;
    layer->image = image.cloneDetached();
    layer->baseTransform = transform;
    layer->transform = transform;
    assignOwnContentIdentity(*layer);
    layer->hasDrawEntry = true;
    layer->hasTransformEntry = true;
    ++layer->imageRevision;
    ++layer->contentRevision;
    ++layer->transformRevision;
    frames[clampedFrameIndex].markDirty();

    LayerEditRecord rec = recBefore;
    if (const TrackLayer* after = layerFor(frames[clampedFrameIndex], trackId))
    {
        rec.hadAfter = true;
        rec.after = *after;
    }
    EditRecord edit{};
    edit.kind = EditRecord::Kind::Layer;
    edit.layer = rec;
    pushFrameEdit(edit);
    if (clampedFrameIndex < redoStack.size())
        redoStack[clampedFrameIndex].clear();
    return true;
}

bool DrawingEngine::clearFrameTrackImage(size_t frameIndex, TrackId trackId)
{
    if (frames.empty())
        frames.resize(1);
    const size_t clampedFrameIndex = (std::min)(strova::limits::clampTimelineFrameCount(frameIndex), static_cast<std::size_t>(strova::limits::kMaxTimelineFrames - 1));
    if (clampedFrameIndex >= frames.size())
        frames.resize(clampedFrameIndex + 1);
    LayerEditRecord recBefore{};
    captureLayerEdit(clampedFrameIndex, trackId, recBefore);

    TrackLayer* layer = ensureOwnDrawLayerMutable(clampedFrameIndex, trackId);
    if (!layer)
        return false;

    layer->image.clear();
    assignOwnContentIdentity(*layer);
    layer->contentType = LayerContentType::Stroke;
    layer->hasDrawEntry = true;
    ++layer->imageRevision;
    ++layer->contentRevision;
    frames[clampedFrameIndex].markDirty();

    LayerEditRecord rec = recBefore;
    if (const TrackLayer* after = layerFor(frames[clampedFrameIndex], trackId))
    {
        rec.hadAfter = true;
        rec.after = *after;
    }
    EditRecord edit{};
    edit.kind = EditRecord::Kind::Layer;
    edit.layer = rec;
    pushFrameEdit(edit);
    if (clampedFrameIndex < redoStack.size())
        redoStack[clampedFrameIndex].clear();
    return true;
}

const DrawingEngine::TransformKeyTrack* DrawingEngine::getTransformKeyTrack(TrackId trackId) const
{
    auto it = transformKeyTracks.find(trackId);
    return (it != transformKeyTracks.end()) ? &it->second : nullptr;
}

DrawingEngine::TransformKeyTrack* DrawingEngine::getTransformKeyTrackMutable(TrackId trackId)
{
    if (trackId == 0)
        return nullptr;
    return &transformKeyTracks[trackId];
}

const std::vector<DrawingEngine::TransformKeyframe>* DrawingEngine::getTransformKeyframes(TrackId trackId, TransformChannel channel) const
{
    const TransformKeyTrack* keys = getTransformKeyTrack(trackId);
    return keys ? keyChannelConst(*keys, channel) : nullptr;
}

std::vector<DrawingEngine::TransformKeyframe>* DrawingEngine::getTransformKeyframesMutable(TrackId trackId, TransformChannel channel)
{
    TransformKeyTrack* keys = getTransformKeyTrackMutable(trackId);
    return keys ? keyChannelMutable(*keys, channel) : nullptr;
}

DrawingEngine::TransformInterpolationMode DrawingEngine::getTransformInterpolationMode(TrackId trackId) const
{
    const TransformKeyTrack* keys = getTransformKeyTrack(trackId);
    return keys ? keys->interpolation : TransformInterpolationMode::Linear;
}

void DrawingEngine::setTransformInterpolationMode(TrackId trackId, TransformInterpolationMode mode)
{
    TransformKeyTrack* keys = getTransformKeyTrackMutable(trackId);
    if (!keys)
        return;
    if (keys->interpolation == mode)
        return;
    keys->interpolation = mode;
    syncFrameTransformsFromKeyframes();
}

bool DrawingEngine::hasTransformKeys(TrackId trackId) const
{
    const TransformKeyTrack* keys = getTransformKeyTrack(trackId);
    return keys && !keys->empty();
}

void DrawingEngine::clearTransformKeys(TrackId trackId)
{
    transformKeyTracks.erase(trackId);
    syncFrameTransformsFromKeyframes();
}

void DrawingEngine::setTransformKeyframe(TrackId trackId, TransformChannel channel, int frameIndex, float value)
{
    if (trackId == 0)
        return;
    std::vector<TransformKeyframe>* keys = getTransformKeyframesMutable(trackId, channel);
    if (!keys)
        return;

    keys->push_back({ frameIndex, value });
    sortAndDeduplicateKeys(*keys);
    syncFrameTransformsFromKeyframes();
}

bool DrawingEngine::removeTransformKeyframe(TrackId trackId, TransformChannel channel, int frameIndex)
{
    std::vector<TransformKeyframe>* keys = getTransformKeyframesMutable(trackId, channel);
    if (!keys)
        return false;

    const size_t before = keys->size();
    removeKeyAtFrame(*keys, frameIndex);
    if (keys->empty())
    {
        auto it = transformKeyTracks.find(trackId);
        if (it != transformKeyTracks.end() && it->second.empty())
            transformKeyTracks.erase(it);
    }
    if (keys->size() != before)
    {
        syncFrameTransformsFromKeyframes();
        return true;
    }
    return false;
}

bool DrawingEngine::moveTransformKeyframe(TrackId trackId, TransformChannel channel, int fromFrame, int toFrame)
{
    if (fromFrame == toFrame)
        return true;
    std::vector<TransformKeyframe>* keys = getTransformKeyframesMutable(trackId, channel);
    if (!keys)
        return false;

    for (auto& key : *keys)
    {
        if (key.frameIndex == fromFrame)
        {
            key.frameIndex = std::max(0, toFrame);
            sortAndDeduplicateKeys(*keys);
            syncFrameTransformsFromKeyframes();
            return true;
        }
    }
    return false;
}

const std::vector<DrawingEngine::VisibilityKey>* DrawingEngine::getVisibilityKeys(TrackId trackId) const
{
    auto it = visibilityKeyTracks.find(trackId);
    return (it != visibilityKeyTracks.end()) ? &it->second : nullptr;
}

std::vector<DrawingEngine::VisibilityKey>* DrawingEngine::getVisibilityKeysMutable(TrackId trackId)
{
    if (trackId == 0)
        return nullptr;
    return &visibilityKeyTracks[trackId];
}

bool DrawingEngine::evaluateTrackVisibility(size_t frameIndex, TrackId trackId) const
{
    const auto* keys = getVisibilityKeys(trackId);
    bool visible = true;
    if (!keys)
        return visible;
    for (const auto& key : *keys)
    {
        if (key.frameIndex <= (int)frameIndex)
            visible = key.visible;
        else
            break;
    }
    return visible;
}

void DrawingEngine::setVisibilityKeyframe(TrackId trackId, int frameIndex, bool visible)
{
    if (trackId == 0)
        return;
    auto* keys = getVisibilityKeysMutable(trackId);
    if (!keys)
        return;
    keys->push_back({ std::max(0, frameIndex), visible });
    sortVisibilityKeys(*keys);
    for (size_t i = 1; i < keys->size();)
    {
        if ((*keys)[i - 1].frameIndex == (*keys)[i].frameIndex)
            keys->erase(keys->begin() + (ptrdiff_t)(i - 1));
        else
            ++i;
    }
    markAllFramesDirty();
}

bool DrawingEngine::removeVisibilityKeyframe(TrackId trackId, int frameIndex)
{
    auto* keys = getVisibilityKeysMutable(trackId);
    if (!keys)
        return false;
    const size_t before = keys->size();
    removeVisibilityKeyAtFrame(*keys, frameIndex);
    if (keys->empty())
        visibilityKeyTracks.erase(trackId);
    if (before != keys->size())
    {
        markAllFramesDirty();
        return true;
    }
    return false;
}

bool DrawingEngine::moveVisibilityKeyframe(TrackId trackId, int fromFrame, int toFrame)
{
    if (fromFrame == toFrame)
        return true;
    auto* keys = getVisibilityKeysMutable(trackId);
    if (!keys)
        return false;
    for (auto& key : *keys)
    {
        if (key.frameIndex == fromFrame)
        {
            key.frameIndex = std::max(0, toFrame);
            sortVisibilityKeys(*keys);
            for (size_t i = 1; i < keys->size();)
            {
                if ((*keys)[i - 1].frameIndex == (*keys)[i].frameIndex)
                    keys->erase(keys->begin() + (ptrdiff_t)(i - 1));
                else
                    ++i;
            }
            markAllFramesDirty();
            return true;
        }
    }
    return false;
}

void DrawingEngine::clearVisibilityKeys(TrackId trackId)
{
    visibilityKeyTracks.erase(trackId);
    markAllFramesDirty();
}

bool DrawingEngine::hasVisibilityKeys(TrackId trackId) const
{
    const auto* keys = getVisibilityKeys(trackId);
    return keys && !keys->empty();
}

const std::vector<FlowLinkClip>& DrawingEngine::getFlowLinkClips(TrackId targetTrackId) const
{
    static const std::vector<FlowLinkClip> kEmpty;
    auto it = flowLinkClips.find(targetTrackId);
    return (it != flowLinkClips.end()) ? it->second : kEmpty;
}

std::vector<FlowLinkClip>* DrawingEngine::getFlowLinkClipsMutable(TrackId targetTrackId)
{
    if (targetTrackId == 0) return nullptr;
    return &flowLinkClips[targetTrackId];
}

void DrawingEngine::addFlowLinkClip(TrackId targetTrackId, const FlowLinkClip& clip)
{
    if (targetTrackId == 0 || clip.empty()) return;
    auto* clips = getFlowLinkClipsMutable(targetTrackId);
    if (!clips) return;
    if ((int)clips->size() >= strova::limits::kMaxFlowLinkClipsPerTrack)
        return;
    FlowLinkClip sanitized = clip;
    sanitized.startFrame = std::clamp(sanitized.startFrame, 0, strova::limits::kMaxTimelineFrames - 1);
    sanitized.duration = std::clamp(sanitized.duration, 1, strova::limits::kMaxFlowGeneratedFrames);
    sanitized.laneIndex = std::max(0, sanitized.laneIndex);
    if ((int)sanitized.samples.size() > strova::limits::kMaxFlowGeneratedFrames)
        sanitized.samples.resize((size_t)strova::limits::kMaxFlowGeneratedFrames);
    clips->push_back(std::move(sanitized));
    std::sort(clips->begin(), clips->end(), [](const FlowLinkClip& a, const FlowLinkClip& b){ return (a.laneIndex == b.laneIndex) ? (a.startFrame < b.startFrame) : (a.laneIndex < b.laneIndex); });
    syncFrameTransformsFromKeyframes();
}

void DrawingEngine::clearFlowLinkClips(TrackId targetTrackId)
{
    flowLinkClips.erase(targetTrackId);
    syncFrameTransformsFromKeyframes();
}

DrawingEngine::LayerTransform DrawingEngine::evaluateFlowLinkTransform(size_t frameIndex, TrackId trackId, const LayerTransform& fallback) const
{
    LayerTransform out = fallback;
    auto it = flowLinkClips.find(trackId);
    if (it == flowLinkClips.end())
        return out;

    const int frame = (int)frameIndex;
    for (const auto& clip : it->second)
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
            if (clip.relative)
            {
                out.posX = fallback.posX + chosen->posX;
                out.posY = fallback.posY + chosen->posY;
                out.rotation = fallback.rotation + chosen->rotation;
            }
            else
            {
                out.posX = chosen->posX;
                out.posY = chosen->posY;
                out.rotation = chosen->rotation;
            }
        }
    }
    return out;
}


DrawingEngine::LayerTransform DrawingEngine::evaluateTrackTransform(size_t frameIndex, TrackId trackId, const LayerTransform& fallback) const
{
    return evaluateFlowLinkTransform(frameIndex, trackId, fallback);
}

DrawingEngine::TrackLayer DrawingEngine::getEvaluatedFrameTrackLayerCopy(size_t frameIndex, TrackId trackId) const
{
    TrackLayer out{};
    const TrackLayer* rawLayer = getFrameTrackLayer(frameIndex, trackId);
    if (!rawLayer)
        return out;

    out = *rawLayer;
    if (const TrackLayer* drawSource = resolveDrawSourceLayer(frameIndex, trackId))
    {
        out.strokes = drawSource->strokes;
        out.contentType = drawSource->contentType;
        out.image = drawSource->image;
        out.imageRevision = drawSource->imageRevision;
        out.contentRevision = drawSource->contentRevision;
        out.hasDrawEntry = drawSource->hasDrawEntry;
        out.celId = drawSource->celId;
        out.ownsDrawContent = drawSource->ownsDrawContent;
    }

    const LayerTransform fallback = resolveBaseTransformForFrame(frameIndex, trackId, rawLayer->baseTransform);
    out.baseTransform = fallback;
    out.transform = evaluateTrackTransform(frameIndex, trackId, fallback);
    out.transformRevision = rawLayer->transformRevision;
    out.visible = evaluateTrackVisibility(frameIndex, trackId);
    out.visibilityRevision = rawLayer->visibilityRevision;
    return out;
}

void DrawingEngine::syncFrameTransformsFromKeyframes()
{
    if (frames.empty())
        return;

    for (size_t fi = 0; fi < frames.size(); ++fi)
    {
        for (auto& layer : frames[fi].layers)
        {
            const LayerTransform fallback = resolveBaseTransformForFrame(fi, layer.trackId, layer.baseTransform);
            const LayerTransform evaluated = evaluateTrackTransform(fi, layer.trackId, fallback);
            if (std::fabs(layer.transform.posX - evaluated.posX) > 0.0001f ||
                std::fabs(layer.transform.posY - evaluated.posY) > 0.0001f ||
                std::fabs(layer.transform.rotation - evaluated.rotation) > 0.0001f)
            {
                layer.transform = evaluated;
                ++layer.transformRevision;
            }
            else
            {
                layer.transform = evaluated;
            }
        }
        frames[fi].markDirty();
    }
}

void DrawingEngine::setFrameStrokes(size_t frameIndex, const std::vector<Stroke>& strokes)
{
    if (activeTrack == 0)
    {
        const TrackId createdTrackId = createTrack(TrackKind::Draw, "Draw");
        if (createdTrackId == 0)
            return;
    }
    setFrameTrackStrokes(frameIndex, activeTrack, strokes);
}


void DrawingEngine::setTool(ToolType t)
{
    currentTool.type = t;
}

void DrawingEngine::setColor(SDL_Color c)
{
    currentTool.color = c;
    currentTool.color.a = (Uint8)std::clamp((int)std::lround(currentToolSettings.opacity * 255.0f), 0, 255);
}

void DrawingEngine::setThickness(float th)
{
    currentTool.thickness = th;
    currentToolSettings.size = th;
    currentToolSettings.clamp();
}

void DrawingEngine::setToolSettings(const ToolSettings& settings)
{
    currentToolSettings = settings;
    currentToolSettings.clamp();
    currentTool.thickness = currentToolSettings.size;
    currentTool.color.a = (Uint8)std::clamp((int)std::lround(currentToolSettings.opacity * 255.0f), 0, 255);

    if (!currentToolSettings.brushId.empty())
    {
        selectedBrushId = currentToolSettings.brushId;
        selectedBrushVersion = std::max(1, currentToolSettings.brushVersion);
        selectedBrushName = currentToolSettings.brushDisplayName.empty() ? selectedBrushId : currentToolSettings.brushDisplayName;
    }
}

void DrawingEngine::setBrushSelection(const std::string& brushId, int version, const std::string& displayName)
{
    if (brushId.empty()) return;
    selectedBrushId = brushId;
    selectedBrushVersion = std::max(1, version);
    selectedBrushName = displayName.empty() ? brushId : displayName;
    currentToolSettings.brushId = selectedBrushId;
    currentToolSettings.brushVersion = selectedBrushVersion;
    currentToolSettings.brushDisplayName = selectedBrushName;
}


void DrawingEngine::setGradientConfig(const GradientConfig& cfg)
{
    currentTool.gradient = cfg;

    
    for (int i = 0; i < STROVA_MAX_GRADIENT_STOPS; ++i)
    {
        currentTool.gradient.stopPos[(size_t)i] = clamp01f(currentTool.gradient.stopPos[(size_t)i]);
        currentTool.gradient.stopColor[(size_t)i].a = 255;
    }

    
    for (int i = 1; i < STROVA_MAX_GRADIENT_STOPS; ++i)
    {
        if (currentTool.gradient.stopPos[(size_t)i] < currentTool.gradient.stopPos[(size_t)i - 1])
            currentTool.gradient.stopPos[(size_t)i] = currentTool.gradient.stopPos[(size_t)i - 1];
    }

    
    if (currentTool.gradient.enabled && currentTool.gradient.mode != 0)
        currentTool.color = sampleGradient(0.5f);
}


void DrawingEngine::beginStroke(float x, float y)
{
    if (frames.empty())
        frames.resize(1);

    if (activeTrack == 0)
    {
        const TrackId createdTrackId = createTrack(TrackKind::Draw, "Draw");
        if (createdTrackId == 0)
            return;
    }

    Track* t = findTrack(activeTrack);
    if (t && t->locked) return;

    TrackKind k = t ? t->kind : TrackKind::Draw;
    TrackLayer* L = layerFor(frames[currentFrame], activeTrack, k);

    Stroke s;
    s.tool = currentTool.type;
    s.settings = currentToolSettings;

    s.gradient = currentTool.gradient;
    s.color = (currentTool.gradient.enabled && currentTool.gradient.mode != 0)
        ? sampleGradient(0.5f)
        : currentTool.color;

    s.thickness = currentTool.thickness;
    s.brushId = currentToolSettings.brushId.empty() ? selectedBrushId : currentToolSettings.brushId;
    s.brushVersion = std::max(1, currentToolSettings.brushVersion > 0 ? currentToolSettings.brushVersion : selectedBrushVersion);
    s.brushName = currentToolSettings.brushDisplayName.empty() ? selectedBrushName : currentToolSettings.brushDisplayName;
    s.brushMissing = false;
    if (const auto* mgr = strova::brush::globalManager())
    {
        if (const auto* pkg = mgr->findById(s.brushId))
        {
            s.brushName = pkg->manifest.name;
            s.brushVersion = pkg->manifest.version;
            s.brushRuntimeRevision = pkg->stamp.revision;
            s.settings.brushId = pkg->manifest.id;
            s.settings.brushDisplayName = pkg->manifest.name;
            s.settings.brushVersion = pkg->manifest.version;
            s.settings.brushSupportsUserColor = pkg->manifest.color.supportsUserColor;
            s.settings.brushSupportsGradient = pkg->manifest.color.supportsGradient;
        }
        else
        {
            s.brushMissing = true;
        }
    }

    StrokePoint p;
    p.x = x; p.y = y; p.pressure = 1.0f;
    s.points.push_back(p);

    pendingStrokeEdit = LayerEditRecord{};
    captureLayerEdit(currentFrame, activeTrack, pendingStrokeEdit);
    strokeEditPending = true;

    assignOwnContentIdentity(*L);
    L->hasDrawEntry = true;
    L->contentType = LayerContentType::Stroke;
    L->strokes.push_back(std::move(s));
    ++L->contentRevision;
    currentStroke = &L->strokes.back();

    frames[currentFrame].markDirty();
}

void DrawingEngine::addPoint(float x, float y)
{
    if (!currentStroke) return;

    
    
    if (!currentStroke->points.empty())
    {
        const StrokePoint& last = currentStroke->points.back();
        const float dx = x - last.x;
        const float dy = y - last.y;
        const float minStep = std::max(0.10f, currentStroke->thickness * 0.10f);
        if ((dx * dx + dy * dy) < (minStep * minStep))
            return;
    }

    StrokePoint p;
    p.x = x; p.y = y; p.pressure = 1.0f;
    currentStroke->points.push_back(p);

    if (TrackLayer* layer = layerFor(frames[currentFrame], activeTrack, findTrack(activeTrack) ? findTrack(activeTrack)->kind : TrackKind::Draw))
        ++layer->contentRevision;
    frames[currentFrame].markDirty();
}

void DrawingEngine::endStroke()
{
    if (strokeEditPending && currentFrame < frames.size())
    {
        LayerEditRecord rec = pendingStrokeEdit;
        if (const TrackLayer* after = layerFor(frames[currentFrame], activeTrack))
        {
            rec.hadAfter = true;
            rec.after = *after;
        }
        EditRecord edit{};
        edit.kind = EditRecord::Kind::Layer;
        edit.layer = rec;
        pushFrameEdit(edit);
        if (currentFrame < redoStack.size())
            redoStack[currentFrame].clear();
    }
    strokeEditPending = false;
    pendingStrokeEdit = LayerEditRecord{};
    currentStroke = nullptr;
}


void DrawingEngine::beginEraseSession()
{
    if (frames.empty())
        frames.resize(1);

    if (activeTrack == 0)
    {
        const TrackId createdTrackId = createTrack(TrackKind::Draw, "Draw");
        if (createdTrackId == 0)
            return;
    }

    Track* t = findTrack(activeTrack);
    if (t && t->locked) return;

    if (erasingSession) return;
    erasingSession = true;

    pendingEraseEdit = LayerEditRecord{};
    captureLayerEdit(currentFrame, activeTrack, pendingEraseEdit);
    eraseEditPending = true;
}

void DrawingEngine::eraseAt(float x, float y, float radiusWorld)
{
    if (!erasingSession) return;

    if (frames.empty()) return;
    if (activeTrack == 0) return;

    Track* t = findTrack(activeTrack);
    if (t && t->locked) return;

    TrackKind k = t ? t->kind : TrackKind::Draw;
    TrackLayer* L = ensureOwnDrawLayerMutable(currentFrame, activeTrack);
    if (!L) return;

    float r2 = radiusWorld * radiusWorld;

    std::vector<Stroke> out;
    out.reserve(L->strokes.size());

    for (const Stroke& s : L->strokes)
    {
        if (s.points.size() < 2)
        {
            if (s.points.size() == 1)
            {
                if (dist2f(s.points[0].x, s.points[0].y, x, y) > r2)
                    out.push_back(s);
            }
            else
            {
                out.push_back(s);
            }
            continue;
        }

        Stroke curFrag;
        bool fragActive = false;

        auto startFrag = [&]() {
            curFrag = s;
            curFrag.points.clear();
            fragActive = true;
            };

        auto flushFrag = [&]() {
            if (!fragActive) return;
            if (curFrag.points.size() >= 2)
                out.push_back(std::move(curFrag));
            fragActive = false;
            };

        for (const auto& p : s.points)
        {
            bool hit = dist2f(p.x, p.y, x, y) <= r2;
            if (hit)
            {
                flushFrag();
                continue;
            }

            if (!fragActive) startFrag();
            curFrag.points.push_back(p);
        }

        flushFrag();
    }

    assignOwnContentIdentity(*L);
    L->strokes.swap(out);
    ++L->contentRevision;
    frames[currentFrame].markDirty();
}

void DrawingEngine::endEraseSession()
{
    if (eraseEditPending && currentFrame < frames.size())
    {
        LayerEditRecord rec = pendingEraseEdit;
        if (const TrackLayer* after = layerFor(frames[currentFrame], activeTrack))
        {
            rec.hadAfter = true;
            rec.after = *after;
        }
        EditRecord edit{};
        edit.kind = EditRecord::Kind::Layer;
        edit.layer = rec;
        pushFrameEdit(edit);
        if (currentFrame < redoStack.size())
            redoStack[currentFrame].clear();
    }
    eraseEditPending = false;
    pendingEraseEdit = LayerEditRecord{};
    erasingSession = false;
    currentStroke = nullptr;
}


void DrawingEngine::nextFrame()
{
    if (frames.empty()) frames.resize(1);
    if (currentFrame + 1 < frames.size())
    {
        currentFrame++;
        currentStroke = nullptr;
    }
}

void DrawingEngine::prevFrame()
{
    if (frames.empty()) frames.resize(1);
    if (currentFrame > 0)
    {
        currentFrame--;
        currentStroke = nullptr;
    }
}

static void shiftDrawSourceRefsForInsert(std::vector<DrawingEngine::Frame>& frames, int at)
{
    for (auto& f : frames)
        for (auto& layer : f.layers)
            if (layer.drawSourceFrame >= at)
                ++layer.drawSourceFrame;
}

static void shiftDrawSourceRefsForDelete(std::vector<DrawingEngine::Frame>& frames, int at)
{
    for (auto& f : frames)
    {
        for (auto& layer : f.layers)
        {
            if (layer.drawSourceFrame > at)
                --layer.drawSourceFrame;
            else if (layer.drawSourceFrame == at)
                layer.drawSourceFrame = (at > 0) ? (at - 1) : -1;
        }
    }
}

static void shiftDrawSourceRefsForDuplicate(std::vector<DrawingEngine::Frame>& frames, int at)
{
    shiftDrawSourceRefsForInsert(frames, at + 1);
}

void DrawingEngine::addFrame()
{
    if (frames.empty())
        frames.resize(1);
    if (frames.size() >= (size_t)strova::limits::kMaxTimelineFrames)
        return;

    Frame nf;
    for (const auto& t : tracks)
        (void)layerFor(nf, t.id, t.kind);
    nf.markDirty();

    const size_t insertAt = currentFrame + 1;
    frames.insert(frames.begin() + (currentFrame + 1), std::move(nf));
    shiftDrawSourceRefsForInsert(frames, (int)insertAt);
    for (auto& item : transformKeyTracks)
        shiftKeyTrackForInsert(item.second, (int)insertAt);
    for (auto& item : visibilityKeyTracks)
        shiftVisibilityTrackForInsert(item.second, (int)insertAt);
    for (auto& item : flowLinkClips)
        shiftFlowLinkClipsForInsert(item.second, (int)insertAt);
    currentFrame++;
    syncFrameTransformsFromKeyframes();

    currentStroke = nullptr;

    ensureHistorySize();
    initHistory();
}


void DrawingEngine::insertFrame(size_t at)
{
    if (frames.empty())
        frames.resize(1);
    if (frames.size() >= (size_t)strova::limits::kMaxTimelineFrames)
        return;

    if (at > frames.size()) at = frames.size();

    Frame nf;
    for (const auto& t : tracks)
        (void)layerFor(nf, t.id, t.kind);
    nf.markDirty();

    frames.insert(frames.begin() + (ptrdiff_t)at, std::move(nf));
    shiftDrawSourceRefsForInsert(frames, (int)at);
    for (auto& item : transformKeyTracks)
        shiftKeyTrackForInsert(item.second, (int)at);
    for (auto& item : visibilityKeyTracks)
        shiftVisibilityTrackForInsert(item.second, (int)at);
    for (auto& item : flowLinkClips)
        shiftFlowLinkClipsForInsert(item.second, (int)at);
    syncFrameTransformsFromKeyframes();

    if (currentFrame >= at)
        currentFrame++;

    currentStroke = nullptr;

    
    ensureHistorySize();
    initHistory();
}

void DrawingEngine::deleteFrame(size_t at)
{
    if (frames.empty())
        frames.resize(1);

    if (frames.size() <= 1)
    {
        
        frames[0] = Frame{};
        for (const auto& t : tracks)
            (void)layerFor(frames[0], t.id, t.kind);
        frames[0].markDirty();
        currentFrame = 0;
        currentStroke = nullptr;
        ensureHistorySize();
        initHistory();
        return;
    }

    if (at >= frames.size()) at = frames.size() - 1;

    frames.erase(frames.begin() + (ptrdiff_t)at);
    shiftDrawSourceRefsForDelete(frames, (int)at);
    for (auto& item : transformKeyTracks)
        shiftKeyTrackForDelete(item.second, (int)at);
    for (auto& item : visibilityKeyTracks)
        shiftVisibilityTrackForDelete(item.second, (int)at);
    for (auto& item : flowLinkClips)
        shiftFlowLinkClipsForDelete(item.second, (int)at);
    syncFrameTransformsFromKeyframes();

    if (currentFrame >= frames.size())
        currentFrame = frames.size() - 1;
    else if (currentFrame > at)
        currentFrame--;

    currentStroke = nullptr;
    ensureHistorySize();
    initHistory();
}

void DrawingEngine::duplicateFrame(size_t at)
{
    if (frames.empty())
        frames.resize(1);
    if (frames.size() >= (size_t)strova::limits::kMaxTimelineFrames)
        return;

    if (at >= frames.size()) at = frames.size() - 1;

    Frame copy = frames[at];
    for (auto& layer : copy.layers)
    {
        if (trackLayerHasOwnDrawContent(layer))
        {
            layer.strokes.clear();
            layer.image.clear();
            layer.ownsDrawContent = false;
            layer.drawSourceFrame = (int)at;
            layer.celId = 0;
        }
    }
    copy.markDirty();

    frames.insert(frames.begin() + (ptrdiff_t)(at + 1), std::move(copy));
    shiftDrawSourceRefsForDuplicate(frames, (int)at);
    for (auto& item : transformKeyTracks)
        shiftKeyTrackForDuplicate(item.second, (int)at);
    for (auto& item : visibilityKeyTracks)
        shiftVisibilityTrackForDuplicate(item.second, (int)at);
    for (auto& item : flowLinkClips)
        shiftFlowLinkClipsForDuplicate(item.second, (int)at);
    syncFrameTransformsFromKeyframes();

    if (currentFrame > at)
        currentFrame++;

    currentStroke = nullptr;
    ensureHistorySize();
    initHistory();
}



bool DrawingEngine::setTrackOrder(const std::vector<TrackId>& orderedIds)
{
    if (tracks.empty()) return true;

    std::vector<Track> reordered;
    reordered.reserve(tracks.size());

    auto appendTrackById = [&](TrackId id)
    {
        for (const auto& t : tracks)
        {
            if (t.id == id)
            {
                bool exists = false;
                for (const auto& r : reordered)
                    if (r.id == id) { exists = true; break; }
                if (!exists) reordered.push_back(t);
                return;
            }
        }
    };

    for (TrackId id : orderedIds)
        appendTrackById(id);

    for (const auto& t : tracks)
        appendTrackById(t.id);

    if (reordered.size() != tracks.size())
        return false;

    bool changed = false;
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        if (tracks[i].id != reordered[i].id)
        {
            changed = true;
            break;
        }
    }

    if (!changed)
        return true;

    tracks = std::move(reordered);
    markAllFramesDirty();
    return true;
}
bool DrawingEngine::setTrackVisible(TrackId id, bool v)
{
    return applyTrackMutation(id, [&](Track& t)
    {
        if (t.visible == v)
            return false;
        t.visible = v;
        return true;
    });
}

bool DrawingEngine::setTrackMuted(TrackId id, bool m)
{
    return applyTrackMutation(id, [&](Track& t)
    {
        if (t.muted == m)
            return false;
        t.muted = m;
        return true;
    });
}

bool DrawingEngine::setTrackLocked(TrackId id, bool l)
{
    return applyTrackMutation(id, [&](Track& t)
    {
        if (t.locked == l)
            return false;
        t.locked = l;
        return true;
    });
}

bool DrawingEngine::setTrackOpacity(TrackId id, float opacity01)
{
    return applyTrackMutation(id, [&](Track& t)
    {
        const float cl = std::clamp(opacity01, 0.0f, 1.0f);
        if (std::fabs(t.opacity - cl) <= 0.0001f)
            return false;
        t.opacity = cl;
        return true;
    });
}

bool DrawingEngine::setTrackName(TrackId id, const std::string& name)
{
    return applyTrackMutation(id, [&](Track& t)
    {
        if (t.name == name)
            return false;
        t.name = name;
        return true;
    });
}


void DrawingEngine::ensureHistorySize()
{
    if (undoStack.size() != frames.size())
    {
        undoStack.resize(frames.size());
        redoStack.resize(frames.size());
    }
}

void DrawingEngine::initHistory()
{
    ensureHistorySize();

    for (size_t i = 0; i < frames.size(); ++i)
    {
        undoStack[i].clear();
        redoStack[i].clear();
    }
}

void DrawingEngine::pushUndoSnapshot()
{
    ensureHistorySize();
}

void DrawingEngine::clearRedo()
{
    clearFrameRedo();
}

bool DrawingEngine::canUndo() const
{
    if (currentFrame >= undoStack.size()) return false;
    return !undoStack[currentFrame].empty() || !timelineUndo.empty();
}

bool DrawingEngine::canRedo() const
{
    if (currentFrame >= redoStack.size()) return !timelineRedo.empty();
    return !redoStack[currentFrame].empty() || !timelineRedo.empty();
}

void DrawingEngine::undo()
{
    if (!timelineUndo.empty())
    {
        TimelineSnapshot cur{ frames, currentFrame, transformKeyTracks, visibilityKeyTracks, flowLinkClips };
        timelineRedo.push_back(std::move(cur));

        TimelineSnapshot snap = timelineUndo.back();
        timelineUndo.pop_back();

        frames = std::move(snap.frames);
        currentFrame = clampIndex(snap.currentFrame, frames.size() ? frames.size() - 1 : 0);
        transformKeyTracks = std::move(snap.transformKeyTracks);
        visibilityKeyTracks = std::move(snap.visibilityKeyTracks);
        flowLinkClips = std::move(snap.flowLinkClips);
        currentStroke = nullptr;

        for (auto& f : frames)
        {
            for (const auto& t : tracks)
                (void)layerFor(f, t.id, t.kind);
            f.markDirty();
        }
        initHistory();
        return;
    }

    if (currentFrame >= undoStack.size() || undoStack[currentFrame].empty())
        return;

    auto rec = undoStack[currentFrame].back();
    undoStack[currentFrame].pop_back();

    if (rec.kind == EditRecord::Kind::Layer)
    {
        if (rec.layer.frameIndex >= frames.size())
            return;

        if (rec.layer.frameIndex >= redoStack.size())
            redoStack.resize(rec.layer.frameIndex + 1);

        LayerEditRecord redoLayer = rec.layer;
        if (const TrackLayer* current = layerFor(frames[rec.layer.frameIndex], rec.layer.trackId))
        {
            redoLayer.hadAfter = true;
            redoLayer.after = *current;
        }
        else
        {
            redoLayer.hadAfter = false;
            redoLayer.after = TrackLayer{};
        }
        redoLayer.hadBefore = rec.layer.hadBefore;
        redoLayer.before = rec.layer.before;

        EditRecord redoRec{};
        redoRec.kind = EditRecord::Kind::Layer;
        redoRec.layer = redoLayer;
        redoStack[rec.layer.frameIndex].push_back(std::move(redoRec));

        restoreLayerState(frames[rec.layer.frameIndex], rec.layer, false);
        frames[rec.layer.frameIndex].markDirty();
    }
    else
    {
        if (auto* t = findTrack(rec.track.trackId))
            *t = rec.track.before;
        if (currentFrame >= redoStack.size())
            redoStack.resize(currentFrame + 1);
        redoStack[currentFrame].push_back(rec);
        markAllFramesDirty();
    }

    currentStroke = nullptr;
}

void DrawingEngine::redo()
{
    if (!timelineRedo.empty())
    {
        TimelineSnapshot cur{ frames, currentFrame, transformKeyTracks, visibilityKeyTracks, flowLinkClips };
        timelineUndo.push_back(std::move(cur));

        TimelineSnapshot snap = timelineRedo.back();
        timelineRedo.pop_back();

        frames = std::move(snap.frames);
        currentFrame = clampIndex(snap.currentFrame, frames.size() ? frames.size() - 1 : 0);
        transformKeyTracks = std::move(snap.transformKeyTracks);
        visibilityKeyTracks = std::move(snap.visibilityKeyTracks);
        flowLinkClips = std::move(snap.flowLinkClips);
        currentStroke = nullptr;

        for (auto& f : frames)
        {
            for (const auto& t : tracks)
                (void)layerFor(f, t.id, t.kind);
            f.markDirty();
        }
        initHistory();
        return;
    }

    if (currentFrame >= redoStack.size() || redoStack[currentFrame].empty())
        return;

    auto rec = redoStack[currentFrame].back();
    redoStack[currentFrame].pop_back();

    if (rec.kind == EditRecord::Kind::Layer)
    {
        if (rec.layer.frameIndex >= frames.size())
            return;

        restoreLayerState(frames[rec.layer.frameIndex], rec.layer, true);
        frames[rec.layer.frameIndex].markDirty();

        if (rec.layer.frameIndex >= undoStack.size())
            undoStack.resize(rec.layer.frameIndex + 1);
        undoStack[rec.layer.frameIndex].push_back(rec);
        if ((int)undoStack[rec.layer.frameIndex].size() > MAX_HISTORY)
            undoStack[rec.layer.frameIndex].erase(undoStack[rec.layer.frameIndex].begin());
    }
    else
    {
        if (auto* t = findTrack(rec.track.trackId))
            *t = rec.track.after;
        if (currentFrame >= undoStack.size())
            undoStack.resize(currentFrame + 1);
        undoStack[currentFrame].push_back(rec);
        if ((int)undoStack[currentFrame].size() > MAX_HISTORY)
            undoStack[currentFrame].erase(undoStack[currentFrame].begin());
        markAllFramesDirty();
    }

    currentStroke = nullptr;
}


void DrawingEngine::beginTimelineTransaction()
{
    if (inTimelineTxn) return;
    inTimelineTxn = true;
    txnBefore.frames = frames;
    txnBefore.currentFrame = currentFrame;
    txnBefore.transformKeyTracks = transformKeyTracks;
    txnBefore.visibilityKeyTracks = visibilityKeyTracks;
    txnBefore.flowLinkClips = flowLinkClips;
}

void DrawingEngine::pushTimelineUndoSnapshot(const TimelineSnapshot& snap)
{
    timelineUndo.push_back(snap);
    if ((int)timelineUndo.size() > MAX_HISTORY)
        timelineUndo.erase(timelineUndo.begin());
}

void DrawingEngine::commitTimelineTransaction()
{
    if (!inTimelineTxn) return;
    inTimelineTxn = false;

    pushTimelineUndoSnapshot(txnBefore);
    timelineRedo.clear();

    for (auto& f : frames)
    {
        for (const auto& t : tracks)
            (void)layerFor(f, t.id, t.kind);
        f.markDirty();
    }
    initHistory();
}

void DrawingEngine::rollbackTimelineTransaction()
{
    if (!inTimelineTxn) return;
    inTimelineTxn = false;

    frames = txnBefore.frames;
    currentFrame = clampIndex(txnBefore.currentFrame, frames.size() ? frames.size() - 1 : 0);
    transformKeyTracks = txnBefore.transformKeyTracks;
    visibilityKeyTracks = txnBefore.visibilityKeyTracks;
    flowLinkClips = txnBefore.flowLinkClips;
    currentStroke = nullptr;

    for (auto& f : frames)
    {
        for (const auto& t : tracks)
            (void)layerFor(f, t.id, t.kind);
        f.markDirty();
    }
    initHistory();
}

void DrawingEngine::shiftTrackRange(TrackId trackId, int srcStartFrame, int lengthFrames, int dstStartFrame)
{
    if (trackId == 0) return;
    if (lengthFrames <= 0) return;

    srcStartFrame = std::max(0, srcStartFrame);
    dstStartFrame = std::max(0, dstStartFrame);

    if (frames.empty())
        frames.resize(1);

    int needed = std::max(srcStartFrame, dstStartFrame) + lengthFrames;
    needed = strova::limits::clampTimelineFrames(needed);
    if (needed > (int)frames.size())
        frames.resize((size_t)needed);

    for (auto& f : frames)
    {
        for (const auto& t : tracks)
            (void)layerFor(f, t.id, t.kind);
    }

    TrackKind kindFallback = TrackKind::Draw;
    if (auto* t = findTrack(trackId))
        kindFallback = t->kind;

    std::vector<std::vector<Stroke>> temp;
    temp.resize((size_t)lengthFrames);

    for (int i = 0; i < lengthFrames; ++i)
    {
        int fidx = srcStartFrame + i;
        if (fidx < 0 || fidx >= (int)frames.size()) continue;

        TrackLayer* L = layerFor(frames[(size_t)fidx], trackId, kindFallback);
        temp[(size_t)i] = L->strokes;
    }

    for (int i = 0; i < lengthFrames; ++i)
    {
        int fidx = srcStartFrame + i;
        if (fidx < 0 || fidx >= (int)frames.size()) continue;

        TrackLayer* L = layerFor(frames[(size_t)fidx], trackId, kindFallback);
        if (!L->strokes.empty())
        {
            L->strokes.clear();
            frames[(size_t)fidx].markDirty();
        }
    }

    int needed2 = strova::limits::clampTimelineFrames(dstStartFrame + lengthFrames);
    if (needed2 > (int)frames.size())
        frames.resize((size_t)needed2);

    for (auto& f : frames)
    {
        for (const auto& t : tracks)
            (void)layerFor(f, t.id, t.kind);
    }

    for (int i = 0; i < lengthFrames; ++i)
    {
        int fidx = dstStartFrame + i;
        if (fidx < 0) continue;

        TrackLayer* L = layerFor(frames[(size_t)fidx], trackId, kindFallback);
        L->strokes = temp[(size_t)i];
        frames[(size_t)fidx].markDirty();
    }

    currentStroke = nullptr;
}
