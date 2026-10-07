// ============================================================================
// Eunoia Engine — Mesh Cluster Culling Plugin
// ClusterCull.hlsl: GPU-Driven Meshlet/Cluster Visibility Culling Compute Shader
// Adria-exact GPU Culling Algorithms: SIMD Frustum AABB Corner Projection & Hi-Z Occlusion
// ============================================================================

struct GPUClusterData
{
    float3 boundsMin;
    float  sphereRadius;
    float3 boundsMax;
    uint   indexOffset;
    float3 sphereCenter;
    uint   indexCount;
    float3 coneApex;
    float  coneCutoff;
    float3 coneAxis;
    uint   baseVertex;
    float4x4 worldMatrix;
};

// D3D12_DRAW_INDEXED_ARGUMENTS (5 uints)
struct DrawIndexedArguments
{
    uint IndexCountPerInstance;
    uint InstanceCount;
    uint StartIndexLocation;
    int  BaseVertexLocation;
    uint StartInstanceLocation;
};

cbuffer CameraCullData : register(b0)
{
    float4x4 g_viewProj;
    float4   g_frustumPlanes[6]; // Left, Right, Bottom, Top, Near, Far: (nx, ny, nz, d)
    float3   g_cameraPos;
    float    g_minScreenSize;    // In pixels
    float2   g_viewportSize;
    float2   g_hizSize;
    uint     g_totalClusters;
    uint     g_enableFrustum;
    uint     g_enableBackface;
    uint     g_enableHiZ;
    uint     g_enableScreenSize;
    uint     g_debugMode;        // 0: Normal, 1: Show Clusters, etc.
    float    g_hizMaxMip;
    float    g_padding;
};

StructuredBuffer<GPUClusterData>            g_clusters           : register(t0);
Texture2D<float>                           g_hizPyramid         : register(t1);
SamplerState                               g_pointSampler       : register(s0);

RWStructuredBuffer<uint>                   g_visibleClusterIds  : register(u0);
RWStructuredBuffer<DrawIndexedArguments>   g_indirectArgs       : register(u1);
RWStructuredBuffer<uint>                   g_visibilityCounters : register(u2); // [0] = visibleCount, [1] = culledCount

// ----------------------------------------------------------------------------
// Adria-exact SIMD helper functions & structures (from GpuDrivenRendering.hlsli)
// ----------------------------------------------------------------------------
float adria_min3(float a, float b, float c)
{
    return min(min(a, b), c);
}

float adria_max3(float a, float b, float c)
{
    return max(max(a, b), c);
}

float3 adria_min3(float3 a, float3 b, float3 c)
{
    return min(min(a, b), c);
}

float3 adria_max3(float3 a, float3 b, float3 c)
{
    return max(max(a, b), c);
}

struct FrustumCullData
{
    bool   isVisible;
    float3 rectMin;
    float3 rectMax;
};

