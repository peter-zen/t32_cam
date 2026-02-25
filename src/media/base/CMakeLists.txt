cmake_minimum_required(VERSION 3.10)

add_library(media_base INTERFACE)
target_include_directories(media_base INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})