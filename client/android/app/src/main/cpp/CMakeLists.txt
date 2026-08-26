cmake_minimum_required(VERSION 3.22)
project(homecloud_android LANGUAGES CXX)

add_library(homecloud_native SHARED
    native_bridge.cpp
    ../../../../../core/src/client.cpp
)

target_include_directories(homecloud_native PRIVATE
    ../../../../../core/include
    ../../../../../third_party
)

target_compile_features(homecloud_native PRIVATE cxx_std_20)
target_compile_options(homecloud_native PRIVATE -Wall -Wextra -Wpedantic)
