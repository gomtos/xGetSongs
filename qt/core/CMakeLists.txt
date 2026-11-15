add_library(xgscore STATIC version.h version.cpp)
target_include_directories(xgscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xgscore PUBLIC Qt6::Core)
xgs_target_defaults(xgscore)
