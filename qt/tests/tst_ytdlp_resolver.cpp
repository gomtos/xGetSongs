#include <QtTest>

#include <QElapsedTimer>

#include <optional>
#include <stop_token>

#include "input/input_classifier.h"
#include "support/fake_process_runner.h"
#include "support/test_tools.h"
#include "ytdlp/ytdlp_resolver.h"

using namespace Qt::StringLiterals;
using xgs::InputKind;
using xgs::Outcome;
using xgs::OutcomeStatus;
using xgs::ResolvedItem;
using xgs::ResolveResult;
using xgs::ToolPaths;
using xgs::process::LineHandler;
using xgs::testsupport::FakeProcessRunner;
using xgs::testsupport::StartFailure;
using xgs::testsupport::testTools;
using xgs::testsupport::toolsOf;
using xgs::ytdlp::VideoMeta;
using xgs::ytdlp::YtDlpResolver;

namespace {

const QString playlistId = u"PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"_s;
const QString playlistUrl = u"https://www.youtube.com/playlist?list="_s + playlistId;

// The Kotlin test's raw string after trimIndent()
const QString playlistJson =
    u"{\"_type\":\"playlist\",\"id\":\""_s + playlistId + u"\",\"title\":\"Melon Daily Top 100\",\"entries\":[\n"
    u"  {\"_type\":\"url\",\"ie_key\":\"Youtube\",\"id\":\"vid00000001\",\"title\":\"소연 (SOYEON) '퇴사할게여 (Narr. 기안84)' Official Music Video\",\"channel\":\"i-dle (아이들)\",\"uploader\":\"i-dle (아이들)\",\"availability\":null},\n"
    u"  {\"id\":\"vid00000002\",\"title\":\"[Private video]\",\"channel\":null,\"availability\":\"needs_auth\"},\n"
    u"  {\"id\":\"vid00000003\",\"title\":\"[Deleted video]\"},\n"
    u"  {\"id\":\"vid00000004\",\"title\":\"Dynamite\",\"channel\":\"BTS - Topic\"}\n"
    u"]}"_s;

// runnerReturning of the Kotlin test: stdout's lines (String.lines()), then the stderr lines, then the exit code.
FakeProcessRunner runnerReturning(const QString& stdoutText, int exitCode = 0, const QStringList& stderrLines = {}) {
    return FakeProcessRunner([=](const QStringList&, const LineHandler& out, const LineHandler& err) {
        for (const QString& line : xgs::testsupport::kotlinLines(stdoutText)) out(line);
        for (const QString& line : stderrLines) err(line);
        return exitCode;
    });
}

// The runner of the Kotlin tests whose lambda throws IOException("Cannot run program").
FakeProcessRunner runnerThatCannotStart() {
    return FakeProcessRunner([](const QStringList&, const LineHandler&, const LineHandler&) -> int {
        throw StartFailure{u"Cannot run program"_s};
    });
}

ResolveResult resolveOk(FakeProcessRunner& runner, const QString& input, const ToolPaths& tools = testTools()) {
    const auto provider = toolsOf(tools);
    const Outcome<ResolveResult> outcome = YtDlpResolver(runner, provider).resolve(input, {});
    if (!outcome.isOk()) qWarning("resolve failed: %s", qPrintable(outcome.error));
    return outcome.value.value_or(ResolveResult{});
}

Outcome<ResolveResult> resolveOutcome(FakeProcessRunner& runner, const QString& input, const ToolPaths& tools = testTools()) {
    const auto provider = toolsOf(tools);
    return YtDlpResolver(runner, provider).resolve(input, {});
}

std::optional<VideoMeta> fetchOf(FakeProcessRunner& runner, const QString& videoId) {
    const auto provider = toolsOf();
    return YtDlpResolver(runner, provider).fetch(videoId, {});
}

QList<int> ranksOf(const QList<ResolvedItem>& items) {
    QList<int> ranks;
    for (const ResolvedItem& item : items) ranks.append(item.rank);
    return ranks;
}

}  // namespace

// Port of YtDlpResolverTest.kt. `assertFailsWith<ResolveException>` is a Failed outcome (and its message where Kotlin
// compares it); Kotlin's runner that throws IOException is a FakeProcessRunner whose handler throws StartFailure.
class YtDlpResolverTest : public QObject {
    Q_OBJECT

private slots:
    void resolvesAPlaylistIntoRankedItems() {
        FakeProcessRunner runner = runnerReturning(playlistJson);

        const ResolveResult response = resolveOk(runner, playlistUrl);

        QCOMPARE(response.kind, InputKind::Playlist);
        QCOMPARE(response.playlistTitle, std::optional<QString>(u"Melon Daily Top 100"_s));
        QCOMPARE(ranksOf(response.items), (QList<int>{1, 2, 3, 4}));
        const ResolvedItem first = response.items.at(0);
        QCOMPARE(first.videoId, u"vid00000001"_s);
        QCOMPARE(first.artist, u"소연 (SOYEON)"_s);
        QCOMPARE(first.track, u"퇴사할게여 (Narr. 기안84)"_s);
        QCOMPARE(first.expectedFileName, std::optional<QString>(u"001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).m4a"_s));
        QVERIFY(!first.lowConfidence);
        QVERIFY(first.available);
    }

