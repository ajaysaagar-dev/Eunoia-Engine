// ============================================================================
// Eunoia Engine - WaterDisturbCS.hlsl
// AquaTerrain DX12 Water Disturbance Compute Shader
// ============================================================================

RWTexture2D<float> gWaterHeight : register(u0);

cbuffer WaterDisturbConstants : register(b0)
{
    float2 GridSize;
    float2 Center; // in UV coordinates [0, 1]
    float  Radius; // in UV coordinates [0, 1]
    float  Strength;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)GridSize.x || id.y >= (uint)GridSize.y) return;

    float2 uv = (float2(id.xy) + 0.5) / GridSize;
    float dist = distance(uv, Center);
    float safeRadius = max(Radius, 0.001);
    float falloff = saturate(1.0 - dist / safeRadius);

    // Smooth hermite curve for realistic radial impulse
    falloff = smoothstep(0.0, 1.0, falloff);

    gWaterHeight[id.xy] = clamp(gWaterHeight[id.xy] + falloff * Strength, -50.0, 50.0);
}
