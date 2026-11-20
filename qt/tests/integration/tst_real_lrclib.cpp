#include <QtTest>

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <optional>
#include <stop_token>
#include <thread>

#include "integration_support.h"
#include "json/json.h"
#include "lyrics/lrclib_lyrics_provider.h"
#include "lyrics/lyrics_matcher.h"
#include "model/models.h"
#include "net/http_client.h"
#include "net/http_fetch.h"
#include "net/qt_http_client.h"
#include "text/kt_text.h"
#include "tools/tool_manager.h"

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::integration::Deadline;
using xgs::net::HttpRequest;
using xgs::net::HttpResponse;
using xgs::net::HttpStatus;

namespace {

/// Counts the requests that reach the service (the tests print the total; it is the only thing printed about them).
class CountingClient final : public xgs::net::HttpClient {
public:
    explicit CountingClient(xgs::net::HttpClient& inner) : m_inner(inner) {}
    HttpResponse get(const HttpRequest& request, std::stop_token stop) override {
        ++m_requests;
        return m_inner.get(request, stop);
    }
    int requests() const { return m_requests; }

private:
    xgs::net::HttpClient& m_inner;
    std::atomic<int> m_requests{0};
};

/// Kotlin's describe(): what can be said about lyrics without showing them: the number of lines and of non-blank lines.
QString describe(const std::optional<QString>& lyrics) {
    if (!lyrics) return u"none"_s;
    const QStringList lines = lyrics->split(u'\n');
    qsizetype nonBlank = 0;
    for (const QString& line : lines) {
        if (!xgs::kt::isBlank(line)) ++nonBlank;
    }
    return u"%1 lines, %2 non-blank, %3 characters"_s.arg(lines.size()).arg(nonBlank).arg(lyrics->size());
}

qsizetype nonBlankLines(const QString& lyrics) {
    qsizetype count = 0;
    for (const QString& line : lyrics.split(u'\n')) {
        if (!xgs::kt::isBlank(line)) ++count;
    }
    return count;
}

/// Kotlin's `text(key)`: the content of a string value, nothing for a value of any other type.
std::optional<QString> textOf(const xgs::json::Object& record, QStringView key) {
    const xgs::json::Value* value = xgs::json::find(record, key);
    if (!value) return std::nullopt;
    const auto* primitive = std::get_if<xgs::json::Primitive>(&value->data);
    if (!primitive || !primitive->isString) return std::nullopt;
    return primitive->content;
}

/// Kotlin's `other(key)`: the primitive when it is not a string.
const xgs::json::Primitive* otherOf(const xgs::json::Object& record, QStringView key) {
    const xgs::json::Value* value = xgs::json::find(record, key);
    if (!value) return nullptr;
    const auto* primitive = std::get_if<xgs::json::Primitive>(&value->data);
    return primitive && !primitive->isString ? primitive : nullptr;
}

/// Equal under Kotlin's String.equals(other, ignoreCase = true), as the provider compares a flag.
bool equalsIgnoringCase(QStringView text, QStringView word) {
    if (text.size() != word.size()) return false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (!xgs::kt::equalsIgnoreCase(text.at(i), word.at(i))) return false;
    }
    return true;
}

/// kotlinx's booleanOrNull as the provider reads it: `true` or `false` in any case.
std::optional<bool> booleanOrNull(const xgs::json::Primitive& primitive) {
    if (equalsIgnoringCase(primitive.content, u"true")) return true;
    if (equalsIgnoringCase(primitive.content, u"false")) return false;
    return std::nullopt;
}

/// kotlinx's doubleOrNull as the provider reads it: Kotlin's toDoubleOrNull of the content.
std::optional<double> doubleOrNull(const xgs::json::Primitive& primitive) {
    return xgs::kt::toDoubleOrNull(primitive.content);
}

