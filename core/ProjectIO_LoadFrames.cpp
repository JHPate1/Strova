/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/ProjectIO_LoadFrames.cpp
   Module:      Core
   Purpose:     Frame loading helpers kept separate for clarity.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "ProjectIO.h"
#include "SerializationUtils.h"
#include "Stroke.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <string>
#include <cctype>

namespace fs = std::filesystem;
using strova::iojson::readTextFile;
using strova::iojson::skipWs;
using strova::iojson::consume;
using strova::iojson::parseFloatAt;
using strova::iojson::parseIntAt;
using strova::iojson::parseBoolAt;
using strova::iojson::parseStringAt;
using strova::iojson::findKeyPosAfterColon;
using strova::iojson::findMatchingBrace;

static bool parseFloat(const std::string& s, size_t& i, float& out) { return parseFloatAt(s, i, out); }
static bool parseInt(const std::string& s, size_t& i, int& out) { return parseIntAt(s, i, out); }
static bool parseBool(const std::string& s, size_t& i, bool& out) { return parseBoolAt(s, i, out); }
static bool parseString(const std::string& s, size_t& i, std::string& out) { return parseStringAt(s, i, out); }
static bool findKeyPos(const std::string& j, const char* key, size_t& outPos) { return findKeyPosAfterColon(j, key, outPos); }

