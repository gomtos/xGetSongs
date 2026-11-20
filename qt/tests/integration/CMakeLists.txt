# Real-tool integration tests (ports of RealFfmpegTaggingIntegrationTest.kt and RealYtDlpIntegrationTest.kt). They run the
# real ffmpeg, ffprobe and yt-dlp only when the environment variable XGS_INTEGRATION is 1 and skip otherwise, so a plain
# `ctest` touches no tool and no network. ctest label: integration (`ctest -L integration` selects them, `-LE` leaves them out).
foreach(test tst_real_ffmpeg_tagging tst_real_ytdlp)
    xgs_add_test(${test} ${test}.cpp ffprobe.h ffprobe.cpp integration_support.h)
    target_include_directories(${test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${test} PROPERTIES LABELS integration TIMEOUT 1200)
endforeach()
