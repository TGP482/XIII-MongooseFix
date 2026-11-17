#include "d3d8to9.hpp"
#include "hd_textures.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

// See hd_textures.hpp.

static std::mutex mHD;
static std::unordered_map<uint64_t, std::wstring> mapIndex;
static std::unordered_map<uint64_t, IDirect3DTexture9 *> mapCache;   // null: the file would not load
static IDirect3DDevice9 *pCacheDevice = nullptr;
static bool bLogUploads = false;
static HDTexturesLogFn pfnLog = nullptr;
static size_t nReplaced = 0;

static constexpr UINT nMaxSize = 2048;

static void Log(const char *szFormat, ...)
{
	if (!pfnLog)
		return;

	char szBuffer[512];
	va_list args;
	va_start(args, szFormat);
	vsnprintf(szBuffer, sizeof(szBuffer), szFormat, args);
	va_end(args);
	pfnLog(szBuffer);
}

// Bytes per stored row and number of rows of one level, or false for formats not hashed.
static bool RowLayout(D3DFORMAT Format, UINT Width, UINT Height, UINT &nRowBytes, UINT &nRows)
{
	switch (Format)
	{
	case D3DFMT_DXT1:
		nRowBytes = ((Width + 3) / 4) * 8;
		nRows = (Height + 3) / 4;
		return true;
	case D3DFMT_DXT2:
	case D3DFMT_DXT3:
	case D3DFMT_DXT4:
	case D3DFMT_DXT5:
		nRowBytes = ((Width + 3) / 4) * 16;
		nRows = (Height + 3) / 4;
		return true;
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
		nRowBytes = Width * 4;
		nRows = Height;
		return true;
	case D3DFMT_R5G6B5:
	case D3DFMT_A1R5G5B5:
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A4R4G4B4:
		nRowBytes = Width * 2;
		nRows = Height;
		return true;
	default:
		return false;
	}
}

static uint64_t Fnv(uint64_t nHash, const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; ++i)
		nHash = (nHash ^ p[i]) * 0x100000001B3ull;
	return nHash;
}

void HDTexturesConfigure(const std::wstring &folder, bool bLog, HDTexturesLogFn pLog)
{
	std::lock_guard lock(mHD);
	mapIndex.clear();
	bLogUploads = bLog;
	pfnLog = pLog;

	if (folder.empty())
		return;

	WIN32_FIND_DATAW data{};
	const auto hFind = FindFirstFileW((folder + L"\\*.dds").c_str(), &data);
	if (hFind == INVALID_HANDLE_VALUE)
		return;

	do
	{
		// <16 hex digits>.dds, anything else in the folder is ignored
		if (wcslen(data.cFileName) != 20)
			continue;

		wchar_t *pEnd = nullptr;
		const auto nHash = wcstoull(data.cFileName, &pEnd, 16);
		if (pEnd != data.cFileName + 16 || _wcsicmp(pEnd, L".dds") != 0)
			continue;

		mapIndex[nHash] = folder + L"\\" + data.cFileName;
	} while (FindNextFileW(hFind, &data));

	FindClose(hFind);
}

size_t HDTexturesIndexed()
{
	std::lock_guard lock(mHD);
	return mapIndex.size();
}

