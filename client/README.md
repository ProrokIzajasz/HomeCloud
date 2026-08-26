# HomeCloud native clients

This directory contains the target no-Qt application architecture:

- `core`: shared C++20 models, session state, API requests, and JSON parsing
- `windows`: Windows App SDK / WinUI 3 client using C++/WinRT
- `android`: Android UI with a small Kotlin layer and shared C++ through NDK/JNI

The shared core is platform-independent and is compiled by the main HomeCloud
CMake build. nlohmann/json 3.12.0 is pinned under `third_party` with its MIT
license and official SHA-256 verified during download.