// Adria's exact SIMD 3-axis corner projection & plane inequality Frustum Culling
FrustumCullData FrustumCull(float3 aabbCenter, float3 aabbExtents, float4x4 worldToClip)
{
    FrustumCullData data = (FrustumCullData)0;
    data.isVisible = true;

    // Transform 3 axis vectors of AABB into clip space
    float3x4 axis;
    axis[0] = mul(float4(aabbExtents.x * 2.0f, 0.0f, 0.0f, 0.0f), worldToClip);
    axis[1] = mul(float4(0.0f, aabbExtents.y * 2.0f, 0.0f, 0.0f), worldToClip);
    axis[2] = mul(float4(0.0f, 0.0f, aabbExtents.z * 2.0f, 0.0f), worldToClip);

    // Derive all 8 corner clip positions with 4 additions
    float4 pos000 = mul(float4(aabbCenter - aabbExtents, 1.0f), worldToClip);
    float4 pos100 = pos000 + axis[0];
    float4 pos010 = pos000 + axis[1];
    float4 pos110 = pos010 + axis[0];
    float4 pos001 = pos000 + axis[2];
    float4 pos101 = pos100 + axis[2];
    float4 pos011 = pos010 + axis[2];
    float4 pos111 = pos110 + axis[2];

    float minW = adria_min3(
        adria_min3(pos000.w, pos100.w, pos010.w),
        adria_min3(pos110.w, pos001.w, pos101.w),
        adria_min3(pos011.w, pos111.w, 1e9f)
    );

    float maxW = adria_max3(
        adria_max3(pos000.w, pos100.w, pos010.w),
        adria_max3(pos110.w, pos001.w, pos101.w),
        adria_max3(pos011.w, pos111.w, -1e9f)
    );

    // Plane inequalities test (SIMD for Left, Right, Bottom, Top)
    float4 planeMins = min(
        min(float4(pos000.xy, -pos000.xy) - pos000.w, float4(pos001.xy, -pos001.xy) - pos001.w),
        min(float4(pos010.xy, -pos010.xy) - pos010.w, float4(pos100.xy, -pos100.xy) - pos100.w)
    );
    planeMins = min(planeMins,
        min(
            min(float4(pos110.xy, -pos110.xy) - pos110.w, float4(pos011.xy, -pos011.xy) - pos011.w),
            min(float4(pos101.xy, -pos101.xy) - pos101.w, float4(pos111.xy, -pos111.xy) - pos111.w)
        )
    );

    // Screen space AABB (perspective divide)
    float3 ssPos000 = pos000.xyz / pos000.w;
    float3 ssPos100 = pos100.xyz / pos100.w;
    float3 ssPos010 = pos010.xyz / pos010.w;
    float3 ssPos110 = pos110.xyz / pos110.w;
    float3 ssPos001 = pos001.xyz / pos001.w;
    float3 ssPos101 = pos101.xyz / pos101.w;
    float3 ssPos011 = pos011.xyz / pos011.w;
    float3 ssPos111 = pos111.xyz / pos111.w;

    data.rectMin = min(
        min(adria_min3(ssPos000, ssPos100, ssPos010), adria_min3(ssPos110, ssPos001, ssPos101)),
        min(ssPos011, ssPos111)
    );

    data.rectMax = max(
        max(adria_max3(ssPos000, ssPos100, ssPos010), adria_max3(ssPos110, ssPos001, ssPos101)),
        max(ssPos011, ssPos111)
    );

    data.isVisible = data.isVisible && (data.rectMax.z > 0.0f);

    if (minW <= 0.0f && maxW > 0.0f)
    {
        // Near plane crossing: box spans camera near plane, conservatively mark visible and full-screen NDC
        data.rectMin = float3(-1.0f, -1.0f, 0.0f);
        data.rectMax = float3(1.0f, 1.0f, 1.0f);
        data.isVisible = true;
    }
    else
    {
        data.isVisible = data.isVisible && (maxW > 0.0f);
    }

    data.isVisible = data.isVisible && !any(planeMins > 0.0f);

    return data;
}

FrustumCullData FrustumCull(float3 aabbCenter, float3 aabbExtents, float4x4 localToWorld, float4x4 worldToClip)
{
    // Transform bounds to world space (using absolute transformed extents)
    float3x3 m = (float3x3)localToWorld;
    float3 worldExtents = abs(m[0]) * aabbExtents.x + abs(m[1]) * aabbExtents.y + abs(m[2]) * aabbExtents.z;
    float3 worldCenter = mul(float4(aabbCenter, 1.0f), localToWorld).xyz;
    return FrustumCull(worldCenter, worldExtents, worldToClip);
}

