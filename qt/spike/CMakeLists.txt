cmake_minimum_required(VERSION 3.21)
project(xgs-qt-spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(xgs-qt-spike WIN32 src/main.cpp src/report.cpp src/report.h)
target_link_libraries(xgs-qt-spike PRIVATE Qt6::Widgets Qt6::Network)
