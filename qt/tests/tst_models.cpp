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
    void defaultConstructedValuesAreDeterminate();
    void toolAndQueryTypesHaveOptionalDefaults();
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

// Default-initialised (not value-initialised: `T a;`, not `T a{}`), so a member without an initialiser would be read
// uninitialised here. The next stages build events and summaries from these types and compare or increment their members.
void ModelsTest::defaultConstructedValuesAreDeterminate() {
    ResolvedItem item1;
    ResolvedItem item2;
    QCOMPARE(item1.rank, 0);
    QVERIFY(item1 == item2);

    ResolveResult result1;
    ResolveResult result2;
    QCOMPARE(result1.kind, InputKind::Playlist);
    QVERIFY(result1 == result2);

    JobSummary summary1;
    JobSummary summary2;
    QCOMPARE(summary1.succeeded, 0);
    QCOMPARE(summary1.skipped, 0);
    QCOMPARE(summary1.failed, 0);
    QVERIFY(summary1 == summary2);

    ItemStarted started1;
    ItemStarted started2;
    QCOMPARE(started1.rank, 0);
    QVERIFY(started1 == started2);

    Progress progress1;
    Progress progress2;
    QCOMPARE(progress1.rank, 0);
    QCOMPARE(progress1.stage, Stage::Downloading);
    QVERIFY(progress1 == progress2);

    ItemDone done1;
    ItemDone done2;
    QCOMPARE(done1.rank, 0);
    QVERIFY(done1 == done2);

    ItemSkipped skipped1;
    ItemSkipped skipped2;
    QCOMPARE(skipped1.rank, 0);
    QVERIFY(skipped1 == skipped2);

    ItemFailed failed1;
    ItemFailed failed2;
    QCOMPARE(failed1.rank, 0);
    QVERIFY(failed1 == failed2);

    JobDone jobDone1;
    JobDone jobDone2;
    QCOMPARE(jobDone1.status, JobStatus::Completed);
    QCOMPARE(jobDone1.summary.succeeded, 0);
    QVERIFY(jobDone1 == jobDone2);

    ToolInfo tool1;
    ToolInfo tool2;
    QVERIFY(!tool1.found);
    QVERIFY(tool1 == tool2);

    ToolsStatus status1;
    ToolsStatus status2;
    QVERIFY(!status1.ytDlp.found && !status1.ffmpeg.found && !status1.jsRuntime.found);
    QVERIFY(status1 == status2);

    // a counter built on a default value counts from 0
    JobSummary counted;
    ++counted.succeeded;
    QCOMPARE(counted, (JobSummary{1, 0, 0}));
}

void ModelsTest::toolAndQueryTypesHaveOptionalDefaults() {
    const ToolPaths paths;
    QVERIFY(!paths.ytDlp.has_value());
    QVERIFY(!paths.ffmpeg.has_value());
    QVERIFY(!paths.jsRuntime.has_value());
    QVERIFY(paths == ToolPaths{});
    QVERIFY(!(paths == ToolPaths{.ffmpeg = QStringLiteral("C:\\ffmpeg.exe")}));

    const ToolInfo info;
    QVERIFY(!info.found);
    QVERIFY(!info.version.has_value());
    QVERIFY(!info.path.has_value());
    QVERIFY(!(info == ToolInfo{.found = true}));

    const ToolsStatus status;
    QVERIFY(status == ToolsStatus{});
    QVERIFY(!(status == ToolsStatus{.ffmpeg = {.found = true, .version = QStringLiteral("7.1")}}));

    const LyricsQuery query;
    QCOMPARE(query.artist, QString());
    QCOMPARE(query.title, QString());
    QVERIFY(!query.album.has_value());
    QVERIFY(!query.durationSeconds.has_value());
    QVERIFY(query == LyricsQuery{});
    QVERIFY(!(query == LyricsQuery{.durationSeconds = 0}));
}

QTEST_APPLESS_MAIN(ModelsTest)
#include "tst_models.moc"
