#include "lyrics/lrclib_lyrics_provider.h"

#include <QByteArray>
#include <QList>

#include <utility>

#include "json/json.h"
#include "lyrics/lyrics_extractor.h"
#include "lyrics/lyrics_matcher.h"
#include "net/url_encoding.h"
#include "text/kt_text.h"

using namespace Qt::StringLiterals;

namespace xgs::lyrics {

namespace {

constexpr int HttpOk = 200;
constexpr int HttpTooManyRequests = 429;
constexpr int HttpBadGateway = 502;
constexpr int HttpServiceUnavailable = 503;
constexpr int HttpGatewayTimeout = 504;

// The primitive at key of record (kotlinx's `record[key] as? JsonPrimitive`); nullptr for anything else. JsonNull is a
// JsonPrimitive in kotlinx (not a string, content "null"); every reader below gives null for it, as it does for json::Null.
const json::Primitive* primitive(const json::Object& record, QStringView key) {
    const json::Value* value = json::find(record, key);
    return value ? std::get_if<json::Primitive>(&value->data) : nullptr;
}

std::optional<QString> text(const json::Object& record, QStringView key) {
    const json::Primitive* value = primitive(record, key);
    if (!value || !value->isString) return std::nullopt;
    return value->content;
}

// A name of the record; longer than LrclibMaxNameUnits reads as missing (not in Kotlin, see the header).
std::optional<QString> name(const json::Object& record, QStringView key) {
    std::optional<QString> value = text(record, key);
    if (value && value->size() > LrclibMaxNameUnits) return std::nullopt;
    return value;
}

// An integer or a decimal (kotlinx's doubleOrNull of a primitive that is not a string: kt::toDoubleOrNull of its content).
std::optional<double> number(const json::Object& record, QStringView key) {
    const json::Primitive* value = primitive(record, key);
    if (!value || value->isString) return std::nullopt;
    return kt::toDoubleOrNull(value->content);
}

// Equal to word under Kotlin's String.equals(other, ignoreCase = true): the same length and each unit equal under
// Char.equals(ignoreCase = true).
bool equalsIgnoringCase(QStringView text, QStringView word) {
    if (text.size() != word.size()) return false;
    for (qsizetype i = 0; i < text.size(); ++i)
        if (!kt::equalsIgnoreCase(text.at(i), word.at(i))) return false;
    return true;
}

// kotlinx's booleanOrNull of a primitive that is not a string. Observed on the JVM (Q2c Task 4 differential): the content
// `true` or `false` in any case is read (`TRUE`, `tRuE`, even `falſe` with U+017F, which uppercases to `S`), as
// String.equals(ignoreCase = true) compares; anything else (`1`, `yes`, `null`, `ｔrue`) is null.
std::optional<bool> flag(const json::Object& record, QStringView key) {
    const json::Primitive* value = primitive(record, key);
    if (!value || value->isString) return std::nullopt;
    if (equalsIgnoringCase(value->content, u"true")) return true;
    if (equalsIgnoringCase(value->content, u"false")) return false;
    return std::nullopt;
}

// The record in element when it is a JSON object; a field of an unexpected type is read as missing.
std::optional<LyricsCandidate> candidateOf(const json::Value* element) {
    const json::Object* record = element ? std::get_if<json::Object>(&element->data) : nullptr;
    if (!record) return std::nullopt;
    return LyricsCandidate{
        name(*record, u"trackName"),
        name(*record, u"artistName"),
        name(*record, u"albumName"),
        number(*record, u"duration"),
        flag(*record, u"instrumental"),
        text(*record, u"plainLyrics"),
    };
}

// record with its lyrics as lyrics::tidy makes them; nothing when they are not usable as lyrics.
std::optional<LyricsCandidate> withTidyLyrics(LyricsCandidate record) {
    std::optional<QString> lyrics = tidy(record.plainLyrics);
    if (!lyrics) return std::nullopt;
    record.plainLyrics = std::move(lyrics);
    return record;
}

// The records in element when it is a JSON array; its elements that are not objects are skipped, and so are the ones without
// usable lyrics.
QList<LyricsCandidate> candidatesOf(const json::Value* element) {
    QList<LyricsCandidate> records;
    const json::Array* items = element ? std::get_if<json::Array>(&element->data) : nullptr;
    if (!items) return records;
    for (const json::Value& item : *items) {
        if (std::optional<LyricsCandidate> record = candidateOf(&item)) {
            if (std::optional<LyricsCandidate> tidied = withTidyLyrics(std::move(*record))) records.append(std::move(*tidied));
        }
    }
    return records;
}

}  // namespace

/// What one request came to.
enum class LrclibLyricsProvider::Outcome {
    Body,        // HTTP 200: the body, decoded as UTF-8
    Miss,        // no result from this request; the next one is still made
    Stop,        // the call ends here: the service is throttling, down or unreachable, or the cap of requests is reached
    Cancelled,   // stop was requested: the call ends with nothing, and no cool-down
};

struct LrclibLyricsProvider::Answer {
    Outcome outcome;
    QString text;   // Body only
};

LrclibLyricsProvider::LrclibLyricsProvider(net::HttpClient& client, LrclibOptions options)
    : m_client(client),
      m_options(std::move(options)),
      m_baseUrl(kt::trimEndChars(m_options.baseUrl, u"/")),
      m_freeSlots(m_options.maxConcurrent < 1 ? 1 : m_options.maxConcurrent) {}

std::optional<QString> LrclibLyricsProvider::find(const LyricsQuery& query, std::stop_token stop) {
    try {
        return lookUp(query, stop);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<QString> LrclibLyricsProvider::lookUp(const LyricsQuery& query, const std::stop_token& stop) {
    if (kt::isBlank(query.artist) || kt::isBlank(query.title) || isCoolingDown()) return std::nullopt;
    int used = 0;
    const auto fetch = [&](const QString& url) -> Answer {
        if (stop.stop_requested()) return {Outcome::Cancelled, {}};
        if (used >= m_options.maxRequests) return {Outcome::Stop, {}};
        ++used;
        return send(url, stop);
    };

    const Answer get = fetch(getUrl(query));
    switch (get.outcome) {
    case Outcome::Stop:
    case Outcome::Cancelled: return std::nullopt;
    case Outcome::Miss: break;
    case Outcome::Body: {
        const std::optional<json::Value> parsed = json::parse(get.text);
        std::optional<LyricsCandidate> record = candidateOf(parsed ? &*parsed : nullptr);
        if (record) record = withTidyLyrics(std::move(*record));
        if (record && isMatch(query, *record)) return record->plainLyrics;
        break;
    }
    }
    for (const QString& artist : artistVariants(query.artist)) {
        for (const QString& title : titleVariants(query.title)) {
            const Answer search = fetch(searchUrl(artist, title));
            switch (search.outcome) {
            case Outcome::Stop:
            case Outcome::Cancelled: return std::nullopt;
            case Outcome::Miss: break;
            case Outcome::Body: {
                const std::optional<json::Value> parsed = json::parse(search.text);
                const QList<LyricsCandidate> records = candidatesOf(parsed ? &*parsed : nullptr);
                const qsizetype chosen = pick(query, records);
                if (chosen >= 0) return records.at(chosen).plainLyrics;
                break;
            }
            }
        }
    }
    return std::nullopt;
}

std::chrono::nanoseconds LrclibLyricsProvider::now() const {
    if (m_options.clock) return m_options.clock();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

// True while the last trouble is less than coolDown ago.
bool LrclibLyricsProvider::isCoolingDown() {
    const std::chrono::nanoseconds time = now();
    std::lock_guard lock(m_mutex);
    return m_troubleMark && time - *m_troubleMark < m_options.coolDown;
}

// The service did not answer as it should: this provider leaves it alone for coolDown.
void LrclibLyricsProvider::startCoolDown() {
    const std::chrono::nanoseconds time = now();
    std::lock_guard lock(m_mutex);
    m_troubleMark = time;
}

// One request, one slot for as long as it lasts. Waiting for the slot ends when stop is requested.
LrclibLyricsProvider::Answer LrclibLyricsProvider::send(const QString& url, const std::stop_token& stop) {
    {
        std::unique_lock lock(m_mutex);
        if (!m_slotFreed.wait(lock, stop, [this] { return m_freeSlots > 0; })) return {Outcome::Cancelled, {}};
        --m_freeSlots;
    }
    struct SlotGuard {
        LrclibLyricsProvider& owner;
        ~SlotGuard() {
            {
                std::lock_guard lock(owner.m_mutex);
                ++owner.m_freeSlots;
            }
            // every waiter: one woken by notify_one might be returning for its stop request and leave the slot unused
            owner.m_slotFreed.notify_all();
        }
    } guard{*this};

    if (stop.stop_requested()) return {Outcome::Cancelled, {}};
    // Another call may have run into trouble while this one waited for its slot.
    if (isCoolingDown()) return {Outcome::Stop, {}};
    // Kotlin's withTimeoutOrNull(requestTimeout) gives up at once for a time that is not positive.
    Answer answer = m_options.requestTimeout.count() > 0 ? exchange(url, stop) : Answer{Outcome::Stop, {}};
    if (answer.outcome == Outcome::Stop) startCoolDown();
    return answer;
}

// The blocking part: sends a GET for url and says what came of it.
LrclibLyricsProvider::Answer LrclibLyricsProvider::exchange(const QString& url, const std::stop_token& stop) {
    net::HttpRequest request;
    request.url = url;
    request.headers = {{"User-Agent", m_options.userAgent.toUtf8()}, {"Accept", "application/json"}};
    // covers the wait for the answer and a body that never ends
    request.totalTimeout = m_options.requestTimeout;
    request.maxBodyBytes = LrclibMaxBodyBytes;
    const net::HttpResponse response = m_client.get(request, stop);
    switch (response.status) {
    case net::HttpStatus::Cancelled: return {Outcome::Cancelled, {}};
    case net::HttpStatus::Unreachable: return {Outcome::Stop, {}};   // could not connect, or the time limit ran out
    case net::HttpStatus::Failed: return {Outcome::Miss, {}};
    case net::HttpStatus::Response: break;
    }
    switch (response.code) {
    case HttpOk:
        if (response.bodyTooLarge) return {Outcome::Miss, {}};
        return {Outcome::Body, kt::decodeUtf8(response.body)};
    case HttpTooManyRequests:
    case HttpBadGateway:
    case HttpServiceUnavailable:
    case HttpGatewayTimeout: return {Outcome::Stop, {}};
    default: return {Outcome::Miss, {}};
    }
}

QString LrclibLyricsProvider::getUrl(const LyricsQuery& query) const {
    QString url = m_baseUrl + u"/get?artist_name="_s + net::encodeQueryValue(query.artist) + u"&track_name="_s +
                  net::encodeQueryValue(query.title);
    if (query.album && !kt::isBlank(*query.album)) url += u"&album_name="_s + net::encodeQueryValue(*query.album);
    if (query.durationSeconds && *query.durationSeconds > 0) url += u"&duration="_s + QString::number(*query.durationSeconds);
    return url;
}

QString LrclibLyricsProvider::searchUrl(const QString& artist, const QString& title) const {
    return m_baseUrl + u"/search?track_name="_s + net::encodeQueryValue(title) + u"&artist_name="_s +
           net::encodeQueryValue(artist);
}

}  // namespace xgs::lyrics
