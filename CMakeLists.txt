cmake_minimum_required(VERSION 3.24)

project(HomeCloud VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
add_subdirectory(client)

add_library(homecloud_core
    src/auth.cpp
    src/module_repository.cpp
    src/storage.cpp
)

target_include_directories(homecloud_core PUBLIC include)

if(WIN32)
    target_sources(homecloud_core PRIVATE src/crypto_windows.cpp)
    target_link_libraries(homecloud_core PRIVATE bcrypt)
else()
    find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
    target_sources(homecloud_core PRIVATE src/crypto_openssl.cpp)
    target_link_libraries(homecloud_core PRIVATE OpenSSL::Crypto)
endif()

if(MSVC)
    target_compile_options(homecloud_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(homecloud_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(homecloud_server src/main.cpp)
target_link_libraries(homecloud_server PRIVATE homecloud_core)

add_executable(homecloud_api src/api_main.cpp)
target_include_directories(homecloud_api PRIVATE third_party/cpp-httplib)
target_link_libraries(homecloud_api PRIVATE homecloud_core)
if(WIN32)
    target_sources(homecloud_api PRIVATE src/image_variant_windows.cpp)
    target_link_libraries(homecloud_api PRIVATE ws2_32 windowscodecs ole32)
else()
    target_sources(homecloud_api PRIVATE src/image_variant_stub.cpp)
endif()

add_executable(homecloud_admin src/admin_main.cpp)
target_link_libraries(homecloud_admin PRIVATE homecloud_core)

add_executable(homecloud_modules src/module_main.cpp)
target_include_directories(homecloud_modules PRIVATE third_party/cpp-httplib)
target_link_libraries(homecloud_modules PRIVATE homecloud_core)
if(WIN32)
    target_link_libraries(homecloud_modules PRIVATE ws2_32)
endif()

add_executable(homecloud_tests tests/storage_tests.cpp tests/auth_tests.cpp tests/module_repository_tests.cpp)
target_link_libraries(homecloud_tests PRIVATE homecloud_core)
add_test(NAME homecloud_storage_tests COMMAND homecloud_tests)

if(WIN32)
    add_executable(homecloud_image_variant_tests
        tests/image_variant_tests.cpp
        src/image_variant_windows.cpp
    )
    target_include_directories(homecloud_image_variant_tests PRIVATE include)
    target_link_libraries(homecloud_image_variant_tests PRIVATE windowscodecs ole32)
    if(MSVC)
        target_compile_options(homecloud_image_variant_tests PRIVATE /W4 /permissive- /EHsc)
    endif()
    add_test(NAME homecloud_image_variant_tests COMMAND homecloud_image_variant_tests)
endif()
