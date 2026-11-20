#pragma once

#include <QtGlobal>

#include <chrono>

#include "net/http_client.h"
#include "tools/tool_manager.h"

// The real tools::Fetch (the Kotlin httpGet of DefaultToolManager) on top of an HttpClient.
namespace xgs::net {

inline constexpr qint64 FetchMaxBytes = 200LL * 1024 * 1024;
inline constexpr std::chrono::milliseconds FetchIdleTimeout{30000};

/// Kotlin httpGet: GET url, redirects followed. Ok with the body when the status is 200; Failed "HTTP <code>" otherwise;
/// Failed with the client's text when it could not be reached or failed; Cancelled when stop was requested. A body longer
/// than maxBytes is Failed. The client is referenced, not owned: it must outlive the returned function.
tools::Fetch httpFetch(HttpClient& client, std::chrono::milliseconds idleTimeout = FetchIdleTimeout,
                       qint64 maxBytes = FetchMaxBytes);

}  // namespace xgs::net
