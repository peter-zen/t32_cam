cmake_minimum_required(VERSION 3.10)
project(hal)

set(CMAKE_CXX_STANDARD 11)

add_subdirectory(audio)
add_subdirectory(video)
add_subdirectory(gpio)
