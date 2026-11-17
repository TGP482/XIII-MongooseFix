#pragma once

// Post-process presets for the "Shader" setting (DirectX 9 only). Plain header on purpose: postfx.ixx
// includes it, and so can a test harness that compiles and renders the very same strings.
//
// Every preset is one HLSL pixel shader (entry "main", profile ps_3_0) drawn over a copy of the finished
// frame. Shared inputs, declared by szPostFXPrelude:
//   s0   the frame, linear filtering, clamped
//   px   (1/width, 1/height, width, height) of the frame
// Effects that work in pixels scale their radius by height/1080 so they look the same at any resolution.
//
// Sharpening is AMD FidelityFX CAS (contrast adaptive sharpening), reimplemented from its published
// formula; FidelityFX is MIT licensed, Copyright (c) 2020 Advanced Micro Devices, Inc.
// Vibrance follows CeeJayDK's SweetFX maths (MIT, Copyright (c) 2014 CeeJay.dk), as crtgamma.ixx does.

static const char* const szPostFXPrelude = R"(
sampler2D s0 : register(s0);
float4 px : register(c0);
static const float3 LUMA = float3(0.2126, 0.7152, 0.0722);

float3 Frame(float2 uv) { return tex2D(s0, uv).rgb; }
float  Luma(float3 c)   { return dot(c, LUMA); }
float  Scale()          { return max(1.0, px.w / 1080.0); }

// AMD CAS, 5 taps. fSharp 0..1.
float3 Sharpen(float2 uv, float fSharp)
{
    float3 c = Frame(uv);
    float3 n = Frame(uv + float2(0.0, -px.y));
    float3 s = Frame(uv + float2(0.0,  px.y));
    float3 w = Frame(uv + float2(-px.x, 0.0));
    float3 e = Frame(uv + float2( px.x, 0.0));
    float3 mn = min(c, min(min(n, s), min(w, e)));
    float3 mx = max(c, max(max(n, s), max(w, e)));
    float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 0.0001)));
    float3 wgt = amp * (-1.0 / lerp(8.0, 5.0, fSharp));
    return saturate((c + (n + s + w + e) * wgt) / (1.0 + 4.0 * wgt));
}

// Lifts undersaturated colours more than vivid ones.
float3 Vibrance(float3 c, float fAmount)
{
    float mx = max(c.r, max(c.g, c.b));
    float mn = min(c.r, min(c.g, c.b));
    return lerp(Luma(c), c, 1.0 + fAmount * (1.0 - (mx - mn)));
}

// Smoothstep S-curve on luma, colour ratios kept.
float3 Contrast(float3 c, float fAmount)
{
    float l = Luma(c);
    float t = l * l * (3.0 - 2.0 * l);
    return saturate(c + (t - l) * fAmount);
}

// Sobel on luma, fRadius in 1080p pixels. 0 flat .. ~1 hard edge.
float Edge(float2 uv, float fRadius)
{
    float2 d = px.xy * fRadius * Scale();
    float tl = Luma(Frame(uv + float2(-d.x, -d.y)));
    float tc = Luma(Frame(uv + float2( 0.0, -d.y)));
    float tr = Luma(Frame(uv + float2( d.x, -d.y)));
    float ml = Luma(Frame(uv + float2(-d.x,  0.0)));
    float mr = Luma(Frame(uv + float2( d.x,  0.0)));
    float bl = Luma(Frame(uv + float2(-d.x,  d.y)));
    float bc = Luma(Frame(uv + float2( 0.0,  d.y)));
    float br = Luma(Frame(uv + float2( d.x,  d.y)));
    float gx = (tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl);
    float gy = (bl + 2.0 * bc + br) - (tl + 2.0 * tc + tr);
    return sqrt(gx * gx + gy * gy);
}

