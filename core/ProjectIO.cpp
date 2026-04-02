/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/ProjectIO.cpp
   Module:      Core
   Purpose:     Project load, save, and cache management.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "ProjectIO.h"
#include "StrovaLimits.h"
#include "SerializationUtils.h"
#include "../ui/Timeline.h"
#include "../plugin/PluginProjectData.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace fs = std::filesystem;
using strova::iojson::readTextFile;
using strova::iojson::writeTextFileAtomic;
using strova::iojson::findInt;
using strova::iojson::findStr;
using strova::iojson::jsonEscape;
using strova::iojson::skipWs;
using strova::iojson::findKeyPosAfterColon;
using strova::iojson::parseIntAt;
using strova::iojson::parseFloatAt;
using strova::iojson::parseBoolAt;
using strova::iojson::parseStringAt;
using strova::iojson::extractArrayText;
static constexpr int kCurrentProjectFormatVersion = 3;

static void parseObjectsInArray(const std::string& arrText, std::vector<std::string>& outObjs)
{
    outObjs.clear();

    size_t i = 0;
    while (i < arrText.size() && arrText[i] != '[') i++;
    if (i >= arrText.size()) return;
    i++; 

    while (i < arrText.size())
    {
        skipWs(arrText, i);
        if (i < arrText.size() && arrText[i] == ']') break;

        size_t objStart = arrText.find('{', i);
        if (objStart == std::string::npos) break;

        int depth = 0;
        for (size_t k = objStart; k < arrText.size(); ++k)
        {
            if (arrText[k] == '{') depth++;
            else if (arrText[k] == '}')
            {
                depth--;
                if (depth == 0)
                {
                    outObjs.push_back(arrText.substr(objStart, (k - objStart) + 1));
                    i = k + 1;
                    break;
                }
            }
        }
    }
}


static bool validateProjectMetadata(Project& project, std::string& outError)
{
    project.width = strova::limits::clampCanvasWidth(project.width);
    project.height = strova::limits::clampCanvasHeight(project.height);
    project.fps = strova::limits::clampProjectFps(project.fps);
    if (project.name.empty())
        project.name = fs::path(project.folderPath).stem().string();
    if (project.width <= 0 || project.height <= 0 || project.fps <= 0)
    {
        outError = "Project metadata failed validation.";
        return false;
    }
    return true;
}

static void sanitizeTimelineState(strova::TimelineState& st)
{
    st.fps = strova::limits::clampProjectFps(st.fps);
    st.totalFrames = strova::limits::clampTimelineFrames(st.totalFrames);
    st.pxPerFrame = std::max(st.minPxPerFrame, std::min(st.pxPerFrame, st.maxPxPerFrame));
    st.trackHeaderW = std::clamp(st.trackHeaderW, 80, 480);
    st.trackH = std::clamp(st.trackH, 24, 200);
    st.rulerH = std::clamp(st.rulerH, 18, 80);
    st.scrollX = std::max(0, st.scrollX);
    st.scrollY = std::max(0, st.scrollY);
    st.nextTrackId = std::max(1, st.nextTrackId);
    st.nextClipId = std::max(1, st.nextClipId);
}

namespace ProjectIO
{
    bool isValidProjectFolder(const std::string& folderPath)
    {
        fs::path p = folderPath;
        return fs::exists(p) && fs::exists(p / "project.json");
    }

