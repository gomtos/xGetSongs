#include <QtTest>

#include <memory>
#include <optional>

#include "app_state_holder.h"
#include "fake_app_api.h"
#include "input/input_classifier.h"
#include "labels.h"

// The tests of shell/tests/holder.test.ts (the scenarios of the Compose app's AppStateHolderTest.kt plus what the TypeScript
// port added), one slot per test and under the same name, every assertion kept. The TypeScript `await flush()` is
// `api.flush()`, `timers.advance` is FakeScheduler::advance, and the test time source is `clock`.
//
// What the C++ engine interface changes about the assertions: there is no resolve ID and no ranks list in a job request; the
// holder sends the items to download themselves (an engine of its own process needs nothing else). So `request.ranks` is
// asserted as the ranks of `request.items` (null: every available item, with the single video's rank), and
// `request.resolveId` as the items of that lookup. Job IDs do not exist either: FakeAppApi names the job it cancels after
// the number of jobs started (`job-2` is the second), which is the ID the TypeScript fake handed out.

using namespace Qt::StringLiterals;
using namespace xgs::ui;
using namespace xgs::ui::test;
using xgs::InputKind;
using xgs::JobEvent;
using xgs::JobOptions;
using xgs::JobStatus;
using xgs::JobSummary;
using xgs::LyricsOutcome;
using xgs::RejectReason;
using xgs::ResolvedItem;
using xgs::ResolveResult;
using xgs::Stage;

namespace {

const QString playlistId = u"PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"_s;

std::optional<QString> some(const QString& text) {
    return text;
}

const std::optional<QString> none;

// ---- the rig ---------------------------------------------------------------------------

/// The holder with a fake engine, fake timers and a time source the test moves. Without a store of the test's it gets one
/// that loads the defaults (the TypeScript `noSettingsStore`; what it records is never looked at).
class Rig {
public:
    explicit Rig(FakeSettingsStore* external = nullptr)
        : store(external ? *external : m_ownStore),
          holder(api, this->store, [this] { return clock; }, timers) {}

    /// Resolves the sample playlist (or whatever the api answers): the TypeScript `resolved()`.
    void resolve() {
        holder.onInput(playlistId);
        holder.resolve();
        api.flush();
    }

    /// Resolved, started, and ended with rank 1 done and rank 3 failed ("boom").
    void finishWithOneFailure();

    FakeAppApi api;
    FakeScheduler timers;
    /// The time source of the holder: the test moves it.
    qint64 clock = 0;

private:
    FakeSettingsStore m_ownStore;

public:
    SettingsStore& store;
    AppStateHolder holder;
};

const UiState& state(const AppStateHolder& holder) {
    return holder.state();
}

ItemRow row(const AppStateHolder& holder, int rank) {
    for (const ItemRow& candidate : holder.state().rows) {
        if (candidate.item.rank == rank) return candidate;
    }
    QTest::qFail(qPrintable(u"no row of rank %1"_s.arg(rank)), __FILE__, __LINE__);
    return ItemRow();
}

QList<int> ranksOf(const QList<ItemRow>& rows) {
    QList<int> ranks;
    for (const ItemRow& r : rows) ranks.append(r.item.rank);
    return ranks;
}

QList<ItemStatus> statusesOf(const QList<ItemRow>& rows) {
    QList<ItemStatus> statuses;
    for (const ItemRow& r : rows) statuses.append(r.status);
    return statuses;
}

/// The ranks a request asks for: the TypeScript `request.ranks`, with null spelled out as every available item.
QList<int> ranksOf(const JobRequest& request) {
    QList<int> ranks;
    for (const ResolvedItem& i : request.items) ranks.append(i.rank);
    return ranks;
}

const JobOptions& optionsOf(const JobRequest& request) {
    return request.options;
}

const JobRequest& lastRequest(const FakeAppApi& api) {
    return api.jobRequests.last();
}

// ---- the events a job sends ----------------------------------------------------------------

JobEvent itemStarted(int rank, const QString& videoId, const QString& fileName) {
    return xgs::ItemStarted{rank, videoId, fileName};
}

JobEvent progress(int rank, Stage stage, std::optional<double> percent = std::nullopt) {
    return xgs::Progress{rank, stage, percent};
}

JobEvent itemDone(int rank, const QString& fileName, std::optional<LyricsOutcome> lyrics = std::nullopt) {
    return xgs::ItemDone{rank, fileName, lyrics};
}

JobEvent itemSkipped(int rank, const QString& reason) {
    return xgs::ItemSkipped{rank, reason};
}

JobEvent itemFailed(int rank, const QString& message) {
    return xgs::ItemFailed{rank, message};
}

JobEvent jobDone(JobStatus status, int succeeded, int skipped, int failed) {
    return xgs::JobDone{status, JobSummary{succeeded, skipped, failed}};
}

void Rig::finishWithOneFailure() {
    resolve();
    holder.startDownload();
    api.flush();
    api.send(itemDone(1, u"001 A1 - T1.m4a"_s), itemFailed(3, u"boom"_s), jobDone(JobStatus::Completed, 1, 0, 1));
    api.closeEvents();
    api.flush();
}

// ---- the statuses a row can have -----------------------------------------------------------

const ItemStatus ready = ItemStatus::ready();
const ItemStatus waiting = ItemStatus::waiting();
const ItemStatus finishing = ItemStatus::finishing();

ItemStatus downloading(std::optional<double> percent) {
    return ItemStatus::downloading(percent);
}

ItemStatus done(std::optional<LyricsOutcome> lyrics = std::nullopt) {
    return ItemStatus::done(lyrics);
}

ItemStatus skipped(const QString& reason) {
    return ItemStatus::skipped(reason);
}

ItemStatus failed(const QString& message) {
    return ItemStatus::failed(message);
}

// ---- remembered options ---------------------------------------------------------------

/// What the holder saves when only the folder differs from the defaults of a fresh FakeSettingsStore.
UserSettings defaultsWith(std::optional<QString> outputDir = u"C:/Music/xGetSongs"_s, bool overwrite = false, bool includeRank = true,
                          bool searchLyricsOnline = true) {
    return UserSettings{std::move(outputDir), overwrite, includeRank, searchLyricsOnline};
}

UserSettings defaultsWithOverwrite(bool overwrite) {
    return defaultsWith(u"C:/Music/xGetSongs"_s, overwrite);
}

UserSettings defaultsWithIncludeRank(bool includeRank) {
    return defaultsWith(u"C:/Music/xGetSongs"_s, false, includeRank);
}

UserSettings defaultsWithSearch(bool searchLyricsOnline) {
    return defaultsWith(u"C:/Music/xGetSongs"_s, false, true, searchLyricsOnline);
}

UserSettings stored(std::optional<QString> outputDir, bool overwrite = false, bool includeRank = true, bool searchLyricsOnline = true) {
    return UserSettings{std::move(outputDir), overwrite, includeRank, searchLyricsOnline};
}

#define ASSERT_SAVED_ONLY_AFTER_THE_QUIET_PERIOD(timers, store, api, expected)                     \
    do {                                                                                            \
        (timers).advance(399);                                                                      \
        (api).flush();                                                                              \
        QVERIFY2((store).saved.isEmpty(), "nothing is saved before 400 ms have passed");            \
        (timers).advance(1);                                                                        \
        (api).flush();                                                                              \
        QCOMPARE((store).saved, QList<UserSettings>{expected});                                     \
    } while (false)

}  // namespace

