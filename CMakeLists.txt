project(ptm)

cmake_minimum_required(VERSION 2.8)

if (UNIX)
    list(APPEND CMAKE_CXX_FLAGS "-stdlib=libc++ -std=c++11")
endif()

add_executable(ptmconvert src/taf_ptm.h src/stb_image.h src/stb_image_write.h src/ptmconvert.cpp)