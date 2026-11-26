# xgsui: the screen logic without widgets (the port of shell/src/state and the settings part of shell/src/desktop.ts) and the
# bridge from the blocking engine of xgscore to the GUI thread. Qt Core only, so its tests need no window system.
add_library(xgsui STATIC
    ui_types.h ui_types.cpp
    labels.h labels.cpp
    settings_store.h settings_store.cpp
    app_api.h
    app_state_holder.h app_state_holder.cpp
    engine_api.h engine_api.cpp)
target_include_directories(xgsui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xgsui PUBLIC xgscore Qt6::Core)
xgs_target_defaults(xgsui)