class AppStateHolderTest : public QObject {
    Q_OBJECT

private slots:
    // ---- resolving ------------------------------------------------------------------------

    void startsIdleWithTheDefaultOutputFolder() {
        Rig r;

        QCOMPARE(state(r.holder).phase, Phase::Idle);
        QCOMPARE(state(r.holder).outputDir, u"C:/Music/xGetSongs"_s);
    }

    void resolveShowsAPreviewOfEveryItem() {
        Rig r;
        r.resolve();

        QCOMPARE(state(r.holder).phase, Phase::Preview);
        QCOMPARE(r.api.resolveInputs, QStringList{playlistId});
        QCOMPARE(ranksOf(state(r.holder).rows), (QList<int>{1, 2, 3}));
        QCOMPARE(row(r.holder, 1).status, ready);
        QCOMPARE(row(r.holder, 2).status, skipped(u"비공개 영상"_s));
        QCOMPARE(row(r.holder, 1).fileName, some(u"001 A1 - T1.m4a"_s));
    }

    void invalidInputIsRejectedBeforeAnyRequest() {
        Rig r;
        r.holder.onInput(u"https://evil.com/x"_s);

        r.holder.resolve();
        r.api.flush();

        QCOMPARE(state(r.holder).error, some(xgs::rejectMessage(RejectReason::UnsupportedHost)));
        QCOMPARE(r.api.resolveInputs.size(), 0);
        QCOMPARE(state(r.holder).phase, Phase::Idle);
    }

    void serverErrorsAreShownAndTheScreenStaysUsable() {
        Rig r;
        r.api.resolveError = apiError(u"비공개 재생목록"_s);
        r.resolve();

        QCOMPARE(state(r.holder).error, some(u"비공개 재생목록"_s));
        QCOMPARE(state(r.holder).phase, Phase::Idle);
    }

    void switchingToTheVideoOnlyResolvesTheVideoUrl() {
        Rig r;
        r.api.resolveResponse = playlist(u"dQw4w9WgXcQ"_s);
        r.resolve();

        r.holder.switchToVideoOnly();
        r.api.flush();

        QCOMPARE(r.api.resolveInputs.last(), u"https://www.youtube.com/watch?v=dQw4w9WgXcQ"_s);
    }

    // ---- single video rank ----------------------------------------------------------------

    void changingTheRankRewritesTheSingleVideoFileName() {
        Rig r;
        r.api.resolveResponse = video();
        r.resolve();

        r.holder.onSingleRank(42);

        QCOMPARE(row(r.holder, 42).fileName, some(u"042 IU - Love.m4a"_s));
        QCOMPARE(state(r.holder).singleRank, 42);
    }

    void rankIsClampedToOneThrough999() {
        Rig r;

        r.holder.onSingleRank(0);
        QCOMPARE(state(r.holder).singleRank, 1);
        r.holder.onSingleRank(5000);
        QCOMPARE(state(r.holder).singleRank, 999);
    }

    // ---- rank in the file name ------------------------------------------------------------

    void theRankIsPartOfTheFileNameUnlessTheUserTurnsItOff() {
        Rig r;

        QCOMPARE(state(r.holder).includeRank, true);
        r.holder.onIncludeRank(false);
        QCOMPARE(state(r.holder).includeRank, false);
        r.holder.onIncludeRank(true);
        QCOMPARE(state(r.holder).includeRank, true);
    }

    void turningTheRankOffRewritesTheFileNamesOfThePreview() {
        Rig r;
        r.resolve();

        r.holder.onIncludeRank(false);

        QCOMPARE(state(r.holder).phase, Phase::Preview);
        QCOMPARE(row(r.holder, 1).fileName, some(u"A1 - T1.m4a"_s));
        QCOMPARE(row(r.holder, 3).fileName, some(u"A3 - T3.m4a"_s));
        QVERIFY2(ranksOf(state(r.holder).rows) == (QList<int>{1, 2, 3}), "the position column stays");
        QCOMPARE(row(r.holder, 1).status, ready);
    }

    void turningTheRankBackOnRestoresTheRankedFileNames() {
        Rig r;
        r.resolve();

        r.holder.onIncludeRank(false);
        r.holder.onIncludeRank(true);

        QCOMPARE(row(r.holder, 1).fileName, some(u"001 A1 - T1.m4a"_s));
        QCOMPARE(row(r.holder, 3).fileName, some(u"003 A3 - T3.m4a"_s));
    }

    void unavailableRowsKeepTheirTextWhenTheOptionChanges() {
        ResolvedItem unavailable = unavailableItem(2, u"비공개 영상"_s);
        unavailable.expectedFileName = u"kept as it was.m4a"_s;
        Rig r;
        ResolveResult response = playlist();
        response.items = {item(1), unavailable};
        r.api.resolveResponse = response;
        r.resolve();

        r.holder.onIncludeRank(false);

        QCOMPARE(row(r.holder, 2).fileName, some(u"kept as it was.m4a"_s));
        QCOMPARE(row(r.holder, 2).status, skipped(u"비공개 영상"_s));
        QCOMPARE(row(r.holder, 1).fileName, some(u"A1 - T1.m4a"_s));
    }

    void resolvingWithTheRankOffShowsFileNamesWithoutTheRank() {
        Rig r;
        r.holder.onIncludeRank(false);
        r.holder.onInput(playlistId);

        r.holder.resolve();
        r.api.flush();

        QCOMPARE(row(r.holder, 1).fileName, some(u"A1 - T1.m4a"_s));
        QCOMPARE(row(r.holder, 3).fileName, some(u"A3 - T3.m4a"_s));
        QCOMPARE(row(r.holder, 2).fileName, none);
    }

    void aVideoResolvedWithTheRankOffShowsAFileNameWithoutTheRank() {
        Rig r;
        r.api.resolveResponse = video();
        r.holder.onIncludeRank(false);
        r.holder.onSingleRank(42);
        r.holder.onInput(u"https://www.youtube.com/watch?v=dQw4w9WgXcQ"_s);

        r.holder.resolve();
        r.api.flush();

        QCOMPARE(row(r.holder, 42).fileName, some(u"IU - Love.m4a"_s));
    }

    void changingTheSingleVideoRankKeepsTheOption() {
        Rig r;
        r.api.resolveResponse = video();
        r.resolve();
        r.holder.onIncludeRank(false);

        r.holder.onSingleRank(42);

        QCOMPARE(row(r.holder, 42).fileName, some(u"IU - Love.m4a"_s));
        QCOMPARE(state(r.holder).singleRank, 42);
        r.holder.onIncludeRank(true);
        QVERIFY2(row(r.holder, 42).fileName == some(u"042 IU - Love.m4a"_s), "the rank typed by the user is what comes back");
    }

    void theOptionChosenWhileResolvingAppliesToTheResult() {
        Rig r;
        r.holder.onInput(playlistId);
        r.holder.resolve();
        r.holder.onIncludeRank(false);  // the lookup has not finished yet: there is nothing to rewrite
        r.api.flush();

        QCOMPARE(state(r.holder).phase, Phase::Preview);
        QCOMPARE(r.api.resolveInputs, QStringList{playlistId});
        QCOMPARE(row(r.holder, 1).fileName, some(u"A1 - T1.m4a"_s));
    }

    void startingSendsTheIncludeRankOption() {
        Rig r;
        r.resolve();

        r.holder.startDownload();
        r.api.flush();

        QVERIFY2(optionsOf(r.api.jobRequests[0]).includeRank == true, "on by default");
    }

