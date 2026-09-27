// ============================================================================
// Eunoia Engine - WaterVS.hlsl
// AquaTerrain DX12 Water Vertex Shader: Displaces with simulated height + analytical waves
// ============================================================================

#include "WaterCommon.hlsli"

Texture2D<float> gHeightMap : register(t0);
SamplerState gLinearSampler : register(s0);

struct VSIn
{
    float3 pos  : POSITION;
    float3 norm : NORMAL;
    float2 uv   : TEXCOORD0;
};

struct VSOut
{
    float4 clipPos  : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float  eyeDist  : TEXCOORD3;
};

VSOut VSMain(VSIn input)
{
    float3 p = input.pos;
    float t = time;

    // 1. GPU Height-field simulation displacement (AquaTerrain)
    float simH = 0.0;
    if (simEnabled > 0.5)
    {
        simH = gHeightMap.SampleLevel(gLinearSampler, input.uv, 0) * heightScale;
    }

    // 2. Procedural analytical waves (for fine detail and large scale water)
    float procH = EvaluateWaves(p.xz, t, waveSpeed, waveScale, waveHeight, flowDirection, flowSpeed);

    float totalH = simH + procH;
    p.y += totalH;

    // 3. Compute normal using finite differences of waves
    float eps = 0.15;
    float hL = EvaluateWaves(p.xz - float2(eps, 0.0), t, waveSpeed, waveScale, waveHeight, flowDirection, flowSpeed);
    float hR = EvaluateWaves(p.xz + float2(eps, 0.0), t, waveSpeed, waveScale, waveHeight, flowDirection, flowSpeed);
    float hD = EvaluateWaves(p.xz - float2(0.0, eps), t, waveSpeed, waveScale, waveHeight, flowDirection, flowSpeed);
    float hU = EvaluateWaves(p.xz + float2(0.0, eps), t, waveSpeed, waveScale, waveHeight, flowDirection, flowSpeed);

    if (simEnabled > 0.5)
    {
        float2 uvEps = float2(1.0 / 256.0, 1.0 / 256.0);
        hL += gHeightMap.SampleLevel(gLinearSampler, input.uv - float2(uvEps.x, 0.0), 0) * heightScale;
        hR += gHeightMap.SampleLevel(gLinearSampler, input.uv + float2(uvEps.x, 0.0), 0) * heightScale;
        hD += gHeightMap.SampleLevel(gLinearSampler, input.uv - float2(0.0, uvEps.y), 0) * heightScale;
        hU += gHeightMap.SampleLevel(gLinearSampler, input.uv + float2(0.0, uvEps.y), 0) * heightScale;
    }

    float dx = (hR - hL) / (2.0 * eps);
    float dz = (hU - hD) / (2.0 * eps);
    float3 N = normalize(float3(-dx * normalScale, 1.0, -dz * normalScale));

    VSOut o;
    o.clipPos  = mul(float4(p, 1.0), viewProj);
    o.worldPos = p;
    o.normal   = N;
    o.uv       = input.uv;
    o.eyeDist  = length(cameraPos - p);
    return o;
}
