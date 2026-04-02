#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace strova::iojson {

namespace fs = std::filesystem;

inline bool readTextFile(const fs::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

inline bool writeTextFileAtomic(const fs::path& p, const std::string& s)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";

    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(s.data(), static_cast<std::streamsize>(s.size()));
        f.flush();
        if (!f) return false;
    }

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

inline std::string jsonEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 16);
    for (char c : s)
    {
        switch (c)
        {
        case '\\': o += "\\\\"; break;
        case '"':  o += "\\\""; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if ((unsigned char)c >= 0x20) o += c;
            break;
        }
    }
    return o;
}

inline void skipWs(const std::string& s, size_t& i)
{
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
}

inline bool consume(const std::string& s, size_t& i, char c)
{
    skipWs(s, i);
    if (i < s.size() && s[i] == c) { ++i; return true; }
    return false;
}

inline bool findKeyPosAfterColon(const std::string& j, const char* key, size_t& outPos)
{
    if (!key) return false;
    const auto k = std::string("\"") + key + "\"";
    size_t pos = j.find(k);
    if (pos == std::string::npos) return false;
    pos = j.find(':', pos);
    if (pos == std::string::npos) return false;
    outPos = pos + 1;
    return true;
}

inline bool parseIntAt(const std::string& j, size_t& i, int& out)
{
    skipWs(j, i);
    size_t start = i;
    if (i < j.size() && (j[i] == '-' || j[i] == '+')) ++i;
    while (i < j.size() && std::isdigit((unsigned char)j[i])) ++i;
    if (i == start) return false;
    try { out = std::stoi(j.substr(start, i - start)); return true; }
    catch (...) { return false; }
}

inline bool parseFloatAt(const std::string& j, size_t& i, float& out)
{
    skipWs(j, i);
    size_t start = i;
    bool dot = false;
    if (i < j.size() && (j[i] == '-' || j[i] == '+')) ++i;
    while (i < j.size())
    {
        const char c = j[i];
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
    skipWs(j, i);
    if (j.compare(i, 4, "true") == 0) { out = true; i += 4; return true; }
    if (j.compare(i, 5, "false") == 0) { out = false; i += 5; return true; }
    if (i < j.size() && j[i] == '1') { out = true; ++i; return true; }
    if (i < j.size() && j[i] == '0') { out = false; ++i; return true; }
    return false;
}

inline bool parseStringAt(const std::string& j, size_t& i, std::string& out)
{
    skipWs(j, i);
    if (i >= j.size() || j[i] != '"') return false;
    ++i;
    std::string s;
    while (i < j.size())
    {
        const char c = j[i++];
        if (c == '"') break;
        if (c == '\\' && i < j.size())
        {
            const char e = j[i++];
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

inline int findInt(const std::string& j, const char* key, int def)
{
    size_t pos = 0;
    if (!findKeyPosAfterColon(j, key, pos)) return def;
    int v = def;
    return parseIntAt(j, pos, v) ? v : def;
}

inline std::string findStr(const std::string& j, const char* key, const std::string& def)
{
    size_t pos = 0;
    if (!findKeyPosAfterColon(j, key, pos)) return def;
    std::string v;
    return parseStringAt(j, pos, v) ? v : def;
}

inline std::string extractArrayText(const std::string& j, const char* key)
{
    size_t pos = 0;
    if (!findKeyPosAfterColon(j, key, pos)) return {};
    pos = j.find('[', pos);
    if (pos == std::string::npos) return {};
    const size_t start = pos;
    int depth = 0;
    bool inStr = false;
    for (size_t i = pos; i < j.size(); ++i)
    {
        const char c = j[i];
        if (c == '"' && (i == 0 || j[i - 1] != '\\')) inStr = !inStr;
        if (inStr) continue;
        if (c == '[') ++depth;
        else if (c == ']')
        {
            --depth;
            if (depth == 0) return j.substr(start, (i - start) + 1);
        }
    }
    return {};
}

inline void parseObjectsInArray(const std::string& arrText, std::vector<std::string>& outObjs)
{
    outObjs.clear();
    size_t i = 0;
    while (i < arrText.size() && arrText[i] != '[') ++i;
    if (i >= arrText.size()) return;
    ++i;
    while (i < arrText.size())
    {
        skipWs(arrText, i);
        if (i < arrText.size() && arrText[i] == ']') break;
        size_t objStart = arrText.find('{', i);
        if (objStart == std::string::npos) break;
        int depth = 0;
        bool inStr = false;
        for (size_t k = objStart; k < arrText.size(); ++k)
        {
            const char c = arrText[k];
            if (c == '"' && (k == 0 || arrText[k - 1] != '\\')) inStr = !inStr;
            if (inStr) continue;
            if (c == '{') ++depth;
            else if (c == '}')
            {
                --depth;
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

inline size_t findMatchingBrace(const std::string& s, size_t openPos)
{
    if (openPos == std::string::npos || openPos >= s.size() || s[openPos] != '{')
        return std::string::npos;
    int depth = 0;
    bool inStr = false;
    for (size_t i = openPos; i < s.size(); ++i)
    {
        const char c = s[i];
        if (c == '"' && (i == 0 || s[i - 1] != '\\')) inStr = !inStr;
        if (inStr) continue;
        if (c == '{') ++depth;
        else if (c == '}')
        {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

struct RgbaAssetHeader
{
    std::uint32_t magic = 0x41564752u; // 'RGBA'
    std::uint32_t version = 1u;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint64_t byteCount = 0;
};

inline bool writeRgbaAssetAtomic(const fs::path& p, int width, int height, const std::vector<std::uint8_t>& rgba)
{
    if (width <= 0 || height <= 0) return false;
    const std::uint64_t expected = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ull;
    if (rgba.size() < expected) return false;

    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";

    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    RgbaAssetHeader hdr{};
    hdr.width = width;
    hdr.height = height;
    hdr.byteCount = expected;
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    f.write(reinterpret_cast<const char*>(rgba.data()), static_cast<std::streamsize>(expected));
    f.flush();
    if (!f) return false;
    f.close();

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

inline bool readRgbaAsset(const fs::path& p, int& width, int& height, std::vector<std::uint8_t>& rgba)
{
    width = 0;
    height = 0;
    rgba.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    RgbaAssetHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f || hdr.magic != 0x41564752u || hdr.version != 1u || hdr.width <= 0 || hdr.height <= 0)
        return false;
    const std::uint64_t expected = static_cast<std::uint64_t>(hdr.width) * static_cast<std::uint64_t>(hdr.height) * 4ull;
    if (hdr.byteCount != expected || expected > static_cast<std::uint64_t>(512) * 1024ull * 1024ull)
        return false;
    rgba.resize(static_cast<size_t>(expected));
    f.read(reinterpret_cast<char*>(rgba.data()), static_cast<std::streamsize>(expected));
    if (!f) { rgba.clear(); return false; }
    width = hdr.width;
    height = hdr.height;
    return true;
}

inline std::string normalizeProjectRelativePath(const std::string& s)
{
    std::string out = s;
    std::replace(out.begin(), out.end(), '\\', '/');
    while (!out.empty() && out.front() == '/') out.erase(out.begin());
    return out;
}

} // namespace strova::iojson
