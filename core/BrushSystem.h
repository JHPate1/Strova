
#pragma once

#include <SDL.h>
#include <SDL_image.h>

#include "../platform/AppPaths.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace strova::brush
{
    enum class BrushType : int
    {
        RasterStamp = 0,
        Procedural = 1,
        ScriptedRaster = 2
    };

    enum class MaskSource : int
    {
        Alpha = 0,
        Luminance = 1,
        Darkness = 2
    };

    enum class RotationMode : int
    {
        Stroke = 0,
        Fixed = 1,
        Random = 2
    };

    enum class BlendMode : int
    {
        Normal = 0,
        Screen = 1,
        Additive = 2,
        Erase = 3,
        Multiply = 4,
        Overlay = 5
    };

    enum class GradientMode : int
    {
        None = 0,
        StrokeProgress = 1,
        Pressure = 2,
        Random = 3,
        Fixed = 4
    };

    enum class GeneratorType : int
    {
        HardCircle = 0,
        SoftCircle,
        Square,
        SoftSquare,
        SpeckleCluster,
        GrainyDisk,
        OvalTip,
        ChalkPatch,
        NoiseBlob
    };

    enum class ValidationSeverity : int
    {
        Info = 0,
        Warning,
        Error
    };

    struct ValidationMessage
    {
        ValidationSeverity severity = ValidationSeverity::Warning;
        std::string text;
    };

    struct ValidationReport
    {
        std::vector<ValidationMessage> messages;
        bool ok = true;

        void addInfo(const std::string& text)
        {
            messages.push_back({ ValidationSeverity::Info, text });
        }

        void addError(const std::string& text)
        {
            messages.push_back({ ValidationSeverity::Error, text });
            ok = false;
        }

        void addWarning(const std::string& text)
        {
            messages.push_back({ ValidationSeverity::Warning, text });
        }

        std::string summary() const
        {
            std::ostringstream ss;
            if (messages.empty())
            {
                ss << "Validation OK";
                return ss.str();
            }
            for (size_t i = 0; i < messages.size(); ++i)
            {
                const char* prefix = "[WARN] ";
                if (messages[i].severity == ValidationSeverity::Info) prefix = "[INFO] ";
                else if (messages[i].severity == ValidationSeverity::Error) prefix = "[ERROR] ";
                ss << prefix << messages[i].text;
                if (i + 1 < messages.size()) ss << "\n";
            }
            return ss.str();
        }
    };

    struct GradientStop
    {
        float pos = 0.0f;
        SDL_Color color{ 255, 255, 255, 255 };
    };

    struct BrushParams
    {
        float sizeDefault = 32.0f;
        float sizeMin = 1.0f;
        float sizeMax = 1024.0f;
        float spacing = 0.12f;
        float opacity = 1.0f;
        float flow = 1.0f;
        float scatter = 0.0f;
        float hardness = 0.85f;
        float pressureSize = 1.0f;
        float pressureOpacity = 0.5f;
        float pressureFlow = 0.25f;
        float jitterSize = 0.0f;
        float jitterOpacity = 0.0f;
        float jitterRotation = 0.0f;
        float spacingJitter = 0.0f;
        float smoothing = 0.0f;
        float fixedAngle = 0.0f;
        bool accumulate = true;
        RotationMode rotationMode = RotationMode::Stroke;
        BlendMode blendMode = BlendMode::Normal;
    };

    struct BrushColorOptions
    {
        MaskSource maskSource = MaskSource::Alpha;
        bool invertMask = false;
        bool supportsUserColor = true;
        bool supportsGradient = true;
        GradientMode gradientMode = GradientMode::None;
        std::array<GradientStop, 4> stops{{
            {0.0f, SDL_Color{255,255,255,255}},
            {0.33f, SDL_Color{255,255,255,255}},
            {0.66f, SDL_Color{255,255,255,255}},
            {1.0f, SDL_Color{255,255,255,255}}
        }};
        SDL_Color fixedColor{255,255,255,255};
        SDL_Color previewTint{92, 132, 220, 255};
    };

    struct BrushStamp
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgba;       // raw imported/source rgba
        std::vector<std::uint8_t> mask;       // normalized alpha mask
        std::uint64_t revision = 0;
        MaskSource interpretedAs = MaskSource::Alpha;
        bool invert = false;
        float threshold = 0.0f;
        float levelsClamp = 1.0f;
        float edgeBoost = 0.0f;

        bool empty() const
        {
            return width <= 0 || height <= 0 || rgba.size() < (size_t)width * (size_t)height * 4ull;
        }
    };

    struct BrushManifest
    {
        std::string format = "sbrush";
        int version = 1;
        std::string id = "strova.builtin.soft_round";
        std::string name = "Soft Round";
        std::string author = "Strova";
        std::string description;
        std::string category = "Built-in";
        std::string tags;
        BrushType type = BrushType::Procedural;
        GeneratorType generator = GeneratorType::SoftCircle;
        std::string engineMin = "1.0.0";
        std::string icon = "preview.png";
        std::string stamp = "stamp.png";
        std::string script = "behavior.lua";
        BrushParams params{};
        BrushColorOptions color{};
    };

    struct BrushPackage
    {
        BrushManifest manifest{};
        BrushStamp stamp{};
        BrushStamp preview{};
        std::string scriptSource;
        std::string sourcePath;
        bool builtIn = false;
        bool missing = false;
        ValidationReport validation{};
    };

    struct BrushProject
    {
        BrushPackage package{};
        GeneratorType generator = GeneratorType::SoftCircle;
        BrushType requestedType = BrushType::Procedural;
        std::string projectPath;
    };

    struct ScriptEffect
    {
        float spacingScale = 1.0f;
        float scatterBoost = 0.0f;
        float alphaScale = 1.0f;
        float sizeScale = 1.0f;
        float rotationBiasDeg = 0.0f;
        bool tintGradient = false;
        bool valid = true;
        std::string error;
    };

    inline float clamp01(float v)
    {
        return std::clamp(v, 0.0f, 1.0f);
    }

    inline std::string trimCopy(const std::string& s)
    {
        size_t a = 0;
        while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
        size_t b = s.size();
        while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    inline std::string jsonEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 16);
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c >= 0x20) out.push_back((char)c);
                break;
            }
        }
        return out;
    }

    inline std::string hexEncode(const std::vector<std::uint8_t>& data)
    {
        static const char* lut = "0123456789ABCDEF";
        std::string out;
        out.resize(data.size() * 2);
        for (size_t i = 0; i < data.size(); ++i)
        {
            out[i * 2 + 0] = lut[(data[i] >> 4) & 0xF];
            out[i * 2 + 1] = lut[data[i] & 0xF];
        }
        return out;
    }

    inline std::vector<std::uint8_t> hexDecode(const std::string& text)
    {
        auto hexVal = [](char c) -> int
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        };

        std::vector<std::uint8_t> out;
        if ((text.size() & 1u) != 0u) return out;
        out.reserve(text.size() / 2);
        for (size_t i = 0; i + 1 < text.size(); i += 2)
        {
            int a = hexVal(text[i]);
            int b = hexVal(text[i + 1]);
            if (a < 0 || b < 0)
            {
                out.clear();
                return out;
            }
            out.push_back((std::uint8_t)((a << 4) | b));
        }
        return out;
    }

    inline bool writeTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        return !!f;
    }

    inline bool readTextFile(const std::filesystem::path& path, std::string& out)
    {
        out.clear();
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    inline bool findKeyPos(const std::string& j, const std::string& key, size_t& outPos)
    {
        const std::string marker = "\"" + key + "\"";
        size_t pos = j.find(marker);
        if (pos == std::string::npos) return false;
        pos = j.find(':', pos + marker.size());
        if (pos == std::string::npos) return false;
        ++pos;
        while (pos < j.size() && std::isspace((unsigned char)j[pos])) ++pos;
        outPos = pos;
        return true;
    }


    inline bool sliceJsonBlockAt(const std::string& j, size_t valuePos, char openCh, char closeCh, std::string& out)
    {
        size_t start = j.find(openCh, valuePos);
        if (start == std::string::npos) return false;
        int depth = 0;
        bool inString = false;
        bool escape = false;
        for (size_t i = start; i < j.size(); ++i)
        {
            char c = j[i];
            if (inString)
            {
                if (escape) { escape = false; continue; }
                if (c == '\\') { escape = true; continue; }
                if (c == '"') inString = false;
                continue;
            }
            if (c == '"') { inString = true; continue; }
            if (c == openCh) ++depth;
            else if (c == closeCh)
            {
                --depth;
                if (depth == 0)
                {
                    out = j.substr(start, i - start + 1);
                    return true;
                }
            }
        }
        return false;
    }

    inline bool getObjectByKey(const std::string& j, const std::string& key, std::string& out)
    {
        size_t p = 0;
        if (!findKeyPos(j, key, p)) return false;
        return sliceJsonBlockAt(j, p, '{', '}', out);
    }

    inline bool getArrayByKey(const std::string& j, const std::string& key, std::string& out)
    {
        size_t p = 0;
        if (!findKeyPos(j, key, p)) return false;
        return sliceJsonBlockAt(j, p, '[', ']', out);
    }

    inline bool parseIntAt(const std::string& j, size_t& i, int& out)
    {
        while (i < j.size() && std::isspace((unsigned char)j[i])) ++i;
        size_t start = i;
        if (i < j.size() && (j[i] == '+' || j[i] == '-')) ++i;
        while (i < j.size() && std::isdigit((unsigned char)j[i])) ++i;
        if (i == start) return false;
        try { out = std::stoi(j.substr(start, i - start)); return true; }
        catch (...) { return false; }
    }

    inline bool parseFloatAt(const std::string& j, size_t& i, float& out)
    {
        while (i < j.size() && std::isspace((unsigned char)j[i])) ++i;
        size_t start = i;
        bool dot = false;
        if (i < j.size() && (j[i] == '+' || j[i] == '-')) ++i;
        while (i < j.size())
        {
            char c = j[i];
            if (std::isdigit((unsigned char)c)) { ++i; continue; }
            if (c == '.' && !dot) { dot = true; ++i; continue; }
            break;
        }
        if (i == start) return false;
        try { out = std::stof(j.substr(start, i - start)); return true; }
        catch (...) { return false; }
    }

    inline bool parseBoolAt(const std::string& j, size_t& i, bool& out)
    {
        while (i < j.size() && std::isspace((unsigned char)j[i])) ++i;
        if (j.compare(i, 4, "true") == 0) { out = true; i += 4; return true; }
        if (j.compare(i, 5, "false") == 0) { out = false; i += 5; return true; }
        if (i < j.size() && j[i] == '1') { out = true; ++i; return true; }
        if (i < j.size() && j[i] == '0') { out = false; ++i; return true; }
        return false;
    }

    inline bool parseStringAt(const std::string& j, size_t& i, std::string& out)
    {
        while (i < j.size() && std::isspace((unsigned char)j[i])) ++i;
        if (i >= j.size() || j[i] != '"') return false;
        ++i;
        std::string s;
        while (i < j.size())
        {
            char c = j[i++];
            if (c == '"') break;
            if (c == '\\' && i < j.size())
            {
                char e = j[i++];
                switch (e)
                {
                case '"': s.push_back('"'); break;
                case '\\': s.push_back('\\'); break;
                case 'n': s.push_back('\n'); break;
                case 'r': s.push_back('\r'); break;
                case 't': s.push_back('\t'); break;
                default: s.push_back(e); break;
                }
            }
            else s.push_back(c);
        }
        out = s;
        return true;
    }

    inline std::string sanitizeId(const std::string& raw)
    {
        std::string out;
        out.reserve(raw.size());
        for (char c : raw)
        {
            if (std::isalnum((unsigned char)c)) out.push_back((char)std::tolower((unsigned char)c));
            else if (c == '.' || c == '_' || c == '-') out.push_back(c);
            else if (std::isspace((unsigned char)c)) out.push_back('_');
        }
        if (out.empty()) out = "strova.local.brush";
        return out;
    }

    inline std::filesystem::path builtinDir()
    {
        return strova::paths::getAppDataDir() / "brushes" / "default";
    }

    inline std::filesystem::path userDir()
    {
        return strova::paths::getAppDataDir() / "brushes" / "user";
    }

    inline std::filesystem::path cacheDir()
    {
        return strova::paths::getAppDataDir() / "cache" / "brushes";
    }

    inline SDL_Color lerpColor(SDL_Color a, SDL_Color b, float t)
    {
        t = clamp01(t);
        SDL_Color o{};
        o.r = (Uint8)std::lround(a.r * (1.0f - t) + b.r * t);
        o.g = (Uint8)std::lround(a.g * (1.0f - t) + b.g * t);
        o.b = (Uint8)std::lround(a.b * (1.0f - t) + b.b * t);
        o.a = (Uint8)std::lround(a.a * (1.0f - t) + b.a * t);
        return o;
    }

    inline SDL_Color sampleGradient(const BrushColorOptions& color, float t)
    {
        if (color.gradientMode == GradientMode::None)
            return color.fixedColor;
        const auto& stops = color.stops;
        if (t <= stops[0].pos) return stops[0].color;
        for (size_t i = 1; i < stops.size(); ++i)
        {
            if (t <= stops[i].pos)
            {
                const float a = stops[i - 1].pos;
                const float b = stops[i].pos;
                const float u = (b > a) ? (t - a) / (b - a) : 0.0f;
                return lerpColor(stops[i - 1].color, stops[i].color, u);
            }
        }
        return stops.back().color;
    }

    inline float luminance(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        return (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
    }

    inline void normalizeStamp(BrushStamp& stamp, MaskSource src, bool invert, float threshold, float levelsClamp, float edgeBoost)
    {
        stamp.interpretedAs = src;
        stamp.invert = invert;
        stamp.threshold = std::clamp(threshold, 0.0f, 1.0f);
        stamp.levelsClamp = std::clamp(levelsClamp, 0.01f, 1.0f);
        stamp.edgeBoost = std::clamp(edgeBoost, 0.0f, 4.0f);

        const size_t pxCount = (size_t)std::max(0, stamp.width) * (size_t)std::max(0, stamp.height);
        stamp.mask.assign(pxCount, 0);

        if (stamp.empty()) return;

        for (size_t i = 0; i < pxCount; ++i)
        {
            const std::uint8_t r = stamp.rgba[i * 4 + 0];
            const std::uint8_t g = stamp.rgba[i * 4 + 1];
            const std::uint8_t b = stamp.rgba[i * 4 + 2];
            const std::uint8_t a = stamp.rgba[i * 4 + 3];

            float v = 0.0f;
            switch (src)
            {
            case MaskSource::Alpha: v = a / 255.0f; break;
            case MaskSource::Luminance: v = luminance(r, g, b) * (a / 255.0f); break;
            case MaskSource::Darkness: v = (1.0f - luminance(r, g, b)) * (a / 255.0f); break;
            }

            if (invert) v = 1.0f - v;
            if (v < stamp.threshold) v = 0.0f;
            v = std::pow(clamp01(v), 1.0f / std::max(0.25f, stamp.levelsClamp));
            if (edgeBoost > 0.0f)
                v = std::pow(v, std::max(0.25f, 1.0f - edgeBoost * 0.18f));
            stamp.mask[i] = (std::uint8_t)std::clamp((int)std::lround(v * 255.0f), 0, 255);
        }
        stamp.revision += 1;
    }

    inline bool loadRgbaFromImage(const std::string& path, BrushStamp& out, std::string& err)
    {
        err.clear();
        out = BrushStamp{};
        SDL_Surface* surf = IMG_Load(path.c_str());
        if (!surf)
        {
            err = IMG_GetError() ? IMG_GetError() : "IMG_Load failed";
            return false;
        }
        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(surf);
        if (!rgba)
        {
            err = SDL_GetError();
            return false;
        }
        out.width = rgba->w;
        out.height = rgba->h;
        out.rgba.resize((size_t)rgba->w * (size_t)rgba->h * 4ull);
        std::memcpy(out.rgba.data(), rgba->pixels, out.rgba.size());
        SDL_FreeSurface(rgba);
        return true;
    }

    inline BrushStamp makeProceduralStamp(GeneratorType type, int size)
    {
        BrushStamp stamp{};
        const int s = std::clamp(size, 32, 256);
        stamp.width = s;
        stamp.height = s;
        stamp.rgba.assign((size_t)s * (size_t)s * 4ull, 0u);

        std::vector<float> alpha((size_t)s * (size_t)s, 0.0f);

        auto addAlpha = [&](int x, int y, float a)
        {
            if (x < 0 || y < 0 || x >= s || y >= s || a <= 0.0f) return;
            alpha[(size_t)y * (size_t)s + (size_t)x] = std::min(1.0f, alpha[(size_t)y * (size_t)s + (size_t)x] + a);
        };

        auto addSoftDot = [&](float cx, float cy, float radius, float strength)
        {
            const int minX = std::max(0, (int)std::floor(cx - radius - 1.0f));
            const int maxX = std::min(s - 1, (int)std::ceil(cx + radius + 1.0f));
            const int minY = std::max(0, (int)std::floor(cy - radius - 1.0f));
            const int maxY = std::min(s - 1, (int)std::ceil(cy + radius + 1.0f));
            const float invR = 1.0f / std::max(0.0001f, radius);
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const float dx = ((float)x + 0.5f) - cx;
                    const float dy = ((float)y + 0.5f) - cy;
                    const float d = std::sqrt(dx * dx + dy * dy) * invR;
                    if (d > 1.0f) continue;
                    const float v = std::pow(clamp01(1.0f - d), 1.6f) * strength;
                    addAlpha(x, y, v);
                }
            }
        };

        auto addHardDot = [&](float cx, float cy, float radius, float strength)
        {
            const int minX = std::max(0, (int)std::floor(cx - radius));
            const int maxX = std::min(s - 1, (int)std::ceil(cx + radius));
            const int minY = std::max(0, (int)std::floor(cy - radius));
            const int maxY = std::min(s - 1, (int)std::ceil(cy + radius));
            const float rr = radius * radius;
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    const float dx = ((float)x + 0.5f) - cx;
                    const float dy = ((float)y + 0.5f) - cy;
                    if ((dx * dx + dy * dy) <= rr)
                        addAlpha(x, y, strength);
                }
            }
        };

        std::mt19937 rng((unsigned int)type * 928371u + (unsigned int)s * 17u);
        std::uniform_real_distribution<float> uni(0.0f, 1.0f);

        const float cx = (float)s * 0.5f;
        const float cy = (float)s * 0.5f;
        const float rx = (type == GeneratorType::OvalTip) ? (float)s * 0.41f : (float)s * 0.43f;
        const float ry = (type == GeneratorType::OvalTip) ? (float)s * 0.24f : (float)s * 0.43f;

        auto radialWeight = [&](float px, float py, float power = 1.4f) -> float
        {
            const float fx = (px - cx) / std::max(1.0f, rx);
            const float fy = (py - cy) / std::max(1.0f, ry);
            const float d = std::sqrt(fx * fx + fy * fy);
            return std::pow(clamp01(1.0f - d), power);
        };

        switch (type)
        {
        case GeneratorType::HardCircle:
        case GeneratorType::SoftCircle:
        case GeneratorType::Square:
        case GeneratorType::SoftSquare:
        case GeneratorType::OvalTip:
            for (int y = 0; y < s; ++y)
            {
                for (int x = 0; x < s; ++x)
                {
                    const float fx = ((float)x + 0.5f - cx) / std::max(1.0f, rx);
                    const float fy = ((float)y + 0.5f - cy) / std::max(1.0f, ry);
                    const float d = std::sqrt(fx * fx + fy * fy);
                    float a = 0.0f;
                    switch (type)
                    {
                    case GeneratorType::HardCircle:
                        a = clamp01((1.02f - d) / 0.06f);
                        break;
                    case GeneratorType::SoftCircle:
                        a = std::pow(clamp01((1.02f - d) / 1.02f), 1.55f);
                        break;
                    case GeneratorType::Square:
                    {
                        const float edge = std::max(std::fabs(fx), std::fabs(fy));
                        a = clamp01((1.02f - edge) / 0.06f);
                        break;
                    }
                    case GeneratorType::SoftSquare:
                    {
                        const float edge = std::max(std::fabs(fx), std::fabs(fy));
                        a = std::pow(clamp01((1.02f - edge) / 1.02f), 1.65f);
                        break;
                    }
                    case GeneratorType::OvalTip:
                        a = std::pow(clamp01((1.02f - d) / 1.02f), 1.28f);
                        break;
                    default: break;
                    }
                    addAlpha(x, y, a);
                }
            }
            break;

        case GeneratorType::SpeckleCluster:
        {
            const int clusterCount = std::max(28, s / 3);
            for (int c = 0; c < clusterCount; ++c)
            {
                const float ang = uni(rng) * 6.2831853f;
                const float rr = std::sqrt(uni(rng)) * (float)s * 0.34f;
                const float pcx = cx + std::cos(ang) * rr;
                const float pcy = cy + std::sin(ang) * rr;
                const float clusterWeight = 0.22f + radialWeight(pcx, pcy, 1.15f) * 0.40f;
                const int microCount = 9 + (int)std::lround(uni(rng) * 14.0f);
                for (int k = 0; k < microCount; ++k)
                {
                    const float a = uni(rng) * 6.2831853f;
                    const float d = std::sqrt(uni(rng)) * (float)s * (0.010f + uni(rng) * 0.040f);
                    const float dotR = 0.45f + uni(rng) * ((float)s * 0.012f);
                    addSoftDot(pcx + std::cos(a) * d, pcy + std::sin(a) * d, dotR, clusterWeight * (0.18f + uni(rng) * 0.28f));
                }
            }
            const int dustCount = std::max(36, s * 3);
            for (int i = 0; i < dustCount; ++i)
            {
                const float ang = uni(rng) * 6.2831853f;
                const float rr = std::sqrt(uni(rng)) * (float)s * 0.48f;
                const float px = cx + std::cos(ang) * rr;
                const float py = cy + std::sin(ang) * rr;
                addHardDot(px, py, 0.35f + uni(rng) * 0.45f, 0.03f + radialWeight(px, py, 0.7f) * 0.08f);
            }
            break;
        }

        case GeneratorType::GrainyDisk:
            for (int y = 0; y < s; ++y)
            {
                for (int x = 0; x < s; ++x)
                {
                    const float base = radialWeight((float)x + 0.5f, (float)y + 0.5f, 1.2f);
                    if (base <= 0.0f) continue;
                    const float grain = 0.55f + uni(rng) * 0.45f;
                    addAlpha(x, y, base * grain);
                }
            }
            break;

        case GeneratorType::ChalkPatch:
            for (int y = 0; y < s; ++y)
            {
                for (int x = 0; x < s; ++x)
                {
                    float base = radialWeight((float)x + 0.5f, (float)y + 0.5f, 0.95f);
                    if (base <= 0.0f) continue;
                    if (uni(rng) < 0.14f + (1.0f - base) * 0.28f) base *= 0.08f;
                    base *= 0.65f + uni(rng) * 0.35f;
                    addAlpha(x, y, base);
                }
            }
            for (int i = 0; i < std::max(14, s / 5); ++i)
            {
                const float px = cx + (uni(rng) * 2.0f - 1.0f) * (float)s * 0.22f;
                const float py = cy + (uni(rng) * 2.0f - 1.0f) * (float)s * 0.16f;
                addSoftDot(px, py, (float)s * (0.03f + uni(rng) * 0.06f), 0.10f + uni(rng) * 0.14f);
            }
            break;

        case GeneratorType::NoiseBlob:
            for (int y = 0; y < s; ++y)
            {
                for (int x = 0; x < s; ++x)
                {
                    const float base = radialWeight((float)x + 0.5f, (float)y + 0.5f, 1.3f);
                    if (base <= 0.0f) continue;
                    const float n = 0.25f + uni(rng) * 0.75f;
                    addAlpha(x, y, base * n);
                }
            }
            for (int i = 0; i < std::max(10, s / 10); ++i)
            {
                addSoftDot(cx + (uni(rng) * 2.0f - 1.0f) * (float)s * 0.12f, cy + (uni(rng) * 2.0f - 1.0f) * (float)s * 0.12f,
                    (float)s * (0.04f + uni(rng) * 0.08f), 0.12f + uni(rng) * 0.18f);
            }
            break;
        }

        for (size_t i = 0; i < alpha.size(); ++i)
        {
            const std::uint8_t v = (std::uint8_t)std::clamp((int)std::lround(alpha[i] * 255.0f), 0, 255);
            stamp.rgba[i * 4 + 0] = 255;
            stamp.rgba[i * 4 + 1] = 255;
            stamp.rgba[i * 4 + 2] = 255;
            stamp.rgba[i * 4 + 3] = v;
        }

        normalizeStamp(stamp, MaskSource::Alpha, false, 0.0f, 1.0f, 0.0f);
        return stamp;
    }

    inline BrushStamp buildPreviewStamp(const BrushStamp& src, SDL_Color tint)
    {
        BrushStamp out = src;
        if (out.empty()) return out;
        const size_t pxCount = (size_t)out.width * (size_t)out.height;
        out.rgba.assign(pxCount * 4ull, 0u);
        for (size_t i = 0; i < pxCount; ++i)
        {
            const std::uint8_t m = src.mask.empty() ? src.rgba[i * 4 + 3] : src.mask[i];
            out.rgba[i * 4 + 0] = tint.r;
            out.rgba[i * 4 + 1] = tint.g;
            out.rgba[i * 4 + 2] = tint.b;
            out.rgba[i * 4 + 3] = m;
        }
        return out;
    }

    inline std::string brushTypeName(BrushType t)
    {
        switch (t)
        {
        case BrushType::RasterStamp: return "raster_stamp";
        case BrushType::Procedural: return "procedural";
        case BrushType::ScriptedRaster: return "scripted_raster";
        }
        return "raster_stamp";
    }

    inline BrushType parseBrushType(const std::string& s)
    {
        if (s == "procedural") return BrushType::Procedural;
        if (s == "scripted_raster") return BrushType::ScriptedRaster;
        return BrushType::RasterStamp;
    }

    inline std::string maskSourceName(MaskSource m)
    {
        switch (m)
        {
        case MaskSource::Alpha: return "alpha";
        case MaskSource::Luminance: return "luminance";
        case MaskSource::Darkness: return "darkness";
        }
        return "alpha";
    }

    inline MaskSource parseMaskSource(const std::string& s)
    {
        if (s == "luminance") return MaskSource::Luminance;
        if (s == "darkness") return MaskSource::Darkness;
        return MaskSource::Alpha;
    }

    inline std::string gradientModeName(GradientMode m)
    {
        switch (m)
        {
        case GradientMode::None: return "none";
        case GradientMode::StrokeProgress: return "stroke_progress";
        case GradientMode::Pressure: return "pressure";
        case GradientMode::Random: return "random";
        case GradientMode::Fixed: return "fixed";
        }
        return "none";
    }

    inline GradientMode parseGradientMode(const std::string& s)
    {
        if (s == "stroke_progress") return GradientMode::StrokeProgress;
        if (s == "pressure") return GradientMode::Pressure;
        if (s == "random") return GradientMode::Random;
        if (s == "fixed") return GradientMode::Fixed;
        return GradientMode::None;
    }

    inline std::string rotationModeName(RotationMode m)
    {
        switch (m)
        {
        case RotationMode::Stroke: return "stroke";
        case RotationMode::Fixed: return "fixed";
        case RotationMode::Random: return "random";
        }
        return "stroke";
    }

    inline RotationMode parseRotationMode(const std::string& s)
    {
        if (s == "fixed") return RotationMode::Fixed;
        if (s == "random") return RotationMode::Random;
        return RotationMode::Stroke;
    }

    inline std::string blendModeName(BlendMode m)
    {
        switch (m)
        {
        case BlendMode::Normal: return "normal";
        case BlendMode::Screen: return "screen";
        case BlendMode::Additive: return "additive";
        case BlendMode::Erase: return "erase";
        case BlendMode::Multiply: return "multiply";
        case BlendMode::Overlay: return "overlay";
        }
        return "normal";
    }

    inline BlendMode parseBlendMode(const std::string& s)
    {
        if (s == "screen") return BlendMode::Screen;
        if (s == "additive") return BlendMode::Additive;
        if (s == "erase") return BlendMode::Erase;
        if (s == "multiply") return BlendMode::Multiply;
        if (s == "overlay") return BlendMode::Overlay;
        return BlendMode::Normal;
    }

    inline std::string generatorTypeName(GeneratorType t)
    {
        switch (t)
        {
        case GeneratorType::HardCircle: return "hard_circle";
        case GeneratorType::SoftCircle: return "soft_circle";
        case GeneratorType::Square: return "square";
        case GeneratorType::SoftSquare: return "soft_square";
        case GeneratorType::SpeckleCluster: return "speckle_cluster";
        case GeneratorType::GrainyDisk: return "grainy_disk";
        case GeneratorType::OvalTip: return "oval_tip";
        case GeneratorType::ChalkPatch: return "chalk_patch";
        case GeneratorType::NoiseBlob: return "noise_blob";
        }
        return "soft_circle";
    }

    inline GeneratorType parseGeneratorType(const std::string& s)
    {
        if (s == "hard_circle") return GeneratorType::HardCircle;
        if (s == "soft_circle") return GeneratorType::SoftCircle;
        if (s == "square") return GeneratorType::Square;
        if (s == "soft_square") return GeneratorType::SoftSquare;
        if (s == "speckle_cluster") return GeneratorType::SpeckleCluster;
        if (s == "grainy_disk") return GeneratorType::GrainyDisk;
        if (s == "oval_tip") return GeneratorType::OvalTip;
        if (s == "chalk_patch") return GeneratorType::ChalkPatch;
        if (s == "noise_blob") return GeneratorType::NoiseBlob;
        return GeneratorType::SoftCircle;
    }

    inline const char* effectiveColorModeName(const BrushColorOptions& color)
    {
        if (color.supportsGradient && color.gradientMode != GradientMode::None)
            return "Package Gradient";
        if (color.supportsUserColor)
            return "User Color";
        return "Fixed Color";
    }

    inline bool previewUsesGradient(const BrushColorOptions& color)
    {
        return color.supportsGradient && color.gradientMode != GradientMode::None && !color.supportsUserColor;
    }

    inline bool generatorPreservesTextureScale(GeneratorType type)
    {
        switch (type)
        {
        case GeneratorType::GrainyDisk:
        case GeneratorType::ChalkPatch:
        case GeneratorType::NoiseBlob:
            return true;
        default:
            return false;
        }
    }

    inline SDL_Color defaultPreviewTintForGenerator(GeneratorType type)
    {
        switch (type)
        {
        case GeneratorType::HardCircle: return SDL_Color{ 96, 136, 235, 255 };
        case GeneratorType::SoftCircle: return SDL_Color{ 102, 150, 236, 255 };
        case GeneratorType::Square: return SDL_Color{ 110, 150, 210, 255 };
        case GeneratorType::SoftSquare: return SDL_Color{ 118, 142, 198, 255 };
        case GeneratorType::SpeckleCluster: return SDL_Color{ 70, 84, 98, 255 };
        case GeneratorType::GrainyDisk: return SDL_Color{ 150, 124, 96, 255 };
        case GeneratorType::OvalTip: return SDL_Color{ 34, 34, 34, 255 };
        case GeneratorType::ChalkPatch: return SDL_Color{ 216, 208, 196, 255 };
        case GeneratorType::NoiseBlob: return SDL_Color{ 118, 100, 124, 255 };
        }
        return SDL_Color{ 92, 132, 220, 255 };
    }

    inline BrushParams familyPresetParams(GeneratorType type)
    {
        BrushParams p{};
        p.sizeMin = 1.0f;
        p.sizeMax = 1024.0f;
        switch (type)
        {
        case GeneratorType::HardCircle:
            p.sizeDefault = 26.0f; p.spacing = 0.08f; p.opacity = 1.0f; p.flow = 0.95f; p.scatter = 0.01f; p.hardness = 1.0f;
            p.pressureSize = 0.65f; p.pressureOpacity = 0.28f; p.pressureFlow = 0.10f; p.smoothing = 0.20f; p.accumulate = true;
            break;
        case GeneratorType::SoftCircle:
            p.sizeDefault = 40.0f; p.spacing = 0.05f; p.opacity = 0.82f; p.flow = 0.45f; p.scatter = 0.02f; p.hardness = 0.18f;
            p.pressureSize = 0.45f; p.pressureOpacity = 0.45f; p.pressureFlow = 0.20f; p.smoothing = 0.34f; p.accumulate = true;
            break;
        case GeneratorType::Square:
            p.sizeDefault = 28.0f; p.spacing = 0.10f; p.opacity = 0.95f; p.flow = 0.90f; p.scatter = 0.01f; p.hardness = 1.0f;
            p.pressureSize = 0.50f; p.pressureOpacity = 0.30f; p.pressureFlow = 0.12f; p.smoothing = 0.18f; p.accumulate = true;
            break;
        case GeneratorType::SoftSquare:
            p.sizeDefault = 26.0f; p.spacing = 0.07f; p.opacity = 0.90f; p.flow = 0.82f; p.scatter = 0.03f; p.hardness = 0.72f;
            p.pressureSize = 0.42f; p.pressureOpacity = 0.22f; p.pressureFlow = 0.12f; p.smoothing = 0.22f; p.accumulate = true;
            break;
        case GeneratorType::SpeckleCluster:
            p.sizeDefault = 24.0f; p.spacing = 0.24f; p.opacity = 0.84f; p.flow = 0.48f; p.scatter = 0.32f; p.hardness = 0.54f;
            p.pressureSize = 0.42f; p.pressureOpacity = 0.24f; p.pressureFlow = 0.18f; p.jitterSize = 0.18f; p.jitterOpacity = 0.12f;
            p.spacingJitter = 0.15f; p.jitterRotation = 0.22f; p.smoothing = 0.08f; p.rotationMode = RotationMode::Random; p.accumulate = true;
            break;
        case GeneratorType::GrainyDisk:
            p.sizeDefault = 34.0f; p.spacing = 0.12f; p.opacity = 0.92f; p.flow = 0.76f; p.scatter = 0.08f; p.hardness = 0.64f;
            p.pressureSize = 0.55f; p.pressureOpacity = 0.32f; p.pressureFlow = 0.16f; p.jitterSize = 0.08f; p.jitterOpacity = 0.08f;
            p.spacingJitter = 0.04f; p.smoothing = 0.16f; p.accumulate = true;
            break;
        case GeneratorType::OvalTip:
            p.sizeDefault = 18.0f; p.spacing = 0.07f; p.opacity = 1.0f; p.flow = 0.98f; p.scatter = 0.01f; p.hardness = 0.90f;
            p.pressureSize = 0.32f; p.pressureOpacity = 0.58f; p.pressureFlow = 0.26f; p.smoothing = 0.14f; p.rotationMode = RotationMode::Stroke; p.accumulate = true;
            break;
        case GeneratorType::ChalkPatch:
            p.sizeDefault = 38.0f; p.spacing = 0.18f; p.opacity = 0.88f; p.flow = 0.62f; p.scatter = 0.18f; p.hardness = 0.42f;
            p.pressureSize = 0.62f; p.pressureOpacity = 0.24f; p.pressureFlow = 0.14f; p.jitterSize = 0.10f; p.jitterOpacity = 0.18f;
            p.spacingJitter = 0.11f; p.smoothing = 0.07f; p.accumulate = true;
            break;
        case GeneratorType::NoiseBlob:
            p.sizeDefault = 32.0f; p.spacing = 0.16f; p.opacity = 0.90f; p.flow = 0.72f; p.scatter = 0.12f; p.hardness = 0.52f;
            p.pressureSize = 0.52f; p.pressureOpacity = 0.30f; p.pressureFlow = 0.20f; p.jitterSize = 0.16f; p.jitterOpacity = 0.20f;
            p.spacingJitter = 0.10f; p.smoothing = 0.12f; p.accumulate = true;
            break;
        }
        return p;
    }

    inline BrushColorOptions familyPresetColor(GeneratorType type)
    {
        BrushColorOptions c{};
        c.maskSource = MaskSource::Alpha;
        c.invertMask = false;
        c.supportsUserColor = true;
        c.supportsGradient = true;
        c.gradientMode = GradientMode::None;
        c.fixedColor = SDL_Color{ 255, 255, 255, 255 };
        c.previewTint = defaultPreviewTintForGenerator(type);
        c.stops[0] = GradientStop{ 0.0f, c.previewTint };
        c.stops[1] = GradientStop{ 0.33f, SDL_Color{ (Uint8)std::min(255, c.previewTint.r + 18), (Uint8)std::min(255, c.previewTint.g + 18), (Uint8)std::min(255, c.previewTint.b + 18), 255 } };
        c.stops[2] = GradientStop{ 0.66f, SDL_Color{ (Uint8)std::max(0, c.previewTint.r - 18), (Uint8)std::max(0, c.previewTint.g - 18), (Uint8)std::max(0, c.previewTint.b - 18), 255 } };
        c.stops[3] = GradientStop{ 1.0f, c.previewTint };

        switch (type)
        {
        case GeneratorType::ChalkPatch:
            c.supportsUserColor = false;
            c.supportsGradient = false;
            c.fixedColor = SDL_Color{ 230, 226, 214, 255 };
            c.previewTint = c.fixedColor;
            break;
        case GeneratorType::OvalTip:
            c.supportsUserColor = false;
            c.supportsGradient = false;
            c.fixedColor = SDL_Color{ 24, 24, 26, 255 };
            c.previewTint = c.fixedColor;
            break;
        default:
            break;
        }
        return c;
    }

    inline void applyFamilyPreset(BrushPackage& pkg, GeneratorType type)
    {
        pkg.manifest.generator = type;
        pkg.manifest.params = familyPresetParams(type);
        pkg.manifest.color = familyPresetColor(type);
        pkg.stamp = makeProceduralStamp(type, 160);
    }

    inline BrushStamp buildPreviewStampGradient(const BrushStamp& src, const BrushColorOptions& color)
    {
        BrushStamp out = src;
        if (out.empty()) return out;
        const size_t pxCount = (size_t)out.width * (size_t)out.height;
        out.rgba.assign(pxCount * 4ull, 0u);
        for (size_t i = 0; i < pxCount; ++i)
        {
            const std::uint8_t m = src.mask.empty() ? src.rgba[i * 4 + 3] : src.mask[i];
            const float t = (float)(i % (size_t)std::max(1, out.width)) / (float)std::max(1, out.width - 1);
            const SDL_Color tint = sampleGradient(color, t);
            out.rgba[i * 4 + 0] = tint.r;
            out.rgba[i * 4 + 1] = tint.g;
            out.rgba[i * 4 + 2] = tint.b;
            out.rgba[i * 4 + 3] = m;
        }
        return out;
    }

    inline BrushStamp buildPackagePreview(const BrushPackage& pkg)
    {
        if (previewUsesGradient(pkg.manifest.color))
            return buildPreviewStampGradient(pkg.stamp, pkg.manifest.color);
        if (!pkg.manifest.color.supportsUserColor)
            return buildPreviewStamp(pkg.stamp, pkg.manifest.color.fixedColor);
        return buildPreviewStamp(pkg.stamp, pkg.manifest.color.previewTint);
    }

    inline ValidationReport validate(const BrushPackage& pkg)
    {
        ValidationReport rep;
        if (trimCopy(pkg.manifest.format) != "sbrush")
            rep.addError("Manifest format must be sbrush.");
        if (trimCopy(pkg.manifest.id).empty())
            rep.addError("Brush id is required.");
        else if (sanitizeId(pkg.manifest.id) != pkg.manifest.id)
            rep.addWarning("Brush id should use the sanitized package form.");
        if (trimCopy(pkg.manifest.name).empty())
            rep.addError("Brush name is required.");
        if (trimCopy(pkg.manifest.author).empty())
            rep.addWarning("Author is empty.");
        if (pkg.manifest.version <= 0)
            rep.addError("Version must be >= 1.");
        if (pkg.stamp.empty())
            rep.addError("Brush package has no valid stamp or generator source.");
        if (pkg.stamp.width > 2048 || pkg.stamp.height > 2048)
            rep.addError("Stamp image dimensions exceed safe limits.");
        if (pkg.manifest.type == BrushType::ScriptedRaster && trimCopy(pkg.scriptSource).empty())
            rep.addError("Script mode is enabled but behavior.lua is empty.");

        const auto& p = pkg.manifest.params;
        const auto& c = pkg.manifest.color;

        if (p.sizeMin < 1.0f || p.sizeMax < p.sizeMin)
            rep.addError("sizeMin > sizeMax or size bounds are invalid.");
        if (p.sizeDefault < p.sizeMin || p.sizeDefault > p.sizeMax)
            rep.addWarning("Default size sits outside the allowed size range.");
        if (p.spacing < 0.001f || p.spacing > 2.0f)
            rep.addWarning("Spacing is unusual.");
        if (p.opacity <= 0.0f || p.flow <= 0.0f)
            rep.addWarning("Opacity or flow is near zero; the brush may appear empty.");
        if (pkg.sourcePath.find("..") != std::string::npos)
            rep.addError("Path traversal detected.");

        if (!c.supportsGradient && c.gradientMode != GradientMode::None)
            rep.addError("Gradient controls are configured but gradient support is disabled.");
        if (!c.supportsUserColor && !c.supportsGradient && c.fixedColor.a == 0)
            rep.addError("Brush is fixed-color only but no valid fixed color is defined.");
        if (c.previewTint.a == 0)
            rep.addError("Preview tint is invalid.");
        if (!c.supportsUserColor && previewUsesGradient(c))
            rep.addInfo("Preview will show the package gradient because user color is locked.");

        float lastPos = -1.0f;
        for (size_t i = 0; i < c.stops.size(); ++i)
        {
            if (c.stops[i].pos < 0.0f || c.stops[i].pos > 1.0f)
                rep.addError("Gradient stop positions must stay between 0 and 1.");
            if (c.stops[i].pos < lastPos)
                rep.addError("Gradient stop positions must be increasing.");
            lastPos = c.stops[i].pos;
        }

        if (!pkg.stamp.mask.empty())
        {
            std::uint8_t maxMask = 0;
            std::size_t nonZero = 0;
            for (std::uint8_t v : pkg.stamp.mask)
            {
                maxMask = std::max(maxMask, v);
                if (v > 0) ++nonZero;
            }
            if (maxMask == 0)
                rep.addError("Brush mask is empty.");
            const float coverage = pkg.stamp.mask.empty() ? 0.0f : (float)nonZero / (float)pkg.stamp.mask.size();
            if (coverage < 0.002f)
                rep.addWarning("Brush mask carries almost no paint information.");
            if (coverage > 0.98f && p.hardness < 0.25f)
                rep.addWarning("Very dense mask plus soft hardness may read as a mushy block.");
        }

        if (pkg.manifest.generator == GeneratorType::SpeckleCluster)
        {
            if (p.spacing < 0.14f)
                rep.addInfo("Speckle spacing is low; result may appear too dense.");
            if (p.flow > 0.80f)
                rep.addWarning("Speckle flow is high; result may lose particulate breakup.");
            if (p.scatter < 0.10f)
                rep.addWarning("Speckle scatter is low; result may look too continuous.");
        }

        if (!c.supportsUserColor && c.previewTint.r == c.fixedColor.r && c.previewTint.g == c.fixedColor.g && c.previewTint.b == c.fixedColor.b)
            rep.addInfo("Preview tint currently matches fixed color; ensure this is intentional.");
        return rep;
    }

    inline ScriptEffect parseScriptEffect(const std::string& script)
    {
        ScriptEffect fx{};
        if (trimCopy(script).empty()) return fx;

        std::istringstream ss(script);
        std::string line;
        while (std::getline(ss, line))
        {
            line = trimCopy(line);
            if (line.empty()) continue;
            auto parseAssign = [&](const char* key, float& dst) -> bool
            {
                const std::string marker = std::string(key) + "=";
                if (line.rfind(marker, 0) == 0)
                {
                    try { dst = std::stof(trimCopy(line.substr(marker.size()))); return true; }
                    catch (...) { fx.valid = false; fx.error = "Script parse failed."; return true; }
                }
                return false;
            };
            if (parseAssign("spacing_scale", fx.spacingScale)) continue;
            if (parseAssign("scatter_boost", fx.scatterBoost)) continue;
            if (parseAssign("alpha_scale", fx.alphaScale)) continue;
            if (parseAssign("size_scale", fx.sizeScale)) continue;
            if (parseAssign("rotation_bias_deg", fx.rotationBiasDeg)) continue;
            if (line == "tint_gradient=true") { fx.tintGradient = true; continue; }
            if (line.rfind("--", 0) == 0) continue;
        }
        fx.spacingScale = std::max(0.1f, fx.spacingScale);
        fx.alphaScale = std::clamp(fx.alphaScale, 0.05f, 4.0f);
        fx.sizeScale = std::clamp(fx.sizeScale, 0.1f, 4.0f);
        fx.scatterBoost = std::clamp(fx.scatterBoost, -1.0f, 4.0f);
        fx.rotationBiasDeg = std::clamp(fx.rotationBiasDeg, -720.0f, 720.0f);
        return fx;
    }

    inline std::string serializePackage(const BrushPackage& pkg)
    {
        std::ostringstream j;
        j << "{\n";
        j << "  \"kind\": \"sbrush\",\n";
        j << "  \"manifest\": {\n";
        j << "    \"format\": \"" << jsonEscape(pkg.manifest.format) << "\",\n";
        j << "    \"version\": " << pkg.manifest.version << ",\n";
        j << "    \"id\": \"" << jsonEscape(pkg.manifest.id) << "\",\n";
        j << "    \"name\": \"" << jsonEscape(pkg.manifest.name) << "\",\n";
        j << "    \"author\": \"" << jsonEscape(pkg.manifest.author) << "\",\n";
        j << "    \"description\": \"" << jsonEscape(pkg.manifest.description) << "\",\n";
        j << "    \"category\": \"" << jsonEscape(pkg.manifest.category) << "\",\n";
        j << "    \"tags\": \"" << jsonEscape(pkg.manifest.tags) << "\",\n";
        j << "    \"type\": \"" << brushTypeName(pkg.manifest.type) << "\",\n";
        j << "    \"generator\": \"" << generatorTypeName(pkg.manifest.generator) << "\",\n";
        j << "    \"engine_min\": \"" << jsonEscape(pkg.manifest.engineMin) << "\",\n";
        j << "    \"params\": {\n";
        j << "      \"size_default\": " << pkg.manifest.params.sizeDefault << ",\n";
        j << "      \"size_min\": " << pkg.manifest.params.sizeMin << ",\n";
        j << "      \"size_max\": " << pkg.manifest.params.sizeMax << ",\n";
        j << "      \"spacing\": " << pkg.manifest.params.spacing << ",\n";
        j << "      \"opacity\": " << pkg.manifest.params.opacity << ",\n";
        j << "      \"flow\": " << pkg.manifest.params.flow << ",\n";
        j << "      \"scatter\": " << pkg.manifest.params.scatter << ",\n";
        j << "      \"hardness\": " << pkg.manifest.params.hardness << ",\n";
        j << "      \"pressure_size\": " << pkg.manifest.params.pressureSize << ",\n";
        j << "      \"pressure_opacity\": " << pkg.manifest.params.pressureOpacity << ",\n";
        j << "      \"pressure_flow\": " << pkg.manifest.params.pressureFlow << ",\n";
        j << "      \"jitter_size\": " << pkg.manifest.params.jitterSize << ",\n";
        j << "      \"jitter_opacity\": " << pkg.manifest.params.jitterOpacity << ",\n";
        j << "      \"jitter_rotation\": " << pkg.manifest.params.jitterRotation << ",\n";
        j << "      \"spacing_jitter\": " << pkg.manifest.params.spacingJitter << ",\n";
        j << "      \"smoothing\": " << pkg.manifest.params.smoothing << ",\n";
        j << "      \"fixed_angle\": " << pkg.manifest.params.fixedAngle << ",\n";
        j << "      \"accumulate\": " << (pkg.manifest.params.accumulate ? "true" : "false") << ",\n";
        j << "      \"rotation_mode\": \"" << rotationModeName(pkg.manifest.params.rotationMode) << "\",\n";
        j << "      \"blend_mode\": \"" << blendModeName(pkg.manifest.params.blendMode) << "\"\n";
        j << "    },\n";
        j << "    \"color\": {\n";
        j << "      \"mask_source\": \"" << maskSourceName(pkg.manifest.color.maskSource) << "\",\n";
        j << "      \"invert_mask\": " << (pkg.manifest.color.invertMask ? "true" : "false") << ",\n";
        j << "      \"supports_user_color\": " << (pkg.manifest.color.supportsUserColor ? "true" : "false") << ",\n";
        j << "      \"supports_gradient\": " << (pkg.manifest.color.supportsGradient ? "true" : "false") << ",\n";
        j << "      \"gradient_mode\": \"" << gradientModeName(pkg.manifest.color.gradientMode) << "\",\n";
        j << "      \"fixed_color\": [" << (int)pkg.manifest.color.fixedColor.r << "," << (int)pkg.manifest.color.fixedColor.g << "," << (int)pkg.manifest.color.fixedColor.b << "," << (int)pkg.manifest.color.fixedColor.a << "],\n";
        j << "      \"preview_tint\": [" << (int)pkg.manifest.color.previewTint.r << "," << (int)pkg.manifest.color.previewTint.g << "," << (int)pkg.manifest.color.previewTint.b << "," << (int)pkg.manifest.color.previewTint.a << "],\n";
        j << "      \"stops\": [";
        for (size_t i = 0; i < pkg.manifest.color.stops.size(); ++i)
        {
            const auto& s = pkg.manifest.color.stops[i];
            j << "{\"pos\":" << s.pos << ",\"color\":[" << (int)s.color.r << "," << (int)s.color.g << "," << (int)s.color.b << "," << (int)s.color.a << "]}";
            if (i + 1 < pkg.manifest.color.stops.size()) j << ",";
        }
        j << "]\n";
        j << "    }\n";
        j << "  },\n";
        j << "  \"stamp\": {\n";
        j << "    \"width\": " << pkg.stamp.width << ",\n";
        j << "    \"height\": " << pkg.stamp.height << ",\n";
        j << "    \"rgba\": \"" << hexEncode(pkg.stamp.rgba) << "\",\n";
        j << "    \"mask\": \"" << hexEncode(pkg.stamp.mask) << "\",\n";
        j << "    \"mask_source\": \"" << maskSourceName(pkg.stamp.interpretedAs) << "\",\n";
        j << "    \"invert\": " << (pkg.stamp.invert ? "true" : "false") << ",\n";
        j << "    \"threshold\": " << pkg.stamp.threshold << ",\n";
        j << "    \"levels_clamp\": " << pkg.stamp.levelsClamp << ",\n";
        j << "    \"edge_boost\": " << pkg.stamp.edgeBoost << "\n";
        j << "  },\n";
        j << "  \"preview\": {\n";
        j << "    \"width\": " << pkg.preview.width << ",\n";
        j << "    \"height\": " << pkg.preview.height << ",\n";
        j << "    \"rgba\": \"" << hexEncode(pkg.preview.rgba) << "\"\n";
        j << "  },\n";
        j << "  \"script\": \"" << jsonEscape(pkg.scriptSource) << "\"\n";
        j << "}\n";
        return j.str();
    }

    inline bool parseColorArray4(const std::string& j, size_t pos, SDL_Color& out)
    {
        size_t a = j.find('[', pos);
        if (a == std::string::npos) return false;
        ++a;
        int r = 255, g = 255, b = 255, aa = 255;
        if (!parseIntAt(j, a, r)) return false;
        a = j.find(',', a); if (a == std::string::npos) return false; ++a;
        if (!parseIntAt(j, a, g)) return false;
        a = j.find(',', a); if (a == std::string::npos) return false; ++a;
        if (!parseIntAt(j, a, b)) return false;
        a = j.find(',', a); if (a == std::string::npos) return false; ++a;
        if (!parseIntAt(j, a, aa)) return false;
        out = SDL_Color{ (Uint8)std::clamp(r,0,255), (Uint8)std::clamp(g,0,255), (Uint8)std::clamp(b,0,255), (Uint8)std::clamp(aa,0,255) };
        return true;
    }

    inline bool parseGradientStopsArray(const std::string& j, std::array<GradientStop, 4>& outStops)
    {
        std::array<GradientStop, 4> parsed = outStops;
        size_t search = 0;
        size_t count = 0;
        while (count < parsed.size())
        {
            size_t posKey = j.find("\"pos\"", search);
            if (posKey == std::string::npos) break;
            size_t posValue = j.find(':', posKey + 5);
            if (posValue == std::string::npos) break;
            ++posValue;
            float stopPos = parsed[count].pos;
            if (!parseFloatAt(j, posValue, stopPos)) break;

            size_t colorKey = j.find("\"color\"", posValue);
            if (colorKey == std::string::npos) break;
            size_t colorValue = j.find(':', colorKey + 7);
            if (colorValue == std::string::npos) break;
            ++colorValue;
            SDL_Color stopColor = parsed[count].color;
            if (!parseColorArray4(j, colorValue, stopColor)) break;

            parsed[count] = GradientStop{ std::clamp(stopPos, 0.0f, 1.0f), stopColor };
            search = colorValue;
            ++count;
        }
        if (count == 0) return false;
        std::sort(parsed.begin(), parsed.end(), [](const GradientStop& a, const GradientStop& b) { return a.pos < b.pos; });
        parsed.front().pos = 0.0f;
        parsed.back().pos = 1.0f;
        outStops = parsed;
        return true;
    }

    inline bool deserializePackage(const std::string& j, BrushPackage& outPkg, std::string& err)
    {
        err.clear();
        outPkg = BrushPackage{};

        std::string manifestObj;
        std::string paramsObj;
        std::string colorObj;
        std::string stampObj;
        if (!getObjectByKey(j, "manifest", manifestObj)) manifestObj = j;
        if (!getObjectByKey(manifestObj, "params", paramsObj)) paramsObj = manifestObj;
        if (!getObjectByKey(manifestObj, "color", colorObj)) colorObj = manifestObj;
        if (!getObjectByKey(j, "stamp", stampObj)) stampObj = j;

        size_t p = 0;
        std::string s;

        if (findKeyPos(manifestObj, "format", p)) { parseStringAt(manifestObj, p, outPkg.manifest.format); }
        if (findKeyPos(manifestObj, "id", p)) { parseStringAt(manifestObj, p, outPkg.manifest.id); }
        if (findKeyPos(manifestObj, "name", p)) { parseStringAt(manifestObj, p, outPkg.manifest.name); }
        if (findKeyPos(manifestObj, "author", p)) { parseStringAt(manifestObj, p, outPkg.manifest.author); }
        if (findKeyPos(manifestObj, "description", p)) { parseStringAt(manifestObj, p, outPkg.manifest.description); }
        if (findKeyPos(manifestObj, "category", p)) { parseStringAt(manifestObj, p, outPkg.manifest.category); }
        if (findKeyPos(manifestObj, "tags", p)) { parseStringAt(manifestObj, p, outPkg.manifest.tags); }
        if (findKeyPos(manifestObj, "type", p)) { parseStringAt(manifestObj, p, s); outPkg.manifest.type = parseBrushType(s); }
        if (findKeyPos(manifestObj, "generator", p)) { parseStringAt(manifestObj, p, s); outPkg.manifest.generator = parseGeneratorType(s); }
        if (findKeyPos(manifestObj, "engine_min", p)) { parseStringAt(manifestObj, p, outPkg.manifest.engineMin); }
        if (findKeyPos(manifestObj, "version", p)) { parseIntAt(manifestObj, p, outPkg.manifest.version); }

        auto readFloatByKey = [&](const std::string& src, const char* key, float& dst)
        {
            size_t q = 0;
            if (findKeyPos(src, key, q)) (void)parseFloatAt(src, q, dst);
        };
        auto readBoolByKey = [&](const std::string& src, const char* key, bool& dst)
        {
            size_t q = 0;
            if (findKeyPos(src, key, q)) (void)parseBoolAt(src, q, dst);
        };

        readFloatByKey(paramsObj, "size_default", outPkg.manifest.params.sizeDefault);
        readFloatByKey(paramsObj, "size_min", outPkg.manifest.params.sizeMin);
        readFloatByKey(paramsObj, "size_max", outPkg.manifest.params.sizeMax);
        readFloatByKey(paramsObj, "spacing", outPkg.manifest.params.spacing);
        readFloatByKey(paramsObj, "opacity", outPkg.manifest.params.opacity);
        readFloatByKey(paramsObj, "flow", outPkg.manifest.params.flow);
        readFloatByKey(paramsObj, "scatter", outPkg.manifest.params.scatter);
        readFloatByKey(paramsObj, "hardness", outPkg.manifest.params.hardness);
        readFloatByKey(paramsObj, "pressure_size", outPkg.manifest.params.pressureSize);
        readFloatByKey(paramsObj, "pressure_opacity", outPkg.manifest.params.pressureOpacity);
        readFloatByKey(paramsObj, "pressure_flow", outPkg.manifest.params.pressureFlow);
        readFloatByKey(paramsObj, "jitter_size", outPkg.manifest.params.jitterSize);
        readFloatByKey(paramsObj, "jitter_opacity", outPkg.manifest.params.jitterOpacity);
        readFloatByKey(paramsObj, "jitter_rotation", outPkg.manifest.params.jitterRotation);
        readFloatByKey(paramsObj, "spacing_jitter", outPkg.manifest.params.spacingJitter);
        readFloatByKey(paramsObj, "smoothing", outPkg.manifest.params.smoothing);
        readFloatByKey(paramsObj, "fixed_angle", outPkg.manifest.params.fixedAngle);
        readBoolByKey(paramsObj, "accumulate", outPkg.manifest.params.accumulate);
        if (findKeyPos(paramsObj, "rotation_mode", p)) { parseStringAt(paramsObj, p, s); outPkg.manifest.params.rotationMode = parseRotationMode(s); }
        if (findKeyPos(paramsObj, "blend_mode", p)) { parseStringAt(paramsObj, p, s); outPkg.manifest.params.blendMode = parseBlendMode(s); }

        if (findKeyPos(colorObj, "mask_source", p)) { parseStringAt(colorObj, p, s); outPkg.manifest.color.maskSource = parseMaskSource(s); }
        readBoolByKey(colorObj, "invert_mask", outPkg.manifest.color.invertMask);
        readBoolByKey(colorObj, "supports_user_color", outPkg.manifest.color.supportsUserColor);
        readBoolByKey(colorObj, "supports_gradient", outPkg.manifest.color.supportsGradient);
        if (findKeyPos(colorObj, "gradient_mode", p)) { parseStringAt(colorObj, p, s); outPkg.manifest.color.gradientMode = parseGradientMode(s); }
        if (findKeyPos(colorObj, "fixed_color", p)) { parseColorArray4(colorObj, p, outPkg.manifest.color.fixedColor); }
        if (findKeyPos(colorObj, "preview_tint", p)) { parseColorArray4(colorObj, p, outPkg.manifest.color.previewTint); }
        std::string stopsArray;
        if (getArrayByKey(colorObj, "stops", stopsArray))
            (void)parseGradientStopsArray(stopsArray, outPkg.manifest.color.stops);

        if (findKeyPos(stampObj, "width", p)) { parseIntAt(stampObj, p, outPkg.stamp.width); }
        if (findKeyPos(stampObj, "height", p)) { parseIntAt(stampObj, p, outPkg.stamp.height); }
        if (findKeyPos(stampObj, "rgba", p)) { parseStringAt(stampObj, p, s); outPkg.stamp.rgba = hexDecode(s); }
        if (findKeyPos(stampObj, "mask", p)) { parseStringAt(stampObj, p, s); outPkg.stamp.mask = hexDecode(s); }
        if (findKeyPos(stampObj, "mask_source", p)) { parseStringAt(stampObj, p, s); outPkg.stamp.interpretedAs = parseMaskSource(s); }
        if (findKeyPos(stampObj, "invert", p)) { parseBoolAt(stampObj, p, outPkg.stamp.invert); }
        if (findKeyPos(stampObj, "threshold", p)) { parseFloatAt(stampObj, p, outPkg.stamp.threshold); }
        if (findKeyPos(stampObj, "levels_clamp", p)) { parseFloatAt(stampObj, p, outPkg.stamp.levelsClamp); }
        if (findKeyPos(stampObj, "edge_boost", p)) { parseFloatAt(stampObj, p, outPkg.stamp.edgeBoost); }
        if (findKeyPos(j, "script", p)) { parseStringAt(j, p, outPkg.scriptSource); }

        if (outPkg.stamp.mask.empty() && !outPkg.stamp.rgba.empty())
            normalizeStamp(outPkg.stamp, outPkg.stamp.interpretedAs, outPkg.stamp.invert, outPkg.stamp.threshold, outPkg.stamp.levelsClamp, outPkg.stamp.edgeBoost);

        outPkg.preview = buildPackagePreview(outPkg);
        outPkg.validation = validate(outPkg);
        if (!outPkg.validation.ok)
        {
            err = outPkg.validation.summary();
            return false;
        }
        return true;
    }

    inline bool loadPackageFromFile(const std::filesystem::path& path, BrushPackage& out, std::string& err)
    {
        std::string j;
        if (!readTextFile(path, j))
        {
            err = "Could not read package file.";
            return false;
        }
        if (!deserializePackage(j, out, err))
            return false;
        out.sourcePath = path.string();
        out.builtIn = path.string().find(builtinDir().string()) == 0;
        return true;
    }

    inline bool savePackageToFile(const std::filesystem::path& path, const BrushPackage& pkg, std::string& err)
    {
        ValidationReport rep = validate(pkg);
        if (!rep.ok)
        {
            err = rep.summary();
            return false;
        }
        if (!writeTextFile(path, serializePackage(pkg)))
        {
            err = "Could not write package file.";
            return false;
        }
        return true;
    }

    inline std::string serializeProject(const BrushProject& proj)
    {
        std::ostringstream j;
        j << "{\n";
        j << "  \"kind\": \"sbrushproj\",\n";
        j << "  \"generator\": " << (int)proj.generator << ",\n";
        j << "  \"requested_type\": \"" << brushTypeName(proj.requestedType) << "\",\n";
        j << "  \"package\": " << serializePackage(proj.package);
        j << "}\n";
        return j.str();
    }

    inline bool saveProjectToFile(const std::filesystem::path& path, const BrushProject& proj, std::string& err)
    {
        if (!writeTextFile(path, serializeProject(proj)))
        {
            err = "Could not write brush project file.";
            return false;
        }
        return true;
    }

    inline bool loadProjectFromFile(const std::filesystem::path& path, BrushProject& out, std::string& err)
    {
        std::string j;
        if (!readTextFile(path, j))
        {
            err = "Could not read brush project file.";
            return false;
        }
        out = BrushProject{};
        size_t p = 0;
        int gen = 0;
        if (findKeyPos(j, "generator", p)) parseIntAt(j, p, gen);
        out.generator = (GeneratorType)std::clamp(gen, 0, (int)GeneratorType::NoiseBlob);
        std::string t;
        if (findKeyPos(j, "requested_type", p)) parseStringAt(j, p, t);
        out.requestedType = parseBrushType(t);
        std::string packageObj;
        if (!getObjectByKey(j, "package", packageObj))
        {
            err = "Brush project is missing package data.";
            return false;
        }
        if (!deserializePackage(packageObj, out.package, err))
            return false;
        out.projectPath = path.string();
        return true;
    }

    inline BrushPackage makeBuiltinBrush(const std::string& id, const std::string& name, GeneratorType gen, BrushType type, BlendMode blend, float size, float hardness, float spacing, float flow, const std::string& category = "Built-in")
    {
        BrushPackage pkg{};
        pkg.builtIn = true;
        pkg.manifest.id = sanitizeId(id);
        pkg.manifest.name = name;
        pkg.manifest.author = "Strova";
        pkg.manifest.category = category;
        pkg.manifest.type = type;
        applyFamilyPreset(pkg, gen);
        pkg.manifest.params.sizeDefault = size;
        pkg.manifest.params.hardness = hardness;
        pkg.manifest.params.spacing = spacing;
        pkg.manifest.params.flow = flow;
        pkg.manifest.params.blendMode = blend;
        pkg.preview = buildPackagePreview(pkg);
        pkg.validation = validate(pkg);
        return pkg;
    }

    class BrushManager
    {
    public:
        bool initialize()
        {
            std::error_code ec;
            std::filesystem::create_directories(builtinDir(), ec);
            std::filesystem::create_directories(userDir(), ec);
            std::filesystem::create_directories(cacheDir(), ec);

            const std::string preferredSelection = !missingSelectionId.empty()
                ? missingSelectionId
                : (!selectedBrushId.empty() ? selectedBrushId : std::string("strova.builtin.soft_round"));

            packages.clear();
            selectedBrushId = "strova.builtin.soft_round";
            missingSelectionId.clear();

            registerBuiltinDefaults();
            scanDirectory(builtinDir(), true);
            scanDirectory(userDir(), false);

            if (!preferredSelection.empty() && findById(preferredSelection))
            {
                selectedBrushId = preferredSelection;
            }
            else if (!findById(selectedBrushId))
            {
                if (!packages.empty()) selectedBrushId = packages.front().manifest.id;
            }
            return !packages.empty();
        }

        void refresh()
        {
            const std::string preferredSelection = !missingSelectionId.empty()
                ? missingSelectionId
                : selectedBrushId;
            initialize();
            if (!preferredSelection.empty())
                select(preferredSelection);
        }

        const std::vector<BrushPackage>& all() const { return packages; }

        const BrushPackage* findById(const std::string& id) const
        {
            for (const auto& pkg : packages)
                if (pkg.manifest.id == id)
                    return &pkg;
            return nullptr;
        }

        BrushPackage* findById(const std::string& id)
        {
            for (auto& pkg : packages)
                if (pkg.manifest.id == id)
                    return &pkg;
            return nullptr;
        }

        bool select(const std::string& id)
        {
            if (findById(id))
            {
                selectedBrushId = id;
                missingSelectionId.clear();
                return true;
            }
            missingSelectionId = id;
            return false;
        }

        const std::string& selectedId() const { return selectedBrushId; }
        const std::string& missingSelectedId() const { return missingSelectionId; }

        const BrushPackage* selected() const
        {
            if (const auto* pkg = findById(selectedBrushId))
                return pkg;
            if (!packages.empty())
                return &packages.front();
            return nullptr;
        }

        BrushProject makeDefaultProject(const std::string& name = "New Brush") const
        {
            BrushProject proj{};
            proj.generator = GeneratorType::SoftCircle;
            proj.requestedType = BrushType::Procedural;
            proj.package = makeBuiltinBrush("strova.local." + sanitizeId(name), name, GeneratorType::SoftCircle, BrushType::Procedural, BlendMode::Normal, 32.0f, 0.18f, 0.05f, 0.45f, "Local");
            proj.package.builtIn = false;
            proj.package.manifest.author = "Creator";
            proj.package.manifest.category = "Local";
            proj.package.manifest.description = "Custom brush";
            proj.package.manifest.color.supportsGradient = true;
            proj.package.manifest.color.supportsUserColor = true;
            proj.package.manifest.color.previewTint = defaultPreviewTintForGenerator(proj.generator);
            proj.package.preview = buildPackagePreview(proj.package);
            return proj;
        }

        bool exportPackage(const BrushPackage& pkg, const std::string& filePath, std::string& err) const
        {
            return savePackageToFile(filePath, pkg, err);
        }

        bool installPackageFile(const std::string& sourcePath, std::string& outInstalledPath, std::string& err)
        {
            BrushPackage pkg{};
            if (!loadPackageFromFile(sourcePath, pkg, err))
                return false;
            const std::filesystem::path dst = userDir() / (sanitizeId(pkg.manifest.id) + ".sbrush");
            if (!savePackageToFile(dst, pkg, err))
                return false;
            outInstalledPath = dst.string();
            refresh();
            selectedBrushId = pkg.manifest.id;
            return true;
        }

        bool saveProjectFile(const BrushProject& proj, const std::string& filePath, std::string& err) const
        {
            return saveProjectToFile(filePath, proj, err);
        }

        bool loadProjectFile(const std::string& filePath, BrushProject& out, std::string& err) const
        {
            return loadProjectFromFile(filePath, out, err);
        }

        ValidationReport validatePackage(const BrushPackage& pkg) const
        {
            return validate(pkg);
        }

        void ensureBuiltinsWritten() const
        {
            std::error_code ec;
            std::filesystem::create_directories(builtinDir(), ec);
            for (const auto& pkg : builtinDefaults)
            {
                const auto dst = builtinDir() / (sanitizeId(pkg.manifest.id) + ".sbrush");
                if (!std::filesystem::exists(dst))
                {
                    std::string err;
                    (void)savePackageToFile(dst, pkg, err);
                }
            }
        }

    private:
        std::vector<BrushPackage> packages;
        std::vector<BrushPackage> builtinDefaults;
        std::string selectedBrushId = "strova.builtin.soft_round";
        std::string missingSelectionId;

        void registerBuiltinDefaults()
        {
            builtinDefaults.clear();
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.hard_round", "Hard Round", GeneratorType::HardCircle, BrushType::Procedural, BlendMode::Normal, 24.0f, 1.0f, 0.10f, 1.0f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.soft_round", "Soft Round", GeneratorType::SoftCircle, BrushType::Procedural, BlendMode::Normal, 32.0f, 0.8f, 0.10f, 1.0f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.square", "Square", GeneratorType::Square, BrushType::Procedural, BlendMode::Normal, 28.0f, 1.0f, 0.12f, 1.0f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.soft_square", "Soft Square", GeneratorType::SoftSquare, BrushType::Procedural, BlendMode::Normal, 32.0f, 0.72f, 0.12f, 1.0f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.speckle", "Speckle Cluster", GeneratorType::SpeckleCluster, BrushType::Procedural, BlendMode::Normal, 22.0f, 0.75f, 0.18f, 0.75f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.grainy_disk", "Grainy Disk", GeneratorType::GrainyDisk, BrushType::Procedural, BlendMode::Normal, 34.0f, 0.65f, 0.10f, 0.9f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.oval_tip", "Oval Tip", GeneratorType::OvalTip, BrushType::Procedural, BlendMode::Normal, 26.0f, 0.85f, 0.10f, 1.0f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.chalk_patch", "Chalk Patch", GeneratorType::ChalkPatch, BrushType::Procedural, BlendMode::Normal, 36.0f, 0.55f, 0.16f, 0.7f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.noise_blob", "Noise Blob", GeneratorType::NoiseBlob, BrushType::Procedural, BlendMode::Normal, 30.0f, 0.7f, 0.12f, 0.85f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.airbrush", "Airbrush Soft", GeneratorType::SoftCircle, BrushType::Procedural, BlendMode::Normal, 60.0f, 0.18f, 0.06f, 0.45f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.glow", "Glow Bloom", GeneratorType::SoftCircle, BrushType::Procedural, BlendMode::Additive, 34.0f, 0.25f, 0.09f, 0.65f));
            builtinDefaults.push_back(makeBuiltinBrush("strova.builtin.marker", "Marker Body", GeneratorType::SoftSquare, BrushType::Procedural, BlendMode::Normal, 24.0f, 0.7f, 0.08f, 0.85f));
            ensureBuiltinsWritten();
            packages = builtinDefaults;
        }

        void scanDirectory(const std::filesystem::path& dir, bool builtInFlag)
        {
            std::error_code ec;
            if (!std::filesystem::exists(dir, ec)) return;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (!entry.is_regular_file()) continue;
                const auto ext = entry.path().extension().string();
                if (ext != ".sbrush") continue;

                BrushPackage pkg{};
                std::string err;
                if (!loadPackageFromFile(entry.path(), pkg, err))
                    continue;
                pkg.builtIn = builtInFlag;
                bool exists = false;
                for (auto& cur : packages)
                {
                    if (cur.manifest.id == pkg.manifest.id)
                    {
                        cur = pkg;
                        exists = true;
                        break;
                    }
                }
                if (!exists) packages.push_back(pkg);
            }
            std::sort(packages.begin(), packages.end(), [](const BrushPackage& a, const BrushPackage& b)
            {
                if (a.builtIn != b.builtIn) return a.builtIn > b.builtIn;
                return a.manifest.name < b.manifest.name;
            });
        }
    };

    inline BrushManager*& globalManagerStorage()
    {
        static BrushManager* g = nullptr;
        return g;
    }

    inline void setGlobalManager(BrushManager* mgr)
    {
        globalManagerStorage() = mgr;
    }

    inline BrushManager* globalManager()
    {
        return globalManagerStorage();
    }
}
