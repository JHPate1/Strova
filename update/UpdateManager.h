/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        update/UpdateManager.h
   Module:      Update
   Purpose:     Update manager state, networking, and install helpers.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include "../platform/AppPaths.h"
#include "../core/AppSettings.h"
#include "../core/DebugLog.h"

#ifdef _WIN32
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace strova {

    struct UpdateInfo
    {
        bool available = false;
        std::string latestVersion;
        std::string downloadUrl;
        std::string platform;
        std::string rawJson;
        std::string errorMessage;
    };

    class UpdateManager
    {
    public:
        enum class State
        {
            Idle,
            Disabled,
            WaitingForDailyWindow,
            Checking,
            UpdateAvailable,
            NoUpdate,
            Downloading,
            Extracting,
            ReadyToLaunchUpdater,
            Failed,
            Declined
        };

        inline UpdateManager() { strova::debug::log("UpdateManager", "Constructed."); }
        inline ~UpdateManager()
        {
            strova::debug::log("UpdateManager", "Destructing. Waiting for worker if needed.");
            if (worker.joinable()) worker.join();
            strova::debug::log("UpdateManager", "Destroyed.");
        }

        inline void beginSplashCheck(const std::string& endpoint, const std::string& currentVersion, const AppSettings& settings)
        {
            strova::debug::log("UpdateManager", std::string("beginSplashCheck called. endpoint=") + endpoint + ", currentVersion=" + currentVersion + ", updateCheckDaily=" + (settings.updateCheckDaily ? "true" : "false") + ", lastUpdateCheckEpoch=" + std::to_string(settings.lastUpdateCheckEpoch));
            if (worker.joinable()) worker.join();

            {
                std::lock_guard<std::mutex> lock(mutex);
                launchRequested = false;
                downloadedZipPath.clear();
                updaterExePath.clear();
                launchArgs.clear();
                info = UpdateInfo{};
                completedCheckEpoch = 0;
            }

            if (!settings.updateCheckDaily)
            {
                strova::debug::log("UpdateManager", "Update checks explicitly disabled in settings.");
                setState(State::Disabled, "Updates are turned off in settings.");
                return;
            }

            const std::int64_t now = detail::nowEpochSeconds();
            const std::int64_t last = settings.lastUpdateCheckEpoch;
            if (last > 0 && (now - last) < 86400)
            {
                strova::debug::log("UpdateManager", "Skipping startup check because daily window has not elapsed yet.");
                setState(State::WaitingForDailyWindow, "Daily update check already completed recently.");
                return;
            }

            strova::debug::log("UpdateManager", "Starting startup update check now.");
            setState(State::Checking, "Pinging server...");
            workerRunning = true;
            worker = std::thread(&UpdateManager::checkWorker, this, endpoint, currentVersion);
        }

        inline void declineUpdate()
        {
            strova::debug::log("UpdateManager", "User declined or dismissed update flow.");
            setState(State::Declined, "Update skipped.");
        }

        inline void startApplyUpdate()
        {
            strova::debug::log("UpdateManager", "User accepted update flow. Preparing package download.");
            if (worker.joinable()) worker.join();
            setState(State::Downloading, "Pinging server and downloading package...");
            workerRunning = true;
            worker = std::thread(&UpdateManager::applyWorker, this);
        }

        inline State getState() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return state;
        }

        inline std::string getStatusLine() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return statusLine;
        }

        inline UpdateInfo getInfo() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return info;
        }

        inline bool shouldBlockSplash() const
        {
            const State s = getState();
            return s == State::Checking || s == State::UpdateAvailable || s == State::Downloading ||
                s == State::Extracting || s == State::Failed || s == State::ReadyToLaunchUpdater;
        }

        inline bool takeLaunchRequest(std::filesystem::path& outUpdaterExe, std::string& outArgs)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!launchRequested) return false;
            launchRequested = false;
            outUpdaterExe = updaterExePath;
            outArgs = launchArgs;
            return true;
        }

        inline std::int64_t getCompletedCheckEpoch() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return completedCheckEpoch;
        }

        inline static bool launchDetached(const std::filesystem::path& exePath, const std::string& args, std::string& outErr)
        {
#ifdef _WIN32
            if ((INT_PTR)ShellExecuteA(nullptr, "open", exePath.string().c_str(), args.c_str(), exePath.parent_path().string().c_str(), SW_HIDE) <= 32)
            {
                outErr = "Could not start updater process.";
                return false;
            }
            return true;
#else
            pid_t pid = fork();
            if (pid < 0)
            {
                outErr = "Could not fork updater process.";
                return false;
            }
            if (pid == 0)
            {
                std::string cmd = detail::escapeCmdArg(exePath.string());
                if (!args.empty()) cmd += " " + args;
                execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
                std::_Exit(127);
            }
            return true;
#endif
        }

    private:
        inline void setState(State newState, const std::string& status = std::string())
        {
            std::lock_guard<std::mutex> lock(mutex);
            state = newState;
            if (!status.empty()) statusLine = status;
            strova::debug::log("UpdateManager", std::string("State changed to ") + stateToString(newState) + (status.empty() ? std::string() : std::string(" | ") + status));
        }

        inline void checkWorker(std::string endpoint, std::string currentVersion)
        {
            std::string body;
            std::string err;
            const std::string url = buildCheckUrl(endpoint, currentVersion);
            strova::debug::log("UpdateManager", std::string("checkWorker started. URL=") + url);

            if (!httpGetToString(url, body, err))
            {
                setInfoErrorAndEpoch(err);
                strova::debug::log("UpdateManager", std::string("Startup update check failed. ") + err);
                setState(State::Failed, "Could not contact update server.");
                workerRunning = false;
                return;
            }

            strova::debug::log("UpdateManager", std::string("Raw update response: ") + body);
            const std::string platform = strova::paths::getPlatformKey();
            bool updateFlag = false;
            bool hasUpdateField = detail::findBoolField(body, "update", updateFlag);
            const std::string latestVersion = detail::trim(
                detail::findStringField(body, "version").empty()
                ? detail::findStringField(body, "latest")
                : detail::findStringField(body, "version")
            );

            std::string downloadUrl = detail::findStringField(body, "url");
            if (downloadUrl.empty())
            {
                const std::string platformObj = detail::findPlatformObject(body, platform);
                downloadUrl = detail::findStringField(platformObj, "url");
            }

            const bool sameVersion = detail::versionsEquivalent(latestVersion, currentVersion);
            const bool updateAvailable = !sameVersion && ((hasUpdateField && updateFlag) || (!hasUpdateField && !downloadUrl.empty()));

            setCheckResultLocked(updateAvailable, latestVersion, downloadUrl, platform, body);

            strova::debug::log(
                "UpdateManager",
                std::string("Parsed update JSON. hasUpdateField=") + (hasUpdateField ? "true" : "false") +
                ", updateFlag=" + (updateFlag ? "true" : "false") +
                ", latestVersion=" + latestVersion +
                ", currentVersion=" + currentVersion +
                ", sameVersion=" + (sameVersion ? "true" : "false") +
                ", platform=" + platform +
                ", downloadUrl=" + downloadUrl
            );

            if (sameVersion)
                setState(State::NoUpdate, "You are up to date.");
            else if (!updateAvailable)
                setState(State::NoUpdate, "You are up to date.");
            else
                setState(State::UpdateAvailable, latestVersion.empty() ? "Update available." : ("Update " + latestVersion + " is available."));

            workerRunning = false;
        }

        inline void applyWorker()
        {
            UpdateInfo localInfo = getInfo();
            std::string err;
            strova::debug::log("UpdateManager", std::string("applyWorker started. downloadUrl=") + localInfo.downloadUrl);

            const std::filesystem::path rootDir = strova::paths::getExecutableDir();

            const std::filesystem::path updateDir = rootDir / "update";
            const std::filesystem::path packageDir = rootDir / "update_package";
            const std::filesystem::path zipPath = updateDir / "update.zip";

            std::error_code ec;
            std::filesystem::create_directories(updateDir, ec);
            std::filesystem::create_directories(packageDir, ec);

            strova::debug::logPath("UpdateManager", "Executable root", rootDir);
            strova::debug::logPath("UpdateManager", "Update directory", updateDir);
            strova::debug::logPath("UpdateManager", "Package directory", packageDir);
            strova::debug::logPath("UpdateManager", "ZIP destination", zipPath);

            if (!downloadToFile(localInfo.downloadUrl, zipPath, err))
            {
                setApplyError(err);
                strova::debug::log("UpdateManager", std::string("Package download failed. ") + err);
                setState(State::Failed, "Download failed.");
                workerRunning = false;
                return;
            }

            strova::debug::log("UpdateManager", "Package download completed. Beginning extraction to update_package folder.");
            setState(State::Extracting, "Unzipping update package...");

            if (!extractZipToRoot(zipPath, packageDir, err))
            {
                setApplyError(err);
                strova::debug::log("UpdateManager", std::string("Package extraction failed. ") + err);
                setState(State::Failed, "Could not unzip update package.");
                workerRunning = false;
                return;
            }

            std::filesystem::path updaterExe = findUpdaterExecutable(packageDir);
            strova::debug::logPath("UpdateManager", "Updater executable resolved", updaterExe);

            if (updaterExe.empty() || !std::filesystem::exists(updaterExe))
            {
                setApplyError("Expected updater executable was not found in update_package or its extracted subfolders.");
                strova::debug::log("UpdateManager", "Expected updater executable missing after extraction.");
                setState(State::Failed, "Update unpacked, but updater executable is missing.");
                workerRunning = false;
                return;
            }

#ifdef _WIN32
            const DWORD pid = GetCurrentProcessId();
#else
            const int pid = (int)getpid();
#endif

            const std::string args =
                "--wait-pid " + std::to_string(pid) +
                " --app-root " + detail::escapeCmdArg(rootDir.string()) +
                " --current-exe " + detail::escapeCmdArg(strova::paths::getExecutablePath().string());

            strova::debug::log("UpdateManager", std::string("Updater launch args prepared: ") + args);

            {
                std::lock_guard<std::mutex> lock(mutex);
                downloadedZipPath = zipPath;
                updaterExePath = updaterExe;
                launchArgs = args;
                launchRequested = true;
            }

            strova::debug::log("UpdateManager", "Update package extracted and updater launch requested.");
            setState(State::ReadyToLaunchUpdater, "Update is ready. Restarting into updater...");
            workerRunning = false;
        }

        inline static bool httpGetToString(const std::string& url, std::string& outBody, std::string& outErr)
        {
            const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / "strova_update_check.json";
            strova::debug::logPath("UpdateManager", "Fetching update JSON into temp file", tempPath);
            if (!downloadToFile(url, tempPath, outErr)) return false;
            outBody = detail::readWholeFile(tempPath);
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            if (outBody.empty())
            {
                outErr = "Empty response from update endpoint.";
                return false;
            }
            strova::debug::log("UpdateManager", std::string("Fetched response body bytes=") + std::to_string(outBody.size()));
            return true;
        }

        inline static bool downloadToFile(const std::string& url, const std::filesystem::path& dest, std::string& outErr)
        {
            std::error_code ec;
            std::filesystem::create_directories(dest.parent_path(), ec);
#ifdef _WIN32
            const std::string cmd = "powershell -NoProfile -ExecutionPolicy Bypass -Command \"try { Invoke-WebRequest -UseBasicParsing -Uri '" + url + "' -OutFile '" + dest.string() + "' } catch { exit 1 }\"";
#else
            const std::string cmd = "curl -L --fail -o " + detail::escapeCmdArg(dest.string()) + " " + detail::escapeCmdArg(url);
#endif
            strova::debug::log("UpdateManager", std::string("Executing download command: ") + cmd);
            return detail::runCommand(cmd, outErr);
        }

        inline static bool extractZipToRoot(const std::filesystem::path& zipPath, const std::filesystem::path& rootDir, std::string& outErr)
        {
#ifdef _WIN32
            const std::string cmd = "powershell -NoProfile -ExecutionPolicy Bypass -Command \"try { Expand-Archive -LiteralPath '" + zipPath.string() + "' -DestinationPath '" + rootDir.string() + "' -Force } catch { exit 1 }\"";
#else
            const std::string cmd = "unzip -o " + detail::escapeCmdArg(zipPath.string()) + " -d " + detail::escapeCmdArg(rootDir.string());
#endif
            strova::debug::log("UpdateManager", std::string("Executing extract command: ") + cmd);
            return detail::runCommand(cmd, outErr);
        }

        inline static std::string buildCheckUrl(const std::string& endpoint, const std::string& currentVersion)
        {
            const std::string sep = (endpoint.find('?') == std::string::npos) ? "?" : "&";
            return endpoint + sep + "platform=" + strova::paths::getPlatformKey() + "&version=" + currentVersion;
        }

        inline static std::filesystem::path findUpdaterExecutable(const std::filesystem::path& packageDir)
        {
            const std::vector<std::filesystem::path> directCandidates = {
    #ifdef _WIN32
                packageDir / "update_replace.exe",
                packageDir / "update" / "update_replace.exe",
                packageDir / "update_package" / "update_replace.exe",
                packageDir / "updater" / "update_replace.exe",
    #else
                packageDir / "update_replace",
                packageDir / "update_replace.exe",
                packageDir / "update" / "update_replace",
                packageDir / "update" / "update_replace.exe",
                packageDir / "update_package" / "update_replace",
                packageDir / "update_package" / "update_replace.exe",
                packageDir / "updater" / "update_replace",
                packageDir / "updater" / "update_replace.exe",
    #endif
            };

            for (const auto& candidate : directCandidates)
            {
                strova::debug::logPath("UpdateManager", "Checking updater candidate", candidate);
                if (std::filesystem::exists(candidate) && std::filesystem::is_regular_file(candidate))
                    return candidate;
            }

            std::error_code ec;
            if (!std::filesystem::exists(packageDir, ec))
                return {};

            for (std::filesystem::recursive_directory_iterator it(packageDir, ec), end; it != end; it.increment(ec))
            {
                if (ec) break;
                if (!it->is_regular_file()) continue;

                const auto name = it->path().filename().string();
                if (name == "update_replace" || name == "update_replace.exe")
                {
                    strova::debug::logPath("UpdateManager", "Found updater via recursive search", it->path());
                    return it->path();
                }
            }

            return {};
        }

    private:
        inline void setInfoErrorAndEpoch(const std::string& error)
        {
            std::lock_guard<std::mutex> guard(mutex);
            info.errorMessage = error;
            completedCheckEpoch = detail::nowEpochSeconds();
        }

        inline void setCheckResultLocked(bool updateAvailable, const std::string& latestVersion,
            const std::string& downloadUrl, const std::string& platform, const std::string& rawJson)
        {
            std::lock_guard<std::mutex> guard(mutex);
            info.available = updateAvailable;
            info.latestVersion = latestVersion;
            info.downloadUrl = updateAvailable ? downloadUrl : std::string{};
            info.platform = platform;
            info.rawJson = rawJson;
            info.errorMessage.clear();
            completedCheckEpoch = detail::nowEpochSeconds();
        }

        inline void setApplyError(const std::string& error)
        {
            std::lock_guard<std::mutex> guard(mutex);
            info.errorMessage = error;
        }

        inline static const char* stateToString(State s)
        {
            switch (s)
            {
            case State::Idle: return "Idle";
            case State::Disabled: return "Disabled";
            case State::WaitingForDailyWindow: return "WaitingForDailyWindow";
            case State::Checking: return "Checking";
            case State::UpdateAvailable: return "UpdateAvailable";
            case State::NoUpdate: return "NoUpdate";
            case State::Downloading: return "Downloading";
            case State::Extracting: return "Extracting";
            case State::ReadyToLaunchUpdater: return "ReadyToLaunchUpdater";
            case State::Failed: return "Failed";
            case State::Declined: return "Declined";
            }
            return "Unknown";
        }

        struct detail
        {
            inline static std::string trim(const std::string& s)
            {
                size_t a = 0;
                while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
                size_t b = s.size();
                while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
                return s.substr(a, b - a);
            }

            inline static std::string readWholeFile(const std::filesystem::path& p)
            {
                std::ifstream f(p, std::ios::binary);
                if (!f) return {};
                std::ostringstream ss;
                ss << f.rdbuf();
                return ss.str();
            }

            inline static std::string escapeCmdArg(const std::string& s)
            {
                std::string out = "\"";
                for (char c : s)
                {
                    if (c == '\\' || c == '\"') out.push_back('\\');
                    out.push_back(c);
                }
                out.push_back('\"');
                return out;
            }

            inline static bool runCommand(const std::string& cmd, std::string& outErr)
            {
                const int rc = std::system(cmd.c_str());
                if (rc != 0)
                {
                    outErr = "Command failed: " + cmd;
                    strova::debug::log("UpdateManager", std::string("Command failed with rc=") + std::to_string(rc) + ". " + outErr);
                    return false;
                }
                strova::debug::log("UpdateManager", std::string("Command succeeded with rc=") + std::to_string(rc));
                return true;
            }

            inline static std::string findStringField(const std::string& text, const std::string& key)
            {
                const std::string marker = "\"" + key + "\"";
                size_t pos = text.find(marker);
                if (pos == std::string::npos) return {};
                pos = text.find(':', pos);
                if (pos == std::string::npos) return {};
                pos = text.find('\"', pos);
                if (pos == std::string::npos) return {};
                ++pos;
                std::string out;
                bool esc = false;
                while (pos < text.size())
                {
                    char c = text[pos++];
                    if (esc) { out.push_back(c); esc = false; continue; }
                    if (c == '\\') { esc = true; continue; }
                    if (c == '\"') break;
                    out.push_back(c);
                }
                return out;
            }

            inline static bool findBoolField(const std::string& text, const std::string& key, bool& outValue)
            {
                const std::string marker = "\"" + key + "\"";
                size_t pos = text.find(marker);
                if (pos == std::string::npos) return false;
                pos = text.find(':', pos);
                if (pos == std::string::npos) return false;
                ++pos;
                while (pos < text.size() && std::isspace((unsigned char)text[pos])) ++pos;
                if (text.compare(pos, 4, "true") == 0) { outValue = true; return true; }
                if (text.compare(pos, 5, "false") == 0) { outValue = false; return true; }
                return false;
            }

            inline static std::string findPlatformObject(const std::string& json, const std::string& platform)
            {
                const std::string marker = "\"" + platform + "\"";
                size_t pos = json.find(marker);
                if (pos == std::string::npos) return {};
                size_t open = json.find('{', pos);
                if (open == std::string::npos) return {};
                int depth = 0;
                for (size_t i = open; i < json.size(); ++i)
                {
                    if (json[i] == '{') depth++;
                    else if (json[i] == '}')
                    {
                        depth--;
                        if (depth == 0) return json.substr(open, i - open + 1);
                    }
                }
                return {};
            }

            inline static std::string normalizeVersion(std::string s)
            {
                s = trim(s);
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });

                if (!s.empty() && s[0] == 'v')
                    s.erase(s.begin());

                s = trim(s);

                while (!s.empty() && (s.back() == '.' || std::isspace((unsigned char)s.back())))
                    s.pop_back();

                return s;
            }

            inline static bool versionsEquivalent(const std::string& a, const std::string& b)
            {
                const std::string na = normalizeVersion(a);
                const std::string nb = normalizeVersion(b);
                return !na.empty() && !nb.empty() && na == nb;
            }

            inline static std::int64_t nowEpochSeconds()
            {
                using namespace std::chrono;
                return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
            }
        };

        mutable std::mutex mutex;
        State state = State::Idle;
        std::string statusLine;
        UpdateInfo info;
        std::thread worker;
        std::atomic<bool> workerRunning{ false };
        std::filesystem::path downloadedZipPath;
        std::filesystem::path updaterExePath;
        std::string launchArgs;
        bool launchRequested = false;
        std::int64_t completedCheckEpoch = 0;
    };

}