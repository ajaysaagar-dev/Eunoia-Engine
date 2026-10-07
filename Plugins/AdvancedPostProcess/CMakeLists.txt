cmake_minimum_required(VERSION 3.15)
project(AdvancedPostProcess)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE ADVPP_SOURCES
    "Source/*.cpp"
    "Include/AdvancedPostProcess/*.h"
)

add_library(AdvancedPostProcess STATIC ${ADVPP_SOURCES})

target_include_directories(AdvancedPostProcess PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Include
    ${EUNOIA_ROOT}/Engine/EngineCore/Include
    ${EUNOIA_DEPS}/glm
)