/// The record of an LRCLIB answer as the matcher sees it (a field of the wrong type counts as missing). Unlike the
/// provider it does not read a name over 1,000 UTF-16 units as missing; real records are far shorter, and the count it
/// feeds ("accepted as the song") is printed only.
xgs::lyrics::LyricsCandidate candidateOf(const xgs::json::Object& record) {
    xgs::lyrics::LyricsCandidate candidate;
    candidate.trackName = textOf(record, u"trackName");
    candidate.artistName = textOf(record, u"artistName");
    candidate.albumName = textOf(record, u"albumName");
    if (const auto* duration = otherOf(record, u"duration")) candidate.duration = doubleOrNull(*duration);
    if (const auto* instrumental = otherOf(record, u"instrumental")) candidate.instrumental = booleanOrNull(*instrumental);
    candidate.plainLyrics = textOf(record, u"plainLyrics");
    return candidate;
}

/// Describes a record by its keys and the kinds of its values, and by how many lines its plain lyrics have; no text.
QString shapeOf(const xgs::json::Object& record) {
    QStringList kinds;
    for (const auto& [key, value] : record) {
        QString kind;
        if (const auto* primitive = std::get_if<xgs::json::Primitive>(&value.data)) {
            if (primitive->isString) kind = u"string"_s;
            else if (booleanOrNull(*primitive)) kind = u"boolean"_s;
            else if (doubleOrNull(*primitive)) kind = u"number"_s;
            else kind = u"null"_s;
        } else if (std::holds_alternative<xgs::json::Null>(value.data)) {
            kind = u"null"_s;
        } else if (std::holds_alternative<xgs::json::Object>(value.data)) {
            kind = u"JsonObject"_s;
        } else {
            kind = u"JsonArray"_s;
        }
        kinds << key + u':' + kind;
    }
    return u"{%1}; plainLyrics %2"_s.arg(kinds.join(u", "_s), describe(candidateOf(record).plainLyrics));
}

/// The object a response body is, when it is one (Kotlin: runCatching { parseToJsonElement } as? JsonObject).
std::optional<xgs::json::Object> objectOf(const HttpResponse& response) {
    const std::optional<xgs::json::Value> root = xgs::json::parse(QString::fromUtf8(response.body));
    if (!root) return std::nullopt;
    const auto* object = std::get_if<xgs::json::Object>(&root->data);
    if (!object) return std::nullopt;
    return *object;
}

}  // namespace

/// Port of RealLrclibIntegrationTest.kt. Talks to the real LRCLIB (https://lrclib.net); it runs only when XGS_INTEGRATION is 1
/// and is skipped (not failed) when the service does not answer. Lyrics are copyrighted works: these tests assert and print
/// STRUCTURE only (status codes, key names, counts of lines and characters), never a word of what the service returns, and no
/// assertion can put the lyrics into a failure message.
class RealLrclibIntegrationTest : public QObject {
    Q_OBJECT

private:
    xgs::net::QtHttpClient m_real;
    CountingClient m_client{m_real};
    xgs::lyrics::LrclibLyricsProvider m_provider{m_client};

    static constexpr QStringView Base = xgs::lyrics::DefaultBaseUrl;

    /// Kotlin's get(url): the real client with the provider's User-Agent, 20 seconds at most.
    HttpResponse get(const QString& url) {
        HttpRequest request;
        request.url = url;
        request.headers = {{"User-Agent", xgs::net::AppUserAgent.toString().toUtf8()}, {"Accept", "application/json"}};
        request.totalTimeout = 20s;
        return m_client.get(request, std::stop_token{});
    }

    /// True when the service answers with anything but a server error; it is asked up to three times, a public service has
    /// its moments.
    bool serviceAnswers() {
        for (int attempt = 0; attempt < 3; ++attempt) {
            const HttpResponse response = get(Base.toString() + u"/search?track_name=zz-no-such-song-xgetsongs-0000"_s);
            if (response.status == HttpStatus::Response && response.code < 500) return true;
            if (attempt < 2) std::this_thread::sleep_for(2s);
        }
        return false;
    }

    /// One lookup under Kotlin's withTimeout(120_000); the lyrics, and whether the time ran out.
    std::optional<QString> find(const xgs::LyricsQuery& query, bool* timedOut) {
        Deadline deadline(120s);
        const std::optional<QString> lyrics = m_provider.find(query, deadline.token());
        *timedOut = deadline.expired();
        return lyrics;
    }

private slots:
    void init() {
        if (!xgs::integration::enabled()) QSKIP(xgs::integration::disabledText());
        // The probe is Kotlin's @BeforeTest; the slots that make their own request do not need it, and the download one
        // does not touch the service.
        const QString name = QString::fromLatin1(QTest::currentTestFunction());
        if (name == u"theRealServiceIsReachedOverSchannelTls" || name == u"downloadsTheRealYtDlpExecutable") return;
        if (!serviceAnswers()) QSKIP("lrclib.net must answer");
    }

