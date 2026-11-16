#include <QtTest>

#include <initializer_list>

#include "text/kt_text.h"
#include "ytdlp/error_classifier.h"

using xgs::ytdlp::Failure;
using xgs::ytdlp::FailureKind;
using namespace Qt::StringLiterals;

namespace {

Failure classify(std::initializer_list<QString> lines) {
    return xgs::ytdlp::classifyError(QStringList(lines));
}

const char* kindName(FailureKind kind) {
    switch (kind) {
        case FailureKind::Unavailable: return "UNAVAILABLE";
        case FailureKind::Transient: return "TRANSIENT";
        case FailureKind::Fatal: return "FATAL";
        case FailureKind::Other: return "OTHER";
    }
    return "?";
}

}  // namespace

// Kotlin's assertEquals(expectedKind, actual): QCOMPARE would need a QTest::toString for the enum.
#define ASSERT_KIND(expected, actual)                                                                          \
    do {                                                                                                       \
        const FailureKind expectedKind = (expected);                                                           \
        const FailureKind actualKind = (actual);                                                               \
        QVERIFY2(expectedKind == actualKind,                                                                   \
                 qPrintable(u"expected kind %1 but was %2"_s.arg(QLatin1StringView(kindName(expectedKind)),    \
                                                                  QLatin1StringView(kindName(actualKind))))); \
    } while (false)

class ErrorClassifierTest : public QObject {
    Q_OBJECT

private slots:
    void privateVideoIsUnavailable() {
        const Failure failure =
            classify({u"ERROR: [youtube] abc: Private video. Sign in if you've been granted access to this video"_s});
        ASSERT_KIND(FailureKind::Unavailable, failure.kind);
        QCOMPARE(failure.message, u"비공개 영상"_s);
    }

    void geoBlockedAndAgeRestrictedAreUnavailable() {
        QCOMPARE(classify({u"ERROR: The uploader has not made this video available in your country"_s}).message,
                 u"지역 제한 영상"_s);
        ASSERT_KIND(FailureKind::Unavailable, classify({u"ERROR: [youtube] abc: Sign in to confirm your age"_s}).kind);
        ASSERT_KIND(FailureKind::Unavailable, classify({u"ERROR: [youtube] abc: Video unavailable"_s}).kind);
    }

    void rateLimitAndNetworkErrorsAreTransient() {
        ASSERT_KIND(FailureKind::Transient,
                    classify({u"ERROR: unable to download video data: HTTP Error 429: Too Many Requests"_s}).kind);
        ASSERT_KIND(FailureKind::Transient, classify({u"ERROR: [Errno 11001] getaddrinfo failed"_s}).kind);
        ASSERT_KIND(FailureKind::Transient, classify({u"WARNING: The read operation timed out"_s}).kind);
    }

    void diskAndPermissionErrorsAreFatal() {
        ASSERT_KIND(FailureKind::Fatal, classify({u"OSError: [Errno 28] No space left on device"_s}).kind);
        ASSERT_KIND(FailureKind::Fatal, classify({u"PermissionError: [Errno 13] Permission denied: 'x.mp3'"_s}).kind);
    }

    void aVideoWithoutAnM4aStreamFailsWithAClearReason() {
        const Failure failure = classify(
            {u"ERROR: [youtube] abc: Requested format is not available. Use --list-formats for a list of available formats"_s});

        ASSERT_KIND(FailureKind::Other, failure.kind);
        QCOMPARE(failure.message, u"m4a 오디오 형식이 없습니다."_s);
    }

    void otherClassificationsComeBeforeTheMissingFormat() {
        ASSERT_KIND(FailureKind::Unavailable,
                    classify({u"ERROR: [youtube] abc: Video unavailable. Requested format is not available"_s}).kind);
        ASSERT_KIND(FailureKind::Transient,
                    classify({u"ERROR: unable to download video data: HTTP Error 503"_s,
                              u"Requested format is not available"_s}).kind);
    }

    void fatalWinsOverTransient() {
        ASSERT_KIND(FailureKind::Fatal,
                    classify({u"ERROR: unable to download video data"_s, u"OSError: [Errno 28] No space left on device"_s}).kind);
    }

    void unknownErrorsKeepTheErrorLine() {
        const Failure failure =
            classify({u"[youtube] abc: Downloading webpage"_s, u"ERROR: [youtube] abc: Something odd happened"_s});
        ASSERT_KIND(FailureKind::Other, failure.kind);
        QCOMPARE(failure.message, u"[youtube] abc: Something odd happened"_s);
    }

    void botCheckIsNotTreatedAsUnavailable() {
        const Failure failure = classify({u"ERROR: [youtube] abc: Sign in to confirm you're not a bot"_s});
        ASSERT_KIND(FailureKind::Other, failure.kind);
    }

    void emptyStderrStillGivesAMessage() {
        // classify() with no lines: isNotBlank() of the message
        const Failure failure = classify({});
        QVERIFY(!xgs::kt::isBlank(failure.message));
    }

    void rateLimitedSessionIsTransientEvenThoughItSaysVideoUnavailable() {
        const Failure failure = classify(
            {u"ERROR: [youtube] abc: Video unavailable. This content isn't available, try again later. "
             u"The current session has been rate-limited by YouTube for up to an hour."_s});
        ASSERT_KIND(FailureKind::Transient, failure.kind);
        QVERIFY(xgs::kt::contains(failure.message, u"rate-limited"));
    }

    void plainVideoUnavailableIsStillUnavailable() {
        ASSERT_KIND(FailureKind::Unavailable, classify({u"ERROR: [youtube] abc: Video unavailable"_s}).kind);
    }

    void fatalStillWinsOverRateLimit() {
        ASSERT_KIND(FailureKind::Fatal,
                    classify({u"ERROR: try again later"_s, u"OSError: [Errno 28] No space left on device"_s}).kind);
    }
};

QTEST_APPLESS_MAIN(ErrorClassifierTest)
#include "tst_error_classifier.moc"
