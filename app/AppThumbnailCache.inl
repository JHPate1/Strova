void App::ensureThumbCacheSize()
{
    size_t n = engine.getFrameCount();
    if (thumbCache.size() > n)
    {
        for (size_t i = n; i < thumbCache.size(); ++i)
        {
            if (thumbCache[i].tex) SDL_DestroyTexture(thumbCache[i].tex);
        }
    }

    thumbCache.resize(n);

    fs::path cacheDir = fs::path(currentProject.folderPath) / ".cache" / "frame_thumbs";
    std::error_code ec;
    fs::create_directories(cacheDir, ec);

    for (size_t i = 0; i < n; ++i)
    {
        auto& t = thumbCache[i];
        char buf[64];
        std::snprintf(buf, sizeof(buf), "frame_%04zu", i);
        t.diskPath = (cacheDir / (std::string(buf) + ".bmp")).string();
        t.keyPath = (cacheDir / (std::string(buf) + ".key")).string();
    }
}

uint64_t App::calcFrameDirtyKey(size_t fi) const
{
    uint64_t key = 1469598103934665603ull;

    auto mix = [&](uint64_t v)
    {
        key ^= v;
        key *= 1099511628211ull;
    };

    for (const auto& tr : engine.getTracks())
    {
        mix((uint64_t)tr.id);
        mix((uint64_t)tr.kind);
        const DrawingEngine::TrackLayer layer = engine.getEvaluatedFrameTrackLayerCopy(fi, tr.id);
        if (layer.trackId != 0)
        {
            mix((uint64_t)layer.strokes.size());
            mix((uint64_t)(int)(layer.transform.posX * 10.0f));
            mix((uint64_t)(int)(layer.transform.posY * 10.0f));
            mix((uint64_t)(int)(layer.transform.rotation * 10.0f));
            mix((uint64_t)(layer.visible ? 1 : 0));
            mix((uint64_t)layer.contentType);
            mix((uint64_t)layer.image.width());
            mix((uint64_t)layer.image.height());
            mix((uint64_t)layer.image.rgba().size());

            if (!layer.image.rgba().empty())
            {
                mix((uint64_t)layer.image.rgba().front());
                mix((uint64_t)layer.image.rgba().back());
            }

            for (const auto& s : layer.strokes)
            {
                mix((uint64_t)s.points.size());
                mix((uint64_t)s.color.r | ((uint64_t)s.color.g << 8) | ((uint64_t)s.color.b << 16) | ((uint64_t)s.color.a << 24));
                mix((uint64_t)(int)(s.thickness * 10.0f));
                if (!s.points.empty())
                {
                    const auto& p0 = s.points.front();
                    const auto& p1 = s.points.back();
                    mix((uint64_t)(int)(p0.x * 10.0f) ^ ((uint64_t)(int)(p0.y * 10.0f) << 32));
                    mix((uint64_t)(int)(p1.x * 10.0f) ^ ((uint64_t)(int)(p1.y * 10.0f) << 32));
                }
            }
        }
    }

    return key;
}


void App::rebuildThumb(size_t fi)
{
    if (!sdlRenderer) return;

    const int TW = 120;
    const int TH = 68;

    Thumb& t = thumbCache[fi];

    if (t.tex) { SDL_DestroyTexture(t.tex); t.tex = nullptr; }

    t.tex = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, TW, TH);
    t.w = TW;
    t.h = TH;

    if (!t.tex) return;

    SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_BLEND);

#if SDL_VERSION_ATLEAST(2,0,12)
    SDL_SetTextureScaleMode(t.tex, SDL_ScaleModeNearest);
