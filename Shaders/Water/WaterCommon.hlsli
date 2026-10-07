// ============================================================================
// Eunoia Engine - WaterCommon.hlsli
// AquaTerrain DX12 Dynamic Water System Common Header
// ============================================================================

#ifndef WATER_COMMON_HLSLI
#define WATER_COMMON_HLSLI

cbuffer WaterConstants : register(b0)
{
    float4x4 viewProj;          // 0-15
    float3   cameraPos;         // 16-18
    float    time;              // 19
    float4   waterColor;        // 20-23 (Deep color RGBA)
    float4   shallowColor;      // 24-27 (Shallow color RGBA)
    float    roughness;         // 28
    float    opacity;           // 29
    float    reflectionStr;     // 30
    float    refractionStr;     // 31
    float    waveSpeed;         // 32
    float    waveScale;         // 33
    float    waveHeight;        // 34
    float    normalScale;       // 35
    float    foamEnabled;       // 36
    float    foamScale;         // 37
    float    depthFade;         // 38 (DepthRange for shallow/deep blend)
    float    specularIntensity; // 39
    float    foamDistance;      // 40
    float    foamStrength;      // 41
    float    foamSpeed;         // 42
    float    fresnelStrength;   // 43
    float    specularPower;     // 44
    float    sparklePower;      // 45
    float    nearPlane;         // 46
    float    farPlane;          // 47
    float2   screenSize;        // 48-49
    float2   flowDirection;     // 50-51
    float    flowSpeed;         // 52
    float    simEnabled;        // 53
    float    heightScale;       // 54
    float    choppiness;        // 55
    float3   renderOrigin;      // 56-58
    float    debugMode;         // 59
};

#define oceanExtent (shallowColor.a)

// ----------------------------------------------------------------------------
// Procedural FBM (Fractal Brownian Motion) and Value Noise for Foam and Breakup
// ----------------------------------------------------------------------------
float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f); // Hermite smoothstep
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0, 0.0));
    float c = Hash21(i + float2(0.0, 1.0));
    float d = Hash21(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float WaterFBM(float2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    float2 shift = float2(100.0, 100.0);
    for (int i = 0; i < 4; ++i)
    {
        value += amplitude * ValueNoise(p);
        p = p * 2.0 + shift;
        amplitude *= 0.5;
    }
    return value;
}

// ----------------------------------------------------------------------------
// Multi-Scale Gerstner Ocean Wave System (dev.md Sections 4, 5, 15, 16)
// Continuous directional rolling waves displacing X, Y, Z with exact normals
// ----------------------------------------------------------------------------
struct GerstnerWave
{
    float2 direction;
    float  wavelength;
    float  amplitude;
    float  speed;
    float  steepness;
};

// 8 Multi-scale ocean waves (large swell -> rolling medium -> fine surface chop)
static const GerstnerWave c_oceanWaves[8] = {
    // Large swell
    { float2(0.894, 0.447),  50.0, 0.30, 1.00, 0.60 },
    { float2(-0.707, 0.707), 35.0, 0.22, 1.15, 0.65 },

    // Medium rolling waves
    { float2(0.316, 0.949),  20.0, 0.14, 1.35, 0.70 },
    { float2(-0.923, -0.385), 12.0, 0.09, 1.65, 0.75 },
    { float2(0.600, -0.800),  7.5, 0.05, 2.00, 0.80 },

    // Small surface chop and ripples
    { float2(-0.447, 0.894),  4.5, 0.030, 2.50, 0.85 },
    { float2(0.707, 0.707),   2.4, 0.015, 3.20, 0.90 },
    { float2(-0.800, -0.600), 1.2, 0.008, 4.00, 0.95 }
};

void EvaluateGerstnerWaves(
    float2 worldXZ,
    float t,
    float speedMultiplier,
    float scaleMultiplier,
    float heightMultiplier,
    float choppinessMultiplier,
    out float3 displacement,
    out float3 normal)
{
    displacement = float3(0.0, 0.0, 0.0);

    float normX = 0.0;
    float normZ = 0.0;
    float normY = 1.0;

    float sMul = clamp(scaleMultiplier, 0.02, 5.0);
    float hMul = clamp(heightMultiplier, 0.0, 5.0);
    float spMul = clamp(speedMultiplier, 0.0, 10.0);
    float chMul = clamp(choppinessMultiplier, 0.0, 2.0);

    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        GerstnerWave w = c_oceanWaves[i];
        float wl = max(w.wavelength * sMul, 0.1);
        float amp = w.amplitude * hMul;
        float spd = w.speed * spMul;
        float q = clamp(w.steepness * chMul, 0.0, 1.0);

        float k = 6.2831853 / wl;
        float omega = k * spd;
        float phase = k * dot(w.direction, worldXZ) - omega * t;

        float cosP = cos(phase);
        float sinP = sin(phase);

        float qAmp = q * amp;
        displacement.x += qAmp * w.direction.x * cosP;
        displacement.z += qAmp * w.direction.y * cosP;
        displacement.y += amp * sinP;

        float wa = k * amp;
        normX -= w.direction.x * wa * cosP;
        normZ -= w.direction.y * wa * cosP;
        normY -= q * wa * sinP;
    }

    normal = normalize(float3(normX, max(normY, 0.1), normZ));
}

// ----------------------------------------------------------------------------
// Alexander Alekseev (TDM) Choppy Seascape Wave Function (BabylonJS #O03DUQ)
// ----------------------------------------------------------------------------
static const float2x2 c_octaveRot = float2x2(1.6, 1.2, -1.2, 1.6);

