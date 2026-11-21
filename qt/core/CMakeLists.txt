add_library(xgscore STATIC
    version.h version.cpp
    outcome.h
    paths/paths.h paths/paths.cpp
    fs/file_ops.h fs/file_ops.cpp
    process/process_runner.h
    process/line_splitter.h process/line_splitter.cpp
    process/command_line.h process/command_line.cpp
    process/system_process_runner.h process/system_process_runner.cpp
    tools/tool_locator.h tools/tool_locator.cpp
    tools/tool_manager.h tools/tool_manager.cpp
    text/kt_text.h text/kt_text.cpp text/kt_tables.inc
    input/input_classifier.h input/input_classifier.cpp
    filename/filename_formatter.h filename/filename_formatter.cpp
    title/title_parser.h title/title_parser.cpp
    lyrics/lyrics_extractor.h lyrics/lyrics_extractor.cpp
    lyrics/lyrics_matcher.h lyrics/lyrics_matcher.cpp
    lyrics/lyrics_provider.h lyrics/lyrics_provider.cpp
    lyrics/lrclib_lyrics_provider.h lyrics/lrclib_lyrics_provider.cpp
    log/log_redaction.h log/log_redaction.cpp
    model/models.h
    json/json.h json/json.cpp
    ytdlp/error_classifier.h ytdlp/error_classifier.cpp
    ytdlp/progress_parser.h ytdlp/progress_parser.cpp
    ytdlp/video_info_file.h ytdlp/video_info_file.cpp
    ytdlp/ytdlp_commands.h ytdlp/ytdlp_commands.cpp
    ytdlp/ytdlp_resolver.h ytdlp/ytdlp_resolver.cpp
    ytdlp/ffmpeg_commands.h ytdlp/ffmpeg_commands.cpp
    tags/track_tags.h tags/ffmetadata.h tags/ffmetadata.cpp
    tags/m4a_tagger.h tags/m4a_tagger.cpp
    output/output_sink.h output/local_folder_sink.h output/local_folder_sink.cpp
    job/download_concurrency.h job/download_concurrency.cpp
    job/item_downloader.h job/item_downloader.cpp
    job/download_service.h job/job_events.h job/job_events.cpp
    job/default_download_service.h job/default_download_service.cpp
    net/http_client.h net/qt_http_client.h net/qt_http_client.cpp
    net/http_fetch.h net/http_fetch.cpp)
target_include_directories(xgscore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xgscore PUBLIC Qt6::Core Qt6::Network)
xgs_target_defaults(xgscore)
