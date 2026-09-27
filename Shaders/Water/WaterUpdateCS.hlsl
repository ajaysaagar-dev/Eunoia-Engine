// ============================================================================
// Eunoia Engine - WaterUpdateCS.hlsl
// AquaTerrain DX12 Discrete 2D Wave Propagation Compute Shader
// ============================================================================

Texture2D<float> gPrevHeight   : register(t0);
Texture2D<float> gCurrHeight   : register(t1);
RWTexture2D<float> gNextHeight : register(u0);

cbuffer WaterUpdateConstants : register(b0)
{
    float2 GridSize;
    float  k1; // Damping / past height factor
    float  k2; // Current center factor
    float  k3; // Neighbor coupling factor
    float  _pad0;
    float  _pad1;
    float  _pad2;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    int2 coord = int2(id.xy);
    if (coord.x >= (int)GridSize.x || coord.y >= (int)GridSize.y) return;

    float prev = gPrevHeight.Load(int3(coord, 0));
    float curr = gCurrHeight.Load(int3(coord, 0));

    // Clamped boundary neighbor sampling
    int2 maxCoord = int2(GridSize) - 1;
    float left  = gCurrHeight.Load(int3(clamp(coord + int2(-1,  0), int2(0, 0), maxCoord), 0));
    float right = gCurrHeight.Load(int3(clamp(coord + int2( 1,  0), int2(0, 0), maxCoord), 0));
    float up    = gCurrHeight.Load(int3(clamp(coord + int2( 0, -1), int2(0, 0), maxCoord), 0));
    float down  = gCurrHeight.Load(int3(clamp(coord + int2( 0,  1), int2(0, 0), maxCoord), 0));

    // Wave equation propagation step:
    // next = k1 * prev + k2 * curr + k3 * (left + right + up + down)
    float next = k1 * prev + k2 * curr + k3 * (left + right + up + down);

    // Damping boundary absorption near edges to prevent harsh wall reflections
    int distFromEdge = min(min(coord.x, (int)GridSize.x - 1 - coord.x),
                           min(coord.y, (int)GridSize.y - 1 - coord.y));
    if (distFromEdge < 4)
    {
        next *= (float)distFromEdge / 4.0;
    }

    // Safety numerical clamp to prevent NaN / float blowups
    next = clamp(next, -50.0, 50.0);

    gNextHeight[coord] = next;
}