// Glow of everything brighter than fThreshold: 24 taps on a golden-angle spiral, fRadius in 1080p pixels.
float3 Glow(float2 uv, float fThreshold, float fRadius)
{
    float3 sum = 0.0;
    float2 r = px.xy * fRadius * Scale();
    for (int i = 0; i < 24; i++)
    {
        float t = (i + 0.5) / 24.0;
        float a = i * 2.39996323;
        float3 c = Frame(uv + float2(cos(a), sin(a)) * sqrt(t) * r);
        sum += c * saturate((Luma(c) - fThreshold) / (1.0 - fThreshold)) * (1.0 - 0.6 * t);
    }
    return sum / 24.0;
}

float Vignette(float2 uv, float fStart, float fAmount)
{
    return 1.0 - fAmount * smoothstep(fStart, 0.95, distance(uv, float2(0.5, 0.5)) * 1.4142);
}
)";

struct PostFXPreset
{
    const char* szName;     // menu label
    const char* szSource;   // float4 main(float2 uv : TEXCOORD0) : COLOR
};

// Index + 1 is the ini value; 0 is off. Append only, the ini stores the number.
static const PostFXPreset aPostFXPresets[] =
{
    { "Sharpen", R"(
float4 main(float2 uv : TEXCOORD0) : COLOR
{
    return float4(Sharpen(uv, 0.7), 1.0);
}
)" },

    { "Vivid", R"(
float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = Sharpen(uv, 0.5);
    c = Vibrance(c, 0.35);
    c = Contrast(c, 0.18);
    return float4(c, 1.0);
}
)" },

    { "Comic Ink", R"(
// Heavier black outlines and flatter, punchier colour: closer to the printed album.
float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = Sharpen(uv, 0.4);
    c = Vibrance(c, 0.25);
    c = Contrast(c, 0.22);
    float ink = smoothstep(0.10, 0.42, Edge(uv, 1.0));
    c = lerp(c, c * 0.12, ink * 0.85);
    return float4(c, 1.0);
}
)" },

    { "Bloom", R"(
float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = Frame(uv);
    float3 g = Glow(uv, 0.62, 22.0) + 0.6 * Glow(uv, 0.62, 7.0);
    c = 1.0 - (1.0 - c) * (1.0 - saturate(g * 1.1));
    return float4(c, 1.0);
}
)" },

    { "Cinematic", R"(
// Cool shadows, warm highlights, more contrast, soft glow and a vignette.
float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = Sharpen(uv, 0.35);
    float l = Luma(c);
    c *= lerp(float3(0.93, 1.00, 1.08), float3(1.07, 1.01, 0.92), smoothstep(0.15, 0.85, l));
    c = Contrast(c, 0.30);
    float3 g = Glow(uv, 0.70, 18.0);
    c = 1.0 - (1.0 - saturate(c)) * (1.0 - saturate(g * 0.7));
    c *= Vignette(uv, 0.55, 0.38);
    return float4(c, 1.0);
}
)" },

    { "Noir", R"(
// Black and white with hard contrast and inked edges; strong reds stay red.
float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = Sharpen(uv, 0.4);
    float red = saturate((c.r - max(c.g, c.b)) * 3.5);
    float l = Luma(c);
    l = saturate((l - 0.5) * 1.35 + 0.5);
    l = l * l * (3.0 - 2.0 * l);
    float3 o = lerp(float3(l, l, l), c * float3(1.15, 0.55, 0.55), red);
    float ink = smoothstep(0.12, 0.45, Edge(uv, 1.0));
    o = lerp(o, o * 0.1, ink * 0.8);
    o *= Vignette(uv, 0.50, 0.45);
    return float4(saturate(o), 1.0);
}
)" },

    { "Remaster", R"(
// The all-rounder: sharper, slightly richer colour, gentle glow on lights.
float4 main(float2 uv : TEXCOORD0) : COLOR
{
    float3 c = Sharpen(uv, 0.6);
    c = Vibrance(c, 0.20);
    c = Contrast(c, 0.12);
    float3 g = Glow(uv, 0.68, 16.0);
    c = 1.0 - (1.0 - c) * (1.0 - saturate(g * 0.55));
    return float4(c, 1.0);
}
)" },
};

static const int nPostFXPresets = static_cast<int>(sizeof(aPostFXPresets) / sizeof(aPostFXPresets[0]));
