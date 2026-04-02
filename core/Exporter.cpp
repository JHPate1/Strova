/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        core/Exporter.cpp
   Module:      Core
   Purpose:     Export pipeline helpers for PNG, GIF, and video output.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "Exporter.h"
#include "DrawingEngine.h"
#include "Stroke.h"
#include "LayerRenderUtil.h"
#include "../render/BrushRenderer.h"

#include <filesystem>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <array>
#include <optional>
#include <cctype>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

namespace fs = std::filesystem;

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace strova::exporter
{
    namespace
    {
        struct TempSequence
        {
            fs::path folder;
            bool keep = false;

            ~TempSequence()
            {
                if (!keep && !folder.empty())
                {
                    std::error_code ec;
                    fs::remove_all(folder, ec);
                }
            }
        };

        static int clampi(int v, int lo, int hi)
        {
            return std::max(lo, std::min(hi, v));
        }

        static bool isBlank(const std::string& s)
        {
            for (char ch : s)
            {
                if (!std::isspace(static_cast<unsigned char>(ch))) return false;
            }
            return true;
        }

        static void clearTexture(SDL_Renderer* r, SDL_Texture* tex, SDL_Color c)
        {
            SDL_SetRenderTarget(r, tex);
            SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
            SDL_RenderClear(r);
        }

        static void flipRGBAInPlaceVertical(std::vector<unsigned char>& rgba, int w, int h)
        {
            if (w <= 0 || h <= 1) return;
            const size_t rowBytes = (size_t)w * 4;
            std::vector<unsigned char> tmp(rowBytes);

            for (int y = 0; y < h / 2; ++y)
            {
                unsigned char* rowA = rgba.data() + (size_t)y * rowBytes;
                unsigned char* rowB = rgba.data() + (size_t)(h - 1 - y) * rowBytes;

                std::memcpy(tmp.data(), rowA, rowBytes);
                std::memcpy(rowA, rowB, rowBytes);
                std::memcpy(rowB, tmp.data(), rowBytes);
            }
        }

        static bool readTargetTextureRGBA(
            SDL_Renderer* r,
            SDL_Texture* target,
            int w, int h,
            std::vector<unsigned char>& outRGBA)
        {
            if (!r || !target || w <= 0 || h <= 0) return false;

            outRGBA.resize((size_t)w * (size_t)h * 4);

            SDL_SetRenderTarget(r, target);
            int pitch = w * 4;
            int ok = SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, outRGBA.data(), pitch);
            SDL_SetRenderTarget(r, nullptr);

            return ok == 0;
        }

        static bool saveRGBAasPNG(const std::string& path, const unsigned char* rgba, int w, int h, int compressionLevel)
        {
            if (!rgba || w <= 0 || h <= 0) return false;

            stbi_write_png_compression_level = clampi(compressionLevel, 0, 9);
            int stride = w * 4;
            return stbi_write_png(path.c_str(), w, h, 4, rgba, stride) != 0;
        }

        static void resolveFrameRange(
            ::DrawingEngine& engine,
            const Settings& settings,
            int& outStart,
            int& outEnd)
        {
            int total = (int)engine.getFrameCount();
            if (total < 1) total = 1;

            int start = settings.startFrame;
            int end = settings.endFrame;

            if (start < 0) start = 0;
            if (end < 0) end = total - 1;

            start = clampi(start, 0, total - 1);
            end = clampi(end, 0, total - 1);

            if (end < start) std::swap(start, end);

            outStart = start;
            outEnd = end;
        }

        static bool ensureParentDirectory(const fs::path& path)
        {
            std::error_code ec;
            fs::path parent = path.parent_path();
            if (parent.empty()) return true;
            fs::create_directories(parent, ec);
            return !ec;
        }

        static fs::path makeSequenceDirectory(const fs::path& finalTarget)
        {
            const fs::path parent = finalTarget.parent_path().empty() ? fs::current_path() : finalTarget.parent_path();
            const std::string stem = finalTarget.stem().string().empty() ? std::string("strova_export") : finalTarget.stem().string();
            const fs::path base = parent / (stem + "_sequence_tmp");

            std::error_code ec;
            fs::remove_all(base, ec);
            fs::create_directories(base, ec);
            if (!ec) return base;

            for (int i = 1; i <= 64; ++i)
            {
                fs::path candidate = parent / (stem + "_sequence_tmp_" + std::to_string(i));
                fs::remove_all(candidate, ec);
                fs::create_directories(candidate, ec);
                if (!ec) return candidate;
            }

            return {};
        }

        static std::string sequencePatternPath(const fs::path& folder, const Settings& settings)
        {
            std::string base = settings.sequenceBaseName.empty() ? std::string("frame_") : settings.sequenceBaseName;
            return (folder / (base + "%05d.png")).string();
        }

#ifdef _WIN32
        static std::string winQuote(const std::string& s)
        {
            std::string out;
            out.reserve(s.size() + 2);
            out.push_back('"');
            for (char ch : s)
            {
                if (ch == '"') out += "\\\"";
                else out.push_back(ch);
            }
            out.push_back('"');
            return out;
        }

        static bool fileExists(const std::string& path)
        {
            std::error_code ec;
            return !path.empty() && fs::exists(fs::path(path), ec);
        }

        static std::optional<std::string> envValue(const char* name)
        {
            const char* v = std::getenv(name);
            if (!v || !*v) return std::nullopt;
            return std::string(v);
        }

        static std::vector<std::string> commonWindowsFfmpegCandidates()
        {
            std::vector<std::string> out;

            auto pushIf = [&](const std::optional<std::string>& base, const std::string& suffix)
            {
                if (!base || base->empty()) return;
                fs::path p = fs::path(*base) / suffix;
                out.push_back(p.string());
            };

            if (auto localAppData = envValue("LOCALAPPDATA"))
            {
                fs::path wingetRoot = fs::path(*localAppData) / "Microsoft/WinGet/Packages";
                std::error_code ec;
                if (fs::exists(wingetRoot, ec))
                {
                    for (const auto& entry : fs::directory_iterator(wingetRoot, ec))
                    {
                        if (ec) break;
                        const std::string name = entry.path().filename().string();
                        if (name.find("FFmpeg") == std::string::npos && name.find("ffmpeg") == std::string::npos) continue;

                        for (const auto& sub : fs::recursive_directory_iterator(entry.path(), ec))
                        {
                            if (ec) break;
                            if (!sub.is_regular_file()) continue;
                            const std::string filename = sub.path().filename().string();
                            if (_stricmp(filename.c_str(), "ffmpeg.exe") == 0)
                                out.push_back(sub.path().string());
                        }
                    }
                }
            }

            pushIf(envValue("ProgramFiles"), "ffmpeg/bin/ffmpeg.exe");
            pushIf(envValue("ProgramFiles(x86)"), "ffmpeg/bin/ffmpeg.exe");
            pushIf(envValue("SystemDrive"), "ffmpeg/bin/ffmpeg.exe");
            pushIf(envValue("USERPROFILE"), "Downloads/ffmpeg/bin/ffmpeg.exe");
            return out;
        }

        static std::string searchPathForExecutable(const std::string& executable)
        {
            auto pathOpt = envValue("PATH");
            if (!pathOpt) return {};

            std::stringstream ss(*pathOpt);
            std::string item;
            while (std::getline(ss, item, ';'))
            {
                if (item.empty()) continue;
                fs::path candidate = fs::path(item) / executable;
                if (fileExists(candidate.string())) return candidate.string();
            }
            return {};
        }

        static std::string readAllFromHandle(HANDLE h)
        {
            std::string out;
            if (!h) return out;

            char buf[4096];
            DWORD got = 0;
            for (;;)
            {
                BOOL ok = ReadFile(h, buf, (DWORD)sizeof(buf), &got, nullptr);
                if (!ok || got == 0) break;
                out.append(buf, buf + got);
            }
            return out;
        }

        static const char* mp4PresetFromIndex(int idx)
        {
            idx = clampi(idx, 1, 8);
            switch (idx)
            {
            case 1: return "ultrafast";
            case 2: return "superfast";
            case 3: return "veryfast";
            case 4: return "faster";
            case 5: return "fast";
            case 6: return "medium";
            case 7: return "slow";
            case 8: return "veryslow";
            default: return "superfast";
            }
        }

        static bool runFfmpegWindows(
            const std::string& ffmpegExe,
            const std::string& arguments,
            std::string* outLog)
        {
            if (ffmpegExe.empty()) return false;

            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;

            HANDLE errRead = NULL, errWrite = NULL;
            if (!CreatePipe(&errRead, &errWrite, &sa, 0))
                return false;
            SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

            std::string cmd = winQuote(ffmpegExe) + " " + arguments;
            std::vector<char> cmdBuf(cmd.begin(), cmd.end());
            cmdBuf.push_back('\0');

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            si.hStdError = errWrite;

            PROCESS_INFORMATION pi{};
            BOOL ok = CreateProcessA(
                nullptr,
                cmdBuf.data(),
                nullptr,
                nullptr,
                TRUE,
                CREATE_NO_WINDOW,
                nullptr,
                nullptr,
                &si,
                &pi);

            CloseHandle(errWrite);
            if (!ok)
            {
                CloseHandle(errRead);
                return false;
            }

            WaitForSingleObject(pi.hProcess, INFINITE);

            DWORD exitCode = 1;
            GetExitCodeProcess(pi.hProcess, &exitCode);

            if (outLog) *outLog = readAllFromHandle(errRead);
            CloseHandle(errRead);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return exitCode == 0;
        }
#else
        static bool fileExists(const std::string& path)
        {
            std::error_code ec;
            return !path.empty() && fs::exists(fs::path(path), ec);
        }

        static std::optional<std::string> envValue(const char* name)
        {
            const char* v = std::getenv(name);
            if (!v || !*v) return std::nullopt;
            return std::string(v);
        }

        static std::string shellQuote(const std::string& s)
        {
            std::string out = "'";
            for (char ch : s)
            {
                if (ch == '\'') out += "'\\''";
                else out.push_back(ch);
            }
            out += "'";
            return out;
        }

        static std::string searchPathForExecutable(const std::string& executable)
        {
            auto pathOpt = envValue("PATH");
            if (!pathOpt) return {};

            std::stringstream ss(*pathOpt);
            std::string item;
            while (std::getline(ss, item, ':'))
            {
                if (item.empty()) continue;
                fs::path candidate = fs::path(item) / executable;
                if (fileExists(candidate.string())) return candidate.string();
            }
            return {};
        }

        static const char* mp4PresetFromIndex(int idx)
        {
            idx = clampi(idx, 1, 8);
            switch (idx)
            {
            case 1: return "ultrafast";
            case 2: return "superfast";
            case 3: return "veryfast";
            case 4: return "faster";
            case 5: return "fast";
            case 6: return "medium";
            case 7: return "slow";
            case 8: return "veryslow";
            default: return "superfast";
            }
        }

        static bool runFfmpegPosix(
            const std::string& ffmpegExe,
            const std::string& arguments,
            std::string* outLog)
        {
            if (ffmpegExe.empty()) return false;
            std::string cmd = shellQuote(ffmpegExe) + " " + arguments + " 2>&1";
            FILE* pipe = popen(cmd.c_str(), "r");
            if (!pipe) return false;

            std::string log;
            std::array<char, 4096> buf{};
            while (fgets(buf.data(), (int)buf.size(), pipe))
                log += buf.data();

            int rc = pclose(pipe);
            if (outLog) *outLog = log;
            if (WIFEXITED(rc)) return WEXITSTATUS(rc) == 0;
            return false;
        }
#endif

        static bool encodeAnimatedFromSequence(
            const std::string& ffmpegExe,
            const std::string& sequencePattern,
            const std::string& outPath,
            const Settings& settings,
            const std::string& argsAfterInput,
            std::string* outLog)
        {
#ifdef _WIN32
            std::string args =
                "-y -hide_banner -loglevel warning "
                "-framerate " + std::to_string(std::max(1, settings.fps)) + " "
                "-start_number " + std::to_string(std::max(0, settings.sequenceStartNumber)) + " "
                "-i " + winQuote(sequencePattern) + " " +
                argsAfterInput + " " +
                winQuote(outPath);
            return runFfmpegWindows(ffmpegExe, args, outLog);
#else
            std::string args =
                "-y -hide_banner -loglevel warning "
                "-framerate " + std::to_string(std::max(1, settings.fps)) + " "
                "-start_number " + std::to_string(std::max(0, settings.sequenceStartNumber)) + " "
                "-i " + shellQuote(sequencePattern) + " " +
                argsAfterInput + " " +
                shellQuote(outPath);
            return runFfmpegPosix(ffmpegExe, args, outLog);
#endif
        }

        static bool exportAnimatedViaSequence(
            SDL_Renderer* renderer,
            ::DrawingEngine& engine,
            const std::string& filePath,
            const Settings& settings,
            const std::string& ffmpegArgs)
        {
            fs::path finalTarget(filePath);
            if (!ensureParentDirectory(finalTarget))
            {
                SDL_Log("Export: failed to create parent folder for %s", filePath.c_str());
                return false;
            }

            TempSequence sequence;
            sequence.folder = makeSequenceDirectory(finalTarget);
            sequence.keep = settings.keepSequenceForAnimatedFormats;
            if (sequence.folder.empty())
            {
                SDL_Log("Export: failed to create temporary sequence folder");
                return false;
            }

            if (!exportPNGSequence(renderer, engine, sequence.folder.string(), settings))
            {
                SDL_Log("Export: failed while rendering intermediate PNG sequence");
                return false;
            }

            const std::string ffmpegExe = resolveFFmpegExecutable(settings);
            if (ffmpegExe.empty())
            {
                SDL_Log("Export: FFmpeg not found. Set STROVA_FFMPEG, FFMPEG_PATH, or install ffmpeg in PATH.");
                sequence.keep = true;
                return false;
            }

            std::string ffmpegLog;
            const std::string sequencePattern = sequencePatternPath(sequence.folder, settings);
            const bool ok = encodeAnimatedFromSequence(ffmpegExe, sequencePattern, filePath, settings, ffmpegArgs, &ffmpegLog);
            if (!ffmpegLog.empty())
                SDL_Log("FFmpeg:\n%s", ffmpegLog.c_str());
            if (!ok)
            {
                SDL_Log("Export: FFmpeg encode failed for %s", filePath.c_str());
                sequence.keep = true;
            }
            return ok;
        }
    }

    ValidationResult validateSettings(
        const DrawingEngine& engine,
        const Settings& settings,
        const std::string& targetPath,
        bool requiresAnimatedEncoder)
    {
        ValidationResult result;

        if (settings.width <= 0 || settings.height <= 0)
        {
            result.ok = false;
            result.message = "Export width and height must be positive.";
            return result;
        }
        if (settings.fps <= 0)
        {
            result.ok = false;
            result.message = "Export FPS must be positive.";
            return result;
        }
        if (engine.getFrameCount() == 0)
        {
            result.ok = false;
            result.message = "Export requires at least one frame.";
            return result;
        }
        if (targetPath.empty() || isBlank(targetPath))
        {
            result.ok = false;
            result.message = "Export target path is empty.";
            return result;
        }

        std::error_code ec;
        fs::path target(targetPath);
        fs::path parent = target.parent_path().empty() ? fs::current_path() : target.parent_path();
        if (!fs::exists(parent, ec))
        {
            fs::create_directories(parent, ec);
            if (ec)
            {
                result.ok = false;
                result.message = "Export target folder could not be created.";
                return result;
            }
        }

        if (settings.startFrame > settings.endFrame && settings.endFrame >= 0)
        {
            result.ok = false;
            result.message = "Start frame cannot be after end frame.";
            return result;
        }

        if (requiresAnimatedEncoder)
        {
            const std::string ffmpegExe = resolveFFmpegExecutable(settings);
            if (ffmpegExe.empty())
            {
                result.ok = false;
                result.message = "FFmpeg was not found. Install FFmpeg or set STROVA_FFMPEG / FFMPEG_PATH.";
                return result;
            }
        }

        return result;
    }

    std::string resolveFFmpegExecutable(const Settings& settings)
    {
        if (!settings.ffmpegPathOverride.empty() && fileExists(settings.ffmpegPathOverride))
            return settings.ffmpegPathOverride;

        const std::array<const char*, 4> envNames = {
            "STROVA_FFMPEG",
            "STROVA_FFMPEG_PATH",
            "FFMPEG_PATH",
            "FFMPEG"
        };

        for (const char* envName : envNames)
        {
            if (auto val = envValue(envName))
            {
                if (fileExists(*val)) return *val;
            }
        }

#ifdef _WIN32
        if (std::string fromPath = searchPathForExecutable("ffmpeg.exe"); !fromPath.empty())
            return fromPath;
        for (const std::string& candidate : commonWindowsFfmpegCandidates())
        {
            if (fileExists(candidate)) return candidate;
        }
#else
        if (std::string fromPath = searchPathForExecutable("ffmpeg"); !fromPath.empty())
            return fromPath;
        static const std::array<const char*, 4> commonPosix = {
            "/usr/bin/ffmpeg",
            "/usr/local/bin/ffmpeg",
            "/opt/homebrew/bin/ffmpeg",
            "/snap/bin/ffmpeg"
        };
        for (const char* candidate : commonPosix)
        {
            if (fileExists(candidate)) return std::string(candidate);
        }
#endif
        return {};
    }

    bool renderFrameToTexture(
        SDL_Renderer* renderer,
        ::DrawingEngine& engine,
        int frameIndex,
        SDL_Texture*& outTex,
        BrushRenderer& brush,
        const Settings& settings)
    {
        if (!renderer) return false;

        const int W = std::max(1, settings.width);
        const int H = std::max(1, settings.height);

        bool recreate = false;
        if (!outTex)
        {
            recreate = true;
        }
        else
        {
            Uint32 format = 0;
            int access = 0;
            int texW = 0;
            int texH = 0;
            if (SDL_QueryTexture(outTex, &format, &access, &texW, &texH) != 0 || texW != W || texH != H)
                recreate = true;
        }

        if (recreate)
        {
            if (outTex) SDL_DestroyTexture(outTex);
            outTex = SDL_CreateTexture(
                renderer,
                SDL_PIXELFORMAT_RGBA8888,
                SDL_TEXTUREACCESS_TARGET,
                W, H);
            if (outTex) SDL_SetTextureBlendMode(outTex, SDL_BLENDMODE_BLEND);
        }

        if (!outTex) return false;

        clearTexture(renderer, outTex, settings.bgColor);
        SDL_SetRenderTarget(renderer, outTex);

        std::unordered_map<std::string, SDL_Texture*> imageCache;
        std::vector<int> drawOrder;
        drawOrder.reserve(engine.getTracks().size());
        for (const auto& tr : engine.getTracks())
        {
            if (tr.kind == DrawingEngine::TrackKind::Draw || tr.kind == DrawingEngine::TrackKind::Flow)
                drawOrder.push_back(tr.id);
        }

        for (int trackId : drawOrder)
        {
            const auto* tr = engine.findTrack(trackId);
            if (!tr) continue;
            if (tr->kind != DrawingEngine::TrackKind::Draw && tr->kind != DrawingEngine::TrackKind::Flow) continue;
            if (!tr->visible || tr->muted) continue;

            const auto layer = engine.getEvaluatedFrameTrackLayerCopy((size_t)frameIndex, tr->id);
            if (layer.trackId == 0 || !layer.visible) continue;
            if (layer.strokes.empty() && layer.image.empty()) continue;

            const std::string imageKey = std::to_string(frameIndex) + ":" + std::to_string(tr->id) + ":" + std::to_string(layer.celId) + ":" + std::to_string(layer.imageRevision);
            const std::string strokeKey = std::to_string(frameIndex) + ":export:" + std::to_string(tr->id) + ":" + std::to_string(layer.contentRevision) + ":" + std::to_string(layer.transformRevision);
            strova::layer_render::drawTrackLayer(
                renderer,
                brush,
                layer,
                1.0f,
                0.0f,
                0.0f,
                0,
                0,
                1.0f,
                &imageCache,
                imageKey,
                strokeKey,
                0);
        }
        for (auto& it : imageCache)
            if (it.second) SDL_DestroyTexture(it.second);

        SDL_SetRenderTarget(renderer, nullptr);
        return true;
    }

    bool exportPNGSequence(
        SDL_Renderer* renderer,
        ::DrawingEngine& engine,
        const std::string& folder,
        const Settings& settings)
    {
        ValidationResult validation = validateSettings(engine, settings, folder + "/dummy.png", false);
        if (!validation.ok)
        {
            SDL_Log("PNG export validation failed: %s", validation.message.c_str());
            return false;
        }
        if (!renderer) return false;

        std::error_code ec;
        fs::create_directories(folder, ec);
        if (ec)
        {
            SDL_Log("PNG export failed to create folder: %s", folder.c_str());
            return false;
        }

        if (settings.pngInterlace)
        {
            SDL_Log("PNG: interlace requested but stb_image_write does not support it (ignored).");
        }

        SDL_Texture* frameTex = nullptr;
        std::vector<unsigned char> rgba;
        BrushRenderer brush(renderer);

        int start = 0, end = 0;
        resolveFrameRange(engine, settings, start, end);

        const int W = std::max(1, settings.width);
        const int H = std::max(1, settings.height);

        int outIndex = std::max(0, settings.sequenceStartNumber);
        const std::string base = settings.sequenceBaseName.empty() ? std::string("frame_") : settings.sequenceBaseName;

        for (int i = start; i <= end; ++i, ++outIndex)
        {
            if (!renderFrameToTexture(renderer, engine, i, frameTex, brush, settings))
            {
                if (frameTex) SDL_DestroyTexture(frameTex);
                return false;
            }

            if (!readTargetTextureRGBA(renderer, frameTex, W, H, rgba))
            {
                if (frameTex) SDL_DestroyTexture(frameTex);
                return false;
            }

            if (settings.flipVertical)
                flipRGBAInPlaceVertical(rgba, W, H);

            std::stringstream ss;
            ss << folder << "/" << base << std::setw(5) << std::setfill('0') << outIndex << ".png";

            if (!saveRGBAasPNG(ss.str(), rgba.data(), W, H, settings.pngCompression))
            {
                if (frameTex) SDL_DestroyTexture(frameTex);
                return false;
            }
        }

        if (frameTex) SDL_DestroyTexture(frameTex);
        return true;
    }

    bool exportGIF(
        SDL_Renderer* renderer,
        ::DrawingEngine& engine,
        const std::string& filePath,
        const Settings& settings)
    {
        ValidationResult validation = validateSettings(engine, settings, filePath, true);
        if (!validation.ok)
        {
            SDL_Log("GIF export validation failed: %s", validation.message.c_str());
            return false;
        }

        const int scalePct = clampi(settings.gifScalePct, 10, 100);
        const int maxColors = clampi(settings.gifMaxColors, 2, 256);
        const std::string scaleExpr =
            "scale=iw*" + std::to_string(scalePct) + "/100:ih*" + std::to_string(scalePct) + "/100:flags=lanczos";
        const std::string ditherStr = settings.gifDither ? "sierra2_4a" : "none";
        const std::string loopArg = settings.gifLoop ? "-loop 0" : "-loop -1";
        const std::string args =
            loopArg + " "
            "-vf \"fps=" + std::to_string(std::max(1, settings.fps)) + "," +
            scaleExpr + ",split[s0][s1];[s0]palettegen=stats_mode=diff:max_colors=" + std::to_string(maxColors) +
            "[p];[s1][p]paletteuse=dither=" + ditherStr + "\" -f gif";
        return exportAnimatedViaSequence(renderer, engine, filePath, settings, args);
    }

    bool exportMP4(
        SDL_Renderer* renderer,
        ::DrawingEngine& engine,
        const std::string& filePath,
        const Settings& settings)
    {
        ValidationResult validation = validateSettings(engine, settings, filePath, true);
        if (!validation.ok)
        {
            SDL_Log("MP4 export validation failed: %s", validation.message.c_str());
            return false;
        }

        if (settings.includeAlpha)
        {
            SDL_Log("MP4: alpha requested, but H.264 MP4 has no alpha channel. Background color will be baked in.");
        }

        const int crf = clampi(settings.mp4Crf, 0, 51);
        const char* preset = mp4PresetFromIndex(settings.mp4PresetIndex);
        const char* pixfmt = settings.mp4UseYuv420 ? "yuv420p" : "yuv444p";

        std::string args =
            "-an -c:v libx264 -preset " + std::string(preset) + " ";

        if (settings.mp4BitrateKbps > 0)
        {
            int br = std::max(1, settings.mp4BitrateKbps);
            int buf = br * 2;
            args +=
                "-b:v " + std::to_string(br) + "k "
                "-maxrate " + std::to_string(br) + "k "
                "-bufsize " + std::to_string(buf) + "k ";
        }
        else
        {
            args += "-crf " + std::to_string(crf) + " ";
        }

        if (settings.mp4UseBaselineProfile)
            args += "-profile:v baseline ";

        args += "-pix_fmt " + std::string(pixfmt) + " ";
        if (settings.mp4FastStart)
            args += "-movflags +faststart ";
        args += "-f mp4";

        return exportAnimatedViaSequence(renderer, engine, filePath, settings, args);
    }
}