    void startingWithTheRankOffSendsIt() {
        Rig r;
        r.resolve();
        r.holder.onIncludeRank(false);

        r.holder.startDownload();
        r.api.flush();

        QCOMPARE(optionsOf(r.api.jobRequests[0]).includeRank, false);
    }

    void aRetrySendsTheOptionToo() {
        Rig r;
        r.finishWithOneFailure();
        r.holder.onIncludeRank(false);

        r.holder.retryFailed();
        r.api.flush();

        QCOMPARE(ranksOf(lastRequest(r.api)), QList<int>{3});
        QCOMPARE(optionsOf(lastRequest(r.api)).includeRank, false);
    }

    void turningTheRankOffWhileAJobRunsLeavesTheRowsOfThatJobAlone() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemStarted(1, u"vid00000001"_s, u"001 Real - Name.m4a"_s));
        r.api.flush();

        r.holder.onIncludeRank(false);

        QCOMPARE(state(r.holder).phase, Phase::Running);
        QCOMPARE(state(r.holder).includeRank, false);
        QCOMPARE(row(r.holder, 1).fileName, some(u"001 Real - Name.m4a"_s));
        QCOMPARE(row(r.holder, 3).fileName, some(u"003 A3 - T3.m4a"_s));
        QCOMPARE(row(r.holder, 1).status, downloading(std::nullopt));
    }

    void turningTheRankOffAfterAJobFinishedLeavesTheRealFileNamesAlone() {
        Rig r;
        r.finishWithOneFailure();

        r.holder.onIncludeRank(false);

        QCOMPARE(state(r.holder).phase, Phase::Finished);
        QCOMPARE(row(r.holder, 1).fileName, some(u"001 A1 - T1.m4a"_s));
        QCOMPARE(row(r.holder, 3).fileName, some(u"003 A3 - T3.m4a"_s));
        QCOMPARE(state(r.holder).includeRank, false);
    }

    void resetKeepsTheIncludeRankOption() {
        Rig r;
        r.resolve();
        r.holder.onIncludeRank(false);

        r.holder.reset();

        QCOMPARE(state(r.holder).phase, Phase::Idle);
        QCOMPARE(state(r.holder).includeRank, false);
    }

    // ---- lyrics search on the internet ----------------------------------------------------

    void theLyricsSearchIsOnUntilTheUserTurnsItOff() {
        Rig r;

        QCOMPARE(state(r.holder).searchLyricsOnline, true);
        r.holder.onSearchLyricsOnline(false);
        QCOMPARE(state(r.holder).searchLyricsOnline, false);
        r.holder.onSearchLyricsOnline(true);
        QCOMPARE(state(r.holder).searchLyricsOnline, true);
    }

    void startingSendsTheSearchLyricsOnlineOption() {
        Rig r;
        r.resolve();

        r.holder.startDownload();
        r.api.flush();

        QVERIFY2(optionsOf(r.api.jobRequests[0]).searchLyricsOnline == true, "on by default");
    }

    void startingWithTheLyricsSearchOffSendsIt() {
        Rig r;
        r.resolve();
        r.holder.onSearchLyricsOnline(false);

        r.holder.startDownload();
        r.api.flush();

        QCOMPARE(optionsOf(r.api.jobRequests[0]).searchLyricsOnline, false);
        QVERIFY2(optionsOf(r.api.jobRequests[0]).includeRank == true, "the other options are not touched");
    }

    void aRetrySendsTheLyricsOptionToo() {
        Rig r;
        r.finishWithOneFailure();
        r.holder.onSearchLyricsOnline(false);

        r.holder.retryFailed();
        r.api.flush();

        QCOMPARE(ranksOf(lastRequest(r.api)), QList<int>{3});
        QCOMPARE(optionsOf(lastRequest(r.api)).searchLyricsOnline, false);
    }

    // ---- how long a job took ---------------------------------------------------------------

    void aFinishedJobKnowsHowLongItTookFromTheStartToTheEnd() {
        Rig r;
        r.resolve();
        QCOMPARE(state(r.holder).elapsedMs, std::optional<qint64>());

        r.clock += 5'000;  // thinking time before the button is pressed does not count
        r.holder.startDownload();
        r.api.flush();
        r.clock += 83'000;
        r.api.send(jobDone(JobStatus::Completed, 1, 0, 0));
        r.api.flush();

        QCOMPARE(state(r.holder).elapsedMs, std::optional<qint64>(83'000));
        QCOMPARE(summaryText(state(r.holder)), some(u"완료 — 성공 1 · 건너뜀 0 · 실패 0 · 소요 1분 23초"_s));
    }

    void aCancelledJobKnowsHowLongItRan() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();

        r.clock += 9'000;
        r.api.send(jobDone(JobStatus::Cancelled, 0, 0, 0));
        r.api.flush();

        QCOMPARE(state(r.holder).elapsedMs, std::optional<qint64>(9'000));
    }

    void aNewJobStartsTheClockAgainAndClearsTheOldTime() {
        Rig r;
        r.finishWithOneFailure();
        QVERIFY2(state(r.holder).elapsedMs == std::optional<qint64>(0), "the helper's job ended at once");

        r.holder.retryFailed();
        r.api.flush();
        QVERIFY2(state(r.holder).elapsedMs == std::nullopt, "no time is shown while the job runs");
        r.clock += 12'000;
        r.api.send(jobDone(JobStatus::Completed, 1, 0, 0));
        r.api.flush();

        QVERIFY2(state(r.holder).elapsedMs == std::optional<qint64>(12'000), "only the retry is timed");
    }

    void noTimeIsShownWhenTheConnectionBreaksBeforeTheJobEnds() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();

        r.clock += 30'000;
        r.api.closeEvents();
        r.api.flush();

        QCOMPARE(state(r.holder).phase, Phase::Finished);
        QCOMPARE(state(r.holder).elapsedMs, std::optional<qint64>());
    }

    void resetClearsTheTime() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.clock += 3'000;
        r.api.send(jobDone(JobStatus::Completed, 1, 0, 0));
        r.api.flush();

        r.holder.reset();

        QCOMPARE(state(r.holder).elapsedMs, std::optional<qint64>());
    }

    // ---- the album name typed by the user, which is also the folder name ------------------------

    void startingWithoutATypedAlbumNameSendsNone() {
        Rig r;
        r.resolve();
        r.holder.onAlbumName(u"   "_s);

        r.holder.startDownload();
        r.api.flush();

        QVERIFY2(optionsOf(r.api.jobRequests[0]).albumName == none, "blank is the same as nothing typed");
    }

    void startingSendsTheTypedAlbumName() {
        Rig r;
        r.resolve();
        r.holder.onAlbumName(u"내 앨범"_s);

        r.holder.startDownload();
        r.api.flush();

        QCOMPARE(optionsOf(r.api.jobRequests[0]).albumName, some(u"내 앨범"_s));
    }

    void aRetrySendsTheTypedAlbumNameToo() {
        Rig r;
        r.finishWithOneFailure();
        r.holder.onAlbumName(u"내 앨범"_s);

        r.holder.retryFailed();
        r.api.flush();

        QCOMPARE(optionsOf(lastRequest(r.api)).albumName, some(u"내 앨범"_s));
    }

    void theTypedAlbumNameLeavesThePreviewAndTheFileNamesAlone() {
        Rig r;
        r.resolve();
        const QList<ItemRow> before = state(r.holder).rows;

        r.holder.onAlbumName(u"내 앨범"_s);

        QVERIFY(state(r.holder).rows == before);
    }

    void resetClearsTheTypedAlbumName() {
        Rig r;
        r.resolve();
        r.holder.onAlbumName(u"내 앨범"_s);

        r.holder.reset();

        QCOMPARE(state(r.holder).albumName, none);
    }

    void anAlbumNameTheUserDidNotTouchIsNotSentEvenThoughTheFieldShowsTheDefault() {
        Rig r;
        r.resolve();
        QVERIFY2(albumNameText(state(r.holder)) == u"Sample"_s, "the field shows the playlist title");

        r.holder.startDownload();
        r.api.flush();

        QVERIFY2(optionsOf(r.api.jobRequests[0]).albumName == none, "the engine uses the playlist title itself");
    }

    void aTouchedFieldSendsItsTextEvenWhenItIsTheDefault() {
        Rig r;
        r.resolve();
        r.holder.onAlbumName(u"Sample"_s);

        r.holder.startDownload();
        r.api.flush();

        QVERIFY2(optionsOf(r.api.jobRequests[0]).albumName == some(u"Sample"_s), "the user asked for the title on every file");
    }

    void aTouchedFieldKeepsItsTextWhenSomethingIsResolvedAgain() {
        Rig r;
        r.resolve();
        r.holder.onAlbumName(u"내 앨범"_s);

        r.api.resolveResponse = video();
        r.holder.resolve();
        r.api.flush();

        QVERIFY2(albumNameText(state(r.holder)) == u"내 앨범"_s, "typed text stays");
    }

    void anUntouchedFieldFollowsTheNewDefaultWhenSomethingIsResolvedAgain() {
        Rig r;
        r.resolve();
        QCOMPARE(albumNameText(state(r.holder)), u"Sample"_s);

        r.api.resolveResponse = video();
        r.holder.resolve();
        r.api.flush();

        QVERIFY2(albumNameText(state(r.holder)) == QString(), "a video has no title to show");
    }

    void theTypedAlbumNameIsNeverRemembered() {
        FakeSettingsStore store;
        Rig r(&store);

        r.holder.onAlbumName(u"내 앨범"_s);
        r.timers.advance(5'000);
        r.api.flush();
        r.holder.flushSettings();

        QCOMPARE(store.saved, QList<UserSettings>());
    }

    void theLyricsOptionLeavesThePreviewAndTheFileNamesAlone() {
        Rig r;
        r.resolve();
        const QList<ItemRow> before = state(r.holder).rows;

        r.holder.onSearchLyricsOnline(false);

        QCOMPARE(state(r.holder).phase, Phase::Preview);
        QVERIFY(state(r.holder).rows == before);
    }

    void resetKeepsTheSearchLyricsOnlineOption() {
        Rig r;
        r.resolve();
        r.holder.onSearchLyricsOnline(false);

        r.holder.reset();

        QCOMPARE(state(r.holder).phase, Phase::Idle);
        QCOMPARE(state(r.holder).searchLyricsOnline, false);
    }

    // ---- remembered options ---------------------------------------------------------------

    void theOptionsStartFromTheStoredSettings() {
        FakeSettingsStore store(stored(u"D:/음악"_s, true, false, false));

        Rig r(&store);

        const UiState& current = state(r.holder);
        QCOMPARE(current.outputDir, u"D:/음악"_s);
        QCOMPARE(current.overwrite, true);
        QCOMPARE(current.includeRank, false);
        QCOMPARE(current.searchLyricsOnline, false);
        QCOMPARE(current.phase, Phase::Idle);
        QCOMPARE(store.loadCalls, 1);
    }

    void aStoredFileFromBeforeTheLyricsOptionStartsWithTheSearchOn() {
        // What an old settings file loads as: every key it has, and the default for the one it lacks.
        FakeSettingsStore store(stored(u"D:/Songs"_s, true));
        Rig r(&store);

        QCOMPARE(state(r.holder).searchLyricsOnline, true);
    }

    void changingTheLyricsSearchSavesTheValuesAfterAQuietPeriod() {
        FakeSettingsStore store;
        Rig r(&store);

        r.holder.onSearchLyricsOnline(false);

        ASSERT_SAVED_ONLY_AFTER_THE_QUIET_PERIOD(r.timers, store, r.api, defaultsWithSearch(false));
    }

    void theLyricsOptionIsSavedWhileAJobRunsToo() {
        FakeSettingsStore store;
        Rig r(&store);
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        QCOMPARE(state(r.holder).phase, Phase::Running);

        r.holder.onSearchLyricsOnline(false);

        ASSERT_SAVED_ONLY_AFTER_THE_QUIET_PERIOD(r.timers, store, r.api, defaultsWithSearch(false));
    }

    void theLyricsOptionIsRestoredByTheNextHolderFromWhatWasSaved() {
        FakeSettingsStore store;
        {
            Rig first(&store);
            first.holder.onSearchLyricsOnline(false);
            first.holder.flushSettings();
        }
        QCOMPARE(store.saved, QList<UserSettings>{defaultsWithSearch(false)});
        store.stored = store.saved[0];  // what the file holds after the first run

        Rig second(&store);

        QCOMPARE(state(second.holder).searchLyricsOnline, false);
    }

    void settingTheLyricsOptionToTheValueItAlreadyHasSavesNothing() {
        FakeSettingsStore store(stored(std::nullopt, false, true, false));
        Rig r(&store);

        r.holder.onSearchLyricsOnline(false);
        r.timers.advance(5'000);
        r.api.flush();
        r.holder.flushSettings();

        QCOMPARE(store.saved, QList<UserSettings>());
    }

    void aMissingOrBlankStoredFolderFallsBackToTheDefaultFolder() {
        for (const std::optional<QString>& folder : {std::optional<QString>(), some(u""_s), some(u"   "_s)}) {
            FakeSettingsStore store(stored(folder, true));
            Rig r(&store);

            QVERIFY2(state(r.holder).outputDir == u"C:/Music/xGetSongs"_s, qPrintable(u"stored folder: "_s + folder.value_or(u"null"_s)));
            QVERIFY2(state(r.holder).overwrite == true, "the other options are still taken from the store");
        }
    }

    void changingTheOutputFolderSavesTheValuesAfterAQuietPeriod() {
        FakeSettingsStore store;
        Rig r(&store);

        r.holder.onOutputDir(u"D:/Songs"_s);

        ASSERT_SAVED_ONLY_AFTER_THE_QUIET_PERIOD(r.timers, store, r.api, defaultsWith(u"D:/Songs"_s));
    }

    void changingOverwriteSavesTheValuesAfterAQuietPeriod() {
        FakeSettingsStore store;
        Rig r(&store);

        r.holder.onOverwrite(true);

        ASSERT_SAVED_ONLY_AFTER_THE_QUIET_PERIOD(r.timers, store, r.api, defaultsWithOverwrite(true));
    }

    void changingTheRankOptionSavesTheValuesAfterAQuietPeriod() {
        FakeSettingsStore store;
        Rig r(&store);

        r.holder.onIncludeRank(false);

        ASSERT_SAVED_ONLY_AFTER_THE_QUIET_PERIOD(r.timers, store, r.api, defaultsWithIncludeRank(false));
    }

    void theRankOptionIsSavedWhileAJobRunsToo() {
        FakeSettingsStore store;
        Rig r(&store);
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        QCOMPARE(state(r.holder).phase, Phase::Running);

        r.holder.onIncludeRank(false);

        ASSERT_SAVED_ONLY_AFTER_THE_QUIET_PERIOD(r.timers, store, r.api, defaultsWithIncludeRank(false));
    }

    void quickChangesAreSavedOnceWithTheLastValues() {
        FakeSettingsStore store;
        Rig r(&store);

        r.holder.onOutputDir(u"D:/S"_s);
        r.timers.advance(300);
        r.holder.onOutputDir(u"D:/Songs"_s);
        r.timers.advance(300);
        r.holder.onIncludeRank(false);
        r.timers.advance(399);
        r.api.flush();
        QVERIFY2(store.saved.isEmpty(), "every change starts the quiet period again");
        r.timers.advance(1);
        r.api.flush();

        QCOMPARE(store.saved, QList<UserSettings>{defaultsWith(u"D:/Songs"_s, false, false)});
        r.timers.advance(5'000);
        r.api.flush();
        QVERIFY2(store.saved.size() == 1, "no second save follows");
    }

    void settingAValueThatIsAlreadySetSavesNothing() {
        FakeSettingsStore store;
        Rig r(&store);

        r.holder.onOverwrite(false);
        r.holder.onIncludeRank(true);
        r.holder.onSearchLyricsOnline(true);
        r.holder.onOutputDir(u"C:/Music/xGetSongs"_s);
        r.timers.advance(5'000);
        r.api.flush();

        QCOMPARE(store.saved, QList<UserSettings>());
    }

    void repeatingAValueDoesNotRestartTheQuietPeriod() {
        FakeSettingsStore store;
        Rig r(&store);

        r.holder.onOverwrite(true);
        r.timers.advance(300);
        r.holder.onOverwrite(true);
        r.timers.advance(99);
        r.api.flush();
        QCOMPARE(store.saved, QList<UserSettings>());
        r.timers.advance(1);
        r.api.flush();

        QCOMPARE(store.saved, QList<UserSettings>{defaultsWithOverwrite(true)});
    }

    void flushSettingsSavesRightAwayAndCancelsThePendingSave() {
        FakeSettingsStore store;
        Rig r(&store);
        r.holder.onOverwrite(true);
        r.timers.advance(100);
        r.api.flush();
        QCOMPARE(store.saved, QList<UserSettings>());

        r.holder.flushSettings();

        QVERIFY2(store.saved == QList<UserSettings>{defaultsWithOverwrite(true)}, "saved at once, without waiting");
        r.timers.advance(5'000);
        r.api.flush();
        QVERIFY2(store.saved.size() == 1, "the pending save was cancelled, so there is no second one");
    }

    void flushSettingsAfterNoChangeSavesNothing() {
        FakeSettingsStore store(stored(u"D:/Songs"_s, false, false));
        Rig r(&store);

        r.holder.flushSettings();

        QCOMPARE(store.saved, QList<UserSettings>());
    }

    void flushSettingsLeavesAFileAloneThatWasLoadedAsTheDefaultsOrNormalised() {
        // A missing, corrupt or hand-edited file loads as the defaults (a blank folder): closing the app without touching an
        // option must leave such a file alone.
        for (const UserSettings& loaded : {UserSettings(), stored(u"  "_s)}) {
            FakeSettingsStore store(loaded);
            Rig r(&store);

            r.holder.flushSettings();

            QVERIFY2(store.saved.isEmpty(), qPrintable(u"stored: "_s + loaded.outputDir.value_or(u"null"_s)));
        }
    }

    void flushSettingsAfterChangesSavesTheCurrentValues() {
        FakeSettingsStore store(stored(u"D:/Songs"_s));
        Rig r(&store);
        r.holder.onOverwrite(true);
        r.holder.onIncludeRank(false);

        r.holder.flushSettings();

        QCOMPARE(store.saved, QList<UserSettings>{defaultsWith(u"D:/Songs"_s, true, false)});
    }

    void flushSettingsAfterADebouncedSaveThatAlreadyWroteTheValuesSavesNothing() {
        FakeSettingsStore store;
        Rig r(&store);
        r.holder.onOverwrite(true);
        r.timers.advance(400);
        r.api.flush();
        QCOMPARE(store.saved, QList<UserSettings>{defaultsWithOverwrite(true)});

        r.holder.flushSettings();

        QVERIFY2(store.saved.size() == 1, "the values are already on disk");
    }

    void flushSettingsSavesAgainWhenTheValuesChangedAfterTheLastSave() {
        FakeSettingsStore store;
        Rig r(&store);
        r.holder.onOverwrite(true);
        r.timers.advance(400);
        r.api.flush();
        r.holder.onIncludeRank(false);

        r.holder.flushSettings();

        QCOMPARE(store.saved, (QList<UserSettings>{defaultsWithOverwrite(true), defaultsWith(u"C:/Music/xGetSongs"_s, true, false)}));
    }

    void changingAValueAndBackBeforeTheQuietPeriodEndsSavesNothing() {
        FakeSettingsStore store;
        Rig r(&store);

        r.holder.onOverwrite(true);
        r.timers.advance(100);
        r.holder.onOverwrite(false);
        r.timers.advance(5'000);
        r.api.flush();
        r.holder.flushSettings();

        QVERIFY2(store.saved.isEmpty(), "the values are the ones that are already stored");
    }

    void changingBackAfterTheValuesWereSavedSavesTheOriginalAgain() {
        FakeSettingsStore store;
        Rig r(&store);
        r.holder.onOverwrite(true);
        r.timers.advance(400);
        r.api.flush();

        r.holder.onOverwrite(false);
        r.timers.advance(400);
        r.api.flush();

        QCOMPARE(store.saved, (QList<UserSettings>{defaultsWithOverwrite(true), defaultsWithOverwrite(false)}));
    }

    void nothingIsSavedAtStartup() {
        FakeSettingsStore store(stored(u" "_s));

        Rig r(&store);
        r.timers.advance(5'000);
        r.api.flush();

        QCOMPARE(store.saved, QList<UserSettings>());
    }

    void resetKeepsTheOptionsAndSavesNothing() {
        FakeSettingsStore store(stored(u"D:/Songs"_s, true, false, false));
        Rig r(&store);
        r.holder.onInput(playlistId);
        r.holder.resolve();
        r.api.flush();

        r.holder.reset();
        r.timers.advance(5'000);
        r.api.flush();

        const UiState& current = state(r.holder);
        QCOMPARE(current.phase, Phase::Idle);
        QCOMPARE(current.outputDir, u"D:/Songs"_s);
        QCOMPARE(current.overwrite, true);
        QCOMPARE(current.includeRank, false);
        QCOMPARE(current.searchLyricsOnline, false);
        QCOMPARE(store.saved, QList<UserSettings>());
    }

    void theTypedInputAndTheSingleVideoRankAreNeverRemembered() {
        // The stored form has room for exactly the four options, so neither the address nor the rank can be written. (A
        // structured binding of four names compiles only for a type of exactly four members.)
        const auto [outputDir, overwrite, includeRank, searchLyricsOnline] = UserSettings();
        QCOMPARE(outputDir, none);
        QCOMPARE(overwrite, false);
        QCOMPARE(includeRank, true);
        QCOMPARE(searchLyricsOnline, true);

        // Typing or choosing a rank saves nothing by itself: one change of an option later, there is exactly one save.
        FakeSettingsStore store;
        Rig r(&store);
        r.holder.onInput(playlistId);
        r.holder.onSingleRank(42);
        r.holder.onOverwrite(true);
        r.timers.advance(400);
        r.api.flush();

        QCOMPARE(store.saved, QList<UserSettings>{defaultsWithOverwrite(true)});
    }

    // ---- downloading ----------------------------------------------------------------------

    void startingSendsTheOptionsAndMarksItemsWaiting() {
        Rig r;
        r.resolve();
        r.holder.onOverwrite(true);
        r.holder.onOutputDir(u"D:/Songs"_s);

        r.holder.startDownload();
        r.api.flush();

        const JobRequest& request = r.api.jobRequests[0];
        QCOMPARE(r.api.jobRequests.size(), 1);
        // request.resolveId == "resolve-1": the items of that lookup, its kind and its title
        QCOMPARE(request.items, (QList<ResolvedItem>{playlist().items[0], playlist().items[2]}));
        QCOMPARE(request.isPlaylist, true);
        QCOMPARE(request.playlistTitle, some(u"Sample"_s));
        QCOMPARE(optionsOf(request).outputDir, some(u"D:/Songs"_s));
        QCOMPARE(optionsOf(request).overwrite, true);
        // request.ranks == null: every available item
        QCOMPARE(ranksOf(request), (QList<int>{1, 3}));
        QCOMPARE(state(r.holder).phase, Phase::Running);
        QCOMPARE(row(r.holder, 1).status, waiting);
        QCOMPARE(row(r.holder, 2).status, skipped(u"비공개 영상"_s));
    }

    void jobEventsDriveTheRowsAndTheSummary() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();

        r.api.send(itemStarted(1, u"vid00000001"_s, u"001 Real - Name.m4a"_s));
        r.api.flush();
        QCOMPARE(row(r.holder, 1).status, downloading(std::nullopt));
        QCOMPARE(row(r.holder, 1).fileName, some(u"001 Real - Name.m4a"_s));

        r.api.send(progress(1, Stage::Downloading, 40));
        r.api.flush();
        QCOMPARE(row(r.holder, 1).status, downloading(40));

        r.api.send(progress(1, Stage::Finishing));
        r.api.flush();
        QCOMPARE(row(r.holder, 1).status, finishing);

        r.api.send(itemDone(1, u"001 Real - Name.m4a"_s), itemFailed(3, u"boom"_s), jobDone(JobStatus::Completed, 1, 0, 1));
        r.api.closeEvents();
        r.api.flush();

        const UiState& current = state(r.holder);
        QVERIFY2(row(r.holder, 1).status == done(std::nullopt), "an event without an outcome leaves it not known");
        QCOMPARE(row(r.holder, 3).status, failed(u"boom"_s));
        QCOMPARE(current.phase, Phase::Finished);
        QVERIFY(current.summary == (JobSummary{1, 0, 1}));
        QVERIFY(current.jobStatus == JobStatus::Completed);
        QCOMPARE(failedRanks(current), QList<int>{3});
    }

    // ---- the lyrics outcome of a finished row ----

    void aFinishedRowShowsTheLyricsOutcomeOfItsEventAndTheFinalFileName() {
        QList<std::optional<LyricsOutcome>> outcomes;
        for (LyricsOutcome outcome : LyricsOutcomes) outcomes.append(outcome);
        outcomes.append(std::nullopt);
        for (const std::optional<LyricsOutcome>& outcome : outcomes) {
            const QByteArray name = outcome ? QByteArray::number(int(*outcome)) : QByteArray("null");
            Rig r;
            r.resolve();
            r.holder.startDownload();
            r.api.flush();
            r.api.send(itemStarted(1, u"vid00000001"_s, u"001 Preview - Name.m4a"_s));

            r.api.send(itemDone(1, u"001 Real - Name.m4a"_s, outcome));
            r.api.flush();

            QVERIFY2(row(r.holder, 1).status == done(outcome), name.constData());
            QVERIFY2(row(r.holder, 1).fileName == some(u"001 Real - Name.m4a"_s), name.constData());
        }
    }

    void everyFinishedRowKeepsItsOwnOutcome() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();

        r.api.send(itemDone(1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Online), itemDone(3, u"003 A3 - T3.m4a"_s, LyricsOutcome::SearchOff));
        r.api.flush();

        QCOMPARE(statusLabel(row(r.holder, 1).status), u"완료 · 가사 ✓ 인터넷"_s);
        QCOMPARE(statusLabel(row(r.holder, 3).status), u"완료 · 가사 없음 (검색 끔)"_s);
        QCOMPARE(row(r.holder, 2).status, skipped(u"비공개 영상"_s));
    }

    void aNewRunReplacesTheOutcomeOfTheRowsItRuns() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemDone(1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Online), jobDone(JobStatus::Completed, 1, 0, 0));
        r.api.closeEvents();
        r.api.flush();
        QCOMPARE(row(r.holder, 1).status, done(LyricsOutcome::Online));

        r.holder.startDownload();
        r.api.flush();
        QVERIFY2(row(r.holder, 1).status == waiting, "the old outcome is gone as soon as the new run starts");

        r.api.send(itemDone(1, u"001 A1 - T1.m4a"_s, LyricsOutcome::NotFound));
        r.api.flush();

        QCOMPARE(row(r.holder, 1).status, done(LyricsOutcome::NotFound));
    }

    void aRetryReplacesTheFailureWithTheOutcomeAndLeavesTheOtherOutcomesAlone() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemDone(1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Description), itemFailed(3, u"boom"_s),
                   jobDone(JobStatus::Completed, 1, 0, 1));
        r.api.closeEvents();
        r.api.flush();
        r.holder.retryFailed();
        r.api.flush();

        r.api.send(itemDone(3, u"003 A3 - T3.m4a"_s, LyricsOutcome::NotFound), jobDone(JobStatus::Completed, 1, 0, 0));
        r.api.closeEvents();
        r.api.flush();

        QCOMPARE(row(r.holder, 1).status, done(LyricsOutcome::Description));
        QCOMPARE(row(r.holder, 3).status, done(LyricsOutcome::NotFound));
        QCOMPARE(failedRanks(state(r.holder)), QList<int>());
    }

    void anEndedOrBrokenJobKeepsTheOutcomeOfTheRowsThatFinished() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemDone(1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Online), itemStarted(3, u"vid00000003"_s, u"003 A3 - T3.m4a"_s));
        r.api.flush();

        r.api.closeEvents();  // no JobDone: the connection broke while row 3 was in flight
        r.api.flush();

        QCOMPARE(row(r.holder, 1).status, done(LyricsOutcome::Online));
        QCOMPARE(row(r.holder, 3).status, ready);
        QCOMPARE(state(r.holder).error, some(u"서버와의 연결이 끊어졌습니다."_s));
    }

    void aCancelledJobKeepsTheOutcomeOfTheRowsThatFinished() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemDone(1, u"001 A1 - T1.m4a"_s, LyricsOutcome::SearchOff), jobDone(JobStatus::Cancelled, 1, 0, 0));
        r.api.closeEvents();
        r.api.flush();

        QCOMPARE(row(r.holder, 1).status, done(LyricsOutcome::SearchOff));
        QCOMPARE(row(r.holder, 3).status, ready);
    }

    void resetClearsTheFinishedRowsWithTheirOutcomes() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemDone(1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Online), jobDone(JobStatus::Completed, 1, 0, 0));
        r.api.closeEvents();
        r.api.flush();
        QCOMPARE(row(r.holder, 1).status, done(LyricsOutcome::Online));

        r.holder.reset();

        QCOMPARE(state(r.holder).rows.size(), 0);
        QCOMPARE(state(r.holder).phase, Phase::Idle);
        QVERIFY(state(r.holder).summary == std::nullopt);
    }

    void skippedItemsShowTheirReason() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();

        r.api.send(itemSkipped(1, u"이미 존재"_s));
        r.api.flush();

        QCOMPARE(row(r.holder, 1).status, skipped(u"이미 존재"_s));
    }

    void retryRunsOnlyTheFailedRanksAndKeepsTheOtherResults() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemDone(1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Description), itemFailed(3, u"boom"_s),
                   jobDone(JobStatus::Completed, 1, 0, 1));
        r.api.closeEvents();
        r.api.flush();

        r.holder.retryFailed();
        r.api.flush();

        QCOMPARE(ranksOf(lastRequest(r.api)), QList<int>{3});
        QCOMPARE(state(r.holder).phase, Phase::Running);
        QCOMPARE(row(r.holder, 1).status, done(LyricsOutcome::Description));
        QCOMPARE(row(r.holder, 3).status, waiting);
    }

    void retryOfASingleVideoRerunsTheWholeVideo() {
        Rig r;
        r.api.resolveResponse = video();
        r.resolve();
        r.holder.onSingleRank(7);
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemFailed(7, u"boom"_s), jobDone(JobStatus::Completed, 0, 0, 1));
        r.api.closeEvents();
        r.api.flush();

        r.holder.retryFailed();
        r.api.flush();

        // ranks == null: the whole video, with the rank the user chose
        QCOMPARE(ranksOf(lastRequest(r.api)), QList<int>{7});
        QCOMPARE(optionsOf(lastRequest(r.api)).singleRank, 7);
    }

    void cancelAsksTheServerToStopTheJob() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();

        r.holder.cancel();
        r.api.flush();
        QCOMPARE(r.api.cancelled, QStringList{u"job-1"_s});

        r.api.send(jobDone(JobStatus::Cancelled, 0, 0, 0));
        r.api.closeEvents();
        r.api.flush();

        QCOMPARE(state(r.holder).phase, Phase::Finished);
        QVERIFY(state(r.holder).jobStatus == JobStatus::Cancelled);
        QCOMPARE(row(r.holder, 1).status, ready);
    }

    void aStreamThatEndsWithoutJobDoneIsReportedAsALostConnection() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();

        r.api.closeEvents();
        r.api.flush();

        QCOMPARE(state(r.holder).phase, Phase::Finished);
        QCOMPARE(state(r.holder).error, some(u"서버와의 연결이 끊어졌습니다."_s));
    }

    void aJobThatCannotStartReturnsToThePreview() {
        Rig r;
        r.resolve();
        r.api.startError = apiError(u"출력 폴더를 만들 수 없습니다"_s);

        r.holder.startDownload();
        r.api.flush();

        QCOMPARE(state(r.holder).phase, Phase::Preview);
        QCOMPARE(state(r.holder).error, some(u"출력 폴더를 만들 수 없습니다"_s));
        QCOMPARE(row(r.holder, 1).status, ready);
    }

    void startingWithoutAnOutputFolderIsRefused() {
        Rig r;
        r.resolve();
        r.holder.onOutputDir(u"  "_s);

        r.holder.startDownload();
        r.api.flush();

        QCOMPARE(r.api.jobRequests.size(), 0);
        QCOMPARE(state(r.holder).error, some(u"출력 폴더를 지정하세요."_s));
    }

    void resetReturnsToAnEmptyScreenButKeepsSettings() {
        Rig r;
        r.resolve();
        r.holder.onOutputDir(u"D:/Songs"_s);

        r.holder.reset();

        const UiState& current = state(r.holder);
        QCOMPARE(current.phase, Phase::Idle);
        QCOMPARE(current.rows.size(), 0);
        QCOMPARE(current.outputDir, u"D:/Songs"_s);
    }

    // ---- tools ----------------------------------------------------------------------------

    void refreshToolsLoadsTheStatus() {
        Rig r;

        r.holder.refreshTools();
        r.api.flush();

        QVERIFY(state(r.holder).tools == r.api.toolsStatus);
    }

    void installingYtDlpReportsTheResultAndRefreshesTheStatus() {
        Rig r;

        r.holder.installYtDlp();
        r.api.flush();

        QCOMPARE(r.api.installCalls, 1);
        QCOMPARE(state(r.holder).toolMessage, some(u"installed"_s));
        QCOMPARE(state(r.holder).toolBusy, false);
        QVERIFY(state(r.holder).tools == r.api.toolsStatus);
    }

    // ---- job endings and failures ---------------------------------------------------------

    void rowsInFlightGoBackToReadyWhenACancelledJobEnds() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemStarted(1, u"vid00000001"_s, u"001 A1 - T1.m4a"_s), progress(1, Stage::Downloading, 40));
        r.api.flush();
        QCOMPARE(row(r.holder, 1).status, downloading(40));

        r.api.send(jobDone(JobStatus::Cancelled, 0, 0, 0));
        r.api.closeEvents();
        r.api.flush();

        QCOMPARE(row(r.holder, 1).status, ready);
        QCOMPARE(state(r.holder).phase, Phase::Finished);
        QVERIFY(state(r.holder).jobStatus == JobStatus::Cancelled);
    }

    void rowsInFlightGoBackToReadyWhenTheStreamBreaks() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemStarted(1, u"vid00000001"_s, u"001 A1 - T1.m4a"_s), progress(1, Stage::Finishing));
        r.api.flush();
        QCOMPARE(row(r.holder, 1).status, finishing);

        r.api.closeEvents();
        r.api.flush();

        QCOMPARE(state(r.holder).phase, Phase::Finished);
        QCOMPARE(state(r.holder).error, some(u"서버와의 연결이 끊어졌습니다."_s));
        QCOMPARE(row(r.holder, 1).status, ready);
    }

    void aRawExceptionDuringResolveDoesNotLeaveTheScreenResolving() {
        Rig r;
        r.api.resolveError = rawError(u"boom"_s);
        r.resolve();

        QCOMPARE(state(r.holder).phase, Phase::Idle);
        QVERIFY(state(r.holder).error.value_or(QString()).contains(u"boom"_s));
    }

    void aRawExceptionFromToolActionsClearsTheBusyFlag() {
        Rig r;
        r.api.toolsError = rawError(u"boom"_s);

        r.holder.installYtDlp();
        r.api.flush();

        QCOMPARE(state(r.holder).toolBusy, false);
        QVERIFY(state(r.holder).toolMessage.value_or(QString()).contains(u"boom"_s));

        r.holder.refreshTools();
        r.api.flush();

        QVERIFY(state(r.holder).error.value_or(QString()).contains(u"boom"_s));
    }

    void aFailedStartFromFinishedKeepsThePreviousResults() {
        Rig r;
        r.finishWithOneFailure();
        r.api.startError = apiError(u"출력 폴더를 만들 수 없습니다"_s);

        r.holder.retryFailed();
        r.api.flush();

        const UiState& current = state(r.holder);
        QCOMPARE(current.phase, Phase::Finished);
        QCOMPARE(row(r.holder, 3).status, failed(u"boom"_s));
        QCOMPARE(row(r.holder, 1).status, done(std::nullopt));
        QVERIFY(current.summary == (JobSummary{1, 0, 1}));
        QVERIFY(current.jobStatus == JobStatus::Completed);
        QCOMPARE(failedRanks(current), QList<int>{3});
        QCOMPARE(current.error, some(u"출력 폴더를 만들 수 없습니다"_s));
    }

    void aRawExceptionWhileStartingARetryRestoresThePreviousScreen() {
        Rig r;
        r.finishWithOneFailure();
        r.api.startError = rawError(u"boom"_s);

        r.holder.retryFailed();
        r.api.flush();

        const UiState& current = state(r.holder);
        QCOMPARE(current.phase, Phase::Finished);
        QCOMPARE(failedRanks(current), QList<int>{3});
        QVERIFY(current.error.value_or(QString()).contains(u"boom"_s));
    }

    void aFailedSecondLookupKeepsThePreviewUsable() {
        Rig r;
        r.resolve();
        const QList<ItemRow> rowsBefore = state(r.holder).rows;
        r.api.resolveError = apiError(u"x"_s);

        r.holder.onInput(playlistId);
        r.holder.resolve();
        r.api.flush();

        const UiState& current = state(r.holder);
        QCOMPARE(current.phase, Phase::Preview);
        QCOMPARE(ranksOf(current.rows), ranksOf(rowsBefore));
        QCOMPARE(statusesOf(current.rows), statusesOf(rowsBefore));
        QCOMPARE(current.error, some(u"x"_s));
    }

    void aFailedSecondLookupShowsTheOldRowsWithTheOptionAsItIsNow() {
        Rig r;
        r.resolve();
        QCOMPARE(row(r.holder, 1).fileName, some(u"001 A1 - T1.m4a"_s));
        r.api.resolveGate = std::make_shared<Gate>();
        r.api.resolveError = apiError(u"x"_s);
        r.holder.onInput(playlistId);
        r.holder.resolve();
        r.api.flush();
        QCOMPARE(state(r.holder).phase, Phase::Resolving);

        r.holder.onIncludeRank(false);  // while the lookup is held: nothing is rewritten yet
        r.api.resolveGate->release();
        r.api.flush();

        const UiState& current = state(r.holder);
        QCOMPARE(current.phase, Phase::Preview);
        QCOMPARE(current.error, some(u"x"_s));
        QCOMPARE(current.includeRank, false);
        QCOMPARE(row(r.holder, 1).fileName, some(u"A1 - T1.m4a"_s));
        QCOMPARE(row(r.holder, 3).fileName, some(u"A3 - T3.m4a"_s));
        QCOMPARE(ranksOf(current.rows), (QList<int>{1, 2, 3}));
        QCOMPARE(row(r.holder, 1).status, ready);
    }

    void aFailedLookupAfterAFinishedJobKeepsTheResults() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(itemDone(1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Online), jobDone(JobStatus::Completed, 1, 0, 0));
        r.api.closeEvents();
        r.api.flush();
        r.api.resolveError = apiError(u"x"_s);

        r.holder.resolve();
        r.api.flush();

        const UiState& current = state(r.holder);
        QCOMPARE(current.phase, Phase::Finished);
        QVERIFY(current.summary == (JobSummary{1, 0, 0}));
        QCOMPARE(row(r.holder, 1).status, done(LyricsOutcome::Online));
        QCOMPARE(current.error, some(u"x"_s));
    }

    // ---- not in the Kotlin file: what the TypeScript port added ----------------------------------------------

    void subscribersAreToldOfEveryChangeUntilTheyStop() {
        Rig r;
        QStringList seen;
        const QMetaObject::Connection stop =
            QObject::connect(&r.holder, &AppStateHolder::changed, [&] { seen.append(r.holder.state().input); });

        r.holder.onInput(u"a"_s);
        r.holder.onInput(u"ab"_s);
        QObject::disconnect(stop);
        r.holder.onInput(u"abc"_s);

        QCOMPARE(seen, (QStringList{u"a"_s, u"ab"_s}));
    }

    void whatAJobReportsAfterTheScreenWasResetIsIgnored() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.holder.reset();

        r.api.send(itemDone(1, u"001 A1 - T1.m4a"_s, LyricsOutcome::Online), jobDone(JobStatus::Completed, 1, 0, 0));
        r.api.closeEvents();
        r.api.flush();

        QCOMPARE(state(r.holder).phase, Phase::Idle);
        QCOMPARE(state(r.holder).rows, QList<ItemRow>());
        QVERIFY(state(r.holder).summary == std::nullopt);
        QCOMPARE(state(r.holder).error, none);
    }

    void aFailureThatIsNotTheServersMessageSaysThatCommunicationFailed() {
        Rig r;
        r.api.resolveError = rawError(u"invoke is not defined"_s);  // a failure that is not a message of the engine
        r.resolve();

        QCOMPARE(state(r.holder).error, some(u"서버와 통신 중 오류가 발생했습니다: invoke is not defined"_s));
    }

    // ---- what the TypeScript port added after the review of stage 3 ------------------------------------------------

    void cancelBeforeTheNewJobHasAnIdDoesNotCancelThePreviousJob() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(jobDone(JobStatus::Completed, 1, 0, 0));
        r.api.closeEvents();
        r.api.flush();
        QCOMPARE(state(r.holder).phase, Phase::Finished);

        r.api.startGate = std::make_shared<Gate>();
        r.holder.startDownload();
        r.api.flush();
        QCOMPARE(state(r.holder).phase, Phase::Running);
        r.holder.cancel();
        r.api.flush();

        QVERIFY2(r.api.cancelled.isEmpty(), "job-1 is over and job-2 has not started yet: there is nothing to cancel");
        QCOMPARE(state(r.holder).error, none);

        r.api.startGate->release();
        r.api.flush();
        r.holder.cancel();
        r.api.flush();

        QCOMPARE(r.api.cancelled, QStringList{u"job-2"_s});
    }

    void theEndOfAnOldWatchDoesNotMarkTheNewJobAsLost() {
        Rig r;
        r.resolve();
        r.holder.startDownload();
        r.api.flush();
        r.api.send(jobDone(JobStatus::Completed, 1, 0, 0));  // the job has reported its end but its event stream is still open
        r.api.flush();
        QCOMPARE(state(r.holder).phase, Phase::Finished);

        r.holder.startDownload();
        r.api.flush();
        QCOMPARE(state(r.holder).phase, Phase::Running);
        r.api.closeEventsOf(1);  // the old stream ends only now
        r.api.flush();

        QVERIFY2(state(r.holder).phase == Phase::Running, "the new job is still running");
        QCOMPARE(state(r.holder).error, none);

        r.api.send(jobDone(JobStatus::Completed, 2, 0, 0));
        r.api.closeEvents();
        r.api.flush();

        QVERIFY(state(r.holder).jobStatus == JobStatus::Completed);
    }

    void theElapsedTimeDoesNotFollowTheWallClock() {
        // TypeScript moved the wall clock by an hour under a holder with its default clock. A test cannot move the system clock
        // here; the holder's default clock is std::chrono::steady_clock, which a change of the wall clock does not move (a
        // static_assert in app_state_holder.cpp checks that it is steady), and a job that runs for a moment reports a moment.
        FakeAppApi api;
        FakeSettingsStore store;
        FakeScheduler timers;
        AppStateHolder holder(api, store, nullptr, timers);
        holder.onInput(playlistId);
        holder.resolve();
        api.flush();
        holder.startDownload();
        api.flush();

        api.send(jobDone(JobStatus::Completed, 1, 0, 0));
        api.closeEvents();
        api.flush();

        const std::optional<qint64> elapsed = holder.state().elapsedMs;
        QVERIFY2(elapsed && *elapsed >= 0 && *elapsed < 60'000,
                 qPrintable(u"the job ran for well under a minute but the elapsed time is %1 ms"_s.arg(elapsed.value_or(-1))));
    }
};

QTEST_GUILESS_MAIN(AppStateHolderTest)
#include "tst_app_state_holder.moc"
