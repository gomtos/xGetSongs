# xgstestsupport: fakes shared by the tests (the process part of Fakes.kt, FfmetadataReader.kt, an HTTP server on
# 127.0.0.1). Test executables that need it link it.
add_library(xgstestsupport STATIC
    fake_process_runner.h fake_process_runner.cpp
    test_tools.h
    ffmetadata_reader.h ffmetadata_reader.cpp
    fake_http_server.h fake_http_server.cpp)
target_include_directories(xgstestsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(xgstestsupport PUBLIC xgscore ws2_32)
xgs_target_defaults(xgstestsupport)
