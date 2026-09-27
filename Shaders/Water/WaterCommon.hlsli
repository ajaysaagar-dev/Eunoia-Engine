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
// Micro-ripple Normal Evaluation for WaterPS (dev.md Section 16)
// ----------------------------------------------------------------------------
float3 EvaluateDetailNormal(float2 worldXZ, float t, float3 baseNormal, float strength)
{
    static const float2 dirs[4] = {
        float2(0.707, 0.707),
        float2(-0.894, 0.447),
        float2(0.316, -0.949),
        float2(-0.600, -0.800)
    };
    static const float freqs[4] = { 2.5, 4.2, 7.8, 12.0 };
    static const float speeds[4] = { 1.8, 2.4, 3.1, 4.0 };
    static const float amps[4] = { 0.04, 0.025, 0.015, 0.008 };

    float dx = 0.0;
    float dz = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float phase = dot(dirs[i], worldXZ) * freqs[i] - t * speeds[i];
        float c = cos(phase) * amps[i] * freqs[i];
        dx += dirs[i].x * c;
        dz += dirs[i].y * c;
    }
    float s = clamp(strength, 0.0, 2.0);
    float3 rippleNorm = float3(-dx * s, 1.0, -dz * s);
    return normalize(baseNormal + rippleNorm * 0.35);
}

// ----------------------------------------------------------------------------
// Depth Linearization helper for Eunoia (Perspective Z in [0, 1])
// ----------------------------------------------------------------------------
float LinearizeDepth(float zBuffer, float nearP, float farP)
{
    return (nearP * farP) / max(farP - zBuffer * (farP - nearP), 1e-5);
}

#endif // WATER_COMMON_HLSLI
