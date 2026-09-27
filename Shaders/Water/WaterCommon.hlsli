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
    float    modelRotY;         // 55
    float3   modelPos;          // 56-58
    float    _padCommon;        // 59
};

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
// Analytical Sine Wave Evaluation (AquaTerrain Section 12)
// ----------------------------------------------------------------------------
float SinWave(float2 pos, float2 dir, float wavelength, float speed, float amplitude, float t)
{
    float safeWl = max(wavelength, 0.05);
    float k = 6.2831853 / safeWl;
    float d = dot(normalize(dir + float2(0.0001, 0.0001)), pos);
    return amplitude * sin(k * d + t * speed);
}

float EvaluateWaves(float2 pos, float t, float speed, float scale, float height, float2 flowDir, float flowSpd)
{
    float2 p = pos + flowDir * (t * flowSpd);
    float safeScale = max(scale, 0.01);
    float safeHeight = clamp(height, 0.0, 20.0);
    float safeSpeed = clamp(speed, 0.0, 10.0);

    float h0 = SinWave(p, float2(1.0, 0.2),  6.0 * safeScale, 1.2 * safeSpeed, 0.45 * safeHeight, t);
    float h1 = SinWave(p, float2(-0.4, 0.9), 3.5 * safeScale, 1.6 * safeSpeed, 0.35 * safeHeight, t);
    float h2 = SinWave(p, float2(0.7, -0.7), 2.0 * safeScale, 2.2 * safeSpeed, 0.20 * safeHeight, t);
    return h0 + h1 + h2;
}

// ----------------------------------------------------------------------------
// Depth Linearization helper for Eunoia (Perspective Z in [0, 1])
// ----------------------------------------------------------------------------
float LinearizeDepth(float zBuffer, float nearP, float farP)
{
    return (nearP * farP) / max(farP - zBuffer * (farP - nearP), 1e-5);
}

#endif // WATER_COMMON_HLSLI
