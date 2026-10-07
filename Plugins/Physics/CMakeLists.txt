cmake_minimum_required(VERSION 3.15)
project(PhysicsPlugin)

set(CMAKE_CXX_STANDARD 17)

include_directories(
    Include
    ../../Engine/EngineScene/Include
    ../../Plugins/Collisions/Include
    ../../ThirdParty/glm
)

set(SOURCES
    Source/PhysicsPlugin.cpp
    Source/PhysicsSystem.cpp
)

add_library(Physics STATIC ${SOURCES})
