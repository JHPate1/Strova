/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        ui/Launcher.cpp
   Module:      Ui
   Purpose:     Launcher UI widgets and project list behavior.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "Launcher.h"
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <ctime>
#include <cstdio>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include <SDL_image.h>
#include "../app/App.h"
#include "../core/FlowCapturer.h"
#include "../core/ProjectData.h"
#include "../core/DebugLog.h"
#include "../platform/AppPaths.h"

namespace fs = std::filesystem;

static std::string trimCopyLauncher(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static bool saveTextureToBmpLauncher(SDL_Renderer* renderer, SDL_Texture* tex, const std::string& path) {
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
    if (rc != 0) { SDL_FreeSurface(surf); return false; }
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    bool ok = (SDL_SaveBMP(surf, path.c_str()) == 0);
    SDL_FreeSurface(surf);
    return ok;
}

static SDL_Texture* loadBmpTextureLauncher(SDL_Renderer* renderer, const std::string& path, int* outW = nullptr, int* outH = nullptr) {
    if (!renderer || path.empty() || !fs::exists(path)) return nullptr;
    SDL_Surface* surf = SDL_LoadBMP(path.c_str());
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        if (outW) *outW = surf->w;
        if (outH) *outH = surf->h;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    SDL_FreeSurface(surf);
    return tex;
}


static void setDrawColor(SDL_Renderer* r, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

static SDL_Color lerpColor(SDL_Color a, SDL_Color b, float t) {
    auto lerpU8 = [&](Uint8 x, Uint8 y) -> Uint8 {
        return (Uint8)(x + (y - x) * t);
        };
    return SDL_Color{ lerpU8(a.r,b.r), lerpU8(a.g,b.g), lerpU8(a.b,b.b), lerpU8(a.a,b.a) };
}

static SDL_Color withAlpha(SDL_Color c, Uint8 a) {
    c.a = a;
    return c;
}

static void fillVerticalGradient(SDL_Renderer* r, const SDL_Rect& rc, SDL_Color top, SDL_Color bot) {
    for (int y = 0; y < rc.h; ++y) {
        float t = (rc.h <= 1) ? 0.f : (float)y / (float)(rc.h - 1);
        SDL_Color c = lerpColor(top, bot, t);
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLine(r, rc.x, rc.y + y, rc.x + rc.w - 1, rc.y + y);
    }
}

static void fillRoundedRect(SDL_Renderer* r, const SDL_Rect& rc, int radius, SDL_Color fill) {
    if (rc.w <= 0 || rc.h <= 0) return;
    radius = std::max(0, std::min(radius, std::min(rc.w, rc.h) / 2));
    setDrawColor(r, fill);

    if (radius == 0) {
        SDL_RenderFillRect(r, &rc);
        return;
    }

    
    SDL_Rect mid{ rc.x + radius, rc.y, rc.w - 2 * radius, rc.h };
    if (mid.w > 0) SDL_RenderFillRect(r, &mid);

    
    SDL_Rect left{ rc.x, rc.y + radius, radius, rc.h - 2 * radius };
    SDL_Rect right{ rc.x + rc.w - radius, rc.y + radius, radius, rc.h - 2 * radius };
    if (left.h > 0) SDL_RenderFillRect(r, &left);
    if (right.h > 0) SDL_RenderFillRect(r, &right);

    
    
    for (int y = 0; y < radius; ++y) {
        float dy = (float)(radius - y);
        float dx = std::sqrt(std::max(0.0f, (float)radius * (float)radius - dy * dy));
        int ix = (int)std::floor(dx);

        
        SDL_RenderDrawLine(r,
            rc.x + (radius - ix),
            rc.y + y,
            rc.x + radius,
            rc.y + y
        );
        
        SDL_RenderDrawLine(r,
            rc.x + rc.w - radius - 1,
            rc.y + y,
            rc.x + rc.w - (radius - ix) - 1,
            rc.y + y
        );
        
        SDL_RenderDrawLine(r,
            rc.x + (radius - ix),
            rc.y + rc.h - y - 1,
            rc.x + radius,
            rc.y + rc.h - y - 1
        );
        
        SDL_RenderDrawLine(r,
            rc.x + rc.w - radius - 1,
            rc.y + rc.h - y - 1,
            rc.x + rc.w - (radius - ix) - 1,
            rc.y + rc.h - y - 1
        );
    }
}

static void drawRoundedRectOutline(SDL_Renderer* r, const SDL_Rect& rc, int radius, SDL_Color col) {
    if (rc.w <= 0 || rc.h <= 0) return;
    radius = std::max(0, std::min(radius, std::min(rc.w, rc.h) / 2));
    setDrawColor(r, col);

    if (radius == 0) {
        SDL_RenderDrawRect(r, &rc);
        return;
    }

    
    SDL_RenderDrawLine(r, rc.x + radius, rc.y, rc.x + rc.w - radius - 1, rc.y);
    SDL_RenderDrawLine(r, rc.x + radius, rc.y + rc.h - 1, rc.x + rc.w - radius - 1, rc.y + rc.h - 1);
    SDL_RenderDrawLine(r, rc.x, rc.y + radius, rc.x, rc.y + rc.h - radius - 1);
    SDL_RenderDrawLine(r, rc.x + rc.w - 1, rc.y + radius, rc.x + rc.w - 1, rc.y + rc.h - radius - 1);

    
    const int steps = std::max(10, radius * 2);
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float ang = t * (float)M_PI_2; 
        int dx = (int)std::round(std::cos(ang) * radius);
        int dy = (int)std::round(std::sin(ang) * radius);

        
        SDL_RenderDrawPoint(r, rc.x + radius - dx, rc.y + radius - dy);
        
        SDL_RenderDrawPoint(r, rc.x + rc.w - radius - 1 + dx, rc.y + radius - dy);
        
        SDL_RenderDrawPoint(r, rc.x + radius - dx, rc.y + rc.h - radius - 1 + dy);
        
        SDL_RenderDrawPoint(r, rc.x + rc.w - radius - 1 + dx, rc.y + rc.h - radius - 1 + dy);
    }
}

static void drawSoftShadow(SDL_Renderer* r, const SDL_Rect& rc, int radius, int spread, SDL_Color shadow) {
    
    
    int passes = std::max(1, spread / 3);
    for (int i = passes; i >= 1; --i) {
        float t = (float)i / (float)passes; 
        Uint8 a = (Uint8)std::round((float)shadow.a * (0.22f * t)); 
        SDL_Rect s = rc;
        s.x -= i;
        s.y -= i;
        s.w += i * 2;
        s.h += i * 2;
        fillRoundedRect(r, s, radius + i, withAlpha(shadow, a));
    }
}

static void drawCard(SDL_Renderer* r, const SDL_Rect& rc, int radius, SDL_Color fill, SDL_Color border, bool shadow = true) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (shadow) drawSoftShadow(r, rc, radius, 14, SDL_Color{ 0,0,0,220 });
    fillRoundedRect(r, rc, radius, fill);
    if (border.a > 0) drawRoundedRectOutline(r, rc, radius, border);
}

static void drawLineThick(SDL_Renderer* r, float x1, float y1, float x2, float y2, int thickness) {
    thickness = std::max(1, thickness);
    int half = thickness / 2;
#if SDL_VERSION_ATLEAST(2,0,10)
    for (int ox = -half; ox <= half; ++ox) {
        for (int oy = -half; oy <= half; ++oy) {
            SDL_RenderDrawLineF(r, x1 + ox, y1 + oy, x2 + ox, y2 + oy);
        }
    }
#else
    for (int ox = -half; ox <= half; ++ox) {
        for (int oy = -half; oy <= half; ++oy) {
            SDL_RenderDrawLine(r, (int)x1 + ox, (int)y1 + oy, (int)x2 + ox, (int)y2 + oy);
        }
    }
#endif
}

