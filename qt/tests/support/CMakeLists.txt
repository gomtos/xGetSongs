# xgstestsupport: fakes shared by the tests (the process part of Fakes.kt, FfmetadataReader.kt). Test executables that need it link it.
add_library(xgstestsupport STATIC
    fake_process_runner.h fake_process_runner.cpp
    test_tools.h
    ffmetadata_reader.h ffmetadata_reader.cpp)
target_include_directories(xgstestsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(xgstestsupport PUBLIC xgscore)
xgs_target_defaults(xgstestsupport)
