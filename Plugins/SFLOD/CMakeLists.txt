# ============================================================================
# Eunoia Engine — SFLOD (Single Face LOD / Imposter Cards) Plugin CMake
# ============================================================================

set(SFLOD_SOURCES
    Source/SFLODSystem.cpp
    Source/SFLODPlugin.cpp
)

eunoia_add_plugin(
    NAME        SFLOD
    SOURCES     ${SFLOD_SOURCES}
    PLUGIN_DIR  ${CMAKE_CURRENT_SOURCE_DIR}
    CORE
    DEPS        EngineCore EnginePlatform EngineRHI EngineAssets EngineRenderer EngineScene
)