    bool saveToFolder(const Project& project, std::string& outError)
    {
        outError.clear();
        try
        {
            fs::path folder = project.folderPath;
            if (folder.empty()) { outError = "Project folderPath is empty."; return false; }

            Project normalized = project;
            normalized.folderPath = folder.string();
            if (!validateProjectMetadata(normalized, outError)) return false;
            const int safeWidth = normalized.width;
            const int safeHeight = normalized.height;
            const int safeFps = normalized.fps;
            const std::string safeName = normalized.name.empty() ? folder.stem().string() : normalized.name;

            fs::create_directories(folder);
            fs::create_directories(folder / "frames");

            std::ostringstream j;
            j << "{\n";
            j << "  \"version\": " << kCurrentProjectFormatVersion << ",\n";
            j << "  \"name\": \"" << jsonEscape(safeName) << "\",\n";
            j << "  \"width\": " << safeWidth << ",\n";
            j << "  \"height\": " << safeHeight << ",\n";
            j << "  \"fps\": " << safeFps;

            std::string pluginJsonAppend;
            strova::plugin::serializeProjectPluginData(normalized, pluginJsonAppend);
            j << pluginJsonAppend << "\n";
            j << "}\n";

            if (!writeTextFileAtomic(folder / "project.json", j.str()))
            {
                outError = "Failed to write project.json";
                return false;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            outError = e.what();
            return false;
        }
    }

    bool loadFromFolder(const std::string& folderPath, Project& outProject, std::string& outError)
    {
        outError.clear();
        try
        {
            fs::path folder = folderPath;
            if (!fs::exists(folder)) { outError = "Folder not found."; return false; }

            std::string pj;
            if (!readTextFile(folder / "project.json", pj)) { outError = "Missing project.json"; return false; }

            outProject = Project{};
            outProject.folderPath = folderPath;
            outProject.name = findStr(pj, "name", folder.stem().string());
            const int version = findInt(pj, "version", 1);
            if (version <= 0)
            {
                outError = "Invalid project version.";
                return false;
            }
            if (version > kCurrentProjectFormatVersion)
            {
                outError = "Project was saved by a newer Strova build.";
                return false;
            }
            outProject.width = findInt(pj, "width", 1920);
            outProject.height = findInt(pj, "height", 1080);
            outProject.fps = findInt(pj, "fps", 30);
            if (!strova::plugin::loadProjectPluginDataFromJson(pj, outProject, outError))
                return false;
            return validateProjectMetadata(outProject, outError);
        }
        catch (const std::exception& e)
        {
            outError = e.what();
            return false;
        }
    }

    bool saveTimelineToFolder(const std::string& folderPath, const strova::TimelineWidget& timeline, std::string& outError)
    {
        outError.clear();
        try
        {
            fs::path folder = folderPath;
            if (folder.empty()) { outError = "folderPath is empty."; return false; }
            fs::create_directories(folder);

            const auto& st = timeline.state();

            std::ostringstream j;
            j << "{\n";
            j << "  \"fps\": " << st.fps << ",\n";
            j << "  \"totalFrames\": " << st.totalFrames << ",\n";
            j << "  \"pxPerFrame\": " << st.pxPerFrame << ",\n";
            j << "  \"scrollX\": " << st.scrollX << ",\n";
            j << "  \"scrollY\": " << st.scrollY << ",\n";
            j << "  \"rulerH\": " << st.rulerH << ",\n";
            j << "  \"trackHeaderW\": " << st.trackHeaderW << ",\n";
            j << "  \"trackH\": " << st.trackH << ",\n";
            j << "  \"nextTrackId\": " << st.nextTrackId << ",\n";
            j << "  \"nextClipId\": " << st.nextClipId << ",\n";

            j << "  \"tracks\": [\n";
            for (size_t i = 0; i < st.tracks.size(); ++i)
            {
                const auto& t = st.tracks[i];
                j << "    {";
                j << "\"id\":" << t.id << ",";
                j << "\"kind\":" << (int)t.kind << ",";
                j << "\"name\":\"" << jsonEscape(t.name) << "\",";
                j << "\"engineTrackId\":" << t.engineTrackId << ",";

                
                j << "\"visible\":" << (t.visible ? "true" : "false") << ",";

                j << "\"locked\":" << (t.locked ? "true" : "false") << ",";
                j << "\"muted\":" << (t.muted ? "true" : "false");
                j << "}";
                if (i + 1 < st.tracks.size()) j << ",";
                j << "\n";
            }
            j << "  ],\n";

            j << "  \"clips\": [\n";
            for (size_t i = 0; i < st.clips.size(); ++i)
            {
                const auto& c = st.clips[i];
                j << "    {";
                j << "\"id\":" << c.id << ",";
                j << "\"trackId\":" << c.trackId << ",";
                j << "\"startFrame\":" << c.startFrame << ",";
                j << "\"lengthFrames\":" << c.lengthFrames << ",";
                j << "\"label\":\"" << jsonEscape(c.label) << "\"";
                j << "}";
                if (i + 1 < st.clips.size()) j << ",";
                j << "\n";
            }
            j << "  ]\n";
            j << "}\n";

            if (!writeTextFileAtomic(folder / "timeline.json", j.str()))
            {
                outError = "Failed to write timeline.json";
                return false;
            }
            return true;
        }
        catch (const std::exception& e)
        {
            outError = e.what();
            return false;
        }
    }

    bool loadTimelineFromFolder(const std::string& folderPath, strova::TimelineWidget& timeline, std::string& outError)
    {
        outError.clear();
        try
        {
            fs::path folder = folderPath;
            fs::path file = folder / "timeline.json";
            if (!fs::exists(file)) return false;

            std::string tj;
            if (!readTextFile(file, tj)) { outError = "Failed to read timeline.json"; return false; }

            
            strova::TimelineState st = timeline.state();

            auto readI = [&](const char* key, int& dst)
                {
                    size_t p = 0;
                    if (!findKeyPosAfterColon(tj, key, p)) return;
                    (void)parseIntAt(tj, p, dst);
                };
            auto readF = [&](const char* key, float& dst)
                {
                    size_t p = 0;
                    if (!findKeyPosAfterColon(tj, key, p)) return;
                    (void)parseFloatAt(tj, p, dst);
                };

            readI("fps", st.fps);
            readI("totalFrames", st.totalFrames);
            readF("pxPerFrame", st.pxPerFrame);
            readI("scrollX", st.scrollX);
            readI("scrollY", st.scrollY);
            readI("rulerH", st.rulerH);
            readI("trackHeaderW", st.trackHeaderW);
            readI("trackH", st.trackH);
            readI("nextTrackId", st.nextTrackId);
            readI("nextClipId", st.nextClipId);

            
            sanitizeTimelineState(st);

            
            st.tracks.clear();
            {
                std::string arr = extractArrayText(tj, "tracks");
                std::vector<std::string> objs;
                parseObjectsInArray(arr, objs);
                int drawTracks = 0;
                int flowTracks = 0;
                int flowLinkTracks = 0;
                int audioTracks = 0;

                for (const auto& obj : objs)
                {
                    strova::TimelineTrack t{};
                    size_t p = 0;

                    if (findKeyPosAfterColon(obj, "id", p)) (void)parseIntAt(obj, p, t.id);

                    if (findKeyPosAfterColon(obj, "kind", p))
                    {
                        int k = (int)strova::TrackKind::Draw;
                        (void)parseIntAt(obj, p, k);
                        t.kind = (strova::TrackKind)k;
                    }

                    if (findKeyPosAfterColon(obj, "name", p)) (void)parseStringAt(obj, p, t.name);
                    if (findKeyPosAfterColon(obj, "engineTrackId", p)) (void)parseIntAt(obj, p, t.engineTrackId);

                    
                    if (findKeyPosAfterColon(obj, "visible", p)) (void)parseBoolAt(obj, p, t.visible);
                    else t.visible = true;

                    
                    if (findKeyPosAfterColon(obj, "locked", p)) (void)parseBoolAt(obj, p, t.locked);
                    if (findKeyPosAfterColon(obj, "muted", p))  (void)parseBoolAt(obj, p, t.muted);

                    if (t.id == 0 || (int)st.tracks.size() >= strova::limits::kMaxTimelineTracks)
                        continue;

                    int* kindCount = nullptr;
                    int kindMax = strova::limits::kMaxTimelineTracks;
                    switch (t.kind)
                    {
                    case strova::TrackKind::Draw:
                        kindCount = &drawTracks;
                        kindMax = strova::limits::kMaxDrawTracks;
                        break;
                    case strova::TrackKind::Flow:
                        kindCount = &flowTracks;
                        kindMax = strova::limits::kMaxFlowTracks;
                        break;
                    case strova::TrackKind::FlowLink:
                        kindCount = &flowLinkTracks;
                        kindMax = strova::limits::kMaxFlowLinkTracks;
                        break;
                    case strova::TrackKind::Audio:
                        kindCount = &audioTracks;
                        kindMax = strova::limits::kMaxAudioTracks;
                        break;
                    default:
                        break;
                    }

                    if (kindCount && *kindCount >= kindMax)
                        continue;

                    st.tracks.push_back(t);
                    if (kindCount)
                        ++(*kindCount);
                }
            }

            
            st.clips.clear();
            {
                std::string arr = extractArrayText(tj, "clips");
                std::vector<std::string> objs;
                parseObjectsInArray(arr, objs);

                for (const auto& obj : objs)
                {
                    strova::TimelineClip c{};
                    size_t p = 0;

                    if (findKeyPosAfterColon(obj, "id", p))           (void)parseIntAt(obj, p, c.id);
                    if (findKeyPosAfterColon(obj, "trackId", p))      (void)parseIntAt(obj, p, c.trackId);
                    if (findKeyPosAfterColon(obj, "startFrame", p))   (void)parseIntAt(obj, p, c.startFrame);
                    if (findKeyPosAfterColon(obj, "lengthFrames", p)) (void)parseIntAt(obj, p, c.lengthFrames);
                    if (findKeyPosAfterColon(obj, "label", p))        (void)parseStringAt(obj, p, c.label);

                    c.startFrame = std::max(0, c.startFrame);
                    c.lengthFrames = std::max(1, c.lengthFrames);

                    if (c.id != 0 && c.trackId != 0)
                        st.clips.push_back(c);
                }
            }

            st.clips.erase(std::remove_if(st.clips.begin(), st.clips.end(), [&](const strova::TimelineClip& clip)
            {
                for (const auto& track : st.tracks)
                    if (track.id == clip.trackId)
                        return false;
                return true;
            }), st.clips.end());

            sanitizeTimelineState(st);
            timeline.state() = st;
            return !timeline.state().tracks.empty();
        }
        catch (const std::exception& e)
        {
            outError = e.what();
            return false;
        }
    }

} 
