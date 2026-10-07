// ============================================================================
// Eunoia Engine - WaterVS.hlsl
// Multi-Scale Directional Gerstner Ocean Wave Vertex Shader
// Dual Layer: Continuous Analytical Gerstner Displacement + Subtle GPU Ripples
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
    // Determine world base position:
    // If oceanExtent > 10.0 (camera-centered ocean mesh):
    // input.pos is in [-0.5, +0.5], so scale by oceanExtent and offset by renderOrigin (which has water elevation locked in Y)
    float3 baseWorldP;
    if (oceanExtent > 10.0)
    {
        baseWorldP = float3(
            input.pos.x * oceanExtent + renderOrigin.x,
            renderOrigin.y,
            input.pos.z * oceanExtent + renderOrigin.z
        );
    }
    else
    {
        baseWorldP = input.pos + renderOrigin;
    }

    // 1. Procedural analytical Gerstner waves (Primary ocean layer, evaluated in world space)
    float3 waveDisp;
    float3 waveNorm;
    EvaluateGerstnerWaves(baseWorldP.xz, time, waveSpeed, waveScale, waveHeight, choppiness, waveDisp, waveNorm);

    // Taper displacement at outer boundary of camera-centered ocean to eliminate any hard edge
    if (oceanExtent > 10.0)
    {
        float distFromCam = length(baseWorldP.xz - cameraPos.xz);
        float taperStart = oceanExtent * 0.40;
        float taperEnd = oceanExtent * 0.50;
        float taper = saturate((taperEnd - distFromCam) / max(taperEnd - taperStart, 1.0));
        waveDisp *= taper;
    }

    // 2. GPU Height-Field simulation displacement (Local interaction / ripples, subtle)
    float simH = 0.0;
    float simDx = 0.0;
    float simDz = 0.0;
    if (simEnabled > 0.5)
    {
        simH = gHeightMap.SampleLevel(gLinearSampler, input.uv, 0) * heightScale;
        if (isnan(simH) || isinf(simH)) simH = 0.0;

        float2 uvEps = float2(1.0 / 256.0, 1.0 / 256.0);
        float hL = gHeightMap.SampleLevel(gLinearSampler, input.uv - float2(uvEps.x, 0.0), 0) * heightScale;
        float hR = gHeightMap.SampleLevel(gLinearSampler, input.uv + float2(uvEps.x, 0.0), 0) * heightScale;
        float hD = gHeightMap.SampleLevel(gLinearSampler, input.uv - float2(0.0, uvEps.y), 0) * heightScale;
        float hU = gHeightMap.SampleLevel(gLinearSampler, input.uv + float2(0.0, uvEps.y), 0) * heightScale;
        simDx = (hR - hL) * 128.0;
        simDz = (hU - hD) * 128.0;
    }

    float3 finalWorldP = baseWorldP + waveDisp + float3(0.0, simH, 0.0);
    float3 finalNormal = normalize(waveNorm + float3(-simDx, 0.0, -simDz));

    VSOut o;
    o.clipPos  = mul(viewProj, float4(finalWorldP, 1.0f));
    o.worldPos = finalWorldP;
    o.normal   = finalNormal;
    o.uv       = input.uv;
    o.eyeDist  = length(cameraPos - finalWorldP);
    return o;
}