static void drawPolylineThick(SDL_Renderer* r, const std::vector<SDL_FPoint>& pts, float thickness) {
    if (pts.size() < 2) return;
    int t = (int)std::round(std::max(1.0f, thickness));
    for (size_t i = 1; i < pts.size(); ++i) {
        drawLineThick(r, pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, t);
    }
}


Launcher::~Launcher() {
    destroyAllPreviews();
    if (logoTex) { SDL_DestroyTexture(logoTex); logoTex = nullptr; }
}

bool Launcher::consumeUpdateChecksChanged(bool& outEnabled) {
    if (!updateChecksDirty) return false;
    updateChecksDirty = false;
    outEnabled = updateChecksEnabled;
    strova::debug::log("Launcher", std::string("consumeUpdateChecksChanged -> ") + (outEnabled ? "true" : "false"));
    return true;
}

bool Launcher::consumePersistentDockingChanged(bool& outEnabled) {
    if (!persistentDockingDirty) return false;
    persistentDockingDirty = false;
    outEnabled = persistentDockingEnabled;
    strova::debug::log("Launcher", std::string("consumePersistentDockingChanged -> ") + (outEnabled ? "true" : "false"));
    return true;
}

void Launcher::destroyAllPreviews() {
    for (auto& kv : previews) {
        if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    }
    previews.clear();
}

void Launcher::init(TTF_Font* f) {
    font = f;

    requestNewProject = requestOpenProject = requestOpenFolder = false;
    requestOpenBrushCreator = false;
    requestOpenBrushProject = false;
    requestedProjectPath.clear();
    requestedBrushProjectPath.clear();

    requestedNewName.clear();
    requestedWidth = 1920;
    requestedHeight = 1080;
    requestedFPS = 30;

    searchQuery.clear();
    searchFocused = false;

    sortMode = SortMode::Recent;

    renaming = false;
    renameProjectIndex = -1;
    renameBuffer.clear();

    menuOpen = false;
    menuProjectIndex = -1;

    hoverIndex = -1;

    triedLogo = false;

    newModalOpen = false;
    viewMode = ViewMode::Home;
    newFocus = NewField::None;
    newName = "Untitled";
    newWidthStr = "1920";
    newHeightStr = "1080";
    newFpsStr = "30";

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_StartTextInput();
    refreshProjects();
}

bool Launcher::pointInRect(int x, int y, const SDL_Rect& rc) const {
    return x >= rc.x && x < rc.x + rc.w && y >= rc.y && y < rc.y + rc.h;
}

void Launcher::clearCardRects() {
    cardRects.clear();
    hoverIndex = -1;
}

SDL_Texture* Launcher::makeText(SDL_Renderer* r, const std::string& text, SDL_Color col, int* outW, int* outH) {
    if (!font) return nullptr;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), col);
    if (!surf) return nullptr;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    if (outW) *outW = surf->w;
    if (outH) *outH = surf->h;
    SDL_FreeSurface(surf);
    return tex;
}

bool Launcher::readTextFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

int Launcher::findIntInJson(const std::string& j, const char* key, int def) {
    std::string k = std::string("\"") + key + "\"";
    size_t pos = j.find(k);
    if (pos == std::string::npos) return def;
    pos = j.find(':', pos);
    if (pos == std::string::npos) return def;
    pos++;
    while (pos < j.size() && std::isspace((unsigned char)j[pos])) pos++;
    try { return std::stoi(j.substr(pos)); }
    catch (...) { return def; }
}

std::string Launcher::findStrInJson(const std::string& j, const char* key, const std::string& def) {
    std::string k = std::string("\"") + key + "\"";
    size_t pos = j.find(k);
    if (pos == std::string::npos) return def;
    pos = j.find(':', pos);
    if (pos == std::string::npos) return def;
    pos = j.find('"', pos);
    if (pos == std::string::npos) return def;
    pos++;
    size_t end = j.find('"', pos);
    if (end == std::string::npos) return def;
    return j.substr(pos, end - pos);
}

uint64_t Launcher::getFolderWriteKey(const std::string& folderPath) {
    std::error_code ec;
    auto t = fs::last_write_time(folderPath, ec);
    if (ec) return 0;
    auto ns = t.time_since_epoch().count();
    if (ns < 0) ns = -ns;
    return (uint64_t)ns;
}

void Launcher::sortProjects() {
    std::sort(projects.begin(), projects.end(), [&](const ProjectItem& a, const ProjectItem& b) {
        switch (sortMode) {
        case SortMode::Recent:
            if (a.lastWriteKey != b.lastWriteKey) return a.lastWriteKey > b.lastWriteKey;
            return a.name < b.name;
        case SortMode::Name:
            return a.name < b.name;
        case SortMode::FPS:
            if (a.fps != b.fps) return a.fps > b.fps;
            return a.name < b.name;
        case SortMode::Frames:
            if (a.frameCount != b.frameCount) return a.frameCount > b.frameCount;
            return a.name < b.name;
        default:
            return a.name < b.name;
        }
        });
}

int Launcher::countFramesOnDisk(const std::string& projectPath) {
    fs::path framesDir = fs::path(projectPath) / "frames";
    if (!fs::exists(framesDir) || !fs::is_directory(framesDir)) return 1;

    int count = 0;
    for (auto& e : fs::directory_iterator(framesDir)) {
        if (!e.is_regular_file()) continue;
        auto p = e.path();
        if (p.extension() == ".json") {
            auto fn = p.filename().string();
            if (fn.rfind("frame_", 0) == 0) count++;
        }
    }
    return std::max(1, count);
}

std::string Launcher::findFirstFrameJson(const std::string& projectPath) {
    fs::path framesDir = fs::path(projectPath) / "frames";
    if (!fs::exists(framesDir) || !fs::is_directory(framesDir)) return {};

    std::string best;
    for (auto& e : fs::directory_iterator(framesDir)) {
        if (!e.is_regular_file()) continue;
        auto p = e.path();
        if (p.extension() != ".json") continue;

        auto fn = p.filename().string();
        if (fn.rfind("frame_", 0) != 0) continue;

        std::string full = p.string();
        if (best.empty() || fn < fs::path(best).filename().string()) best = full;
    }
    return best;
}

void Launcher::refreshProjectMeta(ProjectItem& it) {
    it.lastWriteKey = getFolderWriteKey(it.path);

    fs::path pj = fs::path(it.path) / "project.json";
    std::string j;
    if (fs::exists(pj) && readTextFile(pj.string(), j)) {
        it.name = findStrInJson(j, "name", it.name);
        it.fps = findIntInJson(j, "fps", it.fps);
        int fc = findIntInJson(j, "frameCount", -1);
        if (fc > 0) it.frameCount = fc;
        else it.frameCount = countFramesOnDisk(it.path);
    }
    else {
        it.frameCount = countFramesOnDisk(it.path);
    }

    it.fps = std::max(1, it.fps);
    it.frameCount = std::max(1, it.frameCount);
}

void Launcher::refreshProjects() {
    projects.clear();

    fs::path root = strova::paths::getProjectsDir();
    if (!fs::exists(root)) {
        clearCardRects();
        return;
    }

    for (auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_directory()) continue;

        fs::path p = entry.path();
        if (p.extension() != ".strova") continue;

        fs::path pj = p / "project.json";
        if (!fs::exists(pj)) continue;

        ProjectItem it;
        it.path = p.string();
        it.name = p.stem().string();
        refreshProjectMeta(it);

        projects.push_back(it);
    }

    sortProjects();
    clearCardRects();
}

std::string Launcher::greetingFromLocalTime() {
    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    lt = *std::localtime(&now);
#endif
    int h = lt.tm_hour;
    if (h < 12) return "Good Morning";
    if (h < 17) return "Good Afternoon";
    return "Good Evening";
}

void Launcher::ensureLogo(SDL_Renderer* r) {
    if (logoTex || triedLogo) return;
    triedLogo = true;

    int flags = IMG_INIT_PNG;
    IMG_Init(flags);

    const std::string logoPath = strova::paths::resolveAssetPath("Strova.png").string();
    SDL_Texture* t = IMG_LoadTexture(r, logoPath.c_str());
    if (!t) return;

    logoTex = t;
    SDL_QueryTexture(logoTex, nullptr, nullptr, &logoW, &logoH);
}

