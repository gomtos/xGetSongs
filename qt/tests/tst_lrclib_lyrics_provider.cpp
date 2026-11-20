#include <QtTest>

#include <QMap>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include "lyrics/lrclib_lyrics_provider.h"
#include "net/qt_http_client.h"
#include "support/fake_http_client.h"
#include "support/fake_http_server.h"

// Port of engine/src/test/kotlin/com/xgetsongs/engine/lyrics/LrclibLyricsProviderTest.kt: LrclibLyricsProvider against a
// fake LRCLIB on the loopback interface (FakeHttpServer) through the real QtHttpClient. Every record and every lyric is made
// up; nothing here touches the internet.
//
// How the Kotlin test is carried over:
//   - `blocking { }` is a plain call on the test thread; `async`/`launch` are std::jthreads whose stop token is the
//     coroutine's cancellation; Kotlin's TestTimeSource is a clock the test moves (m_clock).
//   - A handler that sleeps for seconds (Thread.sleep(5_000), Thread.sleep(10_000)) waits on a Gate for at most that long;
//     cleanup() opens the gate before it stops the server, so no test waits for a sleeping handler. Short sleeps
//     (120, 150, 300 ms) are real sleeps, as in Kotlin.
//   - State a handler shares with the test lives in a shared_ptr the handler holds, so a handler that is still running after
//     a slot returned early never touches a dead local.
//   - Where Kotlin throws a CancellationException, find returns nothing; the brief's differences are noted at their tests.

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using xgs::LyricsQuery;
using xgs::lyrics::LrclibLyricsProvider;
using xgs::lyrics::LrclibOptions;
using xgs::net::QtHttpClient;
using xgs::testing::CountingHttpClient;
using xgs::testing::FakeHttpServer;
using Clock = std::chrono::steady_clock;
using Exchange = FakeHttpServer::Exchange;
using Lyrics = std::optional<QString>;
using Paths = QList<QByteArray>;

namespace {

const QString testUserAgent = u"xGetSongs-test/1.0 (https://example.invalid)"_s;
const QByteArray notFound = R"({"code":404,"name":"TrackNotFound","message":"Failed to find specified track"})";

// Stands in for a server that sleeps: wait() returns after `limit` or as soon as the gate is opened.
class Gate {
public:
    void open() {
        {
            std::lock_guard lock(m_mutex);
            m_open = true;
        }
        m_changed.notify_all();
    }
    void wait(std::chrono::milliseconds limit) {
        std::unique_lock lock(m_mutex);
        m_changed.wait_for(lock, limit, [this] { return m_open; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_open = false;
};

// A one-shot signal between threads (Kotlin's CountDownLatch(1)).
class Latch {
public:
    void countDown() {
        {
            std::lock_guard lock(m_mutex);
            m_done = true;
        }
        m_changed.notify_all();
    }
    bool await(std::chrono::milliseconds limit) {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, limit, [this] { return m_done; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_done = false;
};

// Kotlin's AtomicInteger peak.accumulateAndGet(now, ::maxOf) next to an active counter.
struct InFlight {
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    void enter() {
        const int now = ++active;
        int seen = peak.load();
        while (now > seen && !peak.compare_exchange_weak(seen, now)) {
        }
    }
    void leave() { --active; }
};

// Lets handlers wait for one another: arriveAndWait(n, limit) returns once n have arrived, or after limit.
class Rendezvous {
public:
    void arriveAndWait(int n, std::chrono::milliseconds limit) {
        std::unique_lock lock(m_mutex);
        ++m_arrived;
        m_changed.notify_all();
        m_changed.wait_for(lock, limit, [this, n] { return m_arrived >= n; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    int m_arrived = 0;
};

// The JSON text of a string, as kotlinx.serialization writes it: quotes, backslashes and control characters escaped, the rest
// as it is.
QString jsonString(const QString& value) {
    QString out = u"\""_s;
    for (const QChar c : value) {
        switch (c.unicode()) {
        case u'"': out += u"\\\""_s; break;
        case u'\\': out += u"\\\\"_s; break;
        case u'\n': out += u"\\n"_s; break;
        case u'\r': out += u"\\r"_s; break;
        case u'\t': out += u"\\t"_s; break;
        case u'\b': out += u"\\b"_s; break;
        case u'\f': out += u"\\f"_s; break;
        default:
            if (c.unicode() < 0x20) out += u"\\u"_s + QString::number(c.unicode(), 16).rightJustified(4, u'0');
            else out += c;
        }
    }
    return out + u"\""_s;
}

// Three made-up lines that start with tag, so a test can tell which record the lyrics came from.
QString lyricsOf(const QString& tag) {
    return tag + u" one\n"_s + tag + u" two\n"_s + tag + u" three"_s;
}

// The arguments of Kotlin's record(...), in its order; duration is the JSON number as written (Kotlin's Number?).
struct Record {
    QString track = u"Love poem"_s;
    QString artist = u"IU"_s;
    std::optional<QString> album = u"Love poem"_s;
    std::optional<QString> duration = u"258"_s;
    bool instrumental = false;
    std::optional<QString> lyrics = lyricsOf(u"tag"_s);
};

QString orNull(const std::optional<QString>& value) {
    return value ? jsonString(*value) : u"null"_s;
}

// A record the way LRCLIB writes it: the fields the app reads, and some it does not.
QString record(const Record& r = {}) {
    return u"{\"id\":12345,\"name\":"_s + jsonString(r.track) + u",\"trackName\":"_s + jsonString(r.track) +
           u",\"artistName\":"_s + jsonString(r.artist) + u",\"albumName\":"_s + orNull(r.album) + u",\"duration\":"_s +
           r.duration.value_or(u"null"_s) + u",\"instrumental\":"_s + (r.instrumental ? u"true"_s : u"false"_s) +
           u",\"plainLyrics\":"_s + orNull(r.lyrics) + u",\"syncedLyrics\":\"[00:01.00] synthetic\"}"_s;
}

QString array(const QStringList& records) {
    return u"["_s + records.join(u',') + u"]"_s;
}

void reply(Exchange& exchange, int status, const QString& body) {
    exchange.reply(status, body.toUtf8());
}

// The query as key -> raw value (Kotlin's HttpExchange.rawParams()).
QMap<QByteArray, QByteArray> rawParams(const QByteArray& rawQuery) {
    QMap<QByteArray, QByteArray> params;
    for (const QByteArray& part : rawQuery.split('&')) {
        if (part.isEmpty()) continue;
        const qsizetype equals = part.indexOf('=');
        params.insert(equals < 0 ? part : part.left(equals), equals < 0 ? QByteArray() : part.mid(equals + 1));
    }
    return params;
}

// The query as key -> value decoded like URLDecoder.decode(value, UTF_8) (`+` is a space) (Kotlin's Seen.params()).
std::optional<QString> param(const FakeHttpServer::Seen& seen, const QByteArray& key) {
    const QMap<QByteArray, QByteArray> params = rawParams(seen.rawQuery);
    if (!params.contains(key)) return std::nullopt;
    QByteArray value = params.value(key);
    value.replace('+', ' ');
    return QString::fromUtf8(QByteArray::fromPercentEncoding(value));
}

struct QueryArgs {
    QString artist = u"IU"_s;
    QString title = u"Love poem"_s;
    std::optional<QString> album;
    std::optional<int> durationSeconds = 258;
};

LyricsQuery query(const QueryArgs& a = {}) {
    return LyricsQuery{a.artist, a.title, a.album, a.durationSeconds};
}

struct ProviderArgs {
    int maxRequests = 8;
    int maxConcurrent = 2;
    std::chrono::milliseconds requestTimeout = 5s;
    std::optional<QString> baseUrl;   // nullopt: the fake server's
};

constexpr qsizetype twoMebibytes = 2 * 1024 * 1024;

}  // namespace

namespace QTest {
template <>
char* toString(const std::optional<QString>& value) {
    return value ? QTest::toString(u"\""_s + *value + u"\""_s) : qstrdup("null");
}
}  // namespace QTest

// Kotlin's assertEquals(expected, actual, message): a QCOMPARE that also prints the message when it fails.
#define COMPARE_MSG(actual, expected, message)                                                          \
    do {                                                                                                \
        if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__)) {               \
            qWarning("%s", qPrintable(QString(message)));                                               \
            return;                                                                                     \
        }                                                                                               \
    } while (false)

class LrclibLyricsProviderTest : public QObject {
    Q_OBJECT

    std::unique_ptr<FakeHttpServer> m_server;
    std::shared_ptr<Gate> m_gate;
    std::unique_ptr<QtHttpClient> m_client;
    std::atomic<std::int64_t> m_clock{0};   // nanoseconds; only the test moves it
    std::mutex m_handlerMutex;
    FakeHttpServer::Handler m_onGet;
    FakeHttpServer::Handler m_onSearch;
    QString m_base;

    static void defaultGet(Exchange& exchange) { exchange.reply(404, notFound); }
    static void defaultSearch(Exchange& exchange) { exchange.reply(200, "[]"); }

    void onGet(FakeHttpServer::Handler handler) {
        std::lock_guard lock(m_handlerMutex);
        m_onGet = std::move(handler);
    }
    void onSearch(FakeHttpServer::Handler handler) {
        std::lock_guard lock(m_handlerMutex);
        m_onSearch = std::move(handler);
    }

    // The base of the fake service ("http://127.0.0.1:<port>/api").
    QString base() const { return m_base; }

    std::function<std::chrono::nanoseconds()> testClock() {
        return [this] { return std::chrono::nanoseconds(m_clock.load()); };
    }
    void advance(std::chrono::nanoseconds by) { m_clock += by.count(); }

    std::unique_ptr<LrclibLyricsProvider> provider(const ProviderArgs& a = {}) {
        LrclibOptions options;
        options.baseUrl = a.baseUrl.value_or(base());
        options.userAgent = testUserAgent;
        options.requestTimeout = a.requestTimeout;
        options.maxRequests = a.maxRequests;
        options.maxConcurrent = a.maxConcurrent;
        options.clock = testClock();
        return std::make_unique<LrclibLyricsProvider>(*m_client, options);
    }

    QList<FakeHttpServer::Seen> seen() const { return m_server->seen(); }
    Paths paths() const {
        Paths result;
        for (const FakeHttpServer::Seen& request : m_server->seen()) result.append(request.path);
        return result;
    }
    QList<FakeHttpServer::Seen> seenAt(const QByteArray& path) const {
        QList<FakeHttpServer::Seen> result;
        for (const FakeHttpServer::Seen& request : m_server->seen()) {
            if (request.path == path) result.append(request);
        }
        return result;
    }

    // The gate handlers that "sleep" wait on (held by the handler, so it outlives the slot).
    std::shared_ptr<Gate> gate() const { return m_gate; }

    // A record of exactly `size` bytes: its fields, and a field the provider does not read as padding.
    static QString recordOfSize(qsizetype size, const QString& lyrics) {
        QString recordText = record({.lyrics = lyrics});
        recordText.chop(1);
        const QString head = recordText + u",\"padding\":\""_s;
        const QString tail = u"\"}"_s;
        return head + QString(u'x').repeated(size - head.size() - tail.size()) + tail;
    }

private slots:
    void init() {
        m_server = std::make_unique<FakeHttpServer>();
        m_base = m_server->base() + u"/api"_s;
        m_gate = std::make_shared<Gate>();
        m_client = std::make_unique<QtHttpClient>();
        m_clock = 0;
        onGet(defaultGet);
        onSearch(defaultSearch);
        m_server->route("/api/get", [this](Exchange& exchange) {
            FakeHttpServer::Handler handler;
            {
                std::lock_guard lock(m_handlerMutex);
                handler = m_onGet;
            }
            handler(exchange);
        });
        m_server->route("/api/search", [this](Exchange& exchange) {
            FakeHttpServer::Handler handler;
            {
                std::lock_guard lock(m_handlerMutex);
                handler = m_onSearch;
            }
            handler(exchange);
        });
    }

    void cleanup() {
        m_gate->open();
        m_server.reset();   // waits for the handlers, which the open gate has woken
        m_client.reset();
        onGet({});
        onSearch({});
    }

    // ---- /get ----

    void aGetHitIsReturnedWithoutCallingSearch() {
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = lyricsOf(u"get"_s)})); });

        const Lyrics lyrics = provider()->find(query(), {});

        QCOMPARE(lyrics, Lyrics(lyricsOf(u"get"_s)));
        QCOMPARE(paths(), Paths{"/api/get"});
    }