static bool parseFrameFile(const std::string& j, std::vector<Stroke>& outStrokes) {
    outStrokes.clear();

    size_t pos = 0;
    if (!findKeyPos(j, "strokes", pos)) return true; 

    size_t i = j.find('[', pos);
    if (i == std::string::npos) return true;
    i++;

    while (i < j.size()) {
        skipWs(j, i);
        if (i >= j.size()) break;
        if (j[i] == ']') break;

        size_t objStart = j.find('{', i);
        if (objStart == std::string::npos) break;

        size_t objEnd = findMatchingBrace(j, objStart);
        if (objEnd == std::string::npos) break;

        std::string obj = j.substr(objStart, (objEnd - objStart) + 1);

        Stroke s;
        s.color = SDL_Color{ 0,0,0,255 };
        s.thickness = 2.0f;

        {
            size_t cp;
            if (findKeyPos(obj, "color", cp)) {
                size_t ci = obj.find('[', cp);
                if (ci != std::string::npos) {
                    ci++;
                    int r = 0, g = 0, b = 0, a = 255;
                    parseInt(obj, ci, r); consume(obj, ci, ',');
                    parseInt(obj, ci, g); consume(obj, ci, ',');
                    parseInt(obj, ci, b); consume(obj, ci, ',');
                    parseInt(obj, ci, a);

                    r = std::clamp(r, 0, 255);
                    g = std::clamp(g, 0, 255);
                    b = std::clamp(b, 0, 255);
                    a = std::clamp(a, 0, 255);

                    s.color = SDL_Color{ (Uint8)r,(Uint8)g,(Uint8)b,(Uint8)a };
                }
            }
        }

        {
            size_t tp;
            if (findKeyPos(obj, "thickness", tp)) {
                size_t ti = tp;
                float th = 2.0f;
                if (parseFloat(obj, ti, th)) {
                    if (th < 0.1f) th = 0.1f;
                    if (th > 500.0f) th = 500.0f;
                    s.thickness = th;
                }
            }
        }

        {
            size_t tp2;
            if (findKeyPos(obj, "tool", tp2)) {
                size_t ti = tp2;
                int t = 0;
                if (parseInt(obj, ti, t)) {
                    s.tool = (ToolType)std::clamp(t, 0, kToolTypeCount - 1);
                }
            }
            if (findKeyPos(obj, "brushId", tp2)) {
                size_t ti = tp2;
                parseString(obj, ti, s.brushId);
            }
            if (findKeyPos(obj, "brushName", tp2)) {
                size_t ti = tp2;
                parseString(obj, ti, s.brushName);
            }
            if (findKeyPos(obj, "brushVersion", tp2)) {
                size_t ti = tp2;
                parseInt(obj, ti, s.brushVersion);
            }
            bool bm = false;
            if (findKeyPos(obj, "brushMissing", tp2)) {
                size_t ti = tp2;
                if (parseBool(obj, ti, bm)) s.brushMissing = bm;
            }
        }

        {
            size_t sp;
            if (findKeyPos(obj, "settings", sp)) {
                size_t si = obj.find('{', sp);
                if (si != std::string::npos) {
                    size_t se = findMatchingBrace(obj, si);
                    if (se != std::string::npos) {
                        std::string sobj = obj.substr(si, (se - si) + 1);
                        auto readF = [&](const char* key, float& dst) {
                            size_t kp;
                            if (findKeyPos(sobj, key, kp)) {
                                size_t ki = kp;
                                parseFloat(sobj, ki, dst);
                            }
                        };
                        readF("size", s.settings.size);
                        readF("opacity", s.settings.opacity);
                        readF("spacing", s.settings.spacing);
                        readF("flow", s.settings.flow);
                        readF("scatter", s.settings.scatter);
                        readF("hardness", s.settings.hardness);
                        readF("stabilizer", s.settings.stabilizer);
                        readF("airRadius", s.settings.airRadius);
                        readF("airDensity", s.settings.airDensity);
                        readF("strength", s.settings.strength);
                        readF("smudgeStrength", s.settings.smudgeStrength);
                        readF("blurRadius", s.settings.blurRadius);
                        readF("jitterSize", s.settings.jitterSize);
                        readF("jitterOpacity", s.settings.jitterOpacity);
                        readF("jitterRotation", s.settings.jitterRotation);
                        readF("spacingJitter", s.settings.spacingJitter);
                        size_t kp;
                        if (findKeyPos(sobj, "brushId", kp)) { size_t ki = kp; parseString(sobj, ki, s.settings.brushId); }
                        if (findKeyPos(sobj, "brushDisplayName", kp)) { size_t ki = kp; parseString(sobj, ki, s.settings.brushDisplayName); }
                        if (findKeyPos(sobj, "brushVersion", kp)) { size_t ki = kp; parseInt(sobj, ki, s.settings.brushVersion); }
                        bool bv = false;
                        if (findKeyPos(sobj, "brushSupportsUserColor", kp)) { size_t ki = kp; if (parseBool(sobj, ki, bv)) s.settings.brushSupportsUserColor = bv; }
                        if (findKeyPos(sobj, "brushSupportsGradient", kp)) { size_t ki = kp; if (parseBool(sobj, ki, bv)) s.settings.brushSupportsGradient = bv; }
                        s.settings.clamp();
                    }
                }
            }
        }

        {
            size_t fp;
            if (findKeyPos(obj, "fillTolerance", fp)) {
                size_t fi = fp;
                int v = s.fillTolerance;
                if (parseInt(obj, fi, v))
                    s.fillTolerance = std::clamp(v, 0, 255);
            }
            if (findKeyPos(obj, "fillGapClose", fp)) {
                size_t fi = fp;
                int v = s.fillGapClose;
                if (parseInt(obj, fi, v))
                    s.fillGapClose = std::clamp(v, 0, 12);
            }
        }

        
        {
            size_t gp;
            if (findKeyPos(obj, "gradient", gp)) {
                size_t gi = obj.find('{', gp);
                if (gi != std::string::npos) {
                    size_t ge = findMatchingBrace(obj, gi);
                    if (ge != std::string::npos) {
                        std::string gobj = obj.substr(gi, (ge - gi) + 1);

                        size_t ep;
                        if (findKeyPos(gobj, "enabled", ep)) {
                            size_t ei = ep;
                            int e = 0;
                            if (parseInt(gobj, ei, e)) s.gradient.enabled = (e != 0);
                        }
                        size_t mp;
                        if (findKeyPos(gobj, "mode", mp)) {
                            size_t mi = mp;
                            int m = 0;
                            if (parseInt(gobj, mi, m)) s.gradient.mode = m;
                        }

                        size_t spp;
                        if (findKeyPos(gobj, "stopPos", spp)) {
                            size_t si = gobj.find('[', spp);
                            if (si != std::string::npos) {
                                si++;
                                for (int k = 0; k < STROVA_MAX_GRADIENT_STOPS; ++k) {
                                    float v = 0.0f;
                                    if (!parseFloat(gobj, si, v)) break;
                                    s.gradient.stopPos[(size_t)k] = v;
                                    consume(gobj, si, ',');
                                }
                            }
                        }

                        size_t scp;
                        if (findKeyPos(gobj, "stopColor", scp)) {
                            size_t si = gobj.find('[', scp);
                            if (si != std::string::npos) {
                                si++;
                                for (int k = 0; k < STROVA_MAX_GRADIENT_STOPS; ++k) {
                                    skipWs(gobj, si);
                                    if (!consume(gobj, si, '[')) break;
                                    int r = 0, g = 0, b = 0, a = 255;
                                    parseInt(gobj, si, r); consume(gobj, si, ',');
                                    parseInt(gobj, si, g); consume(gobj, si, ',');
                                    parseInt(gobj, si, b); consume(gobj, si, ',');
                                    parseInt(gobj, si, a);
                                    consume(gobj, si, ']');
                                    r = std::clamp(r, 0, 255);
                                    g = std::clamp(g, 0, 255);
                                    b = std::clamp(b, 0, 255);
                                    a = std::clamp(a, 0, 255);
                                    s.gradient.stopColor[(size_t)k] = SDL_Color{ (Uint8)r,(Uint8)g,(Uint8)b,(Uint8)a };
                                    skipWs(gobj, si);
                                    if (si < gobj.size() && gobj[si] == ',') si++;
                                }
                            }
                        }
                    }
                }
            }
        }

        {
            size_t pp;
            if (findKeyPos(obj, "points", pp)) {
                size_t pi = obj.find('[', pp);
                if (pi != std::string::npos) {
                    pi++; 
                    while (pi < obj.size()) {
                        skipWs(obj, pi);
                        if (pi < obj.size() && obj[pi] == ']') { pi++; break; }

                        if (!consume(obj, pi, '[')) break;

                        float x = 0, y = 0, pr = 1.0f;
                        if (!parseFloat(obj, pi, x)) break;
                        consume(obj, pi, ',');
                        if (!parseFloat(obj, pi, y)) break;

                        skipWs(obj, pi);
                        if (consume(obj, pi, ',')) {
                            float tmp = 1.0f;
                            if (parseFloat(obj, pi, tmp)) pr = tmp;
                        }

                        consume(obj, pi, ']');

                        StrokePoint sp;
                        sp.x = x; sp.y = y; sp.pressure = pr;
                        s.points.push_back(sp);

                        skipWs(obj, pi);
                        if (pi < obj.size() && obj[pi] == ',') pi++;
                    }
                }
            }
        }

        outStrokes.push_back(std::move(s));

        i = objEnd + 1;
        skipWs(j, i);
        if (i < j.size() && j[i] == ',') i++;
    }

    return true;
}

bool ProjectIO::loadFramesFromFolder(const std::string& folderPath,
    int frameCount,
    std::vector<std::vector<Stroke>>& outFrameStrokes,
    std::string& err)
{
    err.clear();
    outFrameStrokes.clear();

    try {
        if (frameCount < 1) frameCount = 1;
        outFrameStrokes.resize((size_t)frameCount);

        fs::path folder = folderPath;
        fs::path framesDir = folder / "frames";

        for (int fi = 0; fi < frameCount; fi++) {
            char name[64];
            sprintf_s(name, "frame_%03d.json", fi);
            fs::path fp = framesDir / name;

            if (!fs::exists(fp)) continue;

            std::string fj;
            if (!readTextFile(fp, fj)) continue;

            std::vector<Stroke> strokes;
            parseFrameFile(fj, strokes);
            outFrameStrokes[(size_t)fi] = std::move(strokes);
        }

        return true;
    }
    catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}
