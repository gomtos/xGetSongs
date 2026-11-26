# Qt Test writes each test's full output to qttest/<name>.txt in the build folder as well as to the console, so a failed
# ctest run leaves its output behind even without --output-on-failure.
set(XGS_QTTEST_OUTPUT_DIR ${CMAKE_BINARY_DIR}/qttest)
file(MAKE_DIRECTORY ${XGS_QTTEST_OUTPUT_DIR})

# xgs_add_test(<name> <sources...>): console Qt Test executable linked with xgscore, registered with ctest.
function(xgs_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test xgscore)
    xgs_target_defaults(${name})
    # C4702 (unreachable code) is reported by MSVC at code generation, inside Qt's own literal-length loop
    # (qstringalgorithms.h, lengthHelperContainerLoop), when a u"..." literal becomes a QStringView. The warning is about
    # Qt's header, not about the test; /W4 /WX would fail every test file that uses such a literal.
    target_compile_options(${name} PRIVATE /wd4702)
    add_test(NAME ${name} COMMAND ${name} -o ${XGS_QTTEST_OUTPUT_DIR}/${name}.txt,txt -o -,txt)
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
xgs_add_test(tst_google_search_query tst_google_search_query.cpp)
xgs_add_test(tst_page_classifier tst_page_classifier.cpp)
xgs_add_test(tst_page_watch tst_page_watch.cpp)

add_subdirectory(support)
xgs_add_test(tst_tool_locator tst_tool_locator.cpp)
xgs_add_test(tst_tool_manager tst_tool_manager.cpp)
target_link_libraries(tst_tool_manager PRIVATE xgstestsupport)
xgs_add_test(tst_ytdlp_resolver tst_ytdlp_resolver.cpp)
target_link_libraries(tst_ytdlp_resolver PRIVATE xgstestsupport)
xgs_add_test(tst_ffmetadata_reader tst_ffmetadata_reader.cpp)
target_link_libraries(tst_ffmetadata_reader PRIVATE xgstestsupport)
xgs_add_test(tst_m4a_tagger tst_m4a_tagger.cpp)
target_link_libraries(tst_m4a_tagger PRIVATE xgstestsupport)
xgs_add_test(tst_local_folder_sink tst_local_folder_sink.cpp)
xgs_add_test(tst_qt_http_client tst_qt_http_client.cpp)
target_link_libraries(tst_qt_http_client PRIVATE xgstestsupport ws2_32)
xgs_add_test(tst_http_fetch tst_http_fetch.cpp)
target_link_libraries(tst_http_fetch PRIVATE xgstestsupport)
xgs_add_test(tst_fallback_lyrics_provider tst_fallback_lyrics_provider.cpp)
target_link_libraries(tst_fallback_lyrics_provider PRIVATE xgstestsupport)
xgs_add_test(tst_lrclib_lyrics_provider tst_lrclib_lyrics_provider.cpp)
target_link_libraries(tst_lrclib_lyrics_provider PRIVATE xgstestsupport)
xgs_add_test(tst_item_downloader tst_item_downloader.cpp)
target_link_libraries(tst_item_downloader PRIVATE xgstestsupport)
xgs_add_test(tst_default_download_service tst_default_download_service.cpp)
target_link_libraries(tst_default_download_service PRIVATE xgstestsupport)
# The answers of lyrics_card.js read by parseCard (the Kotlin LyricsCardParserTest's pages, as script answers written by hand),
# and the check that the embedded script is the repository's file.
xgs_add_test(tst_lyrics_card tst_lyrics_card.cpp)
target_link_libraries(tst_lyrics_card PRIVATE xgstestsupport)
target_compile_definitions(tst_lyrics_card PRIVATE XGS_LYRICS_CARD_JS="${PROJECT_SOURCE_DIR}/core/lyrics/google/lyrics_card.js")
xgs_add_test(tst_driven_browser tst_driven_browser.cpp)
target_link_libraries(tst_driven_browser PRIVATE xgstestsupport)
# The channel between the lookup's worker threads and the hidden browser's thread (Kotlin BrowserChannelTest), and the agent
# that carries its commands out on a page (the tests of sidecar-host/src/browser.rs; no Kotlin class).
xgs_add_test(tst_browser_channel tst_browser_channel.cpp)
# A deadlock (detachFromInsideWakeDoesNotDeadlock) hangs the exe; this ends it well before Qt Test's 300 s watchdog. The whole
# exe takes about two seconds.
set_tests_properties(tst_browser_channel PROPERTIES TIMEOUT 120)
xgs_add_test(tst_browser_agent tst_browser_agent.cpp)
# The Google lookup's politeness rules (Kotlin GoogleLyricsFetcherTest) and the provider the download job calls (Kotlin
# GoogleLyricsProviderTest).
xgs_add_test(tst_google_lyrics_fetcher tst_google_lyrics_fetcher.cpp)
target_link_libraries(tst_google_lyrics_fetcher PRIVATE xgstestsupport)
xgs_add_test(tst_google_lyrics_provider tst_google_lyrics_provider.cpp)
target_link_libraries(tst_google_lyrics_provider PRIVATE xgstestsupport)
# Their threaded tests bound every wait with events; this ends a hang that is left (a join after a failed wait) well before Qt
# Test's 300 s watchdog. Each exe takes about a second or less (the fetcher's includes three 200 ms head starts).
set_tests_properties(tst_google_lyrics_fetcher tst_google_lyrics_provider PROPERTIES TIMEOUT 120)
add_subdirectory(integration)
add_subdirectory(webview)

