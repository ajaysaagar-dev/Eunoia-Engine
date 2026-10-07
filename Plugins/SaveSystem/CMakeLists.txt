cmake_minimum_required(VERSION 3.15)
project(SaveSystemPlugin)

set(CMAKE_CXX_STANDARD 17)

include_directories(
    Include
    ../../Engine/EngineCore/Include
    ../../Engine/EunoiaPluginCore/Include
    ../../deps/glm
    ../../deps/json
)

set(SOURCES
    Source/SaveSystem.cpp
    Source/SaveSystemPlugin.cpp
)

add_library(SaveSystem STATIC ${SOURCES})
