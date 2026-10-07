add_library(GlobalIllumination STATIC
    Source/GlobalIllumination.cpp
    Source/GlobalIlluminationPlugin.cpp
)

target_include_directories(GlobalIllumination PUBLIC
    Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EunoiaPluginCore/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EngineRenderer/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EngineScene/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../Graphics/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../RayTracing/Include
)
