#pragma once
#include <cstdint>

namespace Eunoia { namespace Tessellation {

enum class TessellationMode : uint8_t {
    Disabled = 0,
    SilhouetteSmoothing,   // PN Triangles - silhouette curve smoothing
    DisplacementMapping,   // Height-map driven displacement
    Combined,              // PN + displacement
};

struct TessellationSettings {
    TessellationMode mode            = TessellationMode::Disabled;
    float            tessellationFactor = 4.0f;   // Hull shader partition factor (1..64)
    float            maxTessellation    = 64.0f;  // DX12 max hardware tess
    float            minTessellation    = 1.0f;
    // Distance-based LOD
    bool             adaptiveLOD        = true;
    float            lodDistanceMin     = 2.0f;   // Max tess at this distance
    float            lodDistanceMax     = 50.0f;  // Min tess at this distance
    // Displacement
    float            displacementScale  = 0.1f;   // Displacement height scale
    float            displacementBias   = 0.0f;   // Displacement bias
    bool             useDisplacementMap = true;
    // Silhouette (PN Triangles)
    float            pnBlend            = 1.0f;   // Blend between flat and PN
};

class TessellationSystem {
public:
    static TessellationSystem& Get() {
        static TessellationSystem inst;
        return inst;
    }

    void Initialize();
    void Shutdown();

    void SetMode(TessellationMode mode)         { m_settings.mode = mode; }
    TessellationMode GetMode()           const  { return m_settings.mode; }
    bool IsEnabled()                     const  { return m_settings.mode != TessellationMode::Disabled; }

    TessellationSettings&       GetSettingsRef()        { return m_settings; }
    const TessellationSettings& GetSettings()    const  { return m_settings; }

private:
    TessellationSystem() = default;
    TessellationSettings m_settings;
};

}} // namespace Eunoia::Tessellation
