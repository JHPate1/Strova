bool App::saveProjectNow(std::string& outErr)
{
    outErr.clear();
    syncProjectFromEngine();

    const std::string folder = currentProject.folderPath;
    if (folder.empty())
    {
        outErr = "Project folderPath is empty.";
        return false;
    }

    const bool ok = saveProjectFolderInternal(fs::path(folder), outErr, false);
    runtimeState.history.saveRequested = false;
    if (ok)
        clearRecoverySnapshot();
    syncRuntimeStateFromEditor();
    return ok;
}

static bool saveEditorRuntimeStateToFolder(const fs::path& folder, const App& app, std::string& outErr)
{
    try
    {
        std::ostringstream j;
        j << "{\n";
        j << "  \"timelineStart\": 0,\n";
        j << "  \"timelineEnd\": " << std::max(0, (int)app.getEngine().getFrameCount() - 1) << ",\n";
        j << "  \"flowLinkEnabled\": " << (app.flowLinkEnabledValue() ? 1 : 0) << ",\n";
        j << "  \"selectedBrushId\": \"" << jsonEscape(app.getEngine().getBrushSelectionId()) << "\",\n";
        j << "  \"selectedBrushVersion\": " << app.getEngine().getBrushSelectionVersion() << "\n";
        j << "}\n";
        if (!writeTextFileAtomic(folder / "editor_state.json", j.str()))
        {
            outErr = "Failed to write editor_state.json";
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        outErr = e.what();
        return false;
    }
}

static void loadEditorRuntimeStateFromFolder(const fs::path& folder, App& app)
{
    std::string j;
    if (!readTextFile(folder / "editor_state.json", j)) return;
    size_t p = 0;
    if (findKeyPos(j, "timelineStart", p)) { size_t i = p; parseInt(j, i, app.timelineRangeStartRef()); }
    if (findKeyPos(j, "timelineEnd", p)) { size_t i = p; parseInt(j, i, app.timelineRangeEndRef()); }
    int flowLinkEnabled = app.flowLinkEnabledValue() ? 1 : 0;
    if (findKeyPos(j, "flowLinkEnabled", p)) { size_t i = p; parseInt(j, i, flowLinkEnabled); }
    app.flowLinkEnabledRef() = (flowLinkEnabled != 0);
    std::string brushId;
    if (findKeyPos(j, "selectedBrushId", p)) { size_t i = p; strova::brush::parseStringAt(j, i, brushId); }
    int brushVersion = 1;
    if (findKeyPos(j, "selectedBrushVersion", p)) { size_t i = p; parseInt(j, i, brushVersion); }
    if (!brushId.empty())
    {
        app.toolBank.get(ToolType::Brush).brushId = brushId;
        app.toolBank.get(ToolType::Brush).brushVersion = std::max(1, brushVersion);
        if (const auto* pkg = app.brushManager().findById(brushId))
        {
            app.toolBank.get(ToolType::Brush).brushDisplayName = pkg->manifest.name;
            app.toolBank.get(ToolType::Brush).brushSupportsUserColor = pkg->manifest.color.supportsUserColor;
            app.toolBank.get(ToolType::Brush).brushSupportsGradient = pkg->manifest.color.supportsGradient;
            app.brushManager().select(brushId);
        }
    }
    app.timelineRangeStartRef() = 0;
    app.timelineRangeEndRef() = std::max(0, (int)app.getEngine().getFrameCount() - 1);
}

static bool saveFlowLinkClipsToFolder(const fs::path& folder, const App& app, std::string& outErr)
{
    try
    {
        std::ostringstream j;
        j << "{\n  \"clips\": [\n";
        bool firstClip = true;
        for (const auto& tr : app.getEngine().getTracks())
        {
            const auto& clips = app.getEngine().getFlowLinkClips(tr.id);
            for (const auto& clip : clips)
            {
                if (!firstClip) j << ",\n";
                firstClip = false;
                j << "    {\"targetTrackId\":" << clip.targetTrackId
                  << ",\"startFrame\":" << clip.startFrame
                  << ",\"duration\":" << clip.duration
                  << ",\"loop\":" << (clip.loop ? 1 : 0)
                  << ",\"relative\":" << (clip.relative ? 1 : 0)
                  << ",\"laneIndex\":" << clip.laneIndex
                  << ",\"baseX\":" << clip.basePosX
                  << ",\"baseY\":" << clip.basePosY
                  << ",\"baseRotation\":" << clip.baseRotation
                  << ",\"samples\":[";
                for (size_t si = 0; si < clip.samples.size(); ++si)
                {
                    const auto& sample = clip.samples[si];
                    if (si) j << ",";
                    j << "{\"frame\":" << sample.frameOffset
                      << ",\"x\":" << sample.posX
                      << ",\"y\":" << sample.posY
                      << ",\"rotation\":" << sample.rotation << "}";
                }
                j << "]}";
            }
        }
        j << "\n  ]\n}\n";
        if (!writeTextFileAtomic(folder / "flowlink.json", j.str()))
        {
            outErr = "Failed to write flowlink.json";
            return false;
        }
        return true;
    }
    catch (const std::exception& e)
    {
        outErr = e.what();
        return false;
    }
}

static void loadFlowLinkClipsFromFolder(const fs::path& folder, App& app)
{
    std::string j;
    if (!readTextFile(folder / "flowlink.json", j)) return;
    size_t clipsPos = j.find("\"clips\"");
    if (clipsPos == std::string::npos) return;
    size_t a0 = j.find('[', clipsPos);
    if (a0 == std::string::npos) return;
    int depth = 0;
    size_t a1 = std::string::npos;
    for (size_t i = a0; i < j.size(); ++i)
    {
        if (j[i] == '[') depth++;
        else if (j[i] == ']')
        {
            depth--;
            if (depth == 0) { a1 = i; break; }
        }
    }
    if (a1 == std::string::npos) return;
    std::vector<std::string> clipObjs;
    extractJsonObjects(j.substr(a0, a1 - a0 + 1), clipObjs);
    if (clipObjs.empty()) return;

    (void)ensureFlowLinkTimelineTrack(app);
    for (const auto& obj : clipObjs)
    {
        FlowLinkClip clip{};
        size_t p = 0;
        if (findKeyPos(obj, "targetTrackId", p)) { size_t i = p; parseInt(obj, i, clip.targetTrackId); }
        if (findKeyPos(obj, "startFrame", p)) { size_t i = p; parseInt(obj, i, clip.startFrame); }
        if (findKeyPos(obj, "duration", p)) { size_t i = p; parseInt(obj, i, clip.duration); }
        int loopInt = 0;
        if (findKeyPos(obj, "loop", p)) { size_t i = p; parseInt(obj, i, loopInt); }
        clip.loop = (loopInt != 0);
        int relativeInt = 0;
        if (findKeyPos(obj, "relative", p)) { size_t i = p; parseInt(obj, i, relativeInt); }
        clip.relative = (relativeInt != 0);
        if (findKeyPos(obj, "laneIndex", p)) { size_t i = p; parseInt(obj, i, clip.laneIndex); }
        if (findKeyPos(obj, "baseX", p)) { size_t i = p; parseFloat(obj, i, clip.basePosX); }
        if (findKeyPos(obj, "baseY", p)) { size_t i = p; parseFloat(obj, i, clip.basePosY); }
        if (findKeyPos(obj, "baseRotation", p)) { size_t i = p; parseFloat(obj, i, clip.baseRotation); }
        clip.startFrame = std::clamp(clip.startFrame, 0, strova::limits::kMaxTimelineFrames - 1);
        clip.duration = std::clamp(clip.duration, 0, strova::limits::kMaxFlowGeneratedFrames);

        size_t sampPos = obj.find("\"samples\"");
        if (sampPos != std::string::npos)
        {
            size_t s0 = obj.find('[', sampPos);
            if (s0 != std::string::npos)
            {
                int d = 0;
                size_t s1 = std::string::npos;
                for (size_t i = s0; i < obj.size(); ++i)
                {
                    if (obj[i] == '[') d++;
                    else if (obj[i] == ']')
                    {
                        d--;
                        if (d == 0) { s1 = i; break; }
                    }
                }
                if (s1 != std::string::npos)
                {
                    std::vector<std::string> sampleObjs;
                    extractJsonObjects(obj.substr(s0, s1 - s0 + 1), sampleObjs);
                    for (const auto& sobj : sampleObjs)
                    {
                        if ((int)clip.samples.size() >= strova::limits::kMaxFlowGeneratedFrames)
                            break;
                        FlowLinkFrameSample sample{};
                        if (findKeyPos(sobj, "frame", p)) { size_t i = p; parseInt(sobj, i, sample.frameOffset); }
                        if (findKeyPos(sobj, "x", p)) { size_t i = p; parseFloat(sobj, i, sample.posX); }
                        if (findKeyPos(sobj, "y", p)) { size_t i = p; parseFloat(sobj, i, sample.posY); }
                        if (findKeyPos(sobj, "rotation", p)) { size_t i = p; parseFloat(sobj, i, sample.rotation); }
                        clip.samples.push_back(sample);
                    }
                }
            }
        }

        if (!clip.empty())
        {
            app.getEngine().addFlowLinkClip(clip.targetTrackId, clip);
            bool haveUiClip = false;
            const int laneTrackId = ensureFlowLinkTimelineTrackForLane(app, clip.laneIndex);
            for (const auto& uiClip : app.timeline.state().clips)
            {
                if (uiClip.trackId == laneTrackId && uiClip.startFrame == clip.startFrame && uiClip.lengthFrames == clip.duration)
                {
                    haveUiClip = true;
                    break;
                }
            }
            if (!haveUiClip)
            {
                const int laneTrackId = ensureFlowLinkTimelineTrackForLane(app, clip.laneIndex);
                const auto* laneTrack = app.timeline.findTrack(laneTrackId);
                const std::string label = laneTrack ? laneTrack->name : std::string("FlowLink");
                addFlowLinkUiClipOnLane(app, clip.laneIndex, clip.startFrame, clip.duration, label);
            }
        }
    }
}

bool App::saveProjectFolderInternal(const fs::path& folder, std::string& outErr, bool recoverySnapshot)
{
    outErr.clear();
    if (folder.empty())
    {
        outErr = "Project folder path is empty.";
        return false;
    }

    auto writeProjectContents = [&](const fs::path& targetFolder, bool savingRecovery) -> bool
    {
        validateEditorState();

        Project saveProject = currentProject;
        saveProject.folderPath = targetFolder.string();
        saveProject.width = projectW;
        saveProject.height = projectH;
        saveProject.fps = projectFPS;

        fs::create_directories(targetFolder);

        if (!ProjectIO::saveToFolder(saveProject, outErr)) return false;
        if (!saveEngineFramesToFolder(targetFolder.string(), engine, outErr)) return false;
        if (!ProjectIO::saveTimelineToFolder(targetFolder.string(), timeline, outErr)) return false;
        if (!saveEditorRuntimeStateToFolder(targetFolder, *this, outErr)) return false;
        if (!saveFlowLinkClipsToFolder(targetFolder, *this, outErr)) return false;

        layerTree.syncExistingFromTimeline(timeline);
        storeCurrentDrawFrameLayerTree();

        fs::path layersDir = targetFolder / "frame_layers";
        fs::create_directories(layersDir);
        clearDirectoryFiles(layersDir);

        if (!layerTree.saveToFolder(targetFolder.string()))
        {
            outErr = "Failed to write layers.json";
            return false;
        }

        ensureFrameLayerTreeSize();
        for (size_t fi = 0; fi < frameLayerTrees.size(); ++fi)
        {
            frameLayerTrees[fi].syncExistingFromTimeline(timeline);
            char name[64];
            sprintf_s(name, "layers_%03d.json", (int)fi);
            if (!frameLayerTrees[fi].saveToPath(layersDir / name))
            {
                outErr = "Failed to write per-frame layer data.";
                return false;
            }
        }

        if (!savingRecovery)
            strova::debug::logPath("Project", "Saved project", targetFolder);
        return true;
    };

    if (recoverySnapshot)
        return writeProjectContents(folder, true);

    std::error_code ec;
    const fs::path parent = folder.parent_path();
    if (!parent.empty())
        fs::create_directories(parent, ec);

    const fs::path staging = parent / (std::string(".strova_stage_") + folder.filename().string());
    fs::remove_all(staging, ec);

    if (!writeProjectContents(staging, false))
    {
        fs::remove_all(staging, ec);
        return false;
    }

    if (!commitStagedProjectFolder(staging, folder, outErr))
    {
        fs::remove_all(staging, ec);
        return false;
    }
    return true;
}

std::filesystem::path App::recoveryFolderForProject(const std::string& folderPath) const
{
    fs::path base = strova::paths::getAppDataDir() / "recovery";
    std::string key = sanitizeProjectKey(folderPath.empty() ? currentProject.name : folderPath);
    return base / key;
}

bool App::saveRecoverySnapshot(std::string& outErr)
{
    outErr.clear();
    if (currentProject.folderPath.empty()) return true;
    const fs::path recoveryDir = recoveryFolderForProject(currentProject.folderPath);
    return saveProjectFolderInternal(recoveryDir, outErr, true);
}

void App::clearRecoverySnapshot()
{
    if (currentProject.folderPath.empty()) return;
    std::error_code ec;
    fs::remove_all(recoveryFolderForProject(currentProject.folderPath), ec);
}

std::filesystem::path App::resolveOpenFolderWithRecovery(const std::string& folderPath, bool* outUsedRecovery) const
{
    if (outUsedRecovery) *outUsedRecovery = false;
    const fs::path original(folderPath);
    const fs::path recovery = recoveryFolderForProject(folderPath);
    std::error_code ec;
    const fs::path originalJson = original / "project.json";
    const fs::path recoveryJson = recovery / "project.json";
    if (!fs::exists(recoveryJson, ec)) return original;
    if (!fs::exists(originalJson, ec))
    {
        if (outUsedRecovery) *outUsedRecovery = true;
        return recovery;
    }
    const auto recoveryTime = fs::last_write_time(recoveryJson, ec);
    if (ec) return original;
    const auto originalTime = fs::last_write_time(originalJson, ec);
    if (ec) return original;
    if (recoveryTime > originalTime)
    {
        if (outUsedRecovery) *outUsedRecovery = true;
        return recovery;
    }
    return original;
}

