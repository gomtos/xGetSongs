# Real-tool integration tests (ports of RealFfmpegTaggingIntegrationTest.kt, RealYtDlpIntegrationTest.kt and
# RealLrclibIntegrationTest.kt). They run the real ffmpeg, ffprobe and yt-dlp, and ask the real LRCLIB, only when the
# environment variable XGS_INTEGRATION is 1 and skip otherwise, so a plain `ctest` touches no tool and no network. ctest label: integration (`ctest -L integration` selects them, `-LE` leaves them out).
foreach(test tst_real_ffmpeg_tagging tst_real_ytdlp)
    xgs_add_test(${test} ${test}.cpp ffprobe.h ffprobe.cpp integration_support.h)
    target_include_directories(${test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${test} PROPERTIES LABELS integration TIMEOUT 1200)
endforeach()

# Port of RealLrclibIntegrationTest.kt: talks to the real https://lrclib.net (XGS_INTEGRATION=1 only; it prints structure, never
# lyrics). It needs no ffprobe helper. The added download slot also needs XGS_INTEGRATION_DOWNLOAD=1.
xgs_add_test(tst_real_lrclib tst_real_lrclib.cpp integration_support.h)
target_include_directories(tst_real_lrclib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(tst_real_lrclib PROPERTIES LABELS integration TIMEOUT 1200)