std::string Launcher::sanitizeProjectFolderName(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        unsigned char uc = (unsigned char)c;
        if (std::isalnum(uc)) out.push_back(c);
        else if (c == ' ' || c == '-' || c == '_') out.push_back('_');
    }
    if (out.empty()) out = "Untitled";
    return out;
}

std::string Launcher::makeUniqueProjectFolder(const std::string& baseName) {
    fs::path root = strova::paths::getProjectsDir();
    fs::create_directories(root);

    std::string base = sanitizeProjectFolderName(baseName);

    fs::path candidate = root / (base + ".strova");
    if (!fs::exists(candidate)) return candidate.string();

    for (int i = 2; i < 10000; ++i) {
        fs::path p = root / (base + "_" + std::to_string(i) + ".strova");
        if (!fs::exists(p)) return p.string();
    }
    return (root / ("Untitled_" + std::to_string((int)std::time(nullptr)) + ".strova")).string();
}

bool Launcher::createNewProjectOnDisk(const std::string& name, int w, int h, int fps, std::string& outProjectPath) {
    try {
        fs::path projectDir = makeUniqueProjectFolder(name);
        fs::create_directories(projectDir);
        fs::create_directories(projectDir / "frames");

        {
            std::ofstream f(projectDir / "project.json", std::ios::binary);
            if (!f) return false;

            f << "{\n";
            f << "  \"name\": \"" << name << "\",\n";
            f << "  \"fps\": " << fps << ",\n";
            f << "  \"width\": " << w << ",\n";
            f << "  \"height\": " << h << ",\n";
            f << "  \"frameCount\": 1\n";
            f << "}\n";
        }

        {
            std::ofstream f(projectDir / "frames" / "frame_000.json", std::ios::binary);
            if (!f) return false;

            f << "{\n";
            f << "  \"strokes\": []\n";
            f << "}\n";
        }

        outProjectPath = projectDir.string();
        return true;
    }
    catch (...) {
        return false;
    }
}

bool Launcher::parseFrameStrokesFromFile(
    const std::string& frameFilePath,
    std::vector<SDL_FPoint>& outAllPointsWithSeparators,
    std::vector<SDL_Color>& outStrokeColors,
    std::vector<float>& outStrokeThickness
) {
    outAllPointsWithSeparators.clear();
    outStrokeColors.clear();
    outStrokeThickness.clear();

    if (frameFilePath.empty() || !fs::exists(frameFilePath)) return false;

    std::string j;
    if (!readTextFile(frameFilePath, j)) return false;

    auto skipWs = [&](size_t& i) {
        while (i < j.size() && std::isspace((unsigned char)j[i])) i++;
        };
    auto consume = [&](size_t& i, char c) -> bool {
        skipWs(i);
        if (i < j.size() && j[i] == c) { i++; return true; }
        return false;
        };
    auto parseFloat = [&](size_t& i, float& out) -> bool {
        skipWs(i);
        size_t start = i;
        if (i < j.size() && (j[i] == '-' || j[i] == '+')) i++;
        while (i < j.size() && (std::isdigit((unsigned char)j[i]) || j[i] == '.')) i++;
        if (start == i) return false;
        try { out = std::stof(j.substr(start, i - start)); return true; }
        catch (...) { return false; }
        };
    auto parseInt = [&](size_t& i, int& out) -> bool {
        float f = 0.f;
        if (!parseFloat(i, f)) return false;
        out = (int)f;
        return true;
        };

    size_t strokesPos = j.find("\"strokes\"");
    if (strokesPos == std::string::npos) return false;

    size_t i = j.find('[', strokesPos);
    if (i == std::string::npos) return false;
    i++;

    auto pushSep = [&]() { outAllPointsWithSeparators.push_back(SDL_FPoint{ NAN, NAN }); };

    while (i < j.size()) {
        skipWs(i);
        if (i < j.size() && j[i] == ']') break;

        size_t obj = j.find('{', i);
        if (obj == std::string::npos) break;
        i = obj + 1;

        SDL_Color color{ 10, 10, 20, 255 };
        float thickness = 2.0f;
        std::vector<SDL_FPoint> pts;

        while (i < j.size()) {
            skipWs(i);
            if (i < j.size() && j[i] == '}') { i++; break; }

            size_t q1 = j.find('"', i);
            if (q1 == std::string::npos) break;
            size_t q2 = j.find('"', q1 + 1);
            if (q2 == std::string::npos) break;

            std::string key = j.substr(q1 + 1, q2 - (q1 + 1));
            i = q2 + 1;

            if (!consume(i, ':')) break;
            skipWs(i);

            if (key == "color") {
                if (consume(i, '[')) {
                    int r = 0, g = 0, b = 0, a = 255;
                    parseInt(i, r); consume(i, ',');
                    parseInt(i, g); consume(i, ',');
                    parseInt(i, b); consume(i, ',');
                    parseInt(i, a);
                    consume(i, ']');
                    color = SDL_Color{ (Uint8)r,(Uint8)g,(Uint8)b,(Uint8)a };
                }
            }
            else if (key == "thickness") {
                float th = 2.f;
                parseFloat(i, th);
                thickness = th;
            }
            else if (key == "points") {
                if (consume(i, '[')) {
                    while (i < j.size()) {
                        skipWs(i);
                        if (i < j.size() && j[i] == ']') { i++; break; }
                        if (!consume(i, '[')) break;

                        float x = 0, y = 0;
                        if (!parseFloat(i, x)) break;
                        consume(i, ',');
                        if (!parseFloat(i, y)) break;

                        skipWs(i);
                        if (consume(i, ',')) {
                            float tmp;
                            parseFloat(i, tmp);
                        }

                        consume(i, ']');
                        pts.push_back(SDL_FPoint{ x, y });

                        skipWs(i);
                        if (i < j.size() && j[i] == ',') i++;
                    }
                }
            }
            else {
                while (i < j.size() && j[i] != ',' && j[i] != '}') i++;
            }

            skipWs(i);
            if (i < j.size() && j[i] == ',') i++;
        }

        if (!pts.empty()) {
            pushSep();
            outStrokeColors.push_back(color);
            outStrokeThickness.push_back(thickness);
            for (auto& p : pts) outAllPointsWithSeparators.push_back(p);
        }

        skipWs(i);
        if (i < j.size() && j[i] == ',') i++;
    }

    return !outAllPointsWithSeparators.empty();
}

SDL_Texture* Launcher::buildPreviewTexture(SDL_Renderer* r, const std::string& frameFilePath, int outW, int outH) {
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, outW, outH);
    if (!tex) return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    SDL_Texture* prevTarget = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, tex);

    setDrawColor(r, SDL_Color{ 245,245,250,255 });
    SDL_Rect full{ 0,0,outW,outH };
    SDL_RenderFillRect(r, &full);

    setDrawColor(r, SDL_Color{ 0,0,0,18 });
    for (int x = 0; x < outW; x += 16) SDL_RenderDrawLine(r, x, 0, x, outH);
    for (int y = 0; y < outH; y += 16) SDL_RenderDrawLine(r, 0, y, outW, y);

    std::vector<SDL_FPoint> allPts;
    std::vector<SDL_Color> colors;
    std::vector<float> thicks;

    if (parseFrameStrokesFromFile(frameFilePath, allPts, colors, thicks)) {
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (auto& p : allPts) {
            if (!(p.x == p.x) || !(p.y == p.y)) continue;
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }

        if (minX < 1e8f && maxX > -1e8f) {
            float bw = std::max(1.0f, maxX - minX);
            float bh = std::max(1.0f, maxY - minY);

            float pad = 6.0f;
            float sx = (outW - pad * 2.0f) / bw;
            float sy = (outH - pad * 2.0f) / bh;
            float s = std::min(sx, sy);

            float ox = pad + (outW - pad * 2.0f - bw * s) * 0.5f;
            float oy = pad + (outH - pad * 2.0f - bh * s) * 0.5f;

            size_t strokeIndex = (size_t)-1;
            std::vector<SDL_FPoint> stroke;
            stroke.reserve(256);

            auto flush = [&]() {
                if (stroke.size() < 2) { stroke.clear(); return; }
                SDL_Color c = (strokeIndex < colors.size()) ? colors[strokeIndex] : SDL_Color{ 10,10,20,255 };
                float th = (strokeIndex < thicks.size()) ? thicks[strokeIndex] : 2.0f;

                setDrawColor(r, c);
                drawPolylineThick(r, stroke, std::max(1.0f, th * 0.35f));
                stroke.clear();
                };

            for (auto& p : allPts) {
                if (!(p.x == p.x) || !(p.y == p.y)) {
                    flush();
                    strokeIndex++;
                    continue;
                }
                SDL_FPoint q;
                q.x = ox + (p.x - minX) * s;
                q.y = oy + (p.y - minY) * s;
                stroke.push_back(q);
            }
            flush();
        }
    }

    SDL_SetRenderTarget(r, prevTarget);
    return tex;
}

