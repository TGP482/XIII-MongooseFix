#pragma once

#include <cstdint>
#include <string>

struct IDirect3DTexture9;

// HD texture replacement by content: a texture the game uploads is identified by a hash of its first
// level, and when <folder>\<hash>.dds exists that file is drawn in its place. The game keeps its own
// copy, sizes and texture coordinates (Direct3D's are 0..1 whatever the size), so a 4x replacement of a
// HUD icon, a font page or a sprite is drawn at exactly the same place and size, only sharper. That is
// what replacing the texture inside its package cannot do: there the size is read by script and by the
// font and sprite code.
//
// Hash: 64 bit FNV-1a over the width and height (u32 little endian each) and then the level 0 rows as
// stored, without row padding (DXT: rows of 4x4 blocks). For DXT textures these are the package's bytes.

using HDTexturesLogFn = void (*)(const char *szMessage);

// An empty folder turns replacement off. bLogUploads writes every hashed upload to the log.
void HDTexturesConfigure(const std::wstring &folder, bool bLogUploads, HDTexturesLogFn pLog);

// Called with the level 0 bits still locked, just before the unlock. Returns the replacement (owned by
// the cache, not to be released) or null.
IDirect3DTexture9 *HDTexturesOnUpload(IDirect3DTexture9 *pTexture, const void *pBits, int nPitch);

size_t HDTexturesIndexed();
