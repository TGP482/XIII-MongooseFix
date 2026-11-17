module;

#include <common.hxx>
#include <cstring>
#include <string>

export module cutscenes;

import common;
import settings;
import logging;

// Two things, both hung on Bink's file open.
//
// 1. Movies of any size. UPCVideoPlayerDevice::Open opens the Bink file and then has the render device
//    make a movie texture of a fixed 1024x512 in R5G6B5; every frame is decoded into it and drawn with
//    texture coordinates worked out against those two numbers. A movie larger than that (a 4K upscale)
//    overruns the lock and takes the game down inside binkw32. The size and format are nine immediates:
//
//      WinDrv.dll  Open                 PUSH height, PUSH width, CALL [vtbl+0xAC]      create the texture
//      WinDrv.dll  the per frame copy   PUSH flags .. PUSH height .. BinkCopyToBuffer  0xF000000A is 565
//      WinDrv.dll  the per frame draw   PUSH height, PUSH width .. CALL [vtbl+0xC0]    texture extent
//      D3DDrv.dll  Video_CreateTexture  PUSH 0x17                                      D3DFMT_R5G6B5
//      D3DDrv.dll  Video_LockTex        PUSH height, PUSH width, CALL [vtbl+0xAC]      fallback create
//
//    The texture is made anew for every movie, after BinkOpen returns, so they are written per movie:
//    one that fits keeps the stock numbers and plays exactly as it always did, a bigger one gets a
//    texture of its own size in X8R8G8B8 (0x16) and BINKSURFACE32 (0xF0000003).
//
// 2. The "Cutscenes" setting: (0) the game's own movies, (1) the ones in a VideoAI folder next to
//    Video, e.g. an upscaled set, each falling back to Video when VideoAI has no file of that name.
//    Movies are opened when they play, so unlike the texture pack this needs no restart.

static constexpr uint32_t nStockWidth = 0x400;
static constexpr uint32_t nStockHeight = 0x200;
static constexpr uint32_t nStockCopyFlags = 0xF000000A;   // BINKSURFACE565 | BINKCOPYALL
static constexpr uint32_t nLargeCopyFlags = 0xF0000003;   // BINKSURFACE32  | BINKCOPYALL
static constexpr uint8_t nStockFormat = 0x17;             // D3DFMT_R5G6B5
static constexpr uint8_t nLargeFormat = 0x16;             // D3DFMT_X8R8G8B8

// BinkOpen flags that make the first argument something other than a file name.
static constexpr uint32_t BINKFILEHANDLE = 0x00800000;
static constexpr uint32_t BINKFROMMEMORY = 0x04000000;

static std::atomic<int> nCutscenes = 1;

static SafetyHookInline shBinkOpen{};

// Operand addresses; any that was not found stays null and the movie texture stays stock.
static uint8_t* pCreateHeight = nullptr;
static uint8_t* pCreateWidth = nullptr;
static uint8_t* pCopyFlags = nullptr;
static uint8_t* pCopyHeight = nullptr;
static uint8_t* pDrawHeight = nullptr;
static uint8_t* pDrawWidth = nullptr;
static uint8_t* pFormat = nullptr;
static uint8_t* pLockHeight = nullptr;
static uint8_t* pLockWidth = nullptr;

static bool HaveSites()
{
    return pCreateHeight && pCreateWidth && pCopyFlags && pCopyHeight && pDrawHeight && pDrawWidth
        && pFormat && pLockHeight && pLockWidth;
}

static void SetMovieTexture(uint32_t nWidth, uint32_t nHeight, bool bLarge)
{
    injector::WriteMemory<uint32_t>(pCreateHeight, nHeight, true);
    injector::WriteMemory<uint32_t>(pCreateWidth, nWidth, true);
    injector::WriteMemory<uint32_t>(pCopyFlags, bLarge ? nLargeCopyFlags : nStockCopyFlags, true);
    injector::WriteMemory<uint32_t>(pCopyHeight, nHeight, true);
    injector::WriteMemory<uint32_t>(pDrawHeight, nHeight, true);
    injector::WriteMemory<uint32_t>(pDrawWidth, nWidth, true);
    injector::WriteMemory<uint8_t>(pFormat, bLarge ? nLargeFormat : nStockFormat, true);
    injector::WriteMemory<uint32_t>(pLockHeight, nHeight, true);
    injector::WriteMemory<uint32_t>(pLockWidth, nWidth, true);
}