float SeaOctave(float2 uv, float choppy)
{
    uv += ValueNoise(uv);
    float2 wv = 1.0 - abs(sin(uv));
    float2 swv = abs(cos(uv));
    wv = lerp(wv, swv, wv);
    return pow(1.0 - pow(wv.x * wv.y, 0.65), choppy);
}

float SeaHeightMap(float2 uv, float t, float choppy)
{
    float freq = 0.14;
    float amp = 0.55;
    float chp = choppy;
    float h = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float d1 = SeaOctave((uv + t) * freq, chp);
        float d2 = SeaOctave((uv - t) * freq, chp);
        h += (d1 + d2) * amp;
        uv = mul(c_octaveRot, uv);
        freq *= 1.85;
        amp *= 0.24;
        chp = lerp(chp, 1.0, 0.2);
    }
    return h;
}

// ----------------------------------------------------------------------------
// Micro-ripple Normal Evaluation with TDM Seascape Octaves + Analytical Waves
// ----------------------------------------------------------------------------
float3 EvaluateDetailNormal(float2 worldXZ, float t, float3 baseNormal, float strength, float choppy)
{
    float eps = 0.08;
    float hC = SeaHeightMap(worldXZ, t, choppy);
    float hR = SeaHeightMap(worldXZ + float2(eps, 0.0), t, choppy);
    float hU = SeaHeightMap(worldXZ + float2(0.0, eps), t, choppy);

    float dx = (hR - hC) / eps;
    float dz = (hU - hC) / eps;

    float s = clamp(strength, 0.0, 2.5) * 0.45;
    float3 seascapeNormal = normalize(float3(-dx * s, 1.0, -dz * s));

    return normalize(baseNormal + seascapeNormal * 0.65);
}

// ----------------------------------------------------------------------------
// Dynamic Animated Chromatic RGB Caustics (Anderson Mancini / Three.js Water)
// ----------------------------------------------------------------------------
float3 EvaluateCaustics(float2 worldXZ, float t, float depth)
{
    if (depth < 0.02 || depth > 35.0) return float3(0, 0, 0);

    float depthFade = exp(-0.12 * depth);

    float2 uv1 = worldXZ * 0.65 + float2(t * 0.35, t * 0.20);
    float2 uv2 = worldXZ * 1.15 - float2(t * 0.25, t * 0.40);

    // Chromatic dispersion offsets for Red, Green, Blue (RGB separation)
    float2 rOff = float2(0.035, 0.02);
    float2 bOff = float2(-0.035, -0.02);

    float cR = pow(abs(sin(uv1.x + uv2.y + rOff.x) * cos(uv1.y - uv2.x + rOff.y)), 3.8);
    float cG = pow(abs(sin(uv1.x + uv2.y) * cos(uv1.y - uv2.x)), 3.8);
    float cB = pow(abs(sin(uv1.x + uv2.y + bOff.x) * cos(uv1.y - uv2.x + bOff.y)), 3.8);

    float2 uv3 = worldXZ * 2.2 + float2(-t * 0.6, t * 0.3);
    float fine = pow(abs(sin(uv3.x) * cos(uv3.y)), 4.5) * 1.2;

    float3 caustics = (float3(cR, cG, cB) * 2.8 + float3(fine, fine, fine)) * depthFade;
    return caustics;
}

// ----------------------------------------------------------------------------
// Physical Beer-Lambert Spectral Light Absorption
// ----------------------------------------------------------------------------
float3 CalculateBeerLambertAbsorption(float thickness, float3 shallowCol, float3 deepCol, float depthFadeParam)
{
    float3 beta = float3(0.24, 0.075, 0.022) * (10.0 / max(depthFadeParam, 0.1));
    float3 transmittance = exp(-beta * thickness);
    return lerp(deepCol, shallowCol, transmittance);
}

// ----------------------------------------------------------------------------
// Physically-Based Cook-Torrance GGX Specular Microfacet BRDF
// ----------------------------------------------------------------------------
float3 EvaluateCookTorranceGGX(float3 N, float3 V, float3 L, float rgh, float F0)
{
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(N, L));
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float alpha = max(rgh * rgh, 0.002);
    float alpha2 = alpha * alpha;

    // GGX Normal Distribution Function
    float denom = (NdotH * NdotH * (alpha2 - 1.0) + 1.0);
    float D = alpha2 / (3.14159265 * denom * denom);

    // Smith Geometric Masking / Shadowing
    float k = alpha * 0.5;
    float G_V = NdotV / (NdotV * (1.0 - k) + k);
    float G_L = NdotL / (NdotL * (1.0 - k) + k);
    float G = G_V * G_L;

    // Schlick Fresnel
    float F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    float3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    return spec * NdotL;
}

// ----------------------------------------------------------------------------
// Wave Crest Subsurface Scattering (Backlit Translucency)
// ----------------------------------------------------------------------------
float3 EvaluateSubsurfaceScattering(float3 V, float3 L, float3 N, float wavePeak, float waveH)
{
    float3 sssDir = normalize(L + N * 0.35);
    float sssDot = saturate(dot(-V, sssDir));
    float sssPeak = saturate((wavePeak + 0.15) / max(waveH * 0.8 + 0.1, 0.1));
    float sssIntensity = pow(sssDot, 3.5) * sssPeak;

    float3 sssTint = float3(0.04, 0.42, 0.38); // Emerald-cyan scattering tint
    return sssTint * sssIntensity * 2.2;
}

// ----------------------------------------------------------------------------
// Depth Linearization helper for Eunoia (Perspective Z in [0, 1])
// ----------------------------------------------------------------------------
float LinearizeDepth(float zBuffer, float nearP, float farP)
{
    return (nearP * farP) / max(farP - zBuffer * (farP - nearP), 1e-5);
}

#endif // WATER_COMMON_HLSLI
