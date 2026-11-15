cmake_minimum_required(VERSION 3.21)
project(xgs-qt-spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
# vcpkg port "webview2": headers plus WebView2LoaderStatic.lib (no WebView2Loader.dll).
find_package(unofficial-webview2 CONFIG REQUIRED)

add_executable(xgs-qt-spike WIN32 src/main.cpp src/report.cpp src/report.h
    src/job_process.cpp src/job_process.h
    src/hidden_webview.cpp src/hidden_webview.h)
target_link_libraries(xgs-qt-spike PRIVATE Qt6::Widgets Qt6::Network unofficial::webview2::webview2)
