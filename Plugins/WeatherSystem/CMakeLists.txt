add_library(WeatherSystem STATIC
    Source/WeatherSystem.cpp
    Source/WeatherPlugin.cpp
)

target_include_directories(WeatherSystem PUBLIC
    Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EunoiaPluginCore/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EngineRenderer/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Engine/EngineScene/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/../Graphics/Include
)
