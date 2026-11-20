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
xgs_add_test(tst_models tst_models.cpp)
xgs_add_test(tst_json tst_json.cpp)
xgs_add_test(tst_error_classifier tst_error_classifier.cpp)
xgs_add_test(tst_progress_parser tst_progress_parser.cpp)
xgs_add_test(tst_download_concurrency tst_download_concurrency.cpp)
xgs_add_test(tst_video_info_file tst_video_info_file.cpp)
xgs_add_test(tst_ytdlp_commands tst_ytdlp_commands.cpp)
xgs_add_test(tst_ffmpeg_commands tst_ffmpeg_commands.cpp)
xgs_add_test(tst_ffmetadata tst_ffmetadata.cpp)
xgs_add_test(tst_lyrics_matcher tst_lyrics_matcher.cpp)
xgs_add_test(tst_paths tst_paths.cpp)
xgs_add_test(tst_file_ops tst_file_ops.cpp)
xgs_add_test(tst_line_splitter tst_line_splitter.cpp)
xgs_add_test(tst_command_line tst_command_line.cpp)

# xgs-test-child: the child process of tst_system_process_runner (Win32 and C++ only, no Qt). The test finds it through
# XGS_TEST_CHILD.
add_executable(xgs-test-child helpers/test_child.cpp)
xgs_target_defaults(xgs-test-child)
xgs_add_test(tst_system_process_runner tst_system_process_runner.cpp helpers/argument_lists.h)
target_include_directories(tst_system_process_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(tst_system_process_runner PRIVATE XGS_TEST_CHILD="$<TARGET_FILE:xgs-test-child>")
add_dependencies(tst_system_process_runner xgs-test-child)
