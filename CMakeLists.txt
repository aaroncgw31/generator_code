cmake_minimum_required(VERSION 3.5)

file(GLOB SOURCES *.cpp)

add_executable(cme_parser ${SOURCES})

target_compile_options(cme_parser PRIVATE -ggdb -std=c++11)
