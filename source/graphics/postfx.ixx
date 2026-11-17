module;

#include <common.hxx>
#include <d3d8to9/d3dx9.hpp>
#include <d3d8to9/embedded_d3dx9.hpp>
#include <string>

#include "postfx_shaders.h"

export module postfx;

import common;
import settings;
import logging;

// The "Shader" setting: one full-screen HLSL pass over the finished frame, picked from the presets in
// postfx_shaders.h. 0 is off, n is preset n - 1. Runs after the scaling pass and after CRT Gamma, so it
// sees exactly what is about to be shown, HUD included.
//
// Same shape as crtgamma: the frame is copied (StretchRect resolves a multisampled back buffer on the
// way), the copy is drawn back through the shader, and a state block hands the renderer its state back.
// The presets need ps_3_0 and the embedded d3dcompiler, so this is Direct3D 9 only: the Direct3D 8
// device does not answer QueryInterface for IDirect3DDevice9, which is the test.
//
// A preset change is picked up on the next frame: the shader is rebuilt, the copy target is kept.

// UD3DRenderDevice, D3DDrv.dll: +0x66C IDirect3DDevice8*.
static constexpr auto nOffsetDevice = 0x66C;

struct ScreenVertex
{
    float x, y, z, rhw, u, v;
};

static std::atomic<int> nPreset = 0;

// The preset that would not build, so it is not retried every frame. Another choice clears it.
static int nFailedPreset = 0;
static bool bNoDX9Logged = false;

// Borrowed: the wrapper owns the device, this only remembers which one it built against.
static IDirect3DDevice9* pDevice = nullptr;
static IDirect3DPixelShader9* pShader = nullptr;
static int nShaderPreset = 0;
static IDirect3DTexture9* pCopy = nullptr;
static IDirect3DSurface9* pCopySurface = nullptr;
static IDirect3DStateBlock9* pStateBlock = nullptr;
static UINT nWidth = 0;
static UINT nHeight = 0;

template<class T>
static void ReleaseCom(T*& pObject)
{
    if (pObject)
        pObject->Release();

    pObject = nullptr;
}

// The copy target is D3DPOOL_DEFAULT, so none of it may be alive at the next Reset.
export void ReleasePostFX()
{
    ReleaseCom(pStateBlock);
    ReleaseCom(pCopySurface);
    ReleaseCom(pCopy);
    ReleaseCom(pShader);

    pDevice = nullptr;
    nShaderPreset = 0;
    nWidth = 0;
    nHeight = 0;
}

static bool Fail(int nWanted, const char* szReason)
{
    ReleaseCom(pShader);
    nShaderPreset = 0;
    nFailedPreset = nWanted;
    LogWarn("Shader: {}, preset {} is off", szReason, nWanted);
    return false;
}

static bool BuildShader(IDirect3DDevice9* pDev, int nWanted)
{
    ReleaseCom(pShader);
    nShaderPreset = 0;

    const auto& preset = aPostFXPresets[nWanted - 1];
    const auto sSource = std::string(szPostFXPrelude) + preset.szSource;

    ID3DXBuffer* pCode = nullptr;
    ID3DXBuffer* pError = nullptr;

    if (FAILED(EmbeddedD3DCompile(sSource.data(), sSource.size(), preset.szName, "main", "ps_3_0", &pCode, &pError)) || !pCode)
    {
        if (pError)
            LogWarn("Shader: {}", static_cast<const char*>(pError->GetBufferPointer()));

        ReleaseCom(pError);
        ReleaseCom(pCode);
        return Fail(nWanted, "the shader would not compile");
    }

    ReleaseCom(pError);

    const auto hr = pDev->CreatePixelShader(static_cast<const DWORD*>(pCode->GetBufferPointer()), &pShader);
    ReleaseCom(pCode);

    if (FAILED(hr) || !pShader)
        return Fail(nWanted, "the device refused a ps_3_0 pixel shader");

    nShaderPreset = nWanted;
    LogInfo("Shader: {} on at {}x{}", preset.szName, nWidth, nHeight);
    return true;
}

static bool BuildTargets(IDirect3DDevice9* pDev, const D3DSURFACE_DESC& back, int nWanted)
{
    ReleasePostFX();

    if (FAILED(pDev->CreateTexture(back.Width, back.Height, 1, D3DUSAGE_RENDERTARGET, back.Format,
            D3DPOOL_DEFAULT, &pCopy, nullptr)) || !pCopy
        || FAILED(pCopy->GetSurfaceLevel(0, &pCopySurface)) || !pCopySurface)
    {
        ReleasePostFX();
        return Fail(nWanted, "the driver refused a back buffer copy");
    }

    // The renderer caches its own state, so the pass hands back everything it touches.
    if (FAILED(pDev->CreateStateBlock(D3DSBT_ALL, &pStateBlock)) || !pStateBlock)
    {
        ReleasePostFX();
        return Fail(nWanted, "the device would not give up a state block");
    }

    pDevice = pDev;
    nWidth = back.Width;
    nHeight = back.Height;
    return true;
}

