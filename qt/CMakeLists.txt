cmake_minimum_required(VERSION 3.21)
project(xgetsongs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)
# vcpkg port "webview2": headers plus WebView2LoaderStatic.lib (no WebView2Loader.dll). Used by qt/webview only.
find_package(unofficial-webview2 CONFIG REQUIRED)

# Options for our own targets only (not vcpkg's).
function(xgs_target_defaults target)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
    target_compile_definitions(${target} PRIVATE
        _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX
        QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
endfunction()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

enable_testing()
add_subdirectory(core)
add_subdirectory(webview)
add_subdirectory(ui)
add_subdirectory(tests)
add_subdirectory(tools)
