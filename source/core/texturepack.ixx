module;

#include <common.hxx>
#include <filesystem>
#include <mutex>

#include "texturepack_ini.h"

export module texturepack;

import common;
import settings;
import logging;

// The "Texture Pack" setting: (0) the game's own textures, (1) the packages in <game>\TexturesAI, e.g.
// an AI upscaled set. See texturepack_ini.h for the one ini line that does it.
//
// Nothing is redirected at run time. By the time the asi is loaded (WinDrv.dll pulls in dinput8) the
// engine has read XIII.ini and already opened the packages its startup needs, so the choice is written
// into the ini and is the next launch's, like the DirectX version.
//
// The line is brought in step with the setting three times, because the engine rewrites XIII.ini from
// the copy it read at startup whenever a game option changes:
//   - at startup and whenever the fix's ini changes, so the menu toggle lands at once
//   - when the game quits (exitfix kills the process, so this is called from there, not from detach)
// Default.ini gets the line too: the engine rebuilds XIII.ini from it when XIII.ini is missing.
//
// With the folder absent the setting does nothing and the line is removed, so a stale entry never
// points the engine at a folder that is gone.

static std::mutex mSync;
static int nLastLogged = -1;

static std::filesystem::path SystemDir()
{
    wchar_t szExe[MAX_PATH]{};
    const auto nLength = GetModuleFileNameW(nullptr, szExe, MAX_PATH);

    // XIII.exe lives in <game>\system.
    return nLength > 0 && nLength < MAX_PATH ? std::filesystem::path(szExe).parent_path() : std::filesystem::path();
}

export bool IsTexturePackInstalled()
{
    const auto system = SystemDir();
    std::error_code ec;
    return !system.empty() && std::filesystem::is_directory(system.parent_path() / L"TexturesAI", ec);
}

export void SyncTexturePack()
{
    std::lock_guard lock(mSync);

    const auto system = SystemDir();
    if (system.empty())
        return;

    const auto bWanted = MongooseFixSettings.GetInt(PREF_TEXTUREPACK) != 0;
    const auto bOn = bWanted && IsTexturePackInstalled();
    auto bChanged = false;

    for (const auto* szIni : { L"XIII.ini", L"Default.ini" })
    {
        const auto path = system / szIni;
        std::error_code ec;

        if (!std::filesystem::exists(path, ec))
            continue;

        const auto nResult = texturepack::SetPathsLine(path, bOn);

        if (nResult < 0)
            LogWarn("Texture Pack: {} could not be updated", path.filename().string());

        bChanged |= nResult > 0;
    }

    // One line per state, not one per sync.
    const auto nState = bOn ? 1 : bWanted ? 2 : 0;
    if (std::exchange(nLastLogged, nState) != nState || bChanged)
    {
        if (nState == 2)
            LogInfo("Texture Pack: no TexturesAI folder next to TexturesPC, the game's own textures are used");
        else
            LogInfo("Texture Pack: {}{}", bOn ? "TexturesAI" : "the game's own textures",
                bChanged ? ", from the next launch" : "");
    }
}

class TexturePack
{
public:
    TexturePack()
    {
        MongooseFix::onInitEvent() += []() { ApplyAndWatch([]() { SyncTexturePack(); }); };
        MongooseFix::onShutdownEvent() += []() { SyncTexturePack(); };
    }
} TexturePack;