    void unavailableEntriesKeepTheirRankButHaveNoFileName() {
        FakeProcessRunner runner = runnerReturning(playlistJson);
        const QList<ResolvedItem> items = resolveOk(runner, playlistUrl).items;
        QCOMPARE(items.size(), 4);

        QVERIFY(!items.at(1).available);
        QCOMPARE(items.at(1).unavailableReason, std::optional<QString>(u"비공개 영상"_s));
        QCOMPARE(items.at(1).expectedFileName, std::nullopt);
        QCOMPARE(items.at(2).unavailableReason, std::optional<QString>(u"삭제된 영상"_s));
        QCOMPARE(items.at(2).rank, 3);
    }

    void channelFallbackIsMarkedLowConfidence() {
        FakeProcessRunner runner = runnerReturning(playlistJson);
        const QList<ResolvedItem> items = resolveOk(runner, playlistUrl).items;
        QCOMPARE(items.size(), 4);
        const ResolvedItem item = items.at(3);

        QVERIFY(item.lowConfidence);
        QCOMPARE(item.artist, u"BTS"_s);
        QCOMPARE(item.expectedFileName, std::optional<QString>(u"004 BTS - Dynamite.m4a"_s));
    }

    void playlistCommandUsesTheCanonicalUrlAndFlatListing() {
        FakeProcessRunner runner = runnerReturning(playlistJson);

        resolveOk(runner, u"  "_s + playlistId + u"  "_s);

        QCOMPARE(runner.commands().size(), 1);
        const QStringList command = runner.commands().at(0);
        QVERIFY(command.contains(u"--flat-playlist"_s));
        QCOMPARE(command.last(), playlistUrl);
    }

    void playlistsLongerThan999AreTruncated() {
        QStringList entries;
        for (int it = 1; it <= 1001; ++it)
            entries.append(u"{\"id\":\"v%1\",\"title\":\"A - B%2\"}"_s.arg(it, 10, 10, QChar(u'0')).arg(it));
        FakeProcessRunner runner = runnerReturning(u"{\"title\":\"Big\",\"entries\":["_s + entries.join(u',') + u"]}"_s);

        const ResolveResult response = resolveOk(runner, playlistUrl);

        QCOMPARE(response.items.size(), 999);
        QVERIFY(response.truncated);
        QCOMPARE(response.items.last().rank, 999);
    }

    void watchUrlWithListRemembersTheVideoId() {
        FakeProcessRunner runner = runnerReturning(playlistJson);

        const ResolveResult response = resolveOk(runner, u"https://www.youtube.com/watch?v=dQw4w9WgXcQ&list="_s + playlistId);

        QCOMPARE(response.alsoVideoId, std::optional<QString>(u"dQw4w9WgXcQ"_s));
    }

    void resolvesASingleVideoAsRankOne() {
        FakeProcessRunner runner = runnerReturning(
            u"{\"id\":\"dQw4w9WgXcQ\",\"title\":\"Rick Astley - Never Gonna Give You Up (Official Video)\",\"channel\":\"Rick Astley\",\"artist\":\"Rick Astley\",\"track\":\"Never Gonna Give You Up\"}"_s);

        const ResolveResult response = resolveOk(runner, u"https://youtu.be/dQw4w9WgXcQ"_s);

        QCOMPARE(response.kind, InputKind::Video);
        QCOMPARE(response.items.size(), 1);
        const ResolvedItem item = response.items.at(0);
        QCOMPARE(item.rank, 1);
        QCOMPARE(item.expectedFileName, std::optional<QString>(u"001 Rick Astley - Never Gonna Give You Up.m4a"_s));
        QCOMPARE(runner.commands().size(), 1);
        QCOMPARE(runner.commands().at(0).last(), u"https://www.youtube.com/watch?v=dQw4w9WgXcQ"_s);
    }

    void invalidInputIsRejectedWithoutRunningAnything() {
        FakeProcessRunner runner = runnerReturning(u"{}"_s);

        const Outcome<ResolveResult> error = resolveOutcome(runner, u"https://evil.com/x"_s);

        QCOMPARE(error.status, OutcomeStatus::Failed);
        QCOMPARE(error.error, xgs::rejectMessage(xgs::RejectReason::UnsupportedHost));
        QVERIFY(runner.commands().isEmpty());
    }

