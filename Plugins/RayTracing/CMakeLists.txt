add_library(RayTracing STATIC
    Source/RayTracingSystem.cpp
    Source/RayTracingPlugin.cpp
)

target_include_directories(RayTracing PUBLIC
    Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EunoiaPluginCore/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EngineRenderer/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EngineScene/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../Graphics/Include
)
