# xgswebview: the hidden WebView2 window of the Google lyrics lookup and the agent that drives it from a BrowserChannel
# (namespace xgs::webview). The only place where WebView2 and Qt Gui are used (xgscore stays Qt Core, Qt Network and Win32).
# Every WebView2 call happens on the thread that made the window (the GUI thread, a single-threaded COM apartment).
add_library(xgswebview STATIC
    runtime.h runtime.cpp
    hidden_web_view.h hidden_web_view.cpp
    lyrics_browser.h lyrics_browser.cpp)
target_include_directories(xgswebview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(xgswebview PUBLIC xgscore Qt6::Core Qt6::Gui PRIVATE unofficial::webview2::webview2)
xgs_target_defaults(xgswebview)