    void missingYtDlpGivesAHelpfulError() {
        FakeProcessRunner runner = runnerReturning(u"{}"_s);
        ToolPaths tools = testTools();
        tools.ytDlp = std::nullopt;

        const Outcome<ResolveResult> error = resolveOutcome(runner, playlistUrl, tools);

        QCOMPARE(error.status, OutcomeStatus::Failed);
        QVERIFY(error.error.contains(u"yt-dlp"_s));
    }

    void ytDlpFailureBecomesAResolveException() {
        FakeProcessRunner runner =
            runnerReturning(QString(), 1, {u"ERROR: [youtube:tab] "_s + playlistId + u": The playlist does not exist."_s});

        const Outcome<ResolveResult> error = resolveOutcome(runner, playlistUrl);

        QCOMPARE(error.status, OutcomeStatus::Failed);
        QVERIFY(error.error.contains(u"playlist does not exist"_s));
    }

    void privateVideoErrorIsTranslated() {
        FakeProcessRunner runner =
            runnerReturning(QString(), 1, {u"ERROR: [youtube] abc: Private video. Sign in if you've been granted access"_s});

        const Outcome<ResolveResult> error = resolveOutcome(runner, u"dQw4w9WgXcQ"_s);

        QCOMPARE(error.status, OutcomeStatus::Failed);
        QCOMPARE(error.error, u"비공개 영상"_s);
    }

    void unreadableOutputIsAResolveException() {
        FakeProcessRunner runner = runnerReturning(u"not json"_s);

        const Outcome<ResolveResult> error = resolveOutcome(runner, playlistUrl);

        QCOMPARE(error.status, OutcomeStatus::Failed);
        // Kotlin's test only asserts the exception type; the message is the one its parse() throws (observed, kind P)
        QCOMPARE(error.error, u"yt-dlp 응답을 해석할 수 없습니다."_s);
    }

    void fetchReturnsFullMetadata() {
        FakeProcessRunner runner = runnerReturning(
            u"{\"id\":\"dQw4w9WgXcQ\",\"title\":\"Dynamite\",\"channel\":\"BTS - Topic\",\"artist\":\"BTS\",\"track\":\"Dynamite\"}"_s);

        const std::optional<VideoMeta> meta = fetchOf(runner, u"dQw4w9WgXcQ"_s);

        QVERIFY(meta.has_value());
        QCOMPARE(meta->artist, std::optional<QString>(u"BTS"_s));
        QCOMPARE(meta->track, std::optional<QString>(u"Dynamite"_s));
        QCOMPARE(meta->channel, std::optional<QString>(u"BTS - Topic"_s));
    }

    void fetchReturnsNullWhenYtDlpFails() {
        FakeProcessRunner runner = runnerReturning(QString(), 1, {u"ERROR: boom"_s});
        QCOMPARE(fetchOf(runner, u"dQw4w9WgXcQ"_s), std::nullopt);
    }

    void nonPublicAvailabilityMakesAnItemUnavailableAndKeepsItsRank() {
        FakeProcessRunner runner = runnerReturning(
            u"{\"title\":\"T\",\"entries\":[\n"
            u"              {\"id\":\"vid00000001\",\"title\":\"Song A\",\"availability\":\"premium_only\"},\n"
            u"              {\"id\":\"vid00000002\",\"title\":\"Song B\",\"availability\":\"subscriber_only\"},\n"
            u"              {\"id\":\"vid00000003\",\"title\":\"Song C\",\"availability\":\"needs_auth\"},\n"
            u"              {\"id\":\"vid00000004\",\"title\":\"Artist - Song D\",\"availability\":\"public\"},\n"
            u"              {\"id\":\"vid00000005\",\"title\":\"Artist - Song E\",\"availability\":\"unlisted\"}\n"
            u"            ]}"_s);

        const QList<ResolvedItem> items = resolveOk(runner, playlistUrl).items;

        QCOMPARE(ranksOf(items), (QList<int>{1, 2, 3, 4, 5}));
        const QString availabilities[] = {u"premium_only"_s, u"subscriber_only"_s, u"needs_auth"_s};
        for (int index = 0; index < 3; ++index) {
            const ResolvedItem& item = items.at(index);
            QVERIFY(!item.available);
            QCOMPARE(item.expectedFileName, std::nullopt);
            QVERIFY(item.unavailableReason.has_value());
            QVERIFY(item.unavailableReason->contains(availabilities[index]));
        }
        for (const ResolvedItem& item : {items.at(3), items.at(4)}) {
            QVERIFY(item.available);
            QVERIFY(item.expectedFileName.has_value());
        }
    }

    void ytDlpThatCannotBeStartedBecomesAResolveException() {
        FakeProcessRunner runner = runnerThatCannotStart();

        const Outcome<ResolveResult> error = resolveOutcome(runner, playlistUrl);

        QCOMPARE(error.status, OutcomeStatus::Failed);
        QVERIFY(error.error.contains(u"yt-dlp"_s));
    }

