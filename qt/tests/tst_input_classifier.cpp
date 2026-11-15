#include <QtTest>

#include "input/input_classifier.h"

using xgs::ClassifyResult;
using xgs::ParsedInput;
using xgs::RejectReason;

namespace {

// Kotlin's data class equality, as readable text: Ok(Playlist(id, also)) / Ok(Video(id)) / Rejected(NAME).
QString describe(const ClassifyResult& result) {
    if (!result.ok()) return QStringLiteral("Rejected(") + xgs::rejectName(result.reason) + u')';
    const ParsedInput& input = *result.input;
    if (input.kind == ParsedInput::Kind::Video) return QStringLiteral("Ok(Video(") + input.id + u"))";
    return QStringLiteral("Ok(Playlist(") + input.id + u", " + (input.alsoVideoId ? *input.alsoVideoId : QStringLiteral("null")) + u"))";
}

ClassifyResult ok(const ParsedInput& input) {
    ClassifyResult result;
    result.input = input;
    return result;
}

ClassifyResult rejected(RejectReason reason) {
    ClassifyResult result;
    result.reason = reason;
    return result;
}

ParsedInput playlist(const QString& id, std::optional<QString> alsoVideoId = std::nullopt) {
    return ParsedInput{ParsedInput::Kind::Playlist, id, std::move(alsoVideoId)};
}

ParsedInput video(const QString& id) {
    return ParsedInput{ParsedInput::Kind::Video, id, std::nullopt};
}

}  // namespace

#define ASSERT_RESULT(expected, actual)                                      \
    do {                                                                     \
        const ClassifyResult expectedResult = (expected);                    \
        const ClassifyResult actualResult = (actual);                        \
        QCOMPARE(describe(actualResult), describe(expectedResult));          \
        QCOMPARE(actualResult.ok(), expectedResult.ok());                    \
        if (expectedResult.ok()) QVERIFY(*actualResult.input == *expectedResult.input); \
        else QCOMPARE(int(actualResult.reason), int(expectedResult.reason)); \
    } while (false)

class InputClassifierTest : public QObject {
    Q_OBJECT

    const QString playlistId = QStringLiteral("PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV");
    const QString videoId = QStringLiteral("dQw4w9WgXcQ");

private slots:
    void barePlaylistId() {
        ASSERT_RESULT(ok(playlist(playlistId)), xgs::classifyInput(playlistId));
    }

    void bareVideoId() {
        ASSERT_RESULT(ok(video(videoId)), xgs::classifyInput(videoId));
    }

    void elevenCharIdStartingWithPlIsAVideo() {
        ASSERT_RESULT(ok(video(QStringLiteral("PLxxxxxxxxx"))), xgs::classifyInput(QStringLiteral("PLxxxxxxxxx")));
    }

    void playlistUrl() {
        ASSERT_RESULT(ok(playlist(playlistId)), xgs::classifyInput(u"https://www.youtube.com/playlist?list=" + playlistId));
    }

    void watchUrl() {
        ASSERT_RESULT(ok(video(videoId)), xgs::classifyInput(u"https://www.youtube.com/watch?v=" + videoId));
    }

    void shortUrlsAndShorts() {
        ASSERT_RESULT(ok(video(videoId)), xgs::classifyInput(u"https://youtu.be/" + videoId + u"?si=abc"));
        ASSERT_RESULT(ok(video(videoId)), xgs::classifyInput(u"https://www.youtube.com/shorts/" + videoId));
    }

    void urlWithoutScheme() {
        ASSERT_RESULT(ok(video(videoId)), xgs::classifyInput(u"music.youtube.com/watch?v=" + videoId));
    }

    void watchUrlWithListIsPlaylistAndRemembersVideo() {
        ASSERT_RESULT(ok(playlist(playlistId, videoId)),
                      xgs::classifyInput(u"https://www.youtube.com/watch?v=" + videoId + u"&list=" + playlistId));
    }

    void mixListWithVideoFallsBackToVideo() {
        ASSERT_RESULT(ok(video(videoId)), xgs::classifyInput(u"https://www.youtube.com/watch?v=" + videoId + u"&list=RD" + videoId));
    }

    void mixListAloneIsRejected() {
        ASSERT_RESULT(rejected(RejectReason::MixPlaylist),
                      xgs::classifyInput(QStringLiteral("https://www.youtube.com/playlist?list=RDCLAK5uy_abcdefgh")));
    }

    void blankIsRejected() {
        ASSERT_RESULT(rejected(RejectReason::Empty), xgs::classifyInput(QStringLiteral("   ")));
    }

    void foreignHostsAreRejected() {
        ASSERT_RESULT(rejected(RejectReason::UnsupportedHost), xgs::classifyInput(u"https://evil.com/watch?v=" + videoId));
        ASSERT_RESULT(rejected(RejectReason::UnsupportedHost), xgs::classifyInput(u"https://www.youtube.com.evil.com/watch?v=" + videoId));
        ASSERT_RESULT(rejected(RejectReason::UnsupportedHost), xgs::classifyInput(u"https://youtube.com@evil.com/watch?v=" + videoId));
    }

    void garbageIsRejected() {
        ASSERT_RESULT(rejected(RejectReason::Unrecognized), xgs::classifyInput(QStringLiteral("PLshort")));
        ASSERT_RESULT(rejected(RejectReason::Unrecognized), xgs::classifyInput(QStringLiteral("hello world")));
        ASSERT_RESULT(rejected(RejectReason::UnsupportedHost), xgs::classifyInput(QStringLiteral("PLabc; rm -rf /")));
    }

    void canonicalUrlIsRebuiltFromIdOnly() {
        const ClassifyResult result = xgs::classifyInput(u"https://www.youtube.com/watch?v=" + videoId + u"&list=PLx%20--exec&t=10s");
        QVERIFY(result.ok());
        QCOMPARE(result.input->canonicalUrl(), u"https://www.youtube.com/watch?v=" + videoId);
    }

    void playlistCanonicalUrl() {
        const ClassifyResult result = xgs::classifyInput(playlistId);
        QVERIFY(result.ok());
        QCOMPARE(result.input->canonicalUrl(), u"https://www.youtube.com/playlist?list=" + playlistId);
    }
};

QTEST_APPLESS_MAIN(InputClassifierTest)
#include "tst_input_classifier.moc"