    void theLyricsAreTidied() {
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = u"  첫 줄  \r\n둘째 줄\r\n\r\n\r\n\r\n셋째 줄\t\r\n\r\n"_s})); });

        QCOMPARE(provider()->find(query(), {}), Lyrics(u"  첫 줄\n둘째 줄\n\n\n셋째 줄"_s));
    }

    void aGetHitWhoseTitleOnlyMatchesWithoutItsBracketsAndCreditIsAccepted() {
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = lyricsOf(u"get"_s)})); });

        QCOMPARE(provider()->find(query({.title = u"Love poem (feat. nobody)"_s}), {}), Lyrics(lyricsOf(u"get"_s)));
        QCOMPARE(provider()->find(query({.title = u"Love poem feat. nobody"_s}), {}), Lyrics(lyricsOf(u"get"_s)));
    }

    void aGetHitThatIsNotTheSongFallsThroughToSearch() {
        onGet([](Exchange& e) { reply(e, 200, record({.artist = u"Somebody Else"_s, .lyrics = lyricsOf(u"wrong"_s)})); });
        onSearch([](Exchange& e) { reply(e, 200, array({record({.lyrics = lyricsOf(u"search"_s)})})); });

        QCOMPARE(provider()->find(query(), {}), Lyrics(lyricsOf(u"search"_s)));
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search"}));
    }

    void aGetHitWithTheWrongLengthOrWithoutLyricsOrInstrumentalFallsThroughToSearch() {
        const QStringList rejected = {
            record({.duration = u"300"_s, .lyrics = lyricsOf(u"long"_s)}),
            record({.lyrics = std::nullopt}),
            record({.lyrics = u"  "_s}),
            record({.instrumental = true, .lyrics = lyricsOf(u"instrumental"_s)}),
        };
        for (const QString& answer : rejected) {
            m_server->clearSeen();
            onGet([answer](Exchange& e) { reply(e, 200, answer); });
            onSearch([](Exchange& e) { e.reply(200, "[]"); });

            COMPARE_MSG(provider()->find(query(), {}), Lyrics(), answer);
            COMPARE_MSG(paths(), (Paths{"/api/get", "/api/search"}), answer);
        }
    }

    void aGetMissThenASearchPicksTheCandidateWithTheClosestLength() {
        onSearch([](Exchange& e) {
            reply(e, 200,
                  array({
                      record({.track = u"Another song"_s, .duration = u"258"_s, .lyrics = lyricsOf(u"other"_s)}),
                      record({.duration = u"264"_s, .lyrics = lyricsOf(u"far"_s)}),
                      record({.duration = u"259.4"_s, .lyrics = lyricsOf(u"best"_s)}),
                      record({.duration = u"258.9"_s, .instrumental = true, .lyrics = lyricsOf(u"instrumental"_s)}),
                      record({.duration = u"261"_s, .lyrics = lyricsOf(u"near"_s)}),
                  }));
        });

        const Lyrics lyrics = provider()->find(query(), {});

        QCOMPARE(lyrics, Lyrics(lyricsOf(u"best"_s)));
        COMPARE_MSG(paths(), (Paths{"/api/get", "/api/search"}), u"the first accepted result ends the call"_s);
    }

    void theDurationOfARecordMayBeAnIntegerOrADecimalAndUnknownFieldsAreIgnored() {
        onSearch([](Exchange& e) { reply(e, 200, array({record({.duration = u"258.37"_s, .lyrics = lyricsOf(u"decimal"_s)})})); });
        QCOMPARE(provider()->find(query(), {}), Lyrics(lyricsOf(u"decimal"_s)));

        onSearch([](Exchange& e) { reply(e, 200, array({record({.duration = u"260"_s, .lyrics = lyricsOf(u"integer"_s)})})); });
        QCOMPARE(provider()->find(query(), {}), Lyrics(lyricsOf(u"integer"_s)));
    }

    void recordsOfTheWrongShapeInASearchAnswerAreSkipped() {
        const QString good = record({.lyrics = lyricsOf(u"good"_s)});
        onSearch([good](Exchange& e) {
            reply(e, 200,
                  u"[1,\"text\",null,[],{\"trackName\":5,\"artistName\":[\"IU\"],\"plainLyrics\":"_s + jsonString(lyricsOf(u"typed"_s)) +
                      u"},\n                    {\"trackName\":\"Love poem\",\"artistName\":\"IU\",\"duration\":\"long\",\"instrumental\":\"no\",\"plainLyrics\":\"x\"},"_s +
                      good + u"]"_s);
        });

        QCOMPARE(provider()->find(query(), {}), Lyrics(lyricsOf(u"good"_s)));
    }

    // ---- what is sent ----

    void theGetRequestCarriesTheUnmodifiedArtistTitleAlbumAndDurationPercentEncodedAsUtf8() {
        provider()->find(query({.artist = u"소연 (SOYEON)"_s, .title = u"사랑 & 시 (Live)"_s, .album = u"앨범 1"_s, .durationSeconds = 258}), {});

        QVERIFY(!seen().isEmpty());
        const FakeHttpServer::Seen get = seen().first();
        QCOMPARE(get.method, "GET");
        QCOMPARE(get.path, "/api/get");
        QCOMPARE(get.rawQuery,
                 QByteArray("artist_name=%EC%86%8C%EC%97%B0%20%28SOYEON%29&track_name=%EC%82%AC%EB%9E%91%20%26%20%EC%8B%9C%20%28Live%29"
                            "&album_name=%EC%95%A8%EB%B2%94%201&duration=258"));
    }

    void anAbsentAlbumAndAnAbsentDurationAreLeftOutOfTheGetRequest() {
        const auto provider = this->provider();
        provider->find(query({.album = std::nullopt, .durationSeconds = std::nullopt}), {});
        provider->find(query({.album = u"My List"_s, .durationSeconds = std::nullopt}), {});
        provider->find(query({.album = std::nullopt, .durationSeconds = 258}), {});

        QList<QByteArray> gets;
        for (const FakeHttpServer::Seen& get : seenAt("/api/get")) gets.append(get.rawQuery);
        QCOMPARE(gets, (QList<QByteArray>{
                           "artist_name=IU&track_name=Love%20poem",
                           "artist_name=IU&track_name=Love%20poem&album_name=My%20List",
                           "artist_name=IU&track_name=Love%20poem&duration=258",
                       }));
    }

    void everyRequestIsAGetWithTheUserAgentAndTheAcceptHeader() {
        provider()->find(query(), {});

        QCOMPARE(paths(), (Paths{"/api/get", "/api/search"}));
        for (const FakeHttpServer::Seen& request : seen()) {
            QCOMPARE(request.method, "GET");
            QCOMPARE(QString::fromUtf8(request.userAgent), u"xGetSongs-test/1.0 (https://example.invalid)"_s);
            QCOMPARE(request.accept, "application/json");
        }
    }

    void theDefaultUserAgentNamesTheApplicationAndItsHomePage() {
        // The service asks for a descriptive User-Agent; the default must not be Qt's.
        LrclibOptions options;
        options.baseUrl = base();
        LrclibLyricsProvider(*m_client, options).find(query(), {});

        QVERIFY(!seen().isEmpty());
        QCOMPARE(QString::fromUtf8(seen().first().userAgent), u"xGetSongs/1.0 (https://github.com/gomtos/xGetSongs)"_s);
        // Kotlin's DEFAULT_USER_AGENT and DEFAULT_BASE_URL
        QCOMPARE(QString::fromUtf8(seen().first().userAgent), LrclibOptions().userAgent);
        QCOMPARE(QString::fromUtf8(seen().first().userAgent), xgs::net::AppUserAgent.toString());
        QCOMPARE(u"https://lrclib.net/api"_s, xgs::lyrics::DefaultBaseUrl.toString());
        QCOMPARE(LrclibOptions().baseUrl, xgs::lyrics::DefaultBaseUrl.toString());
    }

    void theSearchRequestAsksByTrackNameAndArtistName() {
        provider()->find(query({.artist = u"소연 (SOYEON)"_s, .title = u"Love poem"_s, .album = u"My List"_s}), {});

        const QList<FakeHttpServer::Seen> searches = seenAt("/api/search");
        QVERIFY(!searches.isEmpty());
        QCOMPARE(searches.first().rawQuery, QByteArray("track_name=Love%20poem&artist_name=%EC%86%8C%EC%97%B0%20%28SOYEON%29"));
    }

    void artistAndTitleVariantsAreTriedInOrderAfterGet() {
        const Lyrics lyrics = provider()->find(query({.artist = u"소연 (SOYEON)"_s, .title = u"Title (feat. X)"_s}), {});

        QCOMPARE(lyrics, Lyrics());
        COMPARE_MSG(paths(),
                    (Paths{"/api/get", "/api/search", "/api/search", "/api/search", "/api/search", "/api/search", "/api/search"}),
                    u"one get and one search for each of the 3 artist and 2 title variants"_s);
        QList<std::pair<Lyrics, Lyrics>> searched;
        for (const FakeHttpServer::Seen& search : seenAt("/api/search")) {
            searched.append({param(search, "artist_name"), param(search, "track_name")});
        }
        const QList<std::pair<Lyrics, Lyrics>> expected = {
            {u"소연 (SOYEON)"_s, u"Title (feat. X)"_s},
            {u"소연 (SOYEON)"_s, u"Title"_s},
            {u"소연"_s, u"Title (feat. X)"_s},
            {u"소연"_s, u"Title"_s},
            {u"SOYEON"_s, u"Title (feat. X)"_s},
            {u"SOYEON"_s, u"Title"_s},
        };
        QCOMPARE(searched.size(), expected.size());
        for (qsizetype i = 0; i < expected.size(); ++i) {
            COMPARE_MSG(searched.at(i).first, expected.at(i).first, u"artist of search %1"_s.arg(i));
            COMPARE_MSG(searched.at(i).second, expected.at(i).second, u"title of search %1"_s.arg(i));
        }
        COMPARE_MSG(param(seen().first(), "artist_name"), Lyrics(u"소연 (SOYEON)"_s), u"get uses the unmodified artist"_s);
        COMPARE_MSG(param(seen().first(), "track_name"), Lyrics(u"Title (feat. X)"_s), u"get uses the unmodified title"_s);
    }

    void theSearchStopsAtTheFirstVariantThatFindsTheSong() {
        onSearch([](Exchange& e) {
            const QByteArray title = rawParams(e.request().rawQuery).value("track_name");
            if (title == "Title") reply(e, 200, array({record({.track = u"Title"_s, .artist = u"소연"_s, .lyrics = lyricsOf(u"variant"_s)})}));
            else e.reply(200, "[]");
        });

        const Lyrics lyrics = provider()->find(query({.artist = u"소연 (SOYEON)"_s, .title = u"Title (feat. X)"_s}), {});

        QCOMPARE(lyrics, Lyrics(lyricsOf(u"variant"_s)));
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search", "/api/search"}));
    }

    // ---- the cap ----

    void noMoreThanMaxRequestsAreMadeInOneCall() {
        // 1 get + 3 artist variants x 4 title variants = 13 possible requests.
        provider()->find(query({.artist = u"소연 (SOYEON)"_s, .title = u"\"Title\" (Live) feat. X"_s}), {});

        QCOMPARE(seen().size(), 8);
        QCOMPARE(seen().first().path, "/api/get");
    }

    void aSmallerCapStopsEarlier() {
        provider({.maxRequests = 3})->find(query({.artist = u"소연 (SOYEON)"_s, .title = u"Title (feat. X)"_s}), {});

        QCOMPARE(paths(), (Paths{"/api/get", "/api/search", "/api/search"}));
    }

    void aCapOfOneMakesOnlyTheGetRequestAndZeroMakesNone() {
        QCOMPARE(provider({.maxRequests = 1})->find(query(), {}), Lyrics());
        QCOMPARE(paths(), Paths{"/api/get"});

        m_server->clearSeen();
        QCOMPARE(provider({.maxRequests = 0})->find(query(), {}), Lyrics());
        QCOMPARE(paths(), Paths());
    }

    void theCapIsPerCall() {
        const auto provider = this->provider({.maxRequests = 2});

        provider->find(query(), {});
        provider->find(query(), {});

        QCOMPARE(seen().size(), 4);
    }

    // ---- failures ----

    void aTooManyRequestsAnswerToGetStopsEverything() {
        onGet([](Exchange& e) { e.reply(429, R"({"message":"slow down"})"); });

        QCOMPARE(provider()->find(query({.artist = u"소연 (SOYEON)"_s}), {}), Lyrics());
        QCOMPARE(paths(), Paths{"/api/get"});
    }

    void aTooManyRequestsAnswerToASearchStopsEverything() {
        onSearch([](Exchange& e) { e.reply(429, ""); });

        QCOMPARE(provider()->find(query({.artist = u"소연 (SOYEON)"_s}), {}), Lyrics());
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search"}));
    }

    void aServerErrorOnOneRequestStillLetsTheNextBeTried() {
        onGet([](Exchange& e) { e.reply(500, "oops"); });
        onSearch([](Exchange& e) { reply(e, 200, array({record({.lyrics = lyricsOf(u"after500"_s)})})); });

        QCOMPARE(provider()->find(query(), {}), Lyrics(lyricsOf(u"after500"_s)));
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search"}));
    }

    void everyAnswerThatIsNot200CountsAsNoResultAndTheNextRequestIsStillMade() {
        for (const int status : {400, 403, 404, 500}) {
            m_server->clearSeen();
            onGet([status](Exchange& e) { e.reply(status, "x"); });
            onSearch([status](Exchange& e) { e.reply(status, "x"); });

            COMPARE_MSG(provider()->find(query({.artist = u"소연 (SOYEON)"_s}), {}), Lyrics(), u"status %1"_s.arg(status));
            COMPARE_MSG(seen().size(), qsizetype(1 + 3), u"status %1: get and the three artist variants of one title"_s.arg(status));
        }
    }

    void aServerErrorOnASearchLetsTheNextSearchBeTried() {
        onSearch([](Exchange& e) {
            if (rawParams(e.request().rawQuery).value("artist_name").startsWith("%EC%86%8C%EC%97%B0%20")) {
                e.reply(500, "oops");
            } else {
                reply(e, 200, array({record({.artist = u"소연"_s, .lyrics = lyricsOf(u"second"_s)})}));
            }
        });

        QCOMPARE(provider()->find(query({.artist = u"소연 (SOYEON)"_s}), {}), Lyrics(lyricsOf(u"second"_s)));
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search", "/api/search"}));
    }

    void badJsonGivesNullAndTheNextRequestIsStillTried() {
        onGet([](Exchange& e) { e.reply(200, "this is not json"); });
        onSearch([](Exchange& e) { e.reply(200, "{\"truncated\":"); });

        QCOMPARE(provider()->find(query(), {}), Lyrics());
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search"}));
    }

    void anAnswerOfTheWrongShapeGivesNull() {
        onGet([](Exchange& e) { e.reply(200, "[]"); });
        onSearch([](Exchange& e) { e.reply(200, R"({"records":[]})"); });

        QCOMPARE(provider()->find(query(), {}), Lyrics());

        onGet([](Exchange& e) { e.reply(200, ""); });
        onSearch([](Exchange& e) { e.reply(200, "null"); });
        QCOMPARE(provider()->find(query(), {}), Lyrics());
    }

    void anEmptySearchResultGivesNull() {
        onSearch([](Exchange& e) { e.reply(200, "[]"); });

        QCOMPARE(provider()->find(query(), {}), Lyrics());
    }

    void anInstrumentalRecordGivesNull() {
        onGet([](Exchange& e) { reply(e, 200, record({.instrumental = true, .lyrics = lyricsOf(u"instrumental"_s)})); });
        onSearch([](Exchange& e) { reply(e, 200, array({record({.instrumental = true, .lyrics = lyricsOf(u"instrumental"_s)})})); });

        QCOMPARE(provider()->find(query(), {}), Lyrics());
    }

    void aMismatchingRecordGivesNull() {
        onGet([](Exchange& e) { reply(e, 200, record({.track = u"Another song"_s, .lyrics = lyricsOf(u"title"_s)})); });
        onSearch([](Exchange& e) {
            reply(e, 200,
                  array({
                      record({.artist = u"Somebody Else"_s, .lyrics = lyricsOf(u"artist"_s)}),
                      record({.duration = u"320"_s, .lyrics = lyricsOf(u"length"_s)}),
                      record({.track = u"Love poem 2"_s, .lyrics = lyricsOf(u"title2"_s)}),
                  }));
        });

        QCOMPARE(provider()->find(query(), {}), Lyrics());
    }

    void aGetRecordWhoseLyricsAreTooShortIsNotAnAnswerAndTheSearchIsStillTried() {
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = u"Line one\nLine two"_s})); });
        onSearch([](Exchange& e) { reply(e, 200, array({record({.lyrics = lyricsOf(u"full"_s)})})); });

        QCOMPARE(provider()->find(query(), {}), Lyrics(lyricsOf(u"full"_s)));
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search"}));
    }

    void aCloserSearchRecordWithTooShortLyricsDoesNotHideAnotherAcceptedRecordWithFullLyrics() {
        onSearch([](Exchange& e) {
            reply(e, 200,
                  array({
                      record({.duration = u"258"_s, .lyrics = u"Line one\nLine two"_s}),
                      record({.duration = u"258.2"_s, .lyrics = u"  \n "_s}),
                      record({.duration = u"258.4"_s, .lyrics = std::nullopt}),
                      record({.duration = u"263"_s, .lyrics = lyricsOf(u"full"_s)}),
                  }));
        });

        QCOMPARE(provider()->find(query(), {}), Lyrics(lyricsOf(u"full"_s)));
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search"}));
    }

    void whenNoRecordHasLyricsOfEnoughLinesTheResultIsNull() {
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = u"Line one\nLine two"_s})); });
        onSearch([](Exchange& e) { reply(e, 200, array({record({.lyrics = u"Only one"_s}), record({.lyrics = u""_s})})); });

        QCOMPARE(provider()->find(query(), {}), Lyrics());
    }

    void aReadTimeoutGivesNullAndEndsTheCall() {
        onGet([gate = gate()](Exchange& e) {
            gate->wait(5s);
            reply(e, 200, record());
        });
        const auto begin = Clock::now();

        const Lyrics lyrics = provider({.requestTimeout = 1s})->find(query(), {});

        QCOMPARE(lyrics, Lyrics());
        QVERIFY2(Clock::now() - begin < 4s, "the call must end at the time limit, not when the server wakes up");
        COMPARE_MSG(paths(), Paths{"/api/get"}, u"a service that does not answer is not asked again"_s);
    }

    void aSlowSearchAfterAFastGetEndsTheCallToo() {
        onSearch([gate = gate()](Exchange& e) {
            gate->wait(5s);
            e.reply(200, "[]");
        });
        const auto begin = Clock::now();

        QCOMPARE(provider({.requestTimeout = 1s})->find(query({.artist = u"소연 (SOYEON)"_s}), {}), Lyrics());

        QVERIFY2(Clock::now() - begin < 4s, "the call must end at the time limit, not when the server wakes up");
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search"}));
    }

    void aRefusedConnectionGivesNullWithoutThrowing() {
        const QString deadBase = base();
        m_server->stop();
        const auto begin = Clock::now();

        QCOMPARE(provider({.baseUrl = deadBase})->find(query({.artist = u"소연 (SOYEON)"_s}), {}), Lyrics());
        QCOMPARE(provider({.baseUrl = deadBase})->find(query(), {}), Lyrics());
        QVERIFY(Clock::now() - begin < 10s);
    }

    void aQueryWithoutAnArtistOrATitleMakesNoRequest() {
        QCOMPARE(provider()->find(query({.artist = u"  "_s}), {}), Lyrics());
        QCOMPARE(provider()->find(query({.title = u""_s}), {}), Lyrics());
        QCOMPARE(paths(), Paths());
    }

    void aBlankAlbumAndANonPositiveDurationAreLeftOutOfTheGetRequest() {
        provider({.maxRequests = 1})->find(query({.album = u"  "_s, .durationSeconds = 0}), {});

        QCOMPARE(seen().size(), 1);   // Kotlin's single()
        QCOMPARE(seen().first().rawQuery, QByteArray("artist_name=IU&track_name=Love%20poem"));
    }

    void aBrokenBaseUrlGivesNullWithoutThrowing() {
        QCOMPARE(provider({.baseUrl = u"http://[not a url"_s})->find(query(), {}), Lyrics());
        QCOMPARE(provider({.baseUrl = u""_s})->find(query(), {}), Lyrics());
        QCOMPARE(provider({.baseUrl = u"ftp://127.0.0.1/api"_s})->find(query(), {}), Lyrics());
    }

    void aGivenHttpClientIsUsed() {
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = lyricsOf(u"given"_s)})); });
        // Kotlin checks two traces of a given java.net.http client (its proxy selector was asked, its executor ran the
        // exchange). The C++ provider is always given its client (brief: no client of its own); the trace is the count of a
        // wrapping client.
        CountingHttpClient client(*m_client);
        LrclibOptions options;
        options.baseUrl = base();

        const Lyrics lyrics = LrclibLyricsProvider(client, options).find(query(), {});

        QCOMPARE(lyrics, Lyrics(lyricsOf(u"given"_s)));
        QVERIFY2(client.calls() > 0, "the request must have gone through the given client");
    }

    // ---- the cool-down after trouble ----

    void afterATooManyRequestsAnswerTheNextCallsMakeNoRequestAtAll() {
        const auto provider = this->provider();
        onGet([](Exchange& e) { e.reply(429, "{}"); });
        QCOMPARE(provider->find(query(), {}), Lyrics());
        QCOMPARE(seen().size(), 1);
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = lyricsOf(u"recovered"_s)})); });

        for (int it = 0; it < 3; ++it) {
            COMPARE_MSG(provider->find(query({.title = u"Another song %1"_s.arg(it)}), {}), Lyrics(), u"call %1"_s.arg(it));
        }

        COMPARE_MSG(seen().size(), qsizetype(1), u"the service gets its rest: no request while the provider cools down"_s);
    }

    void theCoolDownEndsAfterFiveMinutesAndTheNextCallAsksAgain() {
        const auto provider = this->provider();
        onGet([](Exchange& e) { e.reply(429, "{}"); });
        QCOMPARE(provider->find(query(), {}), Lyrics());
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = lyricsOf(u"recovered"_s)})); });

        advance(4min + 59s);
        QCOMPARE(provider->find(query(), {}), Lyrics());
        COMPARE_MSG(seen().size(), qsizetype(1), u"4 minutes 59 seconds are not enough"_s);

        advance(1s);
        QCOMPARE(provider->find(query(), {}), Lyrics(lyricsOf(u"recovered"_s)));
        QCOMPARE(seen().size(), 2);
    }

    void aSecondTroubleAfterTheCoolDownStartsANewOne() {
        const auto provider = this->provider();
        onGet([](Exchange& e) { e.reply(429, "{}"); });
        QCOMPARE(provider->find(query(), {}), Lyrics());
        advance(5min);

        QCOMPARE(provider->find(query(), {}), Lyrics());
        COMPARE_MSG(seen().size(), qsizetype(2), u"after the cool-down it asks again, and is throttled again"_s);
        advance(4min);
        QCOMPARE(provider->find(query(), {}), Lyrics());
        COMPARE_MSG(seen().size(), qsizetype(2), u"the new cool-down counts from the second trouble"_s);
        advance(1min);
        QCOMPARE(provider->find(query(), {}), Lyrics());
        QCOMPARE(seen().size(), 3);
    }

    void theCoolDownIsAConstructorParameter() {
        LrclibOptions options;
        options.baseUrl = base();
        options.coolDown = 30s;
        options.clock = testClock();
        LrclibLyricsProvider provider(*m_client, options);
        onGet([](Exchange& e) { e.reply(429, "{}"); });
        QCOMPARE(provider.find(query(), {}), Lyrics());

        advance(29s);
        QCOMPARE(provider.find(query(), {}), Lyrics());
        QCOMPARE(seen().size(), 1);
        advance(1s);
        QCOMPARE(provider.find(query(), {}), Lyrics());
        QCOMPARE(seen().size(), 2);
    }

    void badGatewayServiceUnavailableAndGatewayTimeoutAreTroubleLikeTooManyRequests() {
        for (const int status : {502, 503, 504}) {
            m_server->clearSeen();
            const auto provider = this->provider();
            onGet([status](Exchange& e) { e.reply(status, "x"); });
            onSearch([status](Exchange& e) { e.reply(status, "x"); });

            COMPARE_MSG(provider->find(query({.artist = u"소연 (SOYEON)"_s}), {}), Lyrics(), u"status %1"_s.arg(status));
            COMPARE_MSG(paths(), Paths{"/api/get"}, u"status %1 stops the call after one request"_s.arg(status));
            COMPARE_MSG(provider->find(query({.artist = u"소연 (SOYEON)"_s}), {}), Lyrics(), u"status %1"_s.arg(status));
            COMPARE_MSG(paths(), Paths{"/api/get"}, u"status %1: no request while it cools down"_s.arg(status));
        }
    }

    void aBadGatewayOnASearchStopsTheCallAndStartsTheCoolDownToo() {
        const auto provider = this->provider();
        onSearch([](Exchange& e) { e.reply(502, "x"); });

        QCOMPARE(provider->find(query({.artist = u"소연 (SOYEON)"_s}), {}), Lyrics());
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search"}));
        QCOMPARE(provider->find(query(), {}), Lyrics());
        QCOMPARE(seen().size(), 2);
    }

    void notFoundAndServerErrorsAndOtherClientErrorsDoNotStartACoolDown() {
        for (const int status : {400, 403, 404, 500}) {
            m_server->clearSeen();
            const auto provider = this->provider();
            onGet([status](Exchange& e) { e.reply(status, "x"); });
            onSearch([status](Exchange& e) { e.reply(status, "x"); });

            COMPARE_MSG(provider->find(query(), {}), Lyrics(), u"status %1"_s.arg(status));
            COMPARE_MSG(provider->find(query(), {}), Lyrics(), u"status %1"_s.arg(status));

            COMPARE_MSG(seen().size(), qsizetype(4), u"status %1: both calls made their get and their search"_s.arg(status));
        }
    }

    void aTimeoutStartsTheCoolDown() {
        const auto provider = this->provider({.requestTimeout = 1s});
        onGet([gate = gate()](Exchange& e) {
            gate->wait(5s);
            reply(e, 200, record());
        });
        QCOMPARE(provider->find(query(), {}), Lyrics());
        QCOMPARE(seen().size(), 1);
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = lyricsOf(u"recovered"_s)})); });

        QCOMPARE(provider->find(query(), {}), Lyrics());
        COMPARE_MSG(seen().size(), qsizetype(1), u"no request while it cools down"_s);
        advance(5min);
        QCOMPARE(provider->find(query(), {}), Lyrics(lyricsOf(u"recovered"_s)));
    }

    void aRefusedConnectionStartsTheCoolDown() {
        const QString deadBase = base();
        m_server->stop();
        // A request cannot be counted at a server that is not there; Kotlin counts the calls of its client's proxy selector,
        // which is asked once per request. Here a wrapping client counts the requests handed to the client.
        CountingHttpClient requests(*m_client);
        LrclibOptions options;
        options.baseUrl = deadBase;
        options.clock = testClock();
        LrclibLyricsProvider provider(requests, options);

        QCOMPARE(provider.find(query(), {}), Lyrics());
        COMPARE_MSG(requests.calls(), 1, u"the get request fails to connect and ends the call"_s);
        QCOMPARE(provider.find(query(), {}), Lyrics());
        COMPARE_MSG(requests.calls(), 1, u"no request while it cools down"_s);
        advance(5min);
        QCOMPARE(provider.find(query(), {}), Lyrics());
        COMPARE_MSG(requests.calls(), 2, u"after the cool-down it tries again"_s);
    }

    void anEmptyAnswerABadAnswerAndTheRequestCapDoNotStartACoolDown() {
        const auto empty = provider();
        QCOMPARE(empty->find(query(), {}), Lyrics());
        QCOMPARE(empty->find(query(), {}), Lyrics());
        COMPARE_MSG(seen().size(), qsizetype(4), u"get and an empty search, twice"_s);

        m_server->clearSeen();
        onGet([](Exchange& e) { e.reply(200, "not json"); });
        const auto bad = provider();
        QCOMPARE(bad->find(query(), {}), Lyrics());
        QCOMPARE(bad->find(query(), {}), Lyrics());
        QCOMPARE(seen().size(), 4);

        m_server->clearSeen();
        const auto capped = provider({.maxRequests = 1});
        QCOMPARE(capped->find(query(), {}), Lyrics());
        QCOMPARE(capped->find(query(), {}), Lyrics());
        COMPARE_MSG(seen().size(), qsizetype(2), u"reaching the cap is not trouble of the service"_s);
    }

    void theCoolDownBelongsToTheInstance() {
        const auto throttled = provider();
        const auto other = provider();
        onGet([](Exchange& e) { e.reply(429, "{}"); });
        QCOMPARE(throttled->find(query(), {}), Lyrics());
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = lyricsOf(u"other"_s)})); });

        QCOMPARE(other->find(query(), {}), Lyrics(lyricsOf(u"other"_s)));
        QCOMPARE(throttled->find(query(), {}), Lyrics());
    }

    void callsWaitingForASlotStopWhenAnotherCallRunsIntoTrouble() {
        onGet([](Exchange& e) {
            std::this_thread::sleep_for(300ms);
            e.reply(429, "{}");
        });
        const auto provider = this->provider({.maxConcurrent = 1});

        std::vector<Lyrics> results(3, Lyrics(u"not run"_s));
        {
            std::vector<std::jthread> calls;
            for (int it = 1; it <= 3; ++it) {
                calls.emplace_back([&, it](std::stop_token stop) { results[it - 1] = provider->find(query({.title = u"Song %1"_s.arg(it)}), stop); });
            }
            for (std::jthread& call : calls) call.join();   // awaitAll (a jthread's destructor would request stop first)
        }

        QCOMPARE(results, std::vector<Lyrics>(3));
        COMPARE_MSG(seen().size(), qsizetype(1), u"the two calls queued behind the first one never made their request"_s);
    }

    // ---- the size of an answer ----

    void anAnswerOfExactlyTwoMebibytesIsRead() {
        const QString answer = recordOfSize(twoMebibytes, lyricsOf(u"big"_s));
        QCOMPARE(answer.toUtf8().size(), twoMebibytes);
        onGet([answer](Exchange& e) { reply(e, 200, answer); });

        QCOMPARE(provider()->find(query(), {}), Lyrics(lyricsOf(u"big"_s)));
        QCOMPARE(paths(), Paths{"/api/get"});
    }

    void anAnswerOfOneByteMoreIsAMissAndTheNextRequestIsStillMade() {
        const QString answer = recordOfSize(twoMebibytes + 1, lyricsOf(u"too big"_s));
        QCOMPARE(answer.toUtf8().size(), twoMebibytes + 1);
        onGet([answer](Exchange& e) { reply(e, 200, answer); });
        onSearch([](Exchange& e) { reply(e, 200, array({record({.lyrics = lyricsOf(u"small"_s)})})); });

        QCOMPARE(provider()->find(query(), {}), Lyrics(lyricsOf(u"small"_s)));
        QCOMPARE(paths(), (Paths{"/api/get", "/api/search"}));
    }

    void aBodyThatNeverEndsIsCutOffAfterTwoMebibytesAndNeverBuffered() {
        struct State {
            std::atomic<qint64> written{0};
            Latch handlerDone;
        };
        const auto state = std::make_shared<State>();
        constexpr qint64 total = 32LL * 1024 * 1024;
        onGet([state](Exchange& e) {
            e.sendHeaders(200, true);   // chunked: no length is announced
            const QByteArray block(64 * 1024, 'x');
            while (state->written.load() < total) {
                if (!e.write(block)) break;   // the client went away: that is what is meant to happen
                state->written += block.size();
            }
            state->handlerDone.countDown();
        });

        QCOMPARE(provider()->find(query(), {}), Lyrics());

        QVERIFY2(state->handlerDone.await(15s), "the server must notice that the client went away");
        QVERIFY2(state->written.load() < total,
                 qPrintable(u"the whole body must not have been read: the server wrote %1 of %2 bytes"_s.arg(state->written.load()).arg(total)));
        COMPARE_MSG(paths(), (Paths{"/api/get", "/api/search"}), u"a body that is too large is a miss, the next request is still made"_s);
    }

    void aBodyThatStallsAfterItsHeadersEndsTheCallAtTheTimeLimitAndStartsTheCoolDown() {
        onGet([gate = gate()](Exchange& e) {
            e.sendHeaders(200, true);
            e.write("[");
            gate->wait(5s);
        });
        const auto provider = this->provider({.requestTimeout = 1s});
        const auto begin = Clock::now();

        QCOMPARE(provider->find(query(), {}), Lyrics());

        QVERIFY2(Clock::now() - begin < 4s, "the call must end at the time limit, not when the server wakes up");
        COMPARE_MSG(paths(), Paths{"/api/get"}, u"a service that stalls is not asked again"_s);
        QCOMPARE(provider->find(query(), {}), Lyrics());
        COMPARE_MSG(seen().size(), qsizetype(1), u"no request while it cools down"_s);
    }

    // ---- cancellation and concurrency ----

    // Kotlin: the cancelled call ends with a CancellationException. Here (brief) find returns nothing, within 3 s, with stop
    // requested on its token.
    void cancellingTheCallWhileTheServerSleepsReturnsPromptlyWithACancellationException() {
        const auto started = std::make_shared<Latch>();
        onGet([started, gate = gate()](Exchange& e) {
            started->countDown();
            gate->wait(10s);
            reply(e, 200, record());
        });
        const auto provider = this->provider({.requestTimeout = 30s});
        std::promise<std::pair<Lyrics, bool>> outcome;
        std::future<std::pair<Lyrics, bool>> result = outcome.get_future();
        std::jthread call([&](std::stop_token stop) {
            const Lyrics lyrics = provider->find(query(), stop);
            outcome.set_value({lyrics, stop.stop_requested()});
        });
        QVERIFY2(started->await(5s), "the request must reach the server");
        const auto begin = Clock::now();

        call.request_stop();
        QVERIFY(result.wait_for(3s) == std::future_status::ready);

        QVERIFY(Clock::now() - begin < 3s);
        const auto [lyrics, stopped] = result.get();
        QCOMPARE(lyrics, Lyrics());
        QVERIFY2(stopped, "the call returned because it was stopped");
    }

    void aCancelledCallGivesItsSlotBack() {
        const auto started = std::make_shared<Latch>();
        onGet([started, gate = gate()](Exchange& e) {
            started->countDown();
            gate->wait(10s);
            reply(e, 200, record());
        });
        const auto provider = this->provider({.maxConcurrent = 1, .requestTimeout = 30s});
        std::promise<void> firstDone;
        std::future<void> firstJoined = firstDone.get_future();
        std::jthread first([&](std::stop_token stop) {
            provider->find(query(), stop);
            firstDone.set_value();
        });
        QVERIFY(started->await(5s));
        first.request_stop();
        QVERIFY(firstJoined.wait_for(3s) == std::future_status::ready);
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = lyricsOf(u"later"_s)})); });

        // Kotlin's withTimeout(5.seconds): on a time-out the thread is stopped when the slot returns.
        std::promise<Lyrics> laterDone;
        std::future<Lyrics> later = laterDone.get_future();
        std::jthread second([&](std::stop_token stop) { laterDone.set_value(provider->find(query(), stop)); });
        QVERIFY(later.wait_for(5s) == std::future_status::ready);

        QCOMPARE(later.get(), Lyrics(lyricsOf(u"later"_s)));
    }

    void noMoreThanMaxConcurrentRequestsAreInFlightAtOnce() {
        const auto concurrency = std::make_shared<InFlight>();
        const FakeHttpServer::Handler slow = [concurrency](Exchange& e) {
            concurrency->enter();
            std::this_thread::sleep_for(120ms);
            concurrency->leave();
            if (e.request().path.endsWith("/get")) e.reply(404, notFound);
            else e.reply(200, "[]");
        };
        onGet(slow);
        onSearch(slow);
        const auto provider = this->provider({.maxConcurrent = 2});

        std::vector<Lyrics> results(6, Lyrics(u"not run"_s));
        {
            std::vector<std::jthread> calls;
            for (int it = 1; it <= 6; ++it) {
                calls.emplace_back([&, it](std::stop_token stop) { results[it - 1] = provider->find(query({.title = u"Song %1"_s.arg(it)}), stop); });
            }
            for (std::jthread& call : calls) call.join();   // awaitAll (a jthread's destructor would request stop first)
        }

        QCOMPARE(results, std::vector<Lyrics>(6));
        COMPARE_MSG(seen().size(), qsizetype(12), u"every call made its get and its one search"_s);
        COMPARE_MSG(concurrency->peak.load(), 2, u"two requests ran at the same time, never three"_s);
    }

    void theSlotsAreSharedByEveryCallOfOneProviderButNotByTwoProviders() {
        const auto concurrency = std::make_shared<InFlight>();
        onGet([concurrency](Exchange& e) {
            concurrency->enter();
            std::this_thread::sleep_for(150ms);
            concurrency->leave();
            e.reply(404, notFound);
        });
        const auto one = provider({.maxRequests = 1, .maxConcurrent = 1});
        {
            std::vector<std::jthread> calls;
            for (int it = 1; it <= 4; ++it) calls.emplace_back([&](std::stop_token stop) { one->find(query(), stop); });
            for (std::jthread& call : calls) call.join();
        }
        QCOMPARE(concurrency->peak.load(), 1);

        // Kotlin overlaps the two requests inside the same 150 ms sleep. Here each handler instead waits (at most 5 s) until
        // both requests have arrived, so the overlap does not hang on two threads and two connections starting within
        // 150 ms of each other on a loaded machine; with a limit shared by the two providers the second request could not
        // arrive while the first waits, and the peak would stay 1.
        const auto arrived = std::make_shared<Rendezvous>();
        onGet([concurrency, arrived](Exchange& e) {
            concurrency->enter();
            arrived->arriveAndWait(2, 5s);
            concurrency->leave();
            e.reply(404, notFound);
        });
        concurrency->peak = 0;
        const auto a = provider({.maxRequests = 1, .maxConcurrent = 1});
        const auto b = provider({.maxRequests = 1, .maxConcurrent = 1});
        {
            std::jthread first([&](std::stop_token stop) { a->find(query(), stop); });
            std::jthread second([&](std::stop_token stop) { b->find(query(), stop); });
            first.join();
            second.join();
        }
        COMPARE_MSG(concurrency->peak.load(), 2, u"each provider has its own limit"_s);
    }

    // ---- added in the port (qt/tools/test-classes.json) ----

    // A call that waits for a slot (the semaphore of Kotlin) ends when it is stopped, without a request.
    void aCallWaitingForASlotReturnsWhenItIsCancelled() {
        const auto started = std::make_shared<Latch>();
        onGet([started, gate = gate()](Exchange& e) {
            started->countDown();
            gate->wait(10s);
            reply(e, 200, record());
        });
        const auto provider = this->provider({.maxConcurrent = 1, .requestTimeout = 30s});
        std::promise<Lyrics> waitingDone;
        std::future<Lyrics> waitingResult = waitingDone.get_future();
        std::jthread first([&](std::stop_token stop) { provider->find(query(), stop); });
        QVERIFY(started->await(5s));
        std::jthread waiting([&](std::stop_token stop) { waitingDone.set_value(provider->find(query({.title = u"Waiting"_s}), stop)); });
        // Gives the waiting call time to reach the wait for a slot, so that the stop below lands there. If the thread is
        // late, the stop lands before the call starts and it returns without a request all the same: the test still passes,
        // it just proves less (the provider has no hook that tells when a call is waiting).
        std::this_thread::sleep_for(200ms);
        const auto begin = Clock::now();

        waiting.request_stop();
        // A stopped waiter returns within microseconds; 3 s (the bound of the other "promptly" tests) leaves room for a
        // loaded machine without letting a waiter that ignores the stop (it would wait 10 s for the slot) pass.
        QVERIFY(waitingResult.wait_for(3s) == std::future_status::ready);

        QVERIFY(Clock::now() - begin < 3s);
        QCOMPARE(waitingResult.get(), Lyrics());
        COMPARE_MSG(seen().size(), qsizetype(1), u"the waiting call never made its request"_s);
    }

    // A stopped call is not trouble of the service: the next call of the same provider sends its request.
    void aCancelledCallDoesNotStartTheCoolDown() {
        const auto started = std::make_shared<Latch>();
        onGet([started, gate = gate()](Exchange& e) {
            started->countDown();
            gate->wait(10s);
            reply(e, 200, record());
        });
        const auto provider = this->provider({.requestTimeout = 30s});
        std::promise<void> firstDone;
        std::future<void> firstJoined = firstDone.get_future();
        std::jthread first([&](std::stop_token stop) {
            provider->find(query(), stop);
            firstDone.set_value();
        });
        QVERIFY(started->await(5s));
        first.request_stop();
        QVERIFY(firstJoined.wait_for(3s) == std::future_status::ready);
        onGet([](Exchange& e) { reply(e, 200, record({.lyrics = lyricsOf(u"after"_s)})); });

        QCOMPARE(provider->find(query(), {}), Lyrics(lyricsOf(u"after"_s)));
        COMPARE_MSG(seen().size(), qsizetype(2), u"the call after the stopped one sent its request"_s);
    }

    // The brief caps the names of a record (trackName, artistName, albumName) at LrclibMaxNameUnits UTF-16 units: a longer one
    // reads as missing (the matcher's cost grows with the length). A name of 1,001 units that would match is no answer; one of
    // 1,000 units is.
    void aNameLongerThanAThousandUnitsIsReadAsMissing() {
        const QString title = QString(u'a').repeated(xgs::lyrics::LrclibMaxNameUnits);
        const QString longer = title + u"."_s;   // the same title for the matcher (only letters and digits count)
        QCOMPARE(longer.size(), 1001);
        onGet([longer](Exchange& e) { reply(e, 200, record({.track = longer, .lyrics = lyricsOf(u"long"_s)})); });
        onSearch([](Exchange& e) { e.reply(200, "[]"); });

        QCOMPARE(provider()->find(query({.title = title}), {}), Lyrics());

        onGet([title](Exchange& e) { reply(e, 200, record({.track = title, .lyrics = lyricsOf(u"exact"_s)})); });
        QCOMPARE(provider()->find(query({.title = title}), {}), Lyrics(lyricsOf(u"exact"_s)));
    }

    // A body is decoded as Kotlin's String(bytes, UTF_8) decodes it: a malformed byte is U+FFFD.
    void malformedUtf8InABodyIsReadLikeKotlinDoes() {
        QByteArray body = record({.lyrics = u"tag one\ntag @ two\ntag three"_s}).toUtf8();
        body.replace('@', '\xFF');
        onGet([body](Exchange& e) { e.reply(200, body); });

        QCOMPARE(provider()->find(query(), {}), Lyrics(u"tag one\ntag � two\ntag three"_s));
    }

    // Twelve calls, two in flight and ten waiting for a slot, all stopped at once: every one returns within 3 s.
    void manyCallsCancelledAtOnceAllReturn() {
        onGet([gate = gate()](Exchange& e) {
            gate->wait(10s);
            reply(e, 200, record());
        });
        const auto provider = this->provider({.maxConcurrent = 2, .requestTimeout = 30s});
        constexpr int count = 12;
        std::vector<Lyrics> results(count, Lyrics(u"not run"_s));
        std::vector<Clock::time_point> ended(count);
        std::vector<std::jthread> calls;
        for (int i = 0; i < count; ++i) {
            calls.emplace_back([&, i](std::stop_token stop) {
                results[i] = provider->find(query({.title = u"Song %1"_s.arg(i)}), stop);
                ended[i] = Clock::now();
            });
        }
        const auto deadline = Clock::now() + 5s;
        while (seen().size() < 2 && Clock::now() < deadline) std::this_thread::sleep_for(10ms);
        QCOMPARE(seen().size(), 2);
        // Gives the ten other calls time to reach the wait for a slot, so that the stop below lands there. A thread that is
        // late is stopped before it starts and returns without a request all the same: the test still passes, it just proves
        // less (the provider has no hook that tells when a call is waiting).
        std::this_thread::sleep_for(200ms);

        const auto begin = Clock::now();
        for (std::jthread& call : calls) call.request_stop();
        for (std::jthread& call : calls) call.join();

        for (int i = 0; i < count; ++i) {
            COMPARE_MSG(results[i], Lyrics(), u"call %1"_s.arg(i));
            QVERIFY2(ended[i] - begin < 3s, qPrintable(u"call %1"_s.arg(i)));
        }
        QCOMPARE(seen().size(), 2);
    }

    // Kotlin's withTimeoutOrNull gives up at once for a time that is not positive (and the Q2c Task 4 differential agrees for
    // 0 and -1 s): no request is sent and nothing is found. The provider also treats it as trouble (Stop) and starts the
    // cool-down, as Kotlin does, but that cannot be seen from outside: with the same timeout the next call sends nothing
    // either way, so this test checks only that neither call sends anything. On FakeHttpClient: nothing reaches any network.
    void aRequestTimeoutOfZeroSendsNothing() {
        xgs::testing::FakeHttpClient client([](const xgs::net::HttpRequest&, std::stop_token) {
            return xgs::testing::FakeHttpClient::response(200, record().toUtf8());
        });
        LrclibOptions options;
        options.baseUrl = u"http://h/api"_s;
        options.requestTimeout = 0ms;
        options.clock = testClock();
        LrclibLyricsProvider provider(client, options);

        QCOMPARE(provider.find(query(), {}), Lyrics());
        QCOMPARE(client.urls(), QStringList());
        QCOMPARE(provider.find(query(), {}), Lyrics());
        QCOMPARE(client.urls(), QStringList());
    }

    // Found by the Q2c Task 4 differential: kotlinx's booleanOrNull reads `true` and `false` in any case (as
    // String.equals(ignoreCase = true), so `falſe` with U+017F counts too); `1` and `ｔrue` are null. An instrumental flag
    // `TRUE` makes the record no answer, `falſe` and an unknown flag do not. On FakeHttpClient (no network).
    void anInstrumentalFlagIsReadWhateverItsCase() {
        const auto lookUp = [this](const QString& flag) {
            const QByteArray body = record({.lyrics = lyricsOf(u"flag"_s)}).replace(u"\"instrumental\":false"_s, u"\"instrumental\":"_s + flag).toUtf8();
            xgs::testing::FakeHttpClient client([body](const xgs::net::HttpRequest& request, std::stop_token) {
                return request.url.contains(u"/get?"_s) ? xgs::testing::FakeHttpClient::response(200, body)
                                                        : xgs::testing::FakeHttpClient::response(200, "[]");
            });
            LrclibOptions options;
            options.baseUrl = u"http://h/api"_s;
            options.clock = testClock();
            return LrclibLyricsProvider(client, options).find(query(), {});
        };
        QCOMPARE(lookUp(u"TRUE"_s), Lyrics());
        QCOMPARE(lookUp(u"tRuE"_s), Lyrics());
        QCOMPARE(lookUp(u"false"_s), Lyrics(lyricsOf(u"flag"_s)));
        QCOMPARE(lookUp(u"FALSE"_s), Lyrics(lyricsOf(u"flag"_s)));
        QCOMPARE(lookUp(u"falſe"_s), Lyrics(lyricsOf(u"flag"_s)));
        QCOMPARE(lookUp(u"1"_s), Lyrics(lyricsOf(u"flag"_s)));
        QCOMPARE(lookUp(u"ｔrue"_s), Lyrics(lyricsOf(u"flag"_s)));
    }
};

QTEST_GUILESS_MAIN(LrclibLyricsProviderTest)
#include "tst_lrclib_lyrics_provider.moc"
