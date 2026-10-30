cmake_minimum_required(VERSION 3.15)
project(CharacterMechanicsPlugin)

set(CMAKE_CXX_STANDARD 20)

include_directories(
    Include
    ../../Engine/EngineCore/Include
    ../../Engine/EngineScene/Include
    ../../Engine/EunoiaPluginCore/Include
    ../../deps/glm
)

set(SOURCES
    Source/CharacterMechanicsSystem.cpp
    Source/CharacterMechanicsPlugin.cpp
)

add_library(CharacterMechanics STATIC ${SOURCES})
