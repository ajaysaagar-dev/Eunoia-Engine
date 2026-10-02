cmake_minimum_required(VERSION 3.15)
project(RayTracePlugin)

set(CMAKE_CXX_STANDARD 17)

include_directories(
    Include
    ../../Engine/EngineScene/Include
    ../../Plugins/Collisions/Include
    ../../ThirdParty/glm
)

set(SOURCES
    Source/RayTracePlugin.cpp
    Source/RayTraceSystem.cpp
)

add_library(RayTrace STATIC ${SOURCES})