// Adria's exact conservative Hi-Z Occlusion Culling
bool HZBCull(FrustumCullData cullData, Texture2D<float> hzbTexture)
{
    uint2 hzbDimensions;
    uint hzbMipCount;
    hzbTexture.GetDimensions(0, hzbDimensions.x, hzbDimensions.y, hzbMipCount);

    float2 minXY = cullData.rectMin.xy;
    float2 maxXY = cullData.rectMax.xy;

    // Transform NDC [-1, 1] to UV [0, 1] (Y flipped)
    float4 rect = saturate(float4(minXY, maxXY) * float2(0.5f, -0.5f).xyxy + 0.5f).xwzy;
    int4 rectPixels = int4(rect * hzbDimensions.xyxy + float4(0.5f, 0.5f, -0.5f, -0.5f));
    rectPixels = int4(rectPixels.xy, max(rectPixels.xy, rectPixels.zw));

    // Calculate hi-Z buffer mip based on bounding box extent in texels
    int2 size = (maxXY - minXY) * hzbDimensions.xy * 0.5f;
    int mip = ceil(log2(max(max(size.x, size.y), 1)));
    mip = clamp(mip, 0, (int)hzbMipCount - 1);

    // Texel footprint for the lower (finer-grained) level
    int lowerLevel = max(mip - 1, 0);
    float2 scale = exp2(-(float)lowerLevel);
    float2 a = floor(rect.xy * (float2)hzbDimensions * scale);
    float2 b = ceil(rect.zw * (float2)hzbDimensions * scale);
    float2 dims = b - a;

    // Use lower mip level if footprint touches <= 2 texels in both dimensions
    if (dims.x <= 2.0f && dims.y <= 2.0f)
    {
        mip = lowerLevel;
    }

    rectPixels >>= mip;
    float2 texelSize = (1.0f / (float2)hzbDimensions) * (float)(1u << mip);

    // 4-point PointClamp depth sample across bounding rect
    float4 depths = {
        hzbTexture.SampleLevel(g_pointSampler, (rectPixels.xy + 0.5f) * texelSize, mip),
        hzbTexture.SampleLevel(g_pointSampler, (rectPixels.zy + 0.5f) * texelSize, mip),
        hzbTexture.SampleLevel(g_pointSampler, (rectPixels.xw + 0.5f) * texelSize, mip),
        hzbTexture.SampleLevel(g_pointSampler, (rectPixels.zw + 0.5f) * texelSize, mip)
    };

    // In standard Z (0 = near, 1 = far), conservative occluder is max depth across tile
    float depth = max(max(depths.x, depths.y), max(depths.z, depths.w));
    float minDepth = cullData.rectMin.z;

    const float depthBias = 0.0005f;
    bool isOccluded = (minDepth > (depth + depthBias));

    return cullData.isVisible && !isOccluded;
}