static void DrawQuad(IDirect3DDevice9* pDev)
{
    pDev->SetRenderState(D3DRS_ZENABLE, FALSE);
    pDev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    pDev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    pDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    pDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    pDev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    pDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    pDev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    pDev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    pDev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);

    // Linear: the glow and outline taps land between texels. The centre tap is one texel per pixel.
    pDev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    pDev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    pDev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    pDev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    pDev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    pDev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

    D3DVIEWPORT9 viewport{ 0, 0, nWidth, nHeight, 0.0f, 1.0f };
    pDev->SetViewport(&viewport);

    // px in the shaders: texel size and frame size.
    const float aPixel[4]{ 1.0f / static_cast<float>(nWidth), 1.0f / static_cast<float>(nHeight),
        static_cast<float>(nWidth), static_cast<float>(nHeight) };
    pDev->SetPixelShaderConstantF(0, aPixel, 1);

    pDev->SetTexture(0, pCopy);
    pDev->SetVertexShader(nullptr);
    pDev->SetPixelShader(pShader);
    pDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

    // The half texel the rasteriser puts between pixel centre and texel centre.
    const auto fRight = static_cast<float>(nWidth) - 0.5f;
    const auto fBottom = static_cast<float>(nHeight) - 0.5f;

    const ScreenVertex aVertices[4]
    {
        { -0.5f,  -0.5f,   0.0f, 1.0f, 0.0f, 0.0f },
        { fRight, -0.5f,   0.0f, 1.0f, 1.0f, 0.0f },
        { -0.5f,  fBottom, 0.0f, 1.0f, 0.0f, 1.0f },
        { fRight, fBottom, 0.0f, 1.0f, 1.0f, 1.0f },
    };

    pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, aVertices, sizeof(ScreenVertex));

    pDev->SetTexture(0, nullptr);
    pDev->SetPixelShader(nullptr);
}

// Called by display inside its UD3DRenderDevice::Present hook, after PresentCRTGamma.
export void PresentPostFX(uint8_t* pRenderDevice)
{
    const auto nWanted = std::clamp(nPreset.load(), 0, nPostFXPresets);

    if (nWanted == 0)
    {
        // Off: give the copy target back rather than hold a frame of video memory for nothing.
        if (pDevice)
            ReleasePostFX();

        nFailedPreset = 0;
        return;
    }

    if (nWanted == nFailedPreset || !pRenderDevice)
        return;

    auto pWrapper = *reinterpret_cast<IUnknown**>(pRenderDevice + nOffsetDevice);
    if (!pWrapper)
        return;

    IDirect3DDevice9* pDev = nullptr;
    if (FAILED(pWrapper->QueryInterface(__uuidof(IDirect3DDevice9), reinterpret_cast<void**>(&pDev))) || !pDev)
    {
        if (!std::exchange(bNoDX9Logged, true))
            LogWarn("Shader: the presets need Direct3D 9, set DirectXVersion to 1; the pass is off");

        return;
    }

    // The wrapper holds the reference the device lives on, this one only proved the path.
    pDev->Release();

    IDirect3DSurface9* pBack = nullptr;
    D3DSURFACE_DESC back{};

    if (FAILED(pDev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBack)) || !pBack)
        return;

    if (FAILED(pBack->GetDesc(&back))
        || ((pDev != pDevice || back.Width != nWidth || back.Height != nHeight) && !BuildTargets(pDev, back, nWanted))
        || (nShaderPreset != nWanted && !BuildShader(pDev, nWanted)))
    {
        pBack->Release();
        return;
    }

    pStateBlock->Capture();

    IDirect3DSurface9* pOldTarget = nullptr;
    IDirect3DSurface9* pOldDepth = nullptr;
    pDev->GetRenderTarget(0, &pOldTarget);
    pDev->GetDepthStencilSurface(&pOldDepth);

    // StretchRect resolves a multisampled back buffer on the way into the copy, so the pass reads a
    // plain texture whatever MSAA is set to.
    if (SUCCEEDED(pDev->StretchRect(pBack, nullptr, pCopySurface, nullptr, D3DTEXF_NONE))
        && SUCCEEDED(pDev->SetRenderTarget(0, pBack)))
    {
        pDev->SetDepthStencilSurface(nullptr);

        if (SUCCEEDED(pDev->BeginScene()))
        {
            DrawQuad(pDev);
            pDev->EndScene();
        }
    }

    if (pOldTarget)
        pDev->SetRenderTarget(0, pOldTarget);

    pDev->SetDepthStencilSurface(pOldDepth);

    ReleaseCom(pOldDepth);
    ReleaseCom(pOldTarget);

    pStateBlock->Apply();
    pBack->Release();
}

class PostFX
{
public:
    PostFX()
    {
        MongooseFix::onInitEvent() += []() { BindInt(nPreset, PREF_SHADER); };
        MongooseFix::onShutdownEvent() += []() { ReleasePostFX(); };
    }
} PostFX;
