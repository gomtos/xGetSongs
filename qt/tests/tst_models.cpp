#include <QtTest>

#include "model/models.h"

#include <variant>

using namespace xgs;

class ModelsTest : public QObject {
    Q_OBJECT
private slots:
    void defaultsMatchTheKotlinDataClasses();
    void lyricsOutcomeHasTheKotlinValuesInOrder();
    void jobEventHoldsEachAlternative();
};

void ModelsTest::defaultsMatchTheKotlinDataClasses() {
    const JobOptions options;
    QVERIFY(!options.outputDir.has_value());
    QVERIFY(!options.overwrite);
    QCOMPARE(options.singleRank, 1);
    QVERIFY(options.includeRank);
    QVERIFY(options.searchLyricsOnline);
    QVERIFY(!options.albumName.has_value());

    const ResolvedItem item{.rank = 1, .videoId = QStringLiteral("v"), .title = QStringLiteral("t")};
    QVERIFY(!item.channel.has_value());
    QCOMPARE(item.artist, QString());
    QCOMPARE(item.track, QString());
    QVERIFY(!item.lowConfidence);
    QVERIFY(item.available);
    QVERIFY(!item.unavailableReason.has_value());
    QVERIFY(!item.expectedFileName.has_value());

    const ResolveResult result{.kind = InputKind::Video};
    QCOMPARE(result.resolveId, QString());
    QVERIFY(!result.playlistTitle.has_value());
    QVERIFY(result.items.isEmpty());
    QVERIFY(!result.truncated);
    QVERIFY(!result.alsoVideoId.has_value());

    QVERIFY(JobOptions{} == options);
    QVERIFY(!(item == ResolvedItem{.rank = 2, .videoId = QStringLiteral("v"), .title = QStringLiteral("t")}));
}

void ModelsTest::lyricsOutcomeHasTheKotlinValuesInOrder() {
    // DESCRIPTION, ONLINE, NOT_FOUND, SEARCH_OFF in ApiModels.kt.
    QCOMPARE(static_cast<int>(LyricsOutcome::Description), 0);
    QCOMPARE(static_cast<int>(LyricsOutcome::Online), 1);
    QCOMPARE(static_cast<int>(LyricsOutcome::NotFound), 2);
    QCOMPARE(static_cast<int>(LyricsOutcome::SearchOff), 3);
    QCOMPARE(static_cast<int>(Stage::Downloading), 0);
    QCOMPARE(static_cast<int>(Stage::Finishing), 1);
    QCOMPARE(static_cast<int>(JobStatus::Completed), 0);
    QCOMPARE(static_cast<int>(JobStatus::Cancelled), 1);
    QCOMPARE(static_cast<int>(JobStatus::Failed), 2);
    QCOMPARE(static_cast<int>(InputKind::Playlist), 0);
    QCOMPARE(static_cast<int>(InputKind::Video), 1);
}

void ModelsTest::jobEventHoldsEachAlternative() {
    const JobEvent started = ItemStarted{.rank = 1, .videoId = QStringLiteral("v"), .fileName = QStringLiteral("f")};
    const JobEvent progress = Progress{.rank = 1, .stage = Stage::Downloading};
    const JobEvent done = ItemDone{.rank = 1, .fileName = QStringLiteral("f")};
    const JobEvent skipped = ItemSkipped{.rank = 1, .reason = QStringLiteral("r")};
    const JobEvent failed = ItemFailed{.rank = 1, .message = QStringLiteral("m")};
    const JobEvent finished = JobDone{.status = JobStatus::Completed, .summary = {1, 0, 0}};
    QVERIFY(std::holds_alternative<ItemStarted>(started));
    QVERIFY(std::holds_alternative<Progress>(progress));
    QVERIFY(std::holds_alternative<ItemDone>(done));
    QVERIFY(std::holds_alternative<ItemSkipped>(skipped));
    QVERIFY(std::holds_alternative<ItemFailed>(failed));
    QVERIFY(std::holds_alternative<JobDone>(finished));
    QVERIFY(!std::get<Progress>(progress).percent.has_value());
    QVERIFY(!std::get<ItemDone>(done).lyrics.has_value());
}

QTEST_APPLESS_MAIN(ModelsTest)
#include "tst_models.moc"