bool Launcher::loadPreviewFromDisk(SDL_Renderer* r, ProjectItem& it) {
    auto& pc = previews[it.path];
    if (pc.diskBmpPath.empty()) pc.diskBmpPath = (fs::path(it.path) / ".cache" / "launcher_preview.bmp").string();
    if (pc.diskKeyPath.empty()) pc.diskKeyPath = (fs::path(it.path) / ".cache" / "launcher_preview.key").string();

    if (!fs::exists(pc.diskBmpPath) || !fs::exists(pc.diskKeyPath)) return false;

    std::string keyStr;
    if (!readTextFile(pc.diskKeyPath, keyStr)) return false;
    uint64_t diskKey = 0;
    try { diskKey = (uint64_t)std::stoull(trimCopyLauncher(keyStr)); } catch (...) { return false; }

    uint64_t key = getFolderWriteKey(it.path);
    std::string frameFile = findFirstFrameJson(it.path);
    if (diskKey != key || frameFile.empty()) return false;

    if (pc.tex) { SDL_DestroyTexture(pc.tex); pc.tex = nullptr; }
    pc.tex = loadBmpTextureLauncher(r, pc.diskBmpPath, &pc.w, &pc.h);
    if (!pc.tex) return false;

    pc.lastWriteKey = key;
    pc.frameFileUsed = frameFile;
    pc.attempted = true;
    pc.diskReady = true;
    return true;
}

