# The checks that start a real WebView2 (Q3 Task 6): the hidden window and its rules (tst_hidden_web_view), the card script in
# the real browser (tst_lyrics_card_parser, the port of Kotlin's LyricsCardParserTest) and the whole lookup chain without Google
# (tst_lyrics_browser_chain), all against fixed pages on 127.0.0.1. They show no window but start msedgewebview2.exe processes
# (with a QTemporaryDir as the user-data folder), and skip when the WebView2 runtime is not installed. ctest label: webview
# (`ctest -L webview` selects them, `-LE webview` leaves them out).
foreach(test tst_hidden_web_view tst_lyrics_card_parser tst_lyrics_browser_chain)
    xgs_add_test(${test} ${test}.cpp fixture_server.h fixture_server.cpp webview_test_support.h)
    target_link_libraries(${test} PRIVATE xgswebview xgstestsupport Qt6::Gui)
    target_include_directories(${test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    # A hung page costs five seconds per check and the chain waits for a few; the slowest exe takes well under a minute.
    set_tests_properties(${test} PROPERTIES LABELS webview TIMEOUT 300)
endforeach()
