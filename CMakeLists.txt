cmake_minimum_required(VERSION 3.16.0)
project(smoll CXX)

set(CMAKE_CXX_STANDARD 26)

if (SMOLL_BUILD_TESTS)
    add_subdirectory(tests)
endif()

add_library(smoll smoll_gltf.cpp)