bool Launcher::savePreviewToDisk(SDL_Renderer* r, const PreviewCache& pc) {
    if (!pc.tex || pc.diskBmpPath.empty()) return false;
    if (!saveTextureToBmpLauncher(r, pc.tex, pc.diskBmpPath)) return false;
    std::ofstream out(pc.diskKeyPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << pc.lastWriteKey;
    return true;
}

void Launcher::ensurePreview(SDL_Renderer* r, ProjectItem& it) {
    auto& pc = previews[it.path];

    if (pc.diskBmpPath.empty()) pc.diskBmpPath = (fs::path(it.path) / ".cache" / "launcher_preview.bmp").string();
    if (pc.diskKeyPath.empty()) pc.diskKeyPath = (fs::path(it.path) / ".cache" / "launcher_preview.key").string();

    uint64_t key = getFolderWriteKey(it.path);
    std::string frameFile = findFirstFrameJson(it.path);

    if (pc.tex && pc.lastWriteKey == key && pc.frameFileUsed == frameFile) return;

    if (loadPreviewFromDisk(r, it)) return;

    pc.lastWriteKey = key;
    pc.frameFileUsed = frameFile;

    if (pc.tex) { SDL_DestroyTexture(pc.tex); pc.tex = nullptr; }

    if (frameFile.empty()) {
        pc.attempted = true;
        return;
    }

    pc.tex = buildPreviewTexture(r, frameFile, 220, 74);
    pc.w = 220;
    pc.h = 74;
    pc.attempted = true;
    pc.diskReady = savePreviewToDisk(r, pc);
}

void Launcher::openActionsMenu(int projectIndex, int mx, int my) {
    menuOpen = true;
    menuProjectIndex = projectIndex;

    int w = 220;
    int h = 3 * 40 + 18;
    menuRect = SDL_Rect{ mx, my, w, h };

    if (lastW > 0 && lastH > 0) {
        if (menuRect.x + menuRect.w > lastW) menuRect.x = lastW - menuRect.w - 10;
        if (menuRect.y + menuRect.h > lastH) menuRect.y = lastH - menuRect.h - 10;
        menuRect.x = std::max(menuRect.x, 10);
        menuRect.y = std::max(menuRect.y, 10);
    }

    menuItemRename = SDL_Rect{ menuRect.x + 10, menuRect.y + 10,        menuRect.w - 20, 36 };
    menuItemDelete = SDL_Rect{ menuRect.x + 10, menuRect.y + 10 + 40,   menuRect.w - 20, 36 };
    menuItemReveal = SDL_Rect{ menuRect.x + 10, menuRect.y + 10 + 80,   menuRect.w - 20, 36 };
}

void Launcher::closeActionsMenu() {
    menuOpen = false;
    menuProjectIndex = -1;
}

bool Launcher::deleteProjectFolder(const std::string& folderPath) {
    std::error_code ec;
    fs::remove_all(folderPath, ec);
    return !ec;
}

void Launcher::renameCancel() {
    renaming = false;
    renameProjectIndex = -1;
    renameBuffer.clear();
}

bool Launcher::renameCommit() {
    if (!renaming) return false;
    if (renameProjectIndex < 0 || renameProjectIndex >= (int)projects.size()) return false;

    std::string newNameTrim = renameBuffer;
    while (!newNameTrim.empty() && std::isspace((unsigned char)newNameTrim.front())) newNameTrim.erase(newNameTrim.begin());
    while (!newNameTrim.empty() && std::isspace((unsigned char)newNameTrim.back())) newNameTrim.pop_back();
    if (newNameTrim.empty()) return false;

    ProjectItem& it = projects[renameProjectIndex];
    it.name = newNameTrim;

    fs::path pj = fs::path(it.path) / "project.json";
    std::string j;
    if (readTextFile(pj.string(), j)) {
        size_t k = j.find("\"name\"");
        if (k != std::string::npos) {
            size_t colon = j.find(':', k);
            if (colon != std::string::npos) {
                size_t q1 = j.find('"', colon);
                if (q1 != std::string::npos) {
                    size_t q2 = j.find('"', q1 + 1);
                    if (q2 != std::string::npos) {
                        j.replace(q1 + 1, (q2 - (q1 + 1)), newNameTrim);
                        std::ofstream f(pj, std::ios::binary);
                        if (f) f << j;
                    }
                }
            }
        }
    }

    renameCancel();
    return true;
}

void Launcher::performMenuAction(MenuAction a) {
    if (menuProjectIndex < 0 || menuProjectIndex >= (int)projects.size()) return;
    ProjectItem& it = projects[menuProjectIndex];

    if (a == MenuAction::Rename) {
        renaming = true;
        renameProjectIndex = menuProjectIndex;
        renameBuffer = it.name;
        searchFocused = false;
        closeActionsMenu();
        return;
    }

    if (a == MenuAction::Delete) {
        closeActionsMenu();

        const SDL_MessageBoxButtonData buttons[] = {
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Delete" },
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
        };
        SDL_MessageBoxData data{};
        data.flags = SDL_MESSAGEBOX_WARNING;
        data.window = nullptr;
        data.title = "Delete Project";
        data.message = "This will permanently delete the project folder.\nAre you sure?";
        data.numbuttons = 2;
        data.buttons = buttons;

        int btnid = 0;
        SDL_ShowMessageBox(&data, &btnid);

        if (btnid == 1) {
            deleteProjectFolder(it.path);
            refreshProjects();
        }
        return;
    }

    if (a == MenuAction::Reveal) {
        closeActionsMenu();
#ifdef _WIN32
        std::wstring wp(it.path.begin(), it.path.end());
        ShellExecuteW(nullptr, L"open", wp.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
#endif
        return;
    }
}

bool Launcher::isDigitString(const std::string& s) const {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit((unsigned char)c)) return false;
    return true;
}

void Launcher::openNewProjectModal() {
    newModalOpen = true;
    newFocus = NewField::Name;
    newName = "Untitled";
    newWidthStr = "1920";
    newHeightStr = "1080";
    newFpsStr = "30";
}

void Launcher::closeNewProjectModal() {
    newModalOpen = false;
    viewMode = ViewMode::Home;
    newFocus = NewField::None;
}

void Launcher::commitNewProjectModal() {
    if (newName.empty()) newName = "Untitled";
    if (!isDigitString(newWidthStr) || !isDigitString(newHeightStr) || !isDigitString(newFpsStr)) return;

    int w = std::stoi(newWidthStr);
    int h = std::stoi(newHeightStr);
    int fps = std::stoi(newFpsStr);

    w = std::max(1, w);
    h = std::max(1, h);
    fps = std::max(1, fps);

    requestedNewName = newName;
    requestedWidth = w;
    requestedHeight = h;
    requestedFPS = fps;

    std::string createdPath;
    if (createNewProjectOnDisk(newName, w, h, fps, createdPath)) {
        refreshProjects();

        requestNewProject = true;
        requestOpenProject = true;

        requestedProjectPath = createdPath;
        closeNewProjectModal();
    }
}

void Launcher::drawNewProjectModal(SDL_Renderer* r, int w, int h) {
    
    SDL_Rect overlay{ 0,0,w,h };
    setDrawColor(r, SDL_Color{ 0,0,0,160 });
    SDL_RenderFillRect(r, &overlay);

    
    int mw = 560;
    int mh = 340;
    SDL_Rect card{ (w - mw) / 2, (h - mh) / 2, mw, mh };
    drawCard(r, card, 18, SDL_Color{ 14,16,28,250 }, SDL_Color{ 100,120,255,70 }, true);

    auto drawLabel = [&](const char* txt, int x, int y, SDL_Color col = SDL_Color{ 235,235,245,255 }) {
        int tw = 0, th = 0;
        SDL_Texture* t = makeText(r, txt, col, &tw, &th);
        if (!t) return;
        SDL_Rect dst{ x, y, tw, th };
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        };

    auto drawField = [&](SDL_Rect& rc, int x, int y, int ww, const std::string& value, bool focused) {
        rc = SDL_Rect{ x, y, ww, 40 };
        SDL_Color fill = focused ? SDL_Color{ 18,22,42,255 } : SDL_Color{ 12,14,28,255 };
        SDL_Color border = focused ? SDL_Color{ 120,160,255,120 } : SDL_Color{ 90,100,140,80 };
        drawCard(r, rc, 12, fill, border, false);

        std::string v = value;
        if (focused) v += "|";

        int tw = 0, th = 0;
        SDL_Texture* t = makeText(r, v, SDL_Color{ 240,240,245,255 }, &tw, &th);
        if (t) {
            SDL_Rect dst{ rc.x + 12, rc.y + (rc.h - th) / 2, tw, th };
            SDL_RenderCopy(r, t, nullptr, &dst);
            SDL_DestroyTexture(t);
        }
        };

    drawLabel("New Project", card.x + 24, card.y + 18, SDL_Color{ 245,245,255,255 });
    drawLabel("Create a fresh canvas. You can change settings later.", card.x + 24, card.y + 44, SDL_Color{ 190,195,215,255 });

    int left = card.x + 24;
    int top = card.y + 84;

    drawLabel("Name", left, top);
    drawField(newNameRect, left, top + 22, mw - 48, newName, newFocus == NewField::Name);

    int rowY = top + 92;
    drawLabel("Width", left, rowY);
    drawField(newWRect, left, rowY + 22, 150, newWidthStr, newFocus == NewField::Width);

    drawLabel("Height", left + 190, rowY);
    drawField(newHRect, left + 190, rowY + 22, 150, newHeightStr, newFocus == NewField::Height);

    drawLabel("FPS", left + 380, rowY);
    drawField(newFpsRect, left + 380, rowY + 22, 150, newFpsStr, newFocus == NewField::FPS);

    newCreateBtn = SDL_Rect{ card.x + mw - 24 - 160, card.y + mh - 24 - 46, 160, 46 };
    newCancelBtn = SDL_Rect{ card.x + mw - 24 - 160 - 14 - 120, card.y + mh - 24 - 46, 120, 46 };

    
    drawCard(r, newCancelBtn, 14, SDL_Color{ 18,20,38,255 }, SDL_Color{ 90,100,140,80 }, false);
    drawCard(r, newCreateBtn, 14, SDL_Color{ 40,70,200,255 }, SDL_Color{ 120,170,255,110 }, false);

    drawLabel("Cancel", newCancelBtn.x + 34, newCancelBtn.y + 13);
    drawLabel("Create", newCreateBtn.x + 54, newCreateBtn.y + 13);
}


void Launcher::handleEvent(SDL_Event& e, int mx, int my) {
    if (lastW <= 0 || lastH <= 0) return;

    if (e.type == SDL_MOUSEMOTION) {
        hoverIndex = -1;
        for (int i = 0; i < (int)cardRects.size(); ++i) {
            if (pointInRect(mx, my, cardRects[i])) { hoverIndex = i; break; }
        }
    }

    if (newModalOpen) {
        if (e.type == SDL_TEXTINPUT) {
            std::string add = e.text.text;

            if (newFocus == NewField::Name) newName += add;
            if (newFocus == NewField::Width) { for (char c : add) if (std::isdigit((unsigned char)c)) newWidthStr += c; }
            if (newFocus == NewField::Height) { for (char c : add) if (std::isdigit((unsigned char)c)) newHeightStr += c; }
            if (newFocus == NewField::FPS) { for (char c : add) if (std::isdigit((unsigned char)c)) newFpsStr += c; }
            return;
        }
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.sym == SDLK_BACKSPACE) {
                auto pop = [&](std::string& s) { if (!s.empty()) s.pop_back(); };
                if (newFocus == NewField::Name) pop(newName);
                if (newFocus == NewField::Width) pop(newWidthStr);
                if (newFocus == NewField::Height) pop(newHeightStr);
                if (newFocus == NewField::FPS) pop(newFpsStr);
                return;
            }
            if (e.key.keysym.sym == SDLK_RETURN) {
                commitNewProjectModal();
                return;
            }
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                closeNewProjectModal();
                return;
            }
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            if (pointInRect(mx, my, newNameRect)) newFocus = NewField::Name;
            else if (pointInRect(mx, my, newWRect)) newFocus = NewField::Width;
            else if (pointInRect(mx, my, newHRect)) newFocus = NewField::Height;
            else if (pointInRect(mx, my, newFpsRect)) newFocus = NewField::FPS;
            else if (pointInRect(mx, my, newCancelBtn)) closeNewProjectModal();
            else if (pointInRect(mx, my, newCreateBtn)) commitNewProjectModal();
            else {
            }
            return;
        }
        return;
    }

    if (e.type == SDL_TEXTINPUT) {
        if (renaming) { renameBuffer += e.text.text; return; }
        if (searchFocused) { searchQuery += e.text.text; return; }
    }

    if (e.type == SDL_KEYDOWN) {
        if (renaming) {
            if (e.key.keysym.sym == SDLK_BACKSPACE) { if (!renameBuffer.empty()) renameBuffer.pop_back(); return; }
            if (e.key.keysym.sym == SDLK_RETURN) { renameCommit(); return; }
            if (e.key.keysym.sym == SDLK_ESCAPE) { renameCancel(); return; }
        }
        if (searchFocused) {
            if (e.key.keysym.sym == SDLK_BACKSPACE) { if (!searchQuery.empty()) searchQuery.pop_back(); return; }
            if (e.key.keysym.sym == SDLK_ESCAPE) { searchFocused = false; return; }
        }
    }

    if (e.type == SDL_MOUSEBUTTONDOWN) {
        if (e.button.button == SDL_BUTTON_RIGHT) {
            for (int i = 0; i < (int)cardRects.size() && i < (int)projects.size(); ++i) {
                if (pointInRect(mx, my, cardRects[i])) { openActionsMenu(i, mx, my); return; }
            }
            closeActionsMenu();
        }

        if (e.button.button == SDL_BUTTON_LEFT) {
            if (menuOpen) {
                if (pointInRect(mx, my, menuItemRename)) { performMenuAction(MenuAction::Rename); return; }
                if (pointInRect(mx, my, menuItemDelete)) { performMenuAction(MenuAction::Delete); return; }
                if (pointInRect(mx, my, menuItemReveal)) { performMenuAction(MenuAction::Reveal); return; }
                if (!pointInRect(mx, my, menuRect)) closeActionsMenu();
            }

            searchFocused = pointInRect(mx, my, searchRect);

            if (pointInRect(mx, my, sideProjectsBtn) || pointInRect(mx, my, sideRecentBtn) || pointInRect(mx, my, sideTemplatesBtn)) {
                viewMode = ViewMode::Home;
                return;
            }
            if (pointInRect(mx, my, sideDeveloperToolsBtn)) {
                viewMode = ViewMode::DeveloperTools;
                return;
            }
            if (pointInRect(mx, my, sideSettingsBtn)) {
                viewMode = ViewMode::Settings;
                return;
            }

            if (viewMode == ViewMode::Settings) {
                if (pointInRect(mx, my, settingsBackBtn)) {
                    viewMode = ViewMode::Home;
                    return;
                }
                if (pointInRect(mx, my, updateToggleTrack) || pointInRect(mx, my, updateToggleKnob)) {
                    updateChecksEnabled = !updateChecksEnabled;
                    updateChecksDirty = true;
                    strova::debug::log("Launcher", std::string("Settings toggle clicked. updateChecksEnabled=") + (updateChecksEnabled ? "true" : "false"));
                    return;
                }
                if (pointInRect(mx, my, dockingToggleTrack) || pointInRect(mx, my, dockingToggleKnob)) {
                    persistentDockingEnabled = !persistentDockingEnabled;
                    persistentDockingDirty = true;
                    strova::debug::log("Launcher", std::string("Settings toggle clicked. persistentDockingEnabled=") + (persistentDockingEnabled ? "true" : "false"));
                    return;
                }
                return;
            }

            if (viewMode == ViewMode::DeveloperTools) {
                if (pointInRect(mx, my, btnBrushCreator)) {
                    requestOpenBrushCreator = true;
                    requestedBrushProjectPath.clear();
                    return;
                }
                if (pointInRect(mx, my, btnOpenBrushProject)) {
                    requestOpenBrushProject = true;
                    return;
                }
                return;
            }

            if (pointInRect(mx, my, btnNew)) {
                openNewProjectModal();
                return;
            }
            if (pointInRect(mx, my, btnOpen)) {
                requestOpenProject = true;
                requestedProjectPath.clear();
                return;
            }
            if (pointInRect(mx, my, btnFolder)) {
                requestOpenFolder = true;
                return;
            }
            if (pointInRect(mx, my, btnBrushCreator)) {
                requestOpenBrushCreator = true;
                requestedBrushProjectPath.clear();
                return;
            }
            if (pointInRect(mx, my, btnOpenBrushProject)) {
                requestOpenBrushProject = true;
                return;
            }

            for (int i = 0; i < (int)cardRects.size() && i < (int)projects.size(); ++i) {
                if (pointInRect(mx, my, cardRects[i])) {
                    requestOpenProject = true;
                    requestedProjectPath = projects[i].path;
                    return;
                }
            }
        }
    }
}

