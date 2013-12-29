project(ptm)

cmake_minimum_required(VERSION 2.6.3 FATAL_ERROR)

add_executable(ptmconvert src/ptmconvert.cpp src/stb/stb_image.h src/stb/stb_image.c)