// "<dir>\Video\<file>" -> "<dir>\VideoAI\<file>" when that file exists. Empty when it does not apply.
static std::string AlternativeMovie(const char* szName)
{
    const std::string sName(szName);
    const auto nFile = sName.find_last_of("\\/");

    if (nFile == std::string::npos || nFile == 0)
        return {};

    const auto nDir = sName.find_last_of("\\/", nFile - 1);
    const auto nFolder = nDir == std::string::npos ? 0 : nDir + 1;

    if (_stricmp(sName.substr(nFolder, nFile - nFolder).c_str(), "Video") != 0)
        return {};

    auto sAlternative = sName.substr(0, nFolder) + "VideoAI" + sName.substr(nFile);
    const auto nAttributes = GetFileAttributesA(sAlternative.c_str());

    if (nAttributes == INVALID_FILE_ATTRIBUTES || (nAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return {};

    return sAlternative;
}

// HBINK __stdcall BinkOpen(const char* name, U32 flags). The handle starts with U32 Width, U32 Height.
static void* __stdcall BinkOpen(const char* szName, uint32_t nFlags)
{
    std::string sAlternative;

    if (szName && !(nFlags & (BINKFILEHANDLE | BINKFROMMEMORY)) && nCutscenes.load() != 0)
        sAlternative = AlternativeMovie(szName);

    auto pBink = sAlternative.empty() ? nullptr : shBinkOpen.stdcall<void*>(sAlternative.c_str(), nFlags);

    // A VideoAI file Bink will not open is no reason to lose the cutscene.
    if (!pBink)
    {
        if (!sAlternative.empty())
            LogWarn("Cutscenes: {} would not open, playing the original", sAlternative);

        sAlternative.clear();
        pBink = shBinkOpen.stdcall<void*>(szName, nFlags);
    }

    if (!pBink)
        return nullptr;

    const auto nWidth = static_cast<const uint32_t*>(pBink)[0];
    const auto nHeight = static_cast<const uint32_t*>(pBink)[1];
    const auto bLarge = nWidth > nStockWidth || nHeight > nStockHeight;

    if (HaveSites())
    {
        SetMovieTexture(bLarge ? nWidth : nStockWidth, bLarge ? nHeight : nStockHeight, bLarge);
    }
    else if (bLarge)
    {
        static std::once_flag flag;
        std::call_once(flag, []() { LogWarn("Cutscenes: the movie texture code was not found, a movie larger than 1024x512 can crash"); });
    }

    LogInfo("Cutscenes: {} {}x{}", sAlternative.empty() ? (szName ? szName : "?") : sAlternative.c_str(), nWidth, nHeight);
    return pBink;
}

static void InitWinDrv()
{
    // ADD ESP,30 / PUSH height / PUSH width / CALL [EAX+0xAC]
    auto patternCreate = module_pattern(L"WinDrv.dll", "83 C4 30 68 ? ? ? ? 68 ? ? ? ? FF 90 AC 00 00 00");
    // MOV EAX,[EDI+30] / PUSH flags / PUSH 0 / PUSH 0 / PUSH height / PUSH ECX / PUSH EDX / PUSH EAX / CALL [BinkCopyToBuffer]
    auto patternCopy = module_pattern(L"WinDrv.dll", "8B 47 30 68 ? ? ? ? 6A 00 6A 00 68 ? ? ? ? 51 52 50 FF 15");
    // MOV EDX,[ECX] / PUSH height / PUSH width / PUSH EDI / PUSH EAX / CALL [EDX+0xC0]
    auto patternDraw = module_pattern(L"WinDrv.dll", "8B 11 68 ? ? ? ? 68 ? ? ? ? 57 50 FF 92 C0 00 00 00");

    if (!patternCreate.empty())
    {
        pCreateHeight = patternCreate.get_first<uint8_t>(4);
        pCreateWidth = patternCreate.get_first<uint8_t>(9);
    }

    if (!patternCopy.empty())
    {
        pCopyFlags = patternCopy.get_first<uint8_t>(4);
        pCopyHeight = patternCopy.get_first<uint8_t>(13);
    }

    if (!patternDraw.empty())
    {
        pDrawHeight = patternDraw.get_first<uint8_t>(3);
        pDrawWidth = patternDraw.get_first<uint8_t>(8);
    }

    auto hBink = GetModuleHandleW(L"binkw32.dll");
    auto pOpen = hBink ? GetProcAddress(hBink, "_BinkOpen@8") : nullptr;

    if (!pOpen)
    {
        LogWarn("Cutscenes: binkw32.dll did not export BinkOpen, the setting and the large movie support are off");
        return;
    }

    shBinkOpen = safetyhook::create_inline(pOpen, BinkOpen);

    if (!shBinkOpen)
        LogWarn("Cutscenes: BinkOpen could not be hooked, the setting and the large movie support are off");
}

static void InitD3DDrv()
{
    // PUSH ECX / MOV ECX,[ESP+0C] / PUSH 1 / PUSH format / PUSH 0 / PUSH 1 / PUSH ECX
    auto patternFormat = module_pattern(L"D3DDrv.dll", "51 8B 4C 24 0C 6A 01 6A ? 6A 00 6A 01 51");
    // JNZ +12 / MOV EAX,[ESI] / PUSH height / PUSH width / CALL [EAX+0xAC]
    auto patternLock = module_pattern(L"D3DDrv.dll", "75 12 8B 06 68 ? ? ? ? 68 ? ? ? ? FF 90 AC 00 00 00");

    if (!patternFormat.empty())
        pFormat = patternFormat.get_first<uint8_t>(8);

    if (!patternLock.empty())
    {
        pLockHeight = patternLock.get_first<uint8_t>(5);
        pLockWidth = patternLock.get_first<uint8_t>(10);
    }
}

class Cutscenes
{
public:
    Cutscenes()
    {
        MongooseFix::onInitEvent() += []() { BindInt(nCutscenes, PREF_CUTSCENES); };
        MongooseFix::onWinDrvInitEvent() += []() { InitWinDrv(); };
        MongooseFix::onD3DDrvInitEvent() += []() { InitD3DDrv(); };
    }
} Cutscenes;
