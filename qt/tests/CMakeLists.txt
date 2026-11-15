# xgs_add_test(<name> <sources...>): console Qt Test executable linked with xgscore, registered with ctest.
function(xgs_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test xgscore)
    xgs_target_defaults(${name})
    # C4702 (unreachable code) is reported by MSVC at code generation, inside Qt's own literal-length loop
    # (qstringalgorithms.h, lengthHelperContainerLoop), when a u"..." literal becomes a QStringView. The warning is about
    # Qt's header, not about the test; /W4 /WX would fail every test file that uses such a literal.
    target_compile_options(${name} PRIVATE /wd4702)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

xgs_add_test(tst_smoke tst_smoke.cpp)
xgs_add_test(tst_kt_text tst_kt_text.cpp)
xgs_add_test(tst_input_classifier tst_input_classifier.cpp)
xgs_add_test(tst_filename_formatter tst_filename_formatter.cpp)
xgs_add_test(tst_title_parser tst_title_parser.cpp)
xgs_add_test(tst_lyrics_extractor tst_lyrics_extractor.cpp)
xgs_add_test(tst_log_redaction tst_log_redaction.cpp)
