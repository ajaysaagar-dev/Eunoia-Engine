// ============================================================================
// Eunoia Engine — Custom Native UI Shader (DirectX 12)
// High-performance hardware accelerated 2D UI rendering pipeline.
// Independent of ImGui: renders quads, rounded panels, borders, text, and menus.
// ============================================================================

cbuffer UIConstants : register(b0)
{
    float2 g_screenResolution;  // Viewport width and height in pixels
    float2 g_padding;
};

Texture2D g_uiTexture        : register(t0);
SamplerState g_uiSampler     : register(s0);

struct VSInput
{
    float2 position : POSITION;    // Screen space pixel coordinates (X, Y)
    float2 uv       : TEXCOORD0;   // UV coordinate [0..1]
    float4 color    : COLOR0;      // RGBA color tint
    float4 params   : PARAM0;      // x: cornerRadius, y: borderThickness, z: renderMode, w: shadowSpread
    float4 rect     : RECT0;        // Element bounds: left, top, width, height
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
    float4 params   : PARAM0;
    float4 rect     : RECT0;
    float2 pixelPos : TEXCOORD1;
};

// Vertex Shader: converts pixel screen coordinates into normalized device coordinates
PSInput VS_CustomUI(VSInput input)
{
    PSInput output;
    output.pixelPos = input.position;
    
    // Pixel coordinates [0..W, 0..H] to NDC [-1..1, 1..-1]
    float ndcX = (input.position.x / g_screenResolution.x) * 2.0f - 1.0f;
    float ndcY = 1.0f - (input.position.y / g_screenResolution.y) * 2.0f;
    
    output.position = float4(ndcX, ndcY, 0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    output.params = input.params;
    output.rect = input.rect;
    return output;
}

// Signed Distance Function (SDF) for a 2D rounded rectangle
float RoundedBoxSDF(float2 centerPos, float2 halfSize, float radius)
{
    float2 d = abs(centerPos) - halfSize + float2(radius, radius);
    return min(max(d.x, d.y), 0.0f) + length(max(d, 0.0f)) - radius;
}

// Pixel Shader: supports solid quads, rounded panels, borders, glyphs, and drop shadows
float4 PS_CustomUI(PSInput input) : SV_TARGET
{
    int mode = (int)(input.params.z + 0.5f);
    
    // Mode 0: Solid colored quad
    if (mode == 0)
    {
        return input.color;
    }
    
    // Mode 1: Anti-aliased rounded rectangle with optional border
    if (mode == 1)
    {
        float2 boxCenter = input.rect.xy + input.rect.zw * 0.5f;
        float2 halfSize = input.rect.zw * 0.5f;
        float radius = min(input.params.x, min(halfSize.x, halfSize.y));
        float borderThickness = input.params.y;
        
        float dist = RoundedBoxSDF(input.pixelPos - boxCenter, halfSize, radius);
        
        // Anti-aliased outer edge
        float alpha = 1.0f - smoothstep(-0.5f, 0.5f, dist);
        
        // Border rendering if borderThickness > 0
        float4 finalColor = input.color;
        if (borderThickness > 0.0f)
        {
            float borderDist = abs(dist + borderThickness * 0.5f) - borderThickness * 0.5f;
            float borderAlpha = 1.0f - smoothstep(-0.5f, 0.5f, borderDist);
            finalColor.a *= alpha;
        }
        else
        {
            finalColor.a *= alpha;
        }
        
        return finalColor;
    }
    
    // Mode 2: Textured / sprite quad
    if (mode == 2)
    {
        float4 texColor = g_uiTexture.Sample(g_uiSampler, input.uv);
        return texColor * input.color;
    }
    
    // Mode 3: Font glyph / text rendering
    if (mode == 3)
    {
        float4 glyphTex = g_uiTexture.Sample(g_uiSampler, input.uv);
        float alpha = glyphTex.a * input.color.a;
        return float4(input.color.rgb, alpha);
    }
    
    // Mode 4: Drop Shadow (Gaussian-like soft falloff)
    if (mode == 4)
    {
        float2 boxCenter = input.rect.xy + input.rect.zw * 0.5f;
        float2 halfSize = input.rect.zw * 0.5f;
        float radius = input.params.x;
        float spread = max(input.params.w, 1.0f);
        
        float dist = RoundedBoxSDF(input.pixelPos - boxCenter, halfSize, radius);
        float shadowAlpha = saturate(1.0f - (dist / spread));
        shadowAlpha = shadowAlpha * shadowAlpha; // Soft falloff curve
        
        return float4(input.color.rgb, input.color.a * shadowAlpha);
    }
    
    return input.color;
}
