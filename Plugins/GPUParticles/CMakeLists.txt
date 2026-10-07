cmake_minimum_required(VERSION 3.15)
project(GPUParticles)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE GPUPART_SOURCES
    "Source/*.cpp"
    "Include/GPUParticles/*.h"
)

add_library(GPUParticles STATIC ${GPUPART_SOURCES})

target_include_directories(GPUParticles PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${EUNOIA_ROOT}/Engine/EngineCore/Include
    ${EUNOIA_DEPS}/glm
)