void Launcher::render(SDL_Renderer* r, int w, int h) {
    lastRenderer = r;
    lastW = w;
    lastH = h;

    ensureLogo(r);

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    
    SDL_Rect full{ 0,0,w,h };
    fillVerticalGradient(r, full, SDL_Color{ 10,12,28,255 }, SDL_Color{ 4,4,12,255 });

    
    SDL_Rect topGlow{ 0,0,w,140 };
    fillVerticalGradient(r, topGlow, SDL_Color{ 40,70,200,30 }, SDL_Color{ 0,0,0,0 });

    
    int sidebarW = (int)(w * 0.20f);
    sidebarW = std::max(180, std::min(sidebarW, 280));
    SDL_Rect sidebar{ 22, 18, sidebarW, h - 36 };

    
    drawCard(r, sidebar, 18, SDL_Color{ 10,12,22,210 }, SDL_Color{ 255,255,255,25 }, true);

    int sx = sidebar.x + 16;
    int sy = sidebar.y + 16;

    
    if (logoTex) {
        int targetH = 34;
        float s = (logoH > 0) ? (float)targetH / (float)logoH : 1.0f;
        int targetW = (int)std::round((float)logoW * s);
        SDL_Rect dst{ sx, sy, targetW, targetH };
        SDL_RenderCopy(r, logoTex, nullptr, &dst);
        sy += targetH + 14;
    }
    else {
        int tw = 0, th = 0;
        if (SDL_Texture* t = makeText(r, "STROVA", SDL_Color{ 245,245,255,255 }, &tw, &th)) {
            SDL_Rect dst{ sx, sy, tw, th };
            SDL_RenderCopy(r, t, nullptr, &dst);
            SDL_DestroyTexture(t);
        }
        sy += 30;
    }

    
    searchRect = SDL_Rect{ sx, sy, sidebarW - 32, 42 };
    SDL_Color sFill = searchFocused ? SDL_Color{ 18,22,42,255 } : SDL_Color{ 12,14,28,255 };
    SDL_Color sBorder = searchFocused ? SDL_Color{ 120,160,255,110 } : SDL_Color{ 90,100,140,70 };
    drawCard(r, searchRect, 14, sFill, sBorder, false);

    int tw = 0, th = 0;
    std::string searchLabel = searchQuery.empty() ? "Search projects..." : searchQuery;
    SDL_Color searchCol = searchQuery.empty() ? SDL_Color{ 165,170,190,255 } : SDL_Color{ 235,235,245,255 };
    if (SDL_Texture* st = makeText(r, searchLabel, searchCol, &tw, &th)) {
        SDL_Rect dst{ searchRect.x + 12, searchRect.y + (searchRect.h - th) / 2, tw, th };
        SDL_RenderCopy(r, st, nullptr, &dst);
        SDL_DestroyTexture(st);
    }

    sy += 60;

    auto drawSideBtn = [&](SDL_Rect& outRc, int y, const char* label) {
        outRc = SDL_Rect{ sx, y, sidebarW - 32, 46 };
        int mx, my; SDL_GetMouseState(&mx, &my);
        bool hv = (mx >= outRc.x && mx < outRc.x + outRc.w && my >= outRc.y && my < outRc.y + outRc.h);

        SDL_Color fill = hv ? SDL_Color{ 20,28,60,255 } : SDL_Color{ 14,16,30,255 };
        SDL_Color border = hv ? SDL_Color{ 120,160,255,85 } : SDL_Color{ 255,255,255,24 };
        drawCard(r, outRc, 14, fill, border, false);

        int tw2 = 0, th2 = 0;
        if (SDL_Texture* t = makeText(r, label, SDL_Color{ 235,235,245,255 }, &tw2, &th2)) {
            SDL_Rect dst{ outRc.x + 14, outRc.y + (outRc.h - th2) / 2, tw2, th2 };
            SDL_RenderCopy(r, t, nullptr, &dst);
            SDL_DestroyTexture(t);
        }
        };

    drawSideBtn(sideProjectsBtn, sidebar.y + 150, "Projects");
    drawSideBtn(sideRecentBtn, sidebar.y + 206, "Recent");
    drawSideBtn(sideTemplatesBtn, sidebar.y + 262, "Templates");
    drawSideBtn(sideDeveloperToolsBtn, sidebar.y + 318, "Developer Tools");
    drawSideBtn(sideSettingsBtn, sidebar.y + sidebar.h - 62, "Settings");

    
    int contentX = sidebar.x + sidebar.w + 22;
    int contentY = 18;
    int contentW = w - contentX - 22;
    int contentH = h - 36;
    SDL_Rect content{ contentX, contentY, contentW, contentH };

    
    drawCard(r, content, 18, SDL_Color{ 10,12,22,130 }, SDL_Color{ 255,255,255,18 }, true);

    auto drawScaled = [&](const std::string& txt, int x, int y, float scale, SDL_Color col = SDL_Color{ 240,240,245,255 }) {
        int tw2 = 0, th2 = 0;
        SDL_Texture* t = makeText(r, txt, col, &tw2, &th2);
        if (!t) return;
        SDL_Rect dst{ x, y, (int)(tw2 * scale), (int)(th2 * scale) };
        SDL_RenderCopy(r, t, nullptr, &dst);
        SDL_DestroyTexture(t);
        };

    int pad = 22;
    int cx = content.x + pad;
    int cy = content.y + pad;

    clearCardRects();

    if (viewMode == ViewMode::DeveloperTools) {
        drawScaled("Developer Tools", cx, cy, 1.45f, SDL_Color{ 245,245,255,255 });
        cy += 40;
        drawScaled("Build and manage local creation tools without opening the editor.", cx, cy, 1.0f, SDL_Color{ 185,190,210,255 });
        cy += 34;

        SDL_Rect brushCard{ cx, cy, 280, 160 };
        SDL_Rect projectCard{ cx + 298, cy, 280, 160 };
        SDL_Rect utilCard{ cx + 596, cy, 280, 160 };
        SDL_Rect validateCard{ cx, cy + 178, 280, 120 };
        drawCard(r, brushCard, 18, SDL_Color{ 18,24,42,255 }, SDL_Color{ 120,160,255,70 }, false);
        drawCard(r, projectCard, 18, SDL_Color{ 16,20,34,255 }, SDL_Color{ 255,255,255,24 }, false);
        drawCard(r, utilCard, 18, SDL_Color{ 16,20,34,255 }, SDL_Color{ 255,255,255,24 }, false);
        drawCard(r, validateCard, 18, SDL_Color{ 16,20,34,255 }, SDL_Color{ 255,255,255,24 }, false);

        btnBrushCreator = SDL_Rect{ brushCard.x + 16, brushCard.y + 108, 124, 34 };
        btnOpenBrushProject = SDL_Rect{ brushCard.x + 148, brushCard.y + 108, 116, 34 };
        drawScaled("Brush Creator", brushCard.x + 16, brushCard.y + 18, 1.05f, SDL_Color{ 245,245,255,255 });
        drawScaled("Dedicated brush authoring workspace with test canvas and packaging.", brushCard.x + 16, brushCard.y + 52, 0.92f, SDL_Color{ 185,190,210,255 });
        drawCard(r, btnBrushCreator, 14, SDL_Color{ 32,84,196,255 }, SDL_Color{ 120,160,255,120 }, false);
        drawCard(r, btnOpenBrushProject, 14, SDL_Color{ 20,28,60,255 }, SDL_Color{ 120,160,255,85 }, false);
        drawScaled("Open Creator", btnBrushCreator.x + 10, btnBrushCreator.y + 8, 0.92f);
        drawScaled("Open Project", btnOpenBrushProject.x + 10, btnOpenBrushProject.y + 8, 0.92f);

        drawScaled("Tool Creator", projectCard.x + 16, projectCard.y + 18, 1.05f, SDL_Color{ 245,245,255,255 });
        drawScaled("Future placeholder for custom local tools.", projectCard.x + 16, projectCard.y + 52, 0.92f, SDL_Color{ 165,170,190,255 });
        drawScaled("Coming later", projectCard.x + 16, projectCard.y + 116, 0.95f, SDL_Color{ 165,170,190,255 });

        drawScaled("Asset Utilities", utilCard.x + 16, utilCard.y + 18, 1.05f, SDL_Color{ 245,245,255,255 });
        drawScaled("Future placeholder for local asset conversion utilities.", utilCard.x + 16, utilCard.y + 52, 0.92f, SDL_Color{ 165,170,190,255 });
        drawScaled("Coming later", utilCard.x + 16, utilCard.y + 116, 0.95f, SDL_Color{ 165,170,190,255 });

        drawScaled("Validation Tools", validateCard.x + 16, validateCard.y + 18, 1.05f, SDL_Color{ 245,245,255,255 });
        drawScaled("Future placeholder for local validation helpers.", validateCard.x + 16, validateCard.y + 52, 0.92f, SDL_Color{ 165,170,190,255 });
        return;
    }

    if (viewMode == ViewMode::Settings) {
        drawScaled("Settings", cx, cy, 1.45f, SDL_Color{ 245,245,255,255 });
        cy += 40;
        drawScaled("Control launcher behavior and update checks.", cx, cy, 1.0f, SDL_Color{ 185,190,210,255 });
        cy += 34;

        settingsBackBtn = SDL_Rect{ cx, cy, 140, 42 };
        drawCard(r, settingsBackBtn, 14, SDL_Color{ 18,20,38,255 }, SDL_Color{ 255,255,255,24 }, false);
        drawScaled("Back", settingsBackBtn.x + 50, settingsBackBtn.y + 10, 1.0f, SDL_Color{ 240,240,245,255 });
        cy += 64;

        SDL_Rect cardRc{ cx, cy, std::max(420, contentW - pad * 2), 112 };
        drawCard(r, cardRc, 18, SDL_Color{ 12,14,26,230 }, SDL_Color{ 255,255,255,22 }, true);
        drawScaled("Update Check Daily", cardRc.x + 20, cardRc.y + 22, 1.08f, SDL_Color{ 245,245,255,255 });
        drawScaled("When off, the app will not contact the update endpoint at startup.", cardRc.x + 20, cardRc.y + 56, 0.95f, SDL_Color{ 180,185,205,255 });

        updateToggleTrack = SDL_Rect{ cardRc.x + cardRc.w - 88, cardRc.y + 30, 56, 28 };
        drawCard(r, updateToggleTrack, 14, updateChecksEnabled ? SDL_Color{ 46,130,255,255 } : SDL_Color{ 60,64,84,255 }, SDL_Color{ 255,255,255,18 }, false);
        updateToggleKnob = SDL_Rect{ updateChecksEnabled ? (updateToggleTrack.x + updateToggleTrack.w - 26) : (updateToggleTrack.x + 2), updateToggleTrack.y + 2, 24, 24 };
        drawCard(r, updateToggleKnob, 12, SDL_Color{ 245,245,250,255 }, SDL_Color{ 0,0,0,30 }, false);

        cy += 132;
        SDL_Rect dockRc{ cx, cy, std::max(420, contentW - pad * 2), 112 };
        drawCard(r, dockRc, 18, SDL_Color{ 12,14,26,230 }, SDL_Color{ 255,255,255,22 }, true);
        drawScaled("Persistent Dock Layout", dockRc.x + 20, dockRc.y + 22, 1.08f, SDL_Color{ 245,245,255,255 });
        drawScaled(persistentDockingEnabled ? "On: save one global layout for every project." : "Off: save a separate ui_layout.json inside each project.", dockRc.x + 20, dockRc.y + 56, 0.95f, SDL_Color{ 180,185,205,255 });

        dockingToggleTrack = SDL_Rect{ dockRc.x + dockRc.w - 88, dockRc.y + 30, 56, 28 };
        drawCard(r, dockingToggleTrack, 14, persistentDockingEnabled ? SDL_Color{ 46,130,255,255 } : SDL_Color{ 60,64,84,255 }, SDL_Color{ 255,255,255,18 }, false);
        dockingToggleKnob = SDL_Rect{ persistentDockingEnabled ? (dockingToggleTrack.x + dockingToggleTrack.w - 26) : (dockingToggleTrack.x + 2), dockingToggleTrack.y + 2, 24, 24 };
        drawCard(r, dockingToggleKnob, 12, SDL_Color{ 245,245,250,255 }, SDL_Color{ 0,0,0,30 }, false);

        cy += 132;
        SDL_Rect versionRc{ cx, cy, std::max(420, contentW - pad * 2), 78 };
        drawCard(r, versionRc, 18, SDL_Color{ 12,14,26,230 }, SDL_Color{ 255,255,255,22 }, true);
        drawScaled("Version", versionRc.x + 20, versionRc.y + 18, 1.00f, SDL_Color{ 185,190,210,255 });
        drawScaled("1.0.5", versionRc.x + 20, versionRc.y + 42, 1.18f, SDL_Color{ 245,245,255,255 });
    } else {
        
        drawScaled(greetingFromLocalTime(), cx, cy, 1.55f, SDL_Color{ 245,245,255,255 });
        cy += 36;
        drawScaled("Pick up where you left off  -  or start something new.", cx, cy, 1.0f, SDL_Color{ 185,190,210,255 });
        cy += 34;

        
        drawScaled("Recent Projects", cx, cy, 1.20f, SDL_Color{ 235,235,245,255 });
        cy += 34;

        for (auto& it : projects) {
            uint64_t k = getFolderWriteKey(it.path);
            if (k != it.lastWriteKey) {
                refreshProjectMeta(it);
            }
            ensurePreview(r, it);
        }
        sortProjects();

        int maxCards = std::min(3, (int)projects.size());
        int gap = 18;
        int cardW = (contentW - pad * 2 - gap * 2) / 3;
        cardW = std::max(240, cardW);
        int cardH = 210;

        int cardY = cy;

        for (int i = 0; i < maxCards; ++i) {
            ProjectItem& it = projects[i];

            int x = cx + i * (cardW + gap);
            SDL_Rect cardRc{ x, cardY, cardW, cardH };
            cardRects.push_back(cardRc);

            bool hover = (hoverIndex == i);
            SDL_Color cardFill = hover ? SDL_Color{ 14,18,34,240 } : SDL_Color{ 12,14,26,230 };
            SDL_Color cardBorder = hover ? SDL_Color{ 120,160,255,95 } : SDL_Color{ 255,255,255,20 };
            drawCard(r, cardRc, 18, cardFill, cardBorder, true);

            SDL_Rect prev{ cardRc.x + 16, cardRc.y + 16, cardRc.w - 32, 84 };
            drawCard(r, prev, 14, SDL_Color{ 245,245,250,255 }, SDL_Color{ 0,0,0,30 }, false);

            ensurePreview(r, it);
            auto itPrev = previews.find(it.path);
            if (itPrev != previews.end() && itPrev->second.tex) {
                SDL_RenderCopy(r, itPrev->second.tex, nullptr, &prev);
            }
            else {
                int tw3 = 0, th3 = 0;
                SDL_Texture* pv = makeText(r, "NO PREVIEW", SDL_Color{ 20,20,40,255 }, &tw3, &th3);
                if (pv) {
                    SDL_Rect dst{ prev.x + (prev.w - tw3) / 2, prev.y + (prev.h - th3) / 2, tw3, th3 };
                    SDL_RenderCopy(r, pv, nullptr, &dst);
                    SDL_DestroyTexture(pv);
                }
            }

            std::string title = it.name;
            if (renaming && renameProjectIndex == i) title = renameBuffer + "|";

            drawScaled(title, cardRc.x + 16, cardRc.y + 114, 1.08f, SDL_Color{ 245,245,255,255 });

            std::string meta = std::to_string(it.fps) + " FPS    |    " + std::to_string(it.frameCount) + " Frames";
            drawScaled(meta, cardRc.x + 16, cardRc.y + 146, 1.0f, SDL_Color{ 180,185,205,255 });

            SDL_Rect accent{ cardRc.x + 16, cardRc.y + cardRc.h - 18, cardRc.w - 32, 3 };
            fillRoundedRect(r, accent, 2, hover ? SDL_Color{ 120,160,255,120 } : SDL_Color{ 255,255,255,35 });
        }

        for (size_t i = maxCards; i < projects.size(); ++i) {
            auto itPrev = previews.find(projects[i].path);
            if (itPrev != previews.end() && itPrev->second.tex) {
                SDL_DestroyTexture(itPrev->second.tex);
                itPrev->second.tex = nullptr;
            }
        }

        int quickY = cardY + cardH + 26;
        drawScaled("Quick Actions", cx, quickY, 1.10f, SDL_Color{ 235,235,245,255 });

        const int quickGap = 18;
        const int quickBtnW = std::max(150, std::min(190, (contentW - pad * 2 - quickGap * 3) / 4));
        auto drawQuickBtn = [&](SDL_Rect& outRc, int x, int y, const char* label) {
            outRc = SDL_Rect{ x, y, quickBtnW, 48 };
            int mx, my; SDL_GetMouseState(&mx, &my);
            bool hv = pointInRect(mx, my, outRc);

            SDL_Color fill = hv ? SDL_Color{ 40,70,200,255 } : SDL_Color{ 18,20,38,255 };
            SDL_Color border = hv ? SDL_Color{ 120,170,255,110 } : SDL_Color{ 255,255,255,22 };
            drawCard(r, outRc, 16, fill, border, false);

            int tw2 = 0, th2 = 0;
            if (SDL_Texture* tx = makeText(r, label, SDL_Color{ 240,240,245,255 }, &tw2, &th2)) {
                SDL_Rect dst{ outRc.x + (outRc.w - tw2) / 2, outRc.y + (outRc.h - th2) / 2, tw2, th2 };
                SDL_RenderCopy(r, tx, nullptr, &dst);
                SDL_DestroyTexture(tx);
            }
        };

        int btnY = quickY + 42;
        drawQuickBtn(btnNew, cx, btnY, "New Project");
        drawQuickBtn(btnOpen, cx + (quickBtnW + quickGap), btnY, "Open Project");
        drawQuickBtn(btnFolder, cx + 2 * (quickBtnW + quickGap), btnY, "Open Folder");
        drawQuickBtn(btnBrushCreator, cx + 3 * (quickBtnW + quickGap), btnY, "Brush Creator");
        drawQuickBtn(btnOpenBrushProject, cx, btnY + 60, "Open Brush Project");
    }

    
    if (menuOpen) {
        drawCard(r, menuRect, 16, SDL_Color{ 14,16,28,250 }, SDL_Color{ 120,160,255,70 }, true);

        auto drawMenuItem = [&](SDL_Rect rc, const char* label) {
            int mx, my; SDL_GetMouseState(&mx, &my);
            bool hv = pointInRect(mx, my, rc);

            SDL_Color fill = hv ? SDL_Color{ 20,28,60,255 } : SDL_Color{ 12,14,28,255 };
            SDL_Color border = hv ? SDL_Color{ 120,160,255,85 } : SDL_Color{ 0,0,0,0 };
            drawCard(r, rc, 12, fill, border, false);

            int tw2 = 0, th2 = 0;
            if (SDL_Texture* t = makeText(r, label, SDL_Color{ 240,240,245,255 }, &tw2, &th2)) {
                SDL_Rect dst{ rc.x + 12, rc.y + (rc.h - th2) / 2, tw2, th2 };
                SDL_RenderCopy(r, t, nullptr, &dst);
                SDL_DestroyTexture(t);
            }
            };

        drawMenuItem(menuItemRename, "Rename");
        drawMenuItem(menuItemDelete, "Delete");
        drawMenuItem(menuItemReveal, "Reveal in Explorer");
    }

    
    if (newModalOpen) {
        drawNewProjectModal(r, w, h);
    }
}