// A plain DDS: DXT1/DXT3/DXT5 or 32 bit BGRA, any number of mips. Managed pool, so it survives resets.
static IDirect3DTexture9 *LoadDDS(IDirect3DDevice9 *pDevice, const std::wstring &path)
{
	const auto hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return nullptr;

	LARGE_INTEGER size{};
	GetFileSizeEx(hFile, &size);
	std::vector<uint8_t> file(size.QuadPart > 128 && size.QuadPart < (256 << 20) ? static_cast<size_t>(size.QuadPart) : 0);
	DWORD nRead = 0;
	const auto bRead = !file.empty() && ReadFile(hFile, file.data(), static_cast<DWORD>(file.size()), &nRead, nullptr) && nRead == file.size();
	CloseHandle(hFile);

	if (!bRead || std::memcmp(file.data(), "DDS ", 4) != 0)
		return nullptr;

	auto U32 = [&](size_t n) { uint32_t v; std::memcpy(&v, &file[n], 4); return v; };
	const UINT Height = U32(12), Width = U32(16);
	const UINT nMips = (std::max)(1u, U32(28));
	const auto nPixelFlags = U32(80), nFourCC = U32(84), nBits = U32(88);

	D3DFORMAT Format;
	if (nPixelFlags & 0x4)   // DDPF_FOURCC
	{
		if (nFourCC == MAKEFOURCC('D', 'X', 'T', '1')) Format = D3DFMT_DXT1;
		else if (nFourCC == MAKEFOURCC('D', 'X', 'T', '3')) Format = D3DFMT_DXT3;
		else if (nFourCC == MAKEFOURCC('D', 'X', 'T', '5')) Format = D3DFMT_DXT5;
		else return nullptr;
	}
	else if (nBits == 32 && U32(92) == 0x00FF0000 && U32(96) == 0x0000FF00 && U32(100) == 0x000000FF)
		Format = (nPixelFlags & 0x1) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
	else
		return nullptr;

	if (Width == 0 || Height == 0 || Width > 8192 || Height > 8192)
		return nullptr;

	IDirect3DTexture9 *pTexture = nullptr;
	if (FAILED(pDevice->CreateTexture(Width, Height, nMips, 0, Format, D3DPOOL_MANAGED, &pTexture, nullptr)))
		return nullptr;

	size_t nOffset = 128;
	for (UINT nLevel = 0; nLevel < nMips; ++nLevel)
	{
		const UINT w = (std::max)(1u, Width >> nLevel), h = (std::max)(1u, Height >> nLevel);
		UINT nRowBytes, nRows;
		RowLayout(Format, w, h, nRowBytes, nRows);

		D3DLOCKED_RECT rect{};
		if (nOffset + size_t(nRowBytes) * nRows > file.size() || FAILED(pTexture->LockRect(nLevel, &rect, nullptr, 0)))
		{
			pTexture->Release();
			return nullptr;
		}

		for (UINT y = 0; y < nRows; ++y)
			std::memcpy(static_cast<uint8_t *>(rect.pBits) + size_t(y) * rect.Pitch, &file[nOffset + size_t(y) * nRowBytes], nRowBytes);

		pTexture->UnlockRect(nLevel);
		nOffset += size_t(nRowBytes) * nRows;
	}

	return pTexture;
}

IDirect3DTexture9 *HDTexturesOnUpload(IDirect3DTexture9 *pTexture, const void *pBits, int nPitch)
{
	if (!pBits || nPitch <= 0)
		return nullptr;

	std::lock_guard lock(mHD);

	if (mapIndex.empty() && !bLogUploads)
		return nullptr;

	D3DSURFACE_DESC desc{};
	UINT nRowBytes, nRows;
	if (FAILED(pTexture->GetLevelDesc(0, &desc)) || desc.Width > nMaxSize || desc.Height > nMaxSize ||
		!RowLayout(desc.Format, desc.Width, desc.Height, nRowBytes, nRows) || nRowBytes > UINT(nPitch))
		return nullptr;

	uint64_t nHash = 0xCBF29CE484222325ull;
	const uint32_t aSize[2] = { desc.Width, desc.Height };
	nHash = Fnv(nHash, reinterpret_cast<const uint8_t *>(aSize), sizeof(aSize));
	for (UINT y = 0; y < nRows; ++y)
		nHash = Fnv(nHash, static_cast<const uint8_t *>(pBits) + size_t(y) * nPitch, nRowBytes);

	const auto it = mapIndex.find(nHash);

	if (bLogUploads)
		Log("HD Textures: upload %016llX %ux%u format 0x%X%s", static_cast<unsigned long long>(nHash), desc.Width, desc.Height,
			static_cast<unsigned>(desc.Format), it != mapIndex.end() ? " (replaced)" : "");

	if (it == mapIndex.end())
		return nullptr;

	IDirect3DDevice9 *pDevice = nullptr;
	if (FAILED(pTexture->GetDevice(&pDevice)))
		return nullptr;
	pDevice->Release();   // the texture keeps it alive

	// A new device (the game rebuilt it) starts a new cache; the old one's textures are let go.
	if (pDevice != pCacheDevice)
	{
		for (auto &entry : mapCache)
			if (entry.second)
				entry.second->Release();
		mapCache.clear();
		pCacheDevice = pDevice;
	}

	auto cached = mapCache.find(nHash);
	if (cached == mapCache.end())
	{
		auto pReplacement = LoadDDS(pDevice, it->second);
		cached = mapCache.emplace(nHash, pReplacement).first;

		if (pReplacement)
		{
			D3DSURFACE_DESC hd{};
			pReplacement->GetLevelDesc(0, &hd);
			Log("HD Textures: %016llX %ux%u -> %ux%u (%zu loaded)", static_cast<unsigned long long>(nHash), desc.Width, desc.Height,
				hd.Width, hd.Height, ++nReplaced);
		}
		else
			Log("HD Textures: %016llX could not be loaded, the original is used", static_cast<unsigned long long>(nHash));
	}

	return cached->second;
}
