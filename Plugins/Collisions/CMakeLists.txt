# ============================================================================
# Eunoia Engine — Collisions Plugin CMake
# ============================================================================

set(COLLISIONS_SOURCES
    Source/CollisionSystem.cpp
    Source/CollisionsPlugin.cpp
)

eunoia_add_plugin(
    NAME        Collisions
    SOURCES     ${COLLISIONS_SOURCES}
    PLUGIN_DIR  ${CMAKE_CURRENT_SOURCE_DIR}
    CORE
    DEPS        EngineCore EnginePlatform EngineRHI EngineAssets EngineRenderer EngineScene
)
