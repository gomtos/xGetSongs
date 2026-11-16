add_library(xgscore STATIC
    version.h version.cpp
    text/kt_text.h text/kt_text.cpp text/kt_tables.inc
    input/input_classifier.h input/input_classifier.cpp
    filename/filename_formatter.h filename/filename_formatter.cpp
    title/title_parser.h title/title_parser.cpp
    lyrics/lyrics_extractor.h lyrics/lyrics_extractor.cpp
    log/log_redaction.h log/log_redaction.cpp
    model/models.h)
target_include_directories(xgscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xgscore PUBLIC Qt6::Core)
xgs_target_defaults(xgscore)
