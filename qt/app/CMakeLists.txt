# xgscrash: the crash handlers and the minidump writer (no widgets), so that the crash probe of the tests links them without the
# rest of the app. dbghelp is linked at load time: nothing is loaded when a crash happens.
add_library(xgscrash STATIC crash_dump.h crash_dump.cpp)
target_include_directories(xgscrash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xgscrash PUBLIC xgscore dbghelp)
xgs_target_defaults(xgscrash)

# xgsapp: the widgets of the app (the window, the playlists dialogs, the about dialog, the single instance) and the assembly of
# the real engine (AppServices). A library so that the widget tests (qt/tests/app) link the same code as the exe.
add_library(xgsapp STATIC
    app_services.h app_services.cpp
    main_window.h main_window.cpp
    playlists_dialog.h playlists_dialog.cpp
    playlist_entry_dialog.h playlist_entry_dialog.cpp
    about_dialog.h about_dialog.cpp
    single_instance.h single_instance.cpp
    diagnostics.h diagnostics.cpp)
target_include_directories(xgsapp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xgsapp PUBLIC xgsui xgswebview xgscrash Qt6::Widgets user32)
target_compile_definitions(xgsapp PRIVATE XGS_APP_VERSION="${PROJECT_VERSION}")
xgs_target_defaults(xgsapp)

# xGetSongs.exe: a GUI program (no console). The static Qt links its default plugins (qwindows, the modern Windows style, the
# Schannel TLS backend) by itself. app.qrc carries the style sheet.
set(CMAKE_AUTORCC ON)
add_executable(xGetSongs WIN32 main.cpp app.qrc app.qss)
target_link_libraries(xGetSongs PRIVATE xgsapp)
target_compile_definitions(xGetSongs PRIVATE XGS_APP_VERSION="${PROJECT_VERSION}")
xgs_target_defaults(xGetSongs)
