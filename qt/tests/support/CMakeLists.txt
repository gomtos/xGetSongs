# xgstestsupport: fakes shared by the tests (the process, lyrics and download parts of Fakes.kt, FfmetadataReader.kt, an HTTP server
# on 127.0.0.1, HTTP client stand-ins, the Google lyrics fakes). Test executables that need it link it.
add_library(xgstestsupport STATIC
    fake_process_runner.h fake_process_runner.cpp
    test_tools.h
    ffmetadata_reader.h ffmetadata_reader.cpp
    fake_http_server.h fake_http_server.cpp
    fake_http_client.h fake_http_client.cpp
    fake_lyrics_provider.h
    job_fakes.h job_fakes.cpp
    fake_output_sink.h
    google_fakes.h)
target_include_directories(xgstestsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(xgstestsupport PUBLIC xgscore ws2_32)
xgs_target_defaults(xgstestsupport)