bool IsBackfacing(float3 worldApex, float3 worldAxis, float cutoff)
{
    float3 toCamera = g_cameraPos - worldApex;
    float dist = length(toCamera);
    if (dist < 1e-4f) return false;
    toCamera /= dist;

    // If cone angle is wide (cutoff < 0), cluster can see both front and back
    if (cutoff < -0.99f) return false;

    // Normal cone test: if dot(toCamera, axis) < -sin(half_angle), all normals point away
    float sinHalfAngle = sqrt(max(0.0f, 1.0f - cutoff * cutoff));
    return (dot(toCamera, worldAxis) < -sinHalfAngle);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint clusterIdx = dispatchThreadId.x;
    if (clusterIdx >= g_totalClusters)
        return;

    GPUClusterData cluster = g_clusters[clusterIdx];

    // Compute local AABB center and half-extents
    float3 aabbCenter = (cluster.boundsMin + cluster.boundsMax) * 0.5f;
    float3 aabbExtents = (cluster.boundsMax - cluster.boundsMin) * 0.5f;

    // 1. Adria-exact SIMD Frustum Culling
    FrustumCullData cullData;
    if (g_enableFrustum != 0)
    {
        cullData = FrustumCull(aabbCenter, aabbExtents, cluster.worldMatrix, g_viewProj);
        if (!cullData.isVisible)
        {
            InterlockedAdd(g_visibilityCounters[1], 1);
            // Write culled argument so ExecuteIndirect safely skips draw
            DrawIndexedArguments culledArgs;
            culledArgs.IndexCountPerInstance = 0;
            culledArgs.InstanceCount = 0;
            culledArgs.StartIndexLocation = cluster.indexOffset;
            culledArgs.BaseVertexLocation = (int)cluster.baseVertex;
            culledArgs.StartInstanceLocation = 0;
            g_indirectArgs[clusterIdx] = culledArgs;
            return;
        }
    }
    else
    {
        cullData.isVisible = true;
        cullData.rectMin = float3(-1.0f, -1.0f, 0.0f);
        cullData.rectMax = float3(1.0f, 1.0f, 1.0f);
    }

    // 2. Backface / Normal-Cone Culling
    if (g_enableBackface != 0 && cluster.coneCutoff > -0.95f)
    {
        float3 worldApex = mul(float4(cluster.coneApex, 1.0f), cluster.worldMatrix).xyz;
        float3 worldAxis = normalize(mul(float4(cluster.coneAxis, 0.0f), cluster.worldMatrix).xyz);
        if (IsBackfacing(worldApex, worldAxis, cluster.coneCutoff))
        {
            InterlockedAdd(g_visibilityCounters[1], 1);
            DrawIndexedArguments culledArgs;
            culledArgs.IndexCountPerInstance = 0;
            culledArgs.InstanceCount = 0;
            culledArgs.StartIndexLocation = cluster.indexOffset;
            culledArgs.BaseVertexLocation = (int)cluster.baseVertex;
            culledArgs.StartInstanceLocation = 0;
            g_indirectArgs[clusterIdx] = culledArgs;
            return;
        }
    }

    // 3. Screen-Space Projected Size Test (Adria rect size in pixels)
    if (g_enableScreenSize != 0)
    {
        float2 rectPixelSize = (cullData.rectMax.xy - cullData.rectMin.xy) * 0.5f * g_viewportSize;
        float maxPixelDim = max(rectPixelSize.x, rectPixelSize.y);
        if (maxPixelDim < g_minScreenSize)
        {
            InterlockedAdd(g_visibilityCounters[1], 1);
            DrawIndexedArguments culledArgs;
            culledArgs.IndexCountPerInstance = 0;
            culledArgs.InstanceCount = 0;
            culledArgs.StartIndexLocation = cluster.indexOffset;
            culledArgs.BaseVertexLocation = (int)cluster.baseVertex;
            culledArgs.StartInstanceLocation = 0;
            g_indirectArgs[clusterIdx] = culledArgs;
            return;
        }
    }

    // 4. Adria-exact Hi-Z Occlusion Culling
    if (g_enableHiZ != 0 && g_hizMaxMip > 0.0f)
    {
        if (!HZBCull(cullData, g_hizPyramid))
        {
            InterlockedAdd(g_visibilityCounters[1], 1);
            DrawIndexedArguments culledArgs;
            culledArgs.IndexCountPerInstance = 0;
            culledArgs.InstanceCount = 0;
            culledArgs.StartIndexLocation = cluster.indexOffset;
            culledArgs.BaseVertexLocation = (int)cluster.baseVertex;
            culledArgs.StartInstanceLocation = 0;
            g_indirectArgs[clusterIdx] = culledArgs;
            return;
        }
    }

    // Cluster is VISIBLE!
    uint visibleSlot;
    InterlockedAdd(g_visibilityCounters[0], 1, visibleSlot);

    g_visibleClusterIds[visibleSlot] = clusterIdx;

    // Fill indirect draw arguments at clusterIdx for direct ExecuteIndirect offset
    DrawIndexedArguments args;
    args.IndexCountPerInstance = cluster.indexCount;
    args.InstanceCount = 1;
    args.StartIndexLocation = cluster.indexOffset;
    args.BaseVertexLocation = (int)cluster.baseVertex;
    args.StartInstanceLocation = 0;

    g_indirectArgs[clusterIdx] = args;
}