    void fetchReturnsNullWhenYtDlpCannotBeStarted() {
        FakeProcessRunner runner = runnerThatCannotStart();

        QCOMPARE(fetchOf(runner, u"dQw4w9WgXcQ"_s), std::nullopt);
    }

    // Added (no Kotlin counterpart): where Kotlin's coroutine would be cancelled, resolve is Cancelled and fetch gives nothing.
    void aCancelledRunIsCancelledAndFetchesNothing() {
        std::stop_source source;
        FakeProcessRunner runner([&source](const QStringList&, const LineHandler& out, const LineHandler&) {
            out(u"{\"id\":\"dQw4w9WgXcQ\",\"title\":\"A - B\"}"_s);
            source.request_stop();
            return 0;
        });
        const auto provider = toolsOf();
        YtDlpResolver resolver(runner, provider);

        QCOMPARE(resolver.resolve(playlistUrl, source.get_token()).status, OutcomeStatus::Cancelled);
        std::stop_source second;
        FakeProcessRunner fetchRunner([&second](const QStringList&, const LineHandler& out, const LineHandler&) {
            out(u"{\"id\":\"dQw4w9WgXcQ\",\"title\":\"A - B\"}"_s);
            second.request_stop();
            return 0;
        });
        QCOMPARE(YtDlpResolver(fetchRunner, provider).fetch(u"dQw4w9WgXcQ"_s, second.get_token()), std::nullopt);
        QCOMPARE(runner.commands().size(), 1);
        QCOMPARE(fetchRunner.commands().size(), 1);
    }

    // Added: exactly MAX_RANK entries are all listed and not truncated (Kotlin: `entries.size > MAX_RANK`).
    void aPlaylistOfExactly999IsNotTruncated() {
        QStringList entries;
        for (int it = 1; it <= 999; ++it) entries.append(u"{\"id\":\"v%1\",\"title\":\"A - B%2\"}"_s.arg(it, 10, 10, QChar(u'0')).arg(it));
        FakeProcessRunner runner = runnerReturning(u"{\"title\":\"Big\",\"entries\":["_s + entries.join(u',') + u"]}"_s);

        const ResolveResult response = resolveOk(runner, playlistUrl);

        QCOMPARE(response.items.size(), 999);
        QVERIFY(!response.truncated);
    }

    // Added: a playlist of 5,000 entries arrives as one line of several megabytes; collecting and reading it is not quadratic.
    void resolvesAFiveThousandEntryPlaylistQuickly() {
        QString json = u"{\"_type\":\"playlist\",\"title\":\"Big\",\"entries\":["_s;
        for (int it = 1; it <= 5000; ++it) {
            if (it > 1) json += u',';
            json += u"{\"_type\":\"url\",\"ie_key\":\"Youtube\",\"id\":\"v%1\",\"url\":\"https://www.youtube.com/watch?v=v%1\",\"title\":\"Artist %1 - Song %1 (Official Video)\",\"description\":null,\"duration\":215.0,\"channel_id\":\"UCxxxxxxxxxxxxxxxxxxxxxx\",\"channel\":\"Channel %1\",\"channel_url\":\"https://www.youtube.com/channel/UCxxxxxxxxxxxxxxxxxxxxxx\",\"uploader\":\"Channel %1\",\"thumbnails\":[{\"url\":\"https://i.ytimg.com/vi/x/hqdefault.jpg\",\"height\":94,\"width\":168},{\"url\":\"https://i.ytimg.com/vi/x/hqdefault.jpg\",\"height\":110,\"width\":196}],\"view_count\":123456,\"live_status\":null,\"availability\":null}"_s.arg(it, 10, 10, QChar(u'0'));
        }
        json += u"]}"_s;
        QVERIFY(json.size() > 3'000'000);
        FakeProcessRunner runner = runnerReturning(json);

        QElapsedTimer timer;
        timer.start();
        const ResolveResult response = resolveOk(runner, playlistUrl);
        const qint64 elapsed = timer.elapsed();
#ifdef NDEBUG
        // The time limit is about the optimized build; an unoptimized Debug build with Qt's checks on is many times slower.
        QVERIFY2(elapsed < 3000, qPrintable(u"resolving %1 units took %2 ms"_s.arg(json.size()).arg(elapsed)));
#else
        Q_UNUSED(elapsed);
#endif
        QCOMPARE(response.items.size(), 999);
        QVERIFY(response.truncated);
        QCOMPARE(response.items.at(998).expectedFileName, std::optional<QString>(u"999 Artist 0000000999 - Song 0000000999.m4a"_s));
    }
};

QTEST_APPLESS_MAIN(YtDlpResolverTest)
#include "tst_ytdlp_resolver.moc"