# The screen logic (xgsui): the ports of shell/tests/{labels,holder}.test.ts and of the settings part of desktop.test.ts (with the
# settings-file tests of sidecar-host/src/settings.rs), and the bridge from the engine to the GUI thread.
foreach(test tst_labels tst_settings_store tst_app_state_holder)
    xgs_add_test(${test} ui/${test}.cpp ui/fake_app_api.h)
    target_link_libraries(${test} PRIVATE xgsui)
endforeach()
xgs_add_test(tst_engine_api ui/tst_engine_api.cpp ui/fake_app_api.h)
target_link_libraries(tst_engine_api PRIVATE xgsui xgstestsupport)
# Every wait of the bridge's tests is bounded by 5 s; this ends a hang that is left (a join after a failed wait) well before Qt
# Test's 300 s watchdog. The exe takes a second or two.
set_tests_properties(tst_engine_api PROPERTIES TIMEOUT 120)

# xgs-test-child: the child process of tst_system_process_runner (Win32 and C++ only, no Qt). The test finds it through
# XGS_TEST_CHILD.
add_executable(xgs-test-child helpers/test_child.cpp)
xgs_target_defaults(xgs-test-child)
xgs_add_test(tst_system_process_runner tst_system_process_runner.cpp helpers/argument_lists.h)
target_include_directories(tst_system_process_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(tst_system_process_runner PRIVATE XGS_TEST_CHILD="$<TARGET_FILE:xgs-test-child>")
add_dependencies(tst_system_process_runner xgs-test-child)
# The download job through the real runner and the real folder sink, with xgs-test-child playing yt-dlp and ffmpeg (no Kotlin
# pair: the Kotlin job is tested with fakes only).
xgs_add_test(tst_job_with_real_processes tst_job_with_real_processes.cpp)
target_link_libraries(tst_job_with_real_processes PRIVATE xgstestsupport)
target_compile_definitions(tst_job_with_real_processes PRIVATE XGS_TEST_CHILD="$<TARGET_FILE:xgs-test-child>")
add_dependencies(tst_job_with_real_processes xgs-test-child)

# The playlist logic (xgsui, no Kotlin or TypeScript original): the file, the merge of the remote lists, the check of an entry, and
# the transfer of the remote lists (against 127.0.0.1 only; the real addresses are https and are never contacted).
foreach(test tst_playlist_store tst_playlist_merge tst_playlist_entry_check tst_remote_playlists)
    xgs_add_test(${test} ui/${test}.cpp)
    target_link_libraries(${test} PRIVATE xgsui)
endforeach()
# Every wait of the transfer tests is bounded; this ends a hang that is left well before Qt Test's 300 s watchdog.
set_tests_properties(tst_remote_playlists PROPERTIES TIMEOUT 120)