    void cleanupTestCase() {
        // Structure only: how many requests this class sent to the service (the probes included).
        if (xgs::integration::enabled()) qInfo().noquote() << u"requests sent to the service: %1"_s.arg(m_client.requests());
    }

    void findsTheLyricsOfAKnownSong() {
        bool timedOut = false;
        const std::optional<QString> lyrics =
            find({u"IU"_s, u"Love poem"_s, std::nullopt, 258}, &timedOut);

        qInfo().noquote() << u"lyrics of the known song: "_s + describe(lyrics);
        QVERIFY2(!timedOut, "the lookup took longer than 120 seconds");
        QVERIFY2(lyrics.has_value(), "the lyrics of a well-known song must be found");
        QVERIFY2(nonBlankLines(*lyrics) >= 10, "at least 10 non-blank lines expected");
    }

    void aTitleWithAParentheticalCreditStillFindsTheSong() {
        bool timedOut = false;
        const std::optional<QString> lyrics =
            find({u"IU"_s, u"Love poem (feat. nobody)"_s, std::nullopt, 258}, &timedOut);

        qInfo().noquote() << u"lyrics for the title with a credit: "_s + describe(lyrics);
        QVERIFY2(!timedOut, "the lookup took longer than 120 seconds");
        QVERIFY2(lyrics.has_value(), "the credit in brackets must not keep the song from being found");
        QVERIFY2(nonBlankLines(*lyrics) >= 10, "at least 10 non-blank lines expected");
    }

    void aMadeUpSongIsNotFound() {
        bool timedOut = false;
        const std::optional<QString> lyrics =
            find({u"zz-nobody"_s, u"zz-no-such-song-xgetsongs-0000"_s, std::nullopt, std::nullopt}, &timedOut);

        qInfo().noquote() << u"lyrics of the made-up song: "_s + describe(lyrics);
        QVERIFY2(!timedOut, "the lookup took longer than 120 seconds");
        QVERIFY2(!lyrics.has_value(), "a made-up song must not be found");
    }

