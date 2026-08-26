add_library(homecloud_client_core
    core/src/client.cpp
)

if(MSVC)
    set_property(TARGET homecloud_client_core PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_include_directories(homecloud_client_core PUBLIC
    core/include
    third_party
)

if(MSVC)
    target_compile_options(homecloud_client_core PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(homecloud_client_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(homecloud_client_core_tests core/tests/client_tests.cpp)
target_link_libraries(homecloud_client_core_tests PRIVATE homecloud_client_core)
if(MSVC)
    set_property(TARGET homecloud_client_core_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
add_test(NAME homecloud_client_core_tests COMMAND homecloud_client_core_tests)

if(WIN32)
    set(HOMECLOUD_WEBVIEW2_VERSION "1.0.3179.45" CACHE STRING "WebView2 SDK version")
    set(HOMECLOUD_WEBVIEW2_ROOT "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/${HOMECLOUD_WEBVIEW2_VERSION}")
    if(NOT EXISTS "${HOMECLOUD_WEBVIEW2_ROOT}/build/native/include/WebView2.h")
        message(FATAL_ERROR "WebView2 SDK not found at ${HOMECLOUD_WEBVIEW2_ROOT}")
    endif()

    add_executable(homecloud_windows WIN32
        windows/src/webview_main.cpp
        windows/resources/homecloud.rc
    )
    target_include_directories(homecloud_windows PRIVATE
        "${HOMECLOUD_WEBVIEW2_ROOT}/build/native/include"
    )
    target_link_libraries(homecloud_windows PRIVATE
        "${HOMECLOUD_WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib"
        user32
        ole32
        shlwapi
        version
    )
    target_compile_definitions(homecloud_windows PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        set_property(TARGET homecloud_windows PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    if(MSVC)
        target_compile_options(homecloud_windows PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
endif()
