#pragma once

#include <QString>
#include <QStringView>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>

#include "lyrics/lyrics_provider.h"
#include "net/http_client.h"

// Port of engine/.../lyrics/LrclibLyricsProvider.kt.
namespace xgs::lyrics {

inline constexpr QStringView DefaultBaseUrl = u"https://lrclib.net/api";

struct LrclibOptions {
    QString baseUrl = DefaultBaseUrl.toString();
    /// The service asks for a descriptive User-Agent.
    QString userAgent = net::AppUserAgent.toString();
    /// How long one request may take, from sending it until its answer is in (the whole body included).
    std::chrono::milliseconds requestTimeout{15000};
    int maxRequests = 8;
    int maxConcurrent = 2;                              // below 1 counts as 1
    /// How long this instance makes no request after a call ended in trouble; it belongs to the instance, not to the service.
    std::chrono::milliseconds coolDown{300000};
    /// The clock of the cool-down; a test gives one that it moves itself.
    std::function<std::chrono::nanoseconds()> clock;    // empty: std::chrono::steady_clock
};

/// An answer longer than this (bytes) is not read: the lyrics of a song are a few kilobytes.
inline constexpr qint64 LrclibMaxBodyBytes = 2 * 1024 * 1024;
/// A name of a record (trackName, artistName, albumName) longer than this (UTF-16 units) is read as missing: the matcher's
/// cost grows with the length of what it is given (lyrics_matcher.h). Not in Kotlin.
inline constexpr qsizetype LrclibMaxNameUnits = 1000;

/// Looks lyrics up in LRCLIB (https://lrclib.net), an open lyrics database with a public JSON API meant for programs: no
/// key, and a request for a descriptive `User-Agent` (LrclibOptions::userAgent). Only plain, unsynchronised lyrics are used.
///
/// A call makes at most maxRequests requests and stops at the first accepted record (lyrics::isMatch) that has usable lyrics
/// (at least lyrics::tidy says so: a record with a line or two of "lyrics" is no answer and does not hide another record that
/// has them):
///  1. `GET {baseUrl}/get` with the artist and title as given, and the album and length when there are some: the service's
///     own exact match;
///  2. `GET {baseUrl}/search` for every artist variant and, within it, every title variant (lyrics_matcher.h), best one
///     first; of the records of an answer the best one is taken (lyrics::pick: the best title match, then the same album,
///     then the closest length).
///
/// Whatever is not HTTP 200 with a body that can be read counts as "no result" for that request and the next one is still
/// made, and so does a body of more than LrclibMaxBodyBytes bytes (it is never read to the end). The exceptions are 429 (too
/// many requests), 502, 503 and 504 (the service or what is in front of it is down or overloaded) and a failure to connect
/// or a timeout: those end the call, and the provider then answers nothing at once, without a request, for coolDown, so a
/// service that is down or throttling is not asked again and again, by this call or the calls of the other songs of a
/// playlist. The answer is lyrics::tidy of the lyrics of the accepted record, nothing when there is none.
///
/// The requests block the calling thread and end when its stop token is triggered; a stopped call answers nothing and does
/// not start the cool-down. At most maxConcurrent requests of this provider are in flight at once, however many songs are
/// being looked up (from however many threads); a call waiting for its turn also ends when it is stopped. find never throws.
///
/// Differences from Kotlin (all from the Q2c brief): a stopped call returns nothing where Kotlin throws a
/// CancellationException; the client is always given (Kotlin builds one when it is null), so there is no connect timeout of
/// the provider's own; names longer than LrclibMaxNameUnits read as missing. Also: an unexpected C++ exception (from the
/// client, the JSON reader or the matcher) ends the whole call with nothing, where Kotlin turns an exception that is not an
/// IOException into a miss of that one request and goes on with the next.
class LrclibLyricsProvider final : public LyricsProvider {
public:
    /// The client is referenced and must outlive the provider.
    LrclibLyricsProvider(net::HttpClient& client, LrclibOptions options = {});
    LrclibLyricsProvider(const LrclibLyricsProvider&) = delete;
    LrclibLyricsProvider& operator=(const LrclibLyricsProvider&) = delete;

    std::optional<QString> find(const LyricsQuery& query, std::stop_token stop) override;

private:
    enum class Outcome;
    struct Answer;

    std::optional<QString> lookUp(const LyricsQuery& query, const std::stop_token& stop);
    Answer send(const QString& url, const std::stop_token& stop);
    Answer exchange(const QString& url, const std::stop_token& stop);
    bool isCoolingDown();
    void startCoolDown();
    std::chrono::nanoseconds now() const;
    QString getUrl(const LyricsQuery& query) const;
    QString searchUrl(const QString& artist, const QString& title) const;

    net::HttpClient& m_client;
    LrclibOptions m_options;
    QString m_baseUrl;              // without trailing slashes

    // The slots (Kotlin's Semaphore(maxConcurrent)) and the cool-down share one mutex.
    std::mutex m_mutex;
    std::condition_variable_any m_slotFreed;
    int m_freeSlots;
    /// When the last trouble happened; nothing until there was some. Written by whichever call meets the trouble.
    std::optional<std::chrono::nanoseconds> m_troubleMark;
};

}  // namespace xgs::lyrics