    void theAnswersOfTheServiceHaveTheShapeTheProviderReads() {
        const QString base = Base.toString();
        const xgs::LyricsQuery query{u"IU"_s, u"Love poem"_s, std::nullopt, 258};

        // /search with the track and the artist: an array of records.
        const HttpResponse search = get(base + u"/search?track_name=Love%20poem&artist_name=IU"_s);
        qInfo().noquote() << u"search: HTTP %1"_s.arg(search.code);
        QVERIFY2(search.status == HttpStatus::Response, "the search must get an HTTP answer");
        QCOMPARE(search.code, 200);
        const std::optional<xgs::json::Value> root = xgs::json::parse(QString::fromUtf8(search.body));
        QVERIFY2(root.has_value(), "the search answer must be JSON");
        const auto* records = std::get_if<xgs::json::Array>(&root->data);
        QVERIFY2(records != nullptr, "the search answer must be an array");
        QList<xgs::json::Object> objects;
        for (const xgs::json::Value& record : *records) {
            if (const auto* object = std::get_if<xgs::json::Object>(&record.data)) objects.append(*object);
        }
        QList<xgs::lyrics::LyricsCandidate> candidates;
        for (const xgs::json::Object& object : objects) candidates.append(candidateOf(object));
        const auto count = [&](auto predicate) {
            qsizetype n = 0;
            for (const auto& candidate : candidates) n += predicate(candidate) ? 1 : 0;
            return n;
        };
        qInfo().noquote()
            << u"search: %1 records, %2 with plain lyrics, %3 instrumental, %4 accepted as the song"_s
                   .arg(records->size())
                   .arg(count([](const auto& c) { return c.plainLyrics && !xgs::kt::isBlank(*c.plainLyrics); }))
                   .arg(count([](const auto& c) { return c.instrumental == true; }))
                   .arg(count([&](const auto& c) { return xgs::lyrics::isMatch(query, c); }));
        if (!objects.isEmpty()) qInfo().noquote() << u"search: first record is "_s + shapeOf(objects.first());
        QVERIFY2(!records->empty(), "the search must find records for a well-known song");
        QCOMPARE(static_cast<qsizetype>(records->size()), objects.size());   // every element of the array is an object
        for (const xgs::json::Object& record : objects) {
            for (const QStringView key : {u"trackName", u"artistName", u"duration", u"instrumental", u"plainLyrics"}) {
                QVERIFY2(xgs::json::find(record, key) != nullptr,
                         qPrintable(u"every record has "_s + key.toString()));
            }
        }

        // /get with the signature we can give (no album): what does the service say?
        const HttpResponse withoutAlbum = get(base + u"/get?artist_name=IU&track_name=Love%20poem&duration=258"_s);
        qInfo().noquote() << u"get without an album: HTTP %1"_s.arg(withoutAlbum.code);
        if (const auto object = objectOf(withoutAlbum)) qInfo().noquote() << u"get without an album: "_s + shapeOf(*object);

        // /get with an album too (the album of the song, if the service knows it under this name).
        const HttpResponse withAlbum =
            get(base + u"/get?artist_name=IU&track_name=Love%20poem&album_name=Love%20poem&duration=258"_s);
        qInfo().noquote() << u"get with an album: HTTP %1"_s.arg(withAlbum.code);
        if (const auto object = objectOf(withAlbum)) qInfo().noquote() << u"get with an album: "_s + shapeOf(*object);

        // A search that finds nothing: an empty array or an error status, either is "no result".
        const HttpResponse nothing = get(base + u"/search?track_name=zz-no-such-song-xgetsongs-0000&artist_name=zz-nobody"_s);
        qInfo().noquote() << u"search for nothing: HTTP %1, body %2 characters"_s.arg(nothing.code).arg(
            QString::fromUtf8(nothing.body).size());
        // The live service has been seen answering such a request with a 503: any HTTP answer is acceptable here.
        QVERIFY2(nothing.status == HttpStatus::Response, "the search for nothing must get an HTTP answer");
        QVERIFY(nothing.code >= 200 && nothing.code <= 599);
    }

    // Added: the client of the lookups above reaches the real service over TLS (Schannel).
    void theRealServiceIsReachedOverSchannelTls() {
        const HttpResponse response = get(Base.toString() + u"/search?track_name=zz-no-such-song-xgetsongs-0000"_s);
        qInfo().noquote() << u"TLS probe: status %1, HTTP %2"_s.arg(int(response.status)).arg(response.code);
        QVERIFY2(response.status == HttpStatus::Response, qPrintable(u"no HTTP answer: "_s + response.error));
        QVERIFY(response.code >= 200 && response.code <= 599);
    }

    // Added: the real download of yt-dlp.exe. Never part of the default integration run: it needs its own variable.
    void downloadsTheRealYtDlpExecutable() {
        if (qEnvironmentVariable("XGS_INTEGRATION_DOWNLOAD") != u"1")
            QSKIP("set XGS_INTEGRATION_DOWNLOAD=1 to download the real yt-dlp.exe (about 18 MB, nothing is written to disk)");
        // On m_real, not m_client: the download goes to GitHub, not lrclib.net, so it is deliberately left out of the count.
        const xgs::tools::Fetch fetch = xgs::net::httpFetch(m_real);
        const xgs::Outcome<QByteArray> outcome = fetch(xgs::tools::YtDlpUrl.toString(), std::stop_token{});
        QVERIFY2(outcome.isOk(), qPrintable(u"the download failed: "_s + outcome.error));
        QVERIFY2(outcome.value->size() >= 1000000, "yt-dlp.exe is more than a megabyte");
        QVERIFY2(outcome.value->startsWith("MZ"), "yt-dlp.exe is a Windows executable (starts with MZ)");
    }
};

QTEST_GUILESS_MAIN(RealLrclibIntegrationTest)
#include "tst_real_lrclib.moc"