#endif

    SDL_Texture* prevTarget = SDL_GetRenderTarget(sdlRenderer);
    SDL_BlendMode prevBlend = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(sdlRenderer, &prevBlend);
    SDL_SetRenderTarget(sdlRenderer, t.tex);

    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderDrawColor(sdlRenderer, 245, 245, 250, 255);
    SDL_RenderClear(sdlRenderer);

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 18);
    for (int x = 0; x < TW; x += 16) SDL_RenderDrawLine(sdlRenderer, x, 0, x, TH);
    for (int y = 0; y < TH; y += 16) SDL_RenderDrawLine(sdlRenderer, 0, y, TW, y);

    const float docW = std::max(1, projectW);
    const float docH = std::max(1, projectH);
    const float pad = 4.0f;
    const float sc = std::min((TW - pad * 2.0f) / docW, (TH - pad * 2.0f) / docH);
    const float ox = pad + (TW - pad * 2.0f - docW * sc) * 0.5f;
    const float oy = pad + (TH - pad * 2.0f - docH * sc) * 0.5f;

    BrushRenderer brush(sdlRenderer);
    std::unordered_map<std::string, SDL_Texture*> imageCache;

    for (const auto& uiTrack : timeline.state().tracks)
    {
        if (!isDrawableUiTrackKind(uiTrack.kind) || uiTrack.engineTrackId == 0)
            continue;
        if (!uiTrack.visible || uiTrack.muted)
            continue;

        const DrawingEngine::TrackLayer layer = engine.getEvaluatedFrameTrackLayerCopy(fi, uiTrack.engineTrackId);
        if (layer.trackId == 0 || !layer.visible)
            continue;
        if (layer.strokes.empty() && layer.image.empty())
            continue;

        const std::string imageKey = std::to_string(fi) + ":" + std::to_string(uiTrack.engineTrackId) + ":" + std::to_string(layer.celId) + ":" + std::to_string(layer.imageRevision) + ":" + std::to_string(layer.image.surfaceRevision());
        const std::string strokeKey = std::to_string(fi) + ":thumb:" + std::to_string(uiTrack.engineTrackId) + ":" + std::to_string(layer.contentRevision) + ":" + std::to_string(layer.transformRevision);
        strova::layer_render::drawTrackLayer(
            sdlRenderer,
            brush,
            layer,
            sc,
            ox,
            oy,
            0,
            0,
            1.0f,
            &imageCache,
            imageKey,
            strokeKey,
            runtimeState.diagnostics.frameOrdinal);
    }

    for (auto& kv : imageCache)
    {
        if (kv.second) SDL_DestroyTexture(kv.second);
        strova::layer_render::eraseImageTextureMeta(kv.first);
    }

    SDL_SetRenderTarget(sdlRenderer, prevTarget);
    SDL_SetRenderDrawBlendMode(sdlRenderer, prevBlend);

    t.dirtyKey = calcFrameDirtyKey(fi);
    t.diskReady = saveTextureToBmp(sdlRenderer, t.tex, t.diskPath);
    t.loadedFromDisk = false;
    noteThumbRebuild();
    noteCompositeRebuild(1);

    std::ofstream keyOut(t.keyPath, std::ios::binary | std::ios::trunc);
    if (keyOut) keyOut << t.dirtyKey;
}


bool App::loadThumbFromDisk(size_t fi)
{
    if (!sdlRenderer || fi >= thumbCache.size()) return false;

    Thumb& t = thumbCache[fi];
    if (t.diskPath.empty() || t.keyPath.empty()) return false;
    if (!fs::exists(t.diskPath) || !fs::exists(t.keyPath)) return false;

    std::string keyStr;
    if (!readTextFile(t.keyPath, keyStr)) return false;

    uint64_t diskKey = 0;
    try { diskKey = (uint64_t)std::stoull(trimCopy(keyStr)); }
    catch (...) { return false; }

    const uint64_t curKey = calcFrameDirtyKey(fi);
    if (diskKey != curKey) return false;

    if (t.tex)
    {
        SDL_DestroyTexture(t.tex);
        t.tex = nullptr;
    }

    t.tex = loadBmpTexture(sdlRenderer, t.diskPath, &t.w, &t.h);
    if (!t.tex) return false;

    t.dirtyKey = diskKey;
    t.diskReady = true;
    t.loadedFromDisk = true;
    return true;
}

void App::prepareThumbDiskCache()
{
    ensureThumbCacheSize();
    for (size_t i = 0; i < thumbCache.size(); ++i)
    {
        if (!loadThumbFromDisk(i))
            rebuildThumb(i);

        if (thumbCache[i].tex)
        {
            SDL_DestroyTexture(thumbCache[i].tex);
            thumbCache[i].tex = nullptr;
        }
    }
}

void App::rebuildAllThumbsIfNeeded()
{
    ensureThumbCacheSize();

    for (size_t i = 0; i < thumbCache.size(); ++i)
    {
        uint64_t k = calcFrameDirtyKey(i);

        if ((!thumbCache[i].tex && !loadThumbFromDisk(i)) || thumbCache[i].dirtyKey != k)
        {
            rebuildThumb(i);
        }
    }
}

void App::markThumbDirty(size_t fi)
{
    ensureThumbCacheSize();
    if (fi >= thumbCache.size()) return;

    thumbCache[fi].dirtyKey = 0;
    thumbCache[fi].probeQueued = false;
    thumbCache[fi].probeReady = false;
    thumbCache[fi].probedValid = false;
    thumbCache[fi].probedKey = 0;
}

void App::markAllThumbsDirty()
{
    ensureThumbCacheSize();
    for (auto& t : thumbCache) {
        t.dirtyKey = 0;
        t.probeQueued = false;
        t.probeReady = false;
        t.probedValid = false;
        t.probedKey = 0;
    }
}


