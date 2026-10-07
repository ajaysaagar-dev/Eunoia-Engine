add_library(VolumetricAtmosphere STATIC
    Source/VolumetricAtmosphere.cpp
    Source/VolumetricAtmospherePlugin.cpp
)

target_include_directories(VolumetricAtmosphere PUBLIC
    Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EunoiaPluginCore/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EngineRenderer/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EngineScene/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../Graphics/Include
)
