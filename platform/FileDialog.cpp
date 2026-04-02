/* ============================================================================

   STROVA - 2D ANIMATION ENGINE
   ----------------------------------------------------------------------------
   File:        platform/FileDialog.cpp
   Module:      Platform
   Purpose:     Native file dialog bridge code.

   Notes:
   - Internal source file for the Strova editor and runtime.

   Copyright (c) 2026 Strova Project
   ============================================================================ */
#include "FileDialog.h"

#include "../third_party/nfd/src/include/nfd.h"

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#endif

#include <SDL.h>

namespace platform {

    static void ensureComInitializedOnce() {
#ifdef _WIN32
        static bool s_done = false;
        if (!s_done) {
            s_done = true;

            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
                SDL_Log("COM init failed: 0x%08lx", (unsigned long)hr);
            }
        }
#endif
    }

    static bool ensureNfdInit()
    {
        ensureComInitializedOnce();

        static bool s_inited = false;
        static bool s_initOk = false;

        if (!s_inited) {
            s_inited = true;
            nfdresult_t r = NFD_Init();
            s_initOk = (r == NFD_OKAY);

            if (!s_initOk) {
                const char* err = NFD_GetError();
                SDL_Log("NFD_Init failed: %s", err ? err : "(null)");
            }
        }

        return s_initOk;
    }

    bool pickFolder(std::string& outFolder, const std::string& defaultFolder) {
        outFolder.clear();

        if (!ensureNfdInit()) return false;

        nfdu8char_t* outPath = nullptr;

        const nfdu8char_t* def =
            defaultFolder.empty()
            ? nullptr
            : reinterpret_cast<const nfdu8char_t*>(defaultFolder.c_str());

        nfdresult_t res = NFD_PickFolderU8(&outPath, def);

        if (res == NFD_OKAY && outPath) {
            outFolder = reinterpret_cast<const char*>(outPath);
            NFD_FreePathU8(outPath);
            return true;
        }

        if (res == NFD_CANCEL) {
            SDL_Log("NFD: user cancelled");
            return false;
        }

        const char* err = NFD_GetError();
        SDL_Log("NFD_PickFolderU8 failed: %s", err ? err : "(null)");
        return false;
    }

    bool pickOpenFile(std::string& outPath, const std::string& defaultPath)
    {
        outPath.clear();

        if (!ensureNfdInit()) return false;

        static const nfdu8filteritem_t filters[] = {
            { "Images", "png,jpg,jpeg,webp" },
            { "PNG", "png" },
            { "JPEG", "jpg,jpeg" },
            { "WEBP", "webp" }
        };

        nfdu8char_t* chosenPath = nullptr;
        const nfdu8char_t* def =
            defaultPath.empty()
            ? nullptr
            : reinterpret_cast<const nfdu8char_t*>(defaultPath.c_str());

        const nfdresult_t res = NFD_OpenDialogU8(
            &chosenPath,
            filters,
            (nfdfiltersize_t)(sizeof(filters) / sizeof(filters[0])),
            def);

        if (res == NFD_OKAY && chosenPath)
        {
            outPath = reinterpret_cast<const char*>(chosenPath);
            NFD_FreePathU8(chosenPath);
            return true;
        }

        if (res == NFD_CANCEL)
            return false;

        const char* err = NFD_GetError();
        SDL_Log("NFD_OpenDialogU8 failed: %s", err ? err : "(null)");
        return false;
    }


    bool pickOpenAnyFile(std::string& outPath, const std::string& defaultPath)
    {
        outPath.clear();
        if (!ensureNfdInit()) return false;
        nfdu8char_t* chosenPath = nullptr;
        const nfdu8char_t* def =
            defaultPath.empty()
            ? nullptr
            : reinterpret_cast<const nfdu8char_t*>(defaultPath.c_str());
        const nfdresult_t res = NFD_OpenDialogU8(&chosenPath, nullptr, 0, def);
        if (res == NFD_OKAY && chosenPath)
        {
            outPath = reinterpret_cast<const char*>(chosenPath);
            NFD_FreePathU8(chosenPath);
            return true;
        }
        return false;
    }

    bool pickOpenBrushOrProject(std::string& outPath, const std::string& defaultPath)
    {
        outPath.clear();
        if (!ensureNfdInit()) return false;

        static const nfdu8filteritem_t filters[] = {
            { "Strova Brush", "sbrush,sbrushproj,png" },
            { "Brush Package", "sbrush" },
            { "Brush Project", "sbrushproj" },
            { "PNG Stamp", "png" }
        };

        nfdu8char_t* chosenPath = nullptr;
        const nfdu8char_t* def =
            defaultPath.empty()
            ? nullptr
            : reinterpret_cast<const nfdu8char_t*>(defaultPath.c_str());

        const nfdresult_t res = NFD_OpenDialogU8(
            &chosenPath,
            filters,
            (nfdfiltersize_t)(sizeof(filters) / sizeof(filters[0])),
            def);

        if (res == NFD_OKAY && chosenPath)
        {
            outPath = reinterpret_cast<const char*>(chosenPath);
            NFD_FreePathU8(chosenPath);
            return true;
        }

        return false;
    }

    bool pickOpenBrushProjectFile(std::string& outPath, const std::string& defaultPath)
    {
        outPath.clear();
        if (!ensureNfdInit()) return false;

        static const nfdu8filteritem_t filters[] = {
            { "Brush Project", "sbrushproj" }
        };

        nfdu8char_t* chosenPath = nullptr;
        const nfdu8char_t* def =
            defaultPath.empty()
            ? nullptr
            : reinterpret_cast<const nfdu8char_t*>(defaultPath.c_str());

        const nfdresult_t res = NFD_OpenDialogU8(
            &chosenPath,
            filters,
            (nfdfiltersize_t)(sizeof(filters) / sizeof(filters[0])),
            def);

        if (res == NFD_OKAY && chosenPath)
        {
            outPath = reinterpret_cast<const char*>(chosenPath);
            NFD_FreePathU8(chosenPath);
            return true;
        }
        return false;
    }

    bool pickOpenLuaFile(std::string& outPath, const std::string& defaultPath)
    {
        outPath.clear();
        if (!ensureNfdInit()) return false;

        static const nfdu8filteritem_t filters[] = {
            { "Lua", "lua,txt" },
            { "Text", "txt" }
        };

        nfdu8char_t* chosenPath = nullptr;
        const nfdu8char_t* def =
            defaultPath.empty()
            ? nullptr
            : reinterpret_cast<const nfdu8char_t*>(defaultPath.c_str());

        const nfdresult_t res = NFD_OpenDialogU8(
            &chosenPath,
            filters,
            (nfdfiltersize_t)(sizeof(filters) / sizeof(filters[0])),
            def);

        if (res == NFD_OKAY && chosenPath)
        {
            outPath = reinterpret_cast<const char*>(chosenPath);
            NFD_FreePathU8(chosenPath);
            return true;
        }
        return false;
    }

    static bool saveWithFilters(std::string& outPath, const std::string& defaultPath, const nfdu8filteritem_t* filters, nfdfiltersize_t count)
    {
        outPath.clear();
        if (!ensureNfdInit()) return false;

        nfdu8char_t* savePath = nullptr;
        const nfdu8char_t* def =
            defaultPath.empty()
            ? nullptr
            : reinterpret_cast<const nfdu8char_t*>(defaultPath.c_str());

        const nfdresult_t res = NFD_SaveDialogU8(&savePath, filters, count, def, nullptr);
        if (res == NFD_OKAY && savePath)
        {
            outPath = reinterpret_cast<const char*>(savePath);
            NFD_FreePathU8(savePath);
            return true;
        }
        return false;
    }

    bool pickSaveBrushFile(std::string& outPath, const std::string& defaultPath)
    {
        static const nfdu8filteritem_t filters[] = {
            { "Brush Package", "sbrush" }
        };
        const bool ok = saveWithFilters(outPath, defaultPath, filters, (nfdfiltersize_t)(sizeof(filters) / sizeof(filters[0])));
        if (ok && (outPath.size() < 7 || outPath.substr(outPath.size() - 7) != ".sbrush"))
            outPath += ".sbrush";
        return ok;
    }

    bool pickSaveBrushProjectFile(std::string& outPath, const std::string& defaultPath)
    {
        static const nfdu8filteritem_t filters[] = {
            { "Brush Project", "sbrushproj" }
        };
        const bool ok = saveWithFilters(outPath, defaultPath, filters, (nfdfiltersize_t)(sizeof(filters) / sizeof(filters[0])));
        if (ok && (outPath.size() < 11 || outPath.substr(outPath.size() - 11) != ".sbrushproj"))
            outPath += ".sbrushproj";
        return ok;
    }

    bool pickSaveAnyFile(std::string& outPath, const std::string& defaultPath)
    {
        return saveWithFilters(outPath, defaultPath, nullptr, 0);
    }

    bool pickSaveLuaFile(std::string& outPath, const std::string& defaultPath)
    {
        static const nfdu8filteritem_t filters[] = {
            { "Lua", "lua" },
            { "Text", "txt" }
        };
        const bool ok = saveWithFilters(outPath, defaultPath, filters, (nfdfiltersize_t)(sizeof(filters) / sizeof(filters[0])));
        if (ok)
        {
            if (outPath.size() < 4 || (outPath.substr(outPath.size() - 4) != ".lua" && outPath.substr(outPath.size() - 4) != ".txt"))
                outPath += ".lua";
        }
        return ok;
    }

}
