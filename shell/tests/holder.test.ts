// The scenarios of app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt (the Compose app, deleted in stage 6 of the
// migration: `git show f9dbcba:<that path>`), one test per Kotlin test and under the same name (tests/kotlin-coverage.test.ts checked
// that none was missing while the file existed). Kotlin's `runCurrent()` is `await flush()`,
// `advanceTimeBy` is `timers.advance`, and the test time source is `clock.ms`.

import assert from "node:assert/strict";
import test, { mock } from "node:test";
import type { JobEvent, JobOptions, JobRequest, JobStatus, LyricsOutcome, ResolvedItem, Stage } from "../src/bridge/types.ts";
import { AppStateHolder } from "../src/state/holder.ts";
import { albumNameText, statusLabel, summaryText } from "../src/state/labels.ts";
import { failedRanks, noSettingsStore, userSettings, type ItemRow, type ItemStatus, type SettingsStore, type UserSettings } from "../src/state/types.ts";
import { REJECT_MESSAGES } from "../src/logic/input.ts";
import { Deferred, FakeApi, FakeSettingsStore, FakeTimers, apiError, flush } from "./support/fakes.ts";
import { LYRICS_OUTCOMES, item, playlist, unavailableItem, video } from "./support/fixtures.ts";

const playlistId = "PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV";

// ---- the rig ---------------------------------------------------------------------------

interface Rig {
  api: FakeApi;
  holder: AppStateHolder;
  timers: FakeTimers;
  /** The time source of the holder: the test moves it. */
  clock: { ms: number };
}

function rig({ api = new FakeApi(), store = noSettingsStore }: { api?: FakeApi; store?: SettingsStore } = {}): Rig {
  const timers = new FakeTimers();
  const clock = { ms: 0 };
  const holder = new AppStateHolder({
    api,
    defaultOutputDir: "C:/Music/xGetSongs",
    settings: store,
    now: () => clock.ms,
    scheduler: timers,
  });
  return { api, holder, timers, clock };
}

/** A rig whose holder has resolved the sample playlist (or whatever the api answers). */
async function resolved(options: { api?: FakeApi; store?: SettingsStore } = {}): Promise<Rig> {
  const made = rig(options);
  made.holder.onInput(playlistId);
  made.holder.resolve();
  await flush();
  return made;
}

/** Resolved, started, and ended with rank 1 done and rank 3 failed ("boom"). */
async function finishedWithOneFailure(): Promise<Rig> {
  const made = await resolved();
  made.holder.startDownload();
  await flush();
  made.api.send(itemDone(1, "001 A1 - T1.m4a"), itemFailed(3, "boom"), jobDone("COMPLETED", 1, 0, 1));
  made.api.closeEvents();
  await flush();
  return made;
}

const state = (holder: AppStateHolder) => holder.getState();

function row(holder: AppStateHolder, rank: number): ItemRow {
  const found = holder.getState().rows.find((candidate) => candidate.item.rank === rank);
  assert.ok(found, `no row of rank ${rank}`);
  return found;
}

const optionsOf = (request: JobRequest): JobOptions => request.options as JobOptions;
const lastRequest = (api: FakeApi): JobRequest => api.jobRequests[api.jobRequests.length - 1];

// ---- the events a job sends ----------------------------------------------------------------

const itemStarted = (rank: number, videoId: string, fileName: string): JobEvent => ({ type: "item-started", rank, videoId, fileName });
const progress = (rank: number, stage: Stage, percent: number | null = null): JobEvent => ({ type: "progress", rank, stage, percent });
const itemDone = (rank: number, fileName: string, lyrics: LyricsOutcome | null = null): JobEvent => ({ type: "item-done", rank, fileName, lyrics });
const itemSkipped = (rank: number, reason: string): JobEvent => ({ type: "item-skipped", rank, reason });
const itemFailed = (rank: number, message: string): JobEvent => ({ type: "item-failed", rank, message });
const jobDone = (status: JobStatus, succeeded: number, skipped: number, failed: number): JobEvent => ({
  type: "job-done",
  status,
  summary: { succeeded, skipped, failed },
});

// ---- the statuses a row can have -----------------------------------------------------------

const ready: ItemStatus = { kind: "ready" };
const waiting: ItemStatus = { kind: "waiting" };
const finishing: ItemStatus = { kind: "finishing" };
const downloading = (percent: number | null): ItemStatus => ({ kind: "downloading", percent });
const done = (lyrics: LyricsOutcome | null = null): ItemStatus => ({ kind: "done", lyrics });
const skipped = (reason: string): ItemStatus => ({ kind: "skipped", reason });
const failed = (message: string): ItemStatus => ({ kind: "failed", message });

// ---- resolving ------------------------------------------------------------------------

test("startsIdleWithTheDefaultOutputFolder", () => {
  const { holder } = rig();

  assert.equal(state(holder).phase, "IDLE");
  assert.equal(state(holder).outputDir, "C:/Music/xGetSongs");
});

test("resolveShowsAPreviewOfEveryItem", async () => {
  const { api, holder } = await resolved();

  assert.equal(state(holder).phase, "PREVIEW");
  assert.deepEqual(api.resolveInputs, [playlistId]);
  assert.deepEqual(state(holder).rows.map((r) => r.item.rank), [1, 2, 3]);
  assert.deepEqual(row(holder, 1).status, ready);
  assert.deepEqual(row(holder, 2).status, skipped("비공개 영상"));
  assert.equal(row(holder, 1).fileName, "001 A1 - T1.m4a");
});

test("invalidInputIsRejectedBeforeAnyRequest", async () => {
  const { api, holder } = rig();
  holder.onInput("https://evil.com/x");

  holder.resolve();
  await flush();

  assert.equal(state(holder).error, REJECT_MESSAGES.UNSUPPORTED_HOST);
  assert.equal(api.resolveInputs.length, 0);
  assert.equal(state(holder).phase, "IDLE");
});

test("serverErrorsAreShownAndTheScreenStaysUsable", async () => {
  const api = new FakeApi();
  api.resolveError = apiError("비공개 재생목록");
  const { holder } = await resolved({ api });

  assert.equal(state(holder).error, "비공개 재생목록");
  assert.equal(state(holder).phase, "IDLE");
});

test("switchingToTheVideoOnlyResolvesTheVideoUrl", async () => {
  const api = new FakeApi();
  api.resolveResponse = playlist("dQw4w9WgXcQ");
  const { holder } = await resolved({ api });

  holder.switchToVideoOnly();
  await flush();

  assert.equal(api.resolveInputs[api.resolveInputs.length - 1], "https://www.youtube.com/watch?v=dQw4w9WgXcQ");
});

// ---- single video rank ----------------------------------------------------------------

test("changingTheRankRewritesTheSingleVideoFileName", async () => {
  const api = new FakeApi();
  api.resolveResponse = video();
  const { holder } = await resolved({ api });

  holder.onSingleRank(42);

  assert.equal(row(holder, 42).fileName, "042 IU - Love.m4a");
  assert.equal(state(holder).singleRank, 42);
});

test("rankIsClampedToOneThrough999", () => {
  const { holder } = rig();

  holder.onSingleRank(0);
  assert.equal(state(holder).singleRank, 1);
  holder.onSingleRank(5000);
  assert.equal(state(holder).singleRank, 999);
});

// ---- rank in the file name ------------------------------------------------------------

test("theRankIsPartOfTheFileNameUnlessTheUserTurnsItOff", () => {
  const { holder } = rig();

  assert.equal(state(holder).includeRank, true);
  holder.onIncludeRank(false);
  assert.equal(state(holder).includeRank, false);
  holder.onIncludeRank(true);
  assert.equal(state(holder).includeRank, true);
});

test("turningTheRankOffRewritesTheFileNamesOfThePreview", async () => {
  const { holder } = await resolved();

  holder.onIncludeRank(false);

  assert.equal(state(holder).phase, "PREVIEW");
  assert.equal(row(holder, 1).fileName, "A1 - T1.m4a");
  assert.equal(row(holder, 3).fileName, "A3 - T3.m4a");
  assert.deepEqual(state(holder).rows.map((r) => r.item.rank), [1, 2, 3], "the position column stays");
  assert.deepEqual(row(holder, 1).status, ready);
});

test("turningTheRankBackOnRestoresTheRankedFileNames", async () => {
  const { holder } = await resolved();

  holder.onIncludeRank(false);
  holder.onIncludeRank(true);

  assert.equal(row(holder, 1).fileName, "001 A1 - T1.m4a");
  assert.equal(row(holder, 3).fileName, "003 A3 - T3.m4a");
});

test("unavailableRowsKeepTheirTextWhenTheOptionChanges", async () => {
  const unavailable: ResolvedItem = { ...unavailableItem(2, "비공개 영상"), expectedFileName: "kept as it was.m4a" };
  const api = new FakeApi();
  api.resolveResponse = { ...playlist(), items: [item(1), unavailable] };
  const { holder } = await resolved({ api });

  holder.onIncludeRank(false);

  assert.equal(row(holder, 2).fileName, "kept as it was.m4a");
  assert.deepEqual(row(holder, 2).status, skipped("비공개 영상"));
  assert.equal(row(holder, 1).fileName, "A1 - T1.m4a");
});

test("resolvingWithTheRankOffShowsFileNamesWithoutTheRank", async () => {
  const { holder } = rig();
  holder.onIncludeRank(false);
  holder.onInput(playlistId);

  holder.resolve();
  await flush();

  assert.equal(row(holder, 1).fileName, "A1 - T1.m4a");
  assert.equal(row(holder, 3).fileName, "A3 - T3.m4a");
  assert.equal(row(holder, 2).fileName, null);
});

test("aVideoResolvedWithTheRankOffShowsAFileNameWithoutTheRank", async () => {
  const api = new FakeApi();
  api.resolveResponse = video();
  const { holder } = rig({ api });
  holder.onIncludeRank(false);
  holder.onSingleRank(42);
  holder.onInput("https://www.youtube.com/watch?v=dQw4w9WgXcQ");

  holder.resolve();
  await flush();

  assert.equal(row(holder, 42).fileName, "IU - Love.m4a");
});

test("changingTheSingleVideoRankKeepsTheOption", async () => {
  const api = new FakeApi();
  api.resolveResponse = video();
  const { holder } = await resolved({ api });
  holder.onIncludeRank(false);

  holder.onSingleRank(42);

  assert.equal(row(holder, 42).fileName, "IU - Love.m4a");
  assert.equal(state(holder).singleRank, 42);
  holder.onIncludeRank(true);
  assert.equal(row(holder, 42).fileName, "042 IU - Love.m4a", "the rank typed by the user is what comes back");
});

test("theOptionChosenWhileResolvingAppliesToTheResult", async () => {
  const { api, holder } = rig();
  holder.onInput(playlistId);
  holder.resolve();
  holder.onIncludeRank(false); // the lookup has not finished yet: there is nothing to rewrite
  await flush();

  assert.equal(state(holder).phase, "PREVIEW");
  assert.deepEqual(api.resolveInputs, [playlistId]);
  assert.equal(row(holder, 1).fileName, "A1 - T1.m4a");
});

test("startingSendsTheIncludeRankOption", async () => {
  const { api, holder } = await resolved();

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).includeRank, true, "on by default");
});

test("startingWithTheRankOffSendsIt", async () => {
  const { api, holder } = await resolved();
  holder.onIncludeRank(false);

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).includeRank, false);
});

test("aRetrySendsTheOptionToo", async () => {
  const { api, holder } = await finishedWithOneFailure();
  holder.onIncludeRank(false);

  holder.retryFailed();
  await flush();

  assert.deepEqual(lastRequest(api).ranks, [3]);
  assert.equal(optionsOf(lastRequest(api)).includeRank, false);
});

test("turningTheRankOffWhileAJobRunsLeavesTheRowsOfThatJobAlone", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemStarted(1, "vid00000001", "001 Real - Name.m4a"));
  await flush();

  holder.onIncludeRank(false);

  assert.equal(state(holder).phase, "RUNNING");
  assert.equal(state(holder).includeRank, false);
  assert.equal(row(holder, 1).fileName, "001 Real - Name.m4a");
  assert.equal(row(holder, 3).fileName, "003 A3 - T3.m4a");
  assert.deepEqual(row(holder, 1).status, downloading(null));
});

test("turningTheRankOffAfterAJobFinishedLeavesTheRealFileNamesAlone", async () => {
  const { holder } = await finishedWithOneFailure();

  holder.onIncludeRank(false);

  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(row(holder, 1).fileName, "001 A1 - T1.m4a");
  assert.equal(row(holder, 3).fileName, "003 A3 - T3.m4a");
  assert.equal(state(holder).includeRank, false);
});

test("resetKeepsTheIncludeRankOption", async () => {
  const { holder } = await resolved();
  holder.onIncludeRank(false);

  holder.reset();

  assert.equal(state(holder).phase, "IDLE");
  assert.equal(state(holder).includeRank, false);
});

// ---- lyrics search on the internet ----------------------------------------------------

test("theLyricsSearchIsOnUntilTheUserTurnsItOff", () => {
  const { holder } = rig();

  assert.equal(state(holder).searchLyricsOnline, true);
  holder.onSearchLyricsOnline(false);
  assert.equal(state(holder).searchLyricsOnline, false);
  holder.onSearchLyricsOnline(true);
  assert.equal(state(holder).searchLyricsOnline, true);
});

test("startingSendsTheSearchLyricsOnlineOption", async () => {
  const { api, holder } = await resolved();

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).searchLyricsOnline, true, "on by default");
});

test("startingWithTheLyricsSearchOffSendsIt", async () => {
  const { api, holder } = await resolved();
  holder.onSearchLyricsOnline(false);

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).searchLyricsOnline, false);
  assert.equal(optionsOf(api.jobRequests[0]).includeRank, true, "the other options are not touched");
});

test("aRetrySendsTheLyricsOptionToo", async () => {
  const { api, holder } = await finishedWithOneFailure();
  holder.onSearchLyricsOnline(false);

  holder.retryFailed();
  await flush();

  assert.deepEqual(lastRequest(api).ranks, [3]);
  assert.equal(optionsOf(lastRequest(api)).searchLyricsOnline, false);
});

// ---- how long a job took ---------------------------------------------------------------

test("aFinishedJobKnowsHowLongItTookFromTheStartToTheEnd", async () => {
  const { api, holder, clock } = await resolved();
  assert.equal(state(holder).elapsedMs, null);

  clock.ms += 5_000; // thinking time before the button is pressed does not count
  holder.startDownload();
  await flush();
  clock.ms += 83_000;
  api.send(jobDone("COMPLETED", 1, 0, 0));
  await flush();

  assert.equal(state(holder).elapsedMs, 83_000);
  assert.equal(summaryText(state(holder)), "완료 — 성공 1 · 건너뜀 0 · 실패 0 · 소요 1분 23초");
});

test("aCancelledJobKnowsHowLongItRan", async () => {
  const { api, holder, clock } = await resolved();
  holder.startDownload();
  await flush();

  clock.ms += 9_000;
  api.send(jobDone("CANCELLED", 0, 0, 0));
  await flush();

  assert.equal(state(holder).elapsedMs, 9_000);
});

test("aNewJobStartsTheClockAgainAndClearsTheOldTime", async () => {
  const { api, holder, clock } = await finishedWithOneFailure();
  assert.equal(state(holder).elapsedMs, 0, "the helper's job ended at once");

  holder.retryFailed();
  await flush();
  assert.equal(state(holder).elapsedMs, null, "no time is shown while the job runs");
  clock.ms += 12_000;
  api.send(jobDone("COMPLETED", 1, 0, 0));
  await flush();

  assert.equal(state(holder).elapsedMs, 12_000, "only the retry is timed");
});

test("noTimeIsShownWhenTheConnectionBreaksBeforeTheJobEnds", async () => {
  const { api, holder, clock } = await resolved();
  holder.startDownload();
  await flush();

  clock.ms += 30_000;
  api.closeEvents();
  await flush();

  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(state(holder).elapsedMs, null);
});

test("resetClearsTheTime", async () => {
  const { api, holder, clock } = await resolved();
  holder.startDownload();
  await flush();
  clock.ms += 3_000;
  api.send(jobDone("COMPLETED", 1, 0, 0));
  await flush();

  holder.reset();

  assert.equal(state(holder).elapsedMs, null);
});

// ---- the album name typed by the user, which is also the folder name ------------------------

test("startingWithoutATypedAlbumNameSendsNone", async () => {
  const { api, holder } = await resolved();
  holder.onAlbumName("   ");

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).albumName, null, "blank is the same as nothing typed");
});

test("startingSendsTheTypedAlbumName", async () => {
  const { api, holder } = await resolved();
  holder.onAlbumName("내 앨범");

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).albumName, "내 앨범");
});

test("aRetrySendsTheTypedAlbumNameToo", async () => {
  const { api, holder } = await finishedWithOneFailure();
  holder.onAlbumName("내 앨범");

  holder.retryFailed();
  await flush();

  assert.equal(optionsOf(lastRequest(api)).albumName, "내 앨범");
});

test("theTypedAlbumNameLeavesThePreviewAndTheFileNamesAlone", async () => {
  const { holder } = await resolved();
  const before = state(holder).rows;

  holder.onAlbumName("내 앨범");

  assert.deepEqual(state(holder).rows, before);
});

test("resetClearsTheTypedAlbumName", async () => {
  const { holder } = await resolved();
  holder.onAlbumName("내 앨범");

  holder.reset();

  assert.equal(state(holder).albumName, null);
});

test("anAlbumNameTheUserDidNotTouchIsNotSentEvenThoughTheFieldShowsTheDefault", async () => {
  const { api, holder } = await resolved();
  assert.equal(albumNameText(state(holder)), "Sample", "the field shows the playlist title");

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).albumName, null, "the server uses the playlist title itself");
});

test("aTouchedFieldSendsItsTextEvenWhenItIsTheDefault", async () => {
  const { api, holder } = await resolved();
  holder.onAlbumName("Sample");

  holder.startDownload();
  await flush();

  assert.equal(optionsOf(api.jobRequests[0]).albumName, "Sample", "the user asked for the title on every file");
});

test("aTouchedFieldKeepsItsTextWhenSomethingIsResolvedAgain", async () => {
  const api = new FakeApi();
  const { holder } = await resolved({ api });
  holder.onAlbumName("내 앨범");

  api.resolveResponse = video();
  holder.resolve();
  await flush();

  assert.equal(albumNameText(state(holder)), "내 앨범", "typed text stays");
});

test("anUntouchedFieldFollowsTheNewDefaultWhenSomethingIsResolvedAgain", async () => {
  const api = new FakeApi();
  const { holder } = await resolved({ api });
  assert.equal(albumNameText(state(holder)), "Sample");

  api.resolveResponse = video();
  holder.resolve();
  await flush();

  assert.equal(albumNameText(state(holder)), "", "a video has no title to show");
});

test("theTypedAlbumNameIsNeverRemembered", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = rig({ store });

  holder.onAlbumName("내 앨범");
  timers.advance(5_000);
  await flush();
  holder.flushSettings();

  assert.deepEqual(store.saved, []);
});

test("theLyricsOptionLeavesThePreviewAndTheFileNamesAlone", async () => {
  const { holder } = await resolved();
  const before = state(holder).rows;

  holder.onSearchLyricsOnline(false);

  assert.equal(state(holder).phase, "PREVIEW");
  assert.deepEqual(state(holder).rows, before);
});

test("resetKeepsTheSearchLyricsOnlineOption", async () => {
  const { holder } = await resolved();
  holder.onSearchLyricsOnline(false);

  holder.reset();

  assert.equal(state(holder).phase, "IDLE");
  assert.equal(state(holder).searchLyricsOnline, false);
});

// ---- remembered options ---------------------------------------------------------------

const holderWith = (store: FakeSettingsStore) => rig({ store });

/** What the holder saves when only the folder differs from the defaults of a fresh FakeSettingsStore. */
const defaultsWith = (overrides: Partial<UserSettings> = {}): UserSettings =>
  userSettings({ outputDir: "C:/Music/xGetSongs", overwrite: false, includeRank: true, searchLyricsOnline: true, ...overrides });

async function assertSavedOnlyAfterTheQuietPeriod(timers: FakeTimers, store: FakeSettingsStore, expected: UserSettings): Promise<void> {
  timers.advance(399);
  await flush();
  assert.deepEqual(store.saved, [], "nothing is saved before 400 ms have passed");
  timers.advance(1);
  await flush();
  assert.deepEqual(store.saved, [expected]);
}

test("theOptionsStartFromTheStoredSettings", () => {
  const store = new FakeSettingsStore(userSettings({ outputDir: "D:/음악", overwrite: true, includeRank: false, searchLyricsOnline: false }));

  const { holder } = holderWith(store);

  const current = state(holder);
  assert.equal(current.outputDir, "D:/음악");
  assert.equal(current.overwrite, true);
  assert.equal(current.includeRank, false);
  assert.equal(current.searchLyricsOnline, false);
  assert.equal(current.phase, "IDLE");
  assert.equal(store.loadCalls, 1);
});

test("aStoredFileFromBeforeTheLyricsOptionStartsWithTheSearchOn", () => {
  // What an old settings file loads as: every key it has, and the default for the one it lacks.
  const { holder } = holderWith(new FakeSettingsStore(userSettings({ outputDir: "D:/Songs", overwrite: true })));

  assert.equal(state(holder).searchLyricsOnline, true);
});

test("changingTheLyricsSearchSavesTheValuesAfterAQuietPeriod", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onSearchLyricsOnline(false);

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ searchLyricsOnline: false }));
});

test("theLyricsOptionIsSavedWhileAJobRunsToo", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = await resolved({ store });
  holder.startDownload();
  await flush();
  assert.equal(state(holder).phase, "RUNNING");

  holder.onSearchLyricsOnline(false);

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ searchLyricsOnline: false }));
});

test("theLyricsOptionIsRestoredByTheNextHolderFromWhatWasSaved", () => {
  const store = new FakeSettingsStore();
  const first = holderWith(store).holder;
  first.onSearchLyricsOnline(false);
  first.flushSettings();
  assert.deepEqual(store.saved, [defaultsWith({ searchLyricsOnline: false })]);
  store.stored = store.saved[0]; // what the file holds after the first run

  const second = holderWith(store).holder;

  assert.equal(state(second).searchLyricsOnline, false);
});

test("settingTheLyricsOptionToTheValueItAlreadyHasSavesNothing", async () => {
  const store = new FakeSettingsStore(userSettings({ searchLyricsOnline: false }));
  const { holder, timers } = holderWith(store);

  holder.onSearchLyricsOnline(false);
  timers.advance(5_000);
  await flush();
  holder.flushSettings();

  assert.deepEqual(store.saved, []);
});

test("aMissingOrBlankStoredFolderFallsBackToTheDefaultFolder", () => {
  for (const stored of [null, "", "   "]) {
    const { holder } = holderWith(new FakeSettingsStore(userSettings({ outputDir: stored, overwrite: true })));

    assert.equal(state(holder).outputDir, "C:/Music/xGetSongs", `stored folder: ${stored}`);
    assert.equal(state(holder).overwrite, true, "the other options are still taken from the store");
  }
});

test("changingTheOutputFolderSavesTheValuesAfterAQuietPeriod", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOutputDir("D:/Songs");

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ outputDir: "D:/Songs" }));
});

test("changingOverwriteSavesTheValuesAfterAQuietPeriod", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOverwrite(true);

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ overwrite: true }));
});

test("changingTheRankOptionSavesTheValuesAfterAQuietPeriod", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onIncludeRank(false);

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ includeRank: false }));
});

test("theRankOptionIsSavedWhileAJobRunsToo", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = await resolved({ store });
  holder.startDownload();
  await flush();
  assert.equal(state(holder).phase, "RUNNING");

  holder.onIncludeRank(false);

  await assertSavedOnlyAfterTheQuietPeriod(timers, store, defaultsWith({ includeRank: false }));
});

test("quickChangesAreSavedOnceWithTheLastValues", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOutputDir("D:/S");
  timers.advance(300);
  holder.onOutputDir("D:/Songs");
  timers.advance(300);
  holder.onIncludeRank(false);
  timers.advance(399);
  await flush();
  assert.deepEqual(store.saved, [], "every change starts the quiet period again");
  timers.advance(1);
  await flush();

  assert.deepEqual(store.saved, [defaultsWith({ outputDir: "D:/Songs", includeRank: false })]);
  timers.advance(5_000);
  await flush();
  assert.equal(store.saved.length, 1, "no second save follows");
});

test("settingAValueThatIsAlreadySetSavesNothing", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOverwrite(false);
  holder.onIncludeRank(true);
  holder.onSearchLyricsOnline(true);
  holder.onOutputDir("C:/Music/xGetSongs");
  timers.advance(5_000);
  await flush();

  assert.deepEqual(store.saved, []);
});

test("repeatingAValueDoesNotRestartTheQuietPeriod", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOverwrite(true);
  timers.advance(300);
  holder.onOverwrite(true);
  timers.advance(99);
  await flush();
  assert.deepEqual(store.saved, []);
  timers.advance(1);
  await flush();

  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true })]);
});

test("flushSettingsSavesRightAwayAndCancelsThePendingSave", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);
  holder.onOverwrite(true);
  timers.advance(100);
  await flush();
  assert.deepEqual(store.saved, []);

  holder.flushSettings();

  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true })], "saved at once, without waiting");
  timers.advance(5_000);
  await flush();
  assert.equal(store.saved.length, 1, "the pending save was cancelled, so there is no second one");
});

test("flushSettingsAfterNoChangeSavesNothing", () => {
  const store = new FakeSettingsStore(userSettings({ outputDir: "D:/Songs", includeRank: false }));
  const { holder } = holderWith(store);

  holder.flushSettings();

  assert.deepEqual(store.saved, []);
});

test("flushSettingsLeavesAFileAloneThatWasLoadedAsTheDefaultsOrNormalised", () => {
  // A missing, corrupt or hand-edited file loads as the defaults (a blank folder): closing the app without touching an
  // option must leave such a file alone.
  for (const stored of [userSettings(), userSettings({ outputDir: "  " })]) {
    const store = new FakeSettingsStore(stored);
    const { holder } = holderWith(store);

    holder.flushSettings();

    assert.deepEqual(store.saved, [], `stored: ${JSON.stringify(stored)}`);
  }
});

test("flushSettingsAfterChangesSavesTheCurrentValues", () => {
  const store = new FakeSettingsStore(userSettings({ outputDir: "D:/Songs" }));
  const { holder } = holderWith(store);
  holder.onOverwrite(true);
  holder.onIncludeRank(false);

  holder.flushSettings();

  assert.deepEqual(store.saved, [defaultsWith({ outputDir: "D:/Songs", overwrite: true, includeRank: false })]);
});

test("flushSettingsAfterADebouncedSaveThatAlreadyWroteTheValuesSavesNothing", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);
  holder.onOverwrite(true);
  timers.advance(400);
  await flush();
  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true })]);

  holder.flushSettings();

  assert.equal(store.saved.length, 1, "the values are already on disk");
});

test("flushSettingsSavesAgainWhenTheValuesChangedAfterTheLastSave", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);
  holder.onOverwrite(true);
  timers.advance(400);
  await flush();
  holder.onIncludeRank(false);

  holder.flushSettings();

  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true }), defaultsWith({ overwrite: true, includeRank: false })]);
});

test("changingAValueAndBackBeforeTheQuietPeriodEndsSavesNothing", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);

  holder.onOverwrite(true);
  timers.advance(100);
  holder.onOverwrite(false);
  timers.advance(5_000);
  await flush();
  holder.flushSettings();

  assert.deepEqual(store.saved, [], "the values are the ones that are already stored");
});

test("changingBackAfterTheValuesWereSavedSavesTheOriginalAgain", async () => {
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);
  holder.onOverwrite(true);
  timers.advance(400);
  await flush();

  holder.onOverwrite(false);
  timers.advance(400);
  await flush();

  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true }), defaultsWith({ overwrite: false })]);
});

test("nothingIsSavedAtStartup", async () => {
  const store = new FakeSettingsStore(userSettings({ outputDir: " " }));

  const { timers } = holderWith(store);
  timers.advance(5_000);
  await flush();

  assert.deepEqual(store.saved, []);
});

test("resetKeepsTheOptionsAndSavesNothing", async () => {
  const store = new FakeSettingsStore(userSettings({ outputDir: "D:/Songs", overwrite: true, includeRank: false, searchLyricsOnline: false }));
  const { holder, timers } = holderWith(store);
  holder.onInput(playlistId);
  holder.resolve();
  await flush();

  holder.reset();
  timers.advance(5_000);
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "IDLE");
  assert.equal(current.outputDir, "D:/Songs");
  assert.equal(current.overwrite, true);
  assert.equal(current.includeRank, false);
  assert.equal(current.searchLyricsOnline, false);
  assert.deepEqual(store.saved, []);
});

test("theTypedInputAndTheSingleVideoRankAreNeverRemembered", async () => {
  // The stored form has room for exactly the four options, so neither the address nor the rank can be written.
  assert.deepEqual(Object.keys(userSettings()).sort(), ["includeRank", "outputDir", "overwrite", "searchLyricsOnline"]);

  // Typing or choosing a rank saves nothing by itself: one change of an option later, there is exactly one save.
  const store = new FakeSettingsStore();
  const { holder, timers } = holderWith(store);
  holder.onInput(playlistId);
  holder.onSingleRank(42);
  holder.onOverwrite(true);
  timers.advance(400);
  await flush();

  assert.deepEqual(store.saved, [defaultsWith({ overwrite: true })]);
});

// ---- downloading ----------------------------------------------------------------------

test("startingSendsTheOptionsAndMarksItemsWaiting", async () => {
  const { api, holder } = await resolved();
  holder.onOverwrite(true);
  holder.onOutputDir("D:/Songs");

  holder.startDownload();
  await flush();

  const request = api.jobRequests[0];
  assert.equal(api.jobRequests.length, 1);
  assert.equal(request.resolveId, "resolve-1");
  assert.equal(optionsOf(request).outputDir, "D:/Songs");
  assert.equal(optionsOf(request).overwrite, true);
  assert.equal(request.ranks, null);
  assert.equal(state(holder).phase, "RUNNING");
  assert.deepEqual(row(holder, 1).status, waiting);
  assert.deepEqual(row(holder, 2).status, skipped("비공개 영상"));
});

test("jobEventsDriveTheRowsAndTheSummary", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();

  api.send(itemStarted(1, "vid00000001", "001 Real - Name.m4a"));
  await flush();
  assert.deepEqual(row(holder, 1).status, downloading(null));
  assert.equal(row(holder, 1).fileName, "001 Real - Name.m4a");

  api.send(progress(1, "DOWNLOADING", 40));
  await flush();
  assert.deepEqual(row(holder, 1).status, downloading(40));

  api.send(progress(1, "FINISHING"));
  await flush();
  assert.deepEqual(row(holder, 1).status, finishing);

  api.send(itemDone(1, "001 Real - Name.m4a"), itemFailed(3, "boom"), jobDone("COMPLETED", 1, 0, 1));
  api.closeEvents();
  await flush();

  const current = state(holder);
  assert.deepEqual(row(holder, 1).status, done(null), "an event without an outcome leaves it not known");
  assert.deepEqual(row(holder, 3).status, failed("boom"));
  assert.equal(current.phase, "FINISHED");
  assert.deepEqual(current.summary, { succeeded: 1, skipped: 0, failed: 1 });
  assert.equal(current.jobStatus, "COMPLETED");
  assert.deepEqual(failedRanks(current), [3]);
});

// ---- the lyrics outcome of a finished row ----

test("aFinishedRowShowsTheLyricsOutcomeOfItsEventAndTheFinalFileName", async () => {
  for (const outcome of [...LYRICS_OUTCOMES, null]) {
    const { api, holder } = await resolved();
    holder.startDownload();
    await flush();
    api.send(itemStarted(1, "vid00000001", "001 Preview - Name.m4a"));

    api.send(itemDone(1, "001 Real - Name.m4a", outcome));
    await flush();

    assert.deepEqual(row(holder, 1).status, done(outcome), String(outcome));
    assert.equal(row(holder, 1).fileName, "001 Real - Name.m4a", String(outcome));
  }
});

test("everyFinishedRowKeepsItsOwnOutcome", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();

  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), itemDone(3, "003 A3 - T3.m4a", "SEARCH_OFF"));
  await flush();

  assert.equal(statusLabel(row(holder, 1).status), "완료 · 가사 ✓ 인터넷");
  assert.equal(statusLabel(row(holder, 3).status), "완료 · 가사 없음 (검색 끔)");
  assert.deepEqual(row(holder, 2).status, skipped("비공개 영상"));
});

test("aNewRunReplacesTheOutcomeOfTheRowsItRuns", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();
  assert.deepEqual(row(holder, 1).status, done("ONLINE"));

  holder.startDownload();
  await flush();
  assert.deepEqual(row(holder, 1).status, waiting, "the old outcome is gone as soon as the new run starts");

  api.send(itemDone(1, "001 A1 - T1.m4a", "NOT_FOUND"));
  await flush();

  assert.deepEqual(row(holder, 1).status, done("NOT_FOUND"));
});

test("aRetryReplacesTheFailureWithTheOutcomeAndLeavesTheOtherOutcomesAlone", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "DESCRIPTION"), itemFailed(3, "boom"), jobDone("COMPLETED", 1, 0, 1));
  api.closeEvents();
  await flush();
  holder.retryFailed();
  await flush();

  api.send(itemDone(3, "003 A3 - T3.m4a", "NOT_FOUND"), jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();

  assert.deepEqual(row(holder, 1).status, done("DESCRIPTION"));
  assert.deepEqual(row(holder, 3).status, done("NOT_FOUND"));
  assert.deepEqual(failedRanks(state(holder)), []);
});

test("anEndedOrBrokenJobKeepsTheOutcomeOfTheRowsThatFinished", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), itemStarted(3, "vid00000003", "003 A3 - T3.m4a"));
  await flush();

  api.closeEvents(); // no JobDone: the connection broke while row 3 was in flight
  await flush();

  assert.deepEqual(row(holder, 1).status, done("ONLINE"));
  assert.deepEqual(row(holder, 3).status, ready);
  assert.equal(state(holder).error, "서버와의 연결이 끊어졌습니다.");
});

test("aCancelledJobKeepsTheOutcomeOfTheRowsThatFinished", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "SEARCH_OFF"), jobDone("CANCELLED", 1, 0, 0));
  api.closeEvents();
  await flush();

  assert.deepEqual(row(holder, 1).status, done("SEARCH_OFF"));
  assert.deepEqual(row(holder, 3).status, ready);
});

test("resetClearsTheFinishedRowsWithTheirOutcomes", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();
  assert.deepEqual(row(holder, 1).status, done("ONLINE"));

  holder.reset();

  assert.equal(state(holder).rows.length, 0);
  assert.equal(state(holder).phase, "IDLE");
  assert.equal(state(holder).summary, null);
});

test("skippedItemsShowTheirReason", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();

  api.send(itemSkipped(1, "이미 존재"));
  await flush();

  assert.deepEqual(row(holder, 1).status, skipped("이미 존재"));
});

test("retryRunsOnlyTheFailedRanksAndKeepsTheOtherResults", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "DESCRIPTION"), itemFailed(3, "boom"), jobDone("COMPLETED", 1, 0, 1));
  api.closeEvents();
  await flush();

  holder.retryFailed();
  await flush();

  assert.deepEqual(lastRequest(api).ranks, [3]);
  assert.equal(state(holder).phase, "RUNNING");
  assert.deepEqual(row(holder, 1).status, done("DESCRIPTION"));
  assert.deepEqual(row(holder, 3).status, waiting);
});

test("retryOfASingleVideoRerunsTheWholeVideo", async () => {
  const api = new FakeApi();
  api.resolveResponse = video();
  const { holder } = await resolved({ api });
  holder.onSingleRank(7);
  holder.startDownload();
  await flush();
  api.send(itemFailed(7, "boom"), jobDone("COMPLETED", 0, 0, 1));
  api.closeEvents();
  await flush();

  holder.retryFailed();
  await flush();

  assert.equal(lastRequest(api).ranks, null);
  assert.equal(optionsOf(lastRequest(api)).singleRank, 7);
});

test("cancelAsksTheServerToStopTheJob", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();

  holder.cancel();
  await flush();
  assert.deepEqual(api.cancelled, ["job-1"]);

  api.send(jobDone("CANCELLED", 0, 0, 0));
  api.closeEvents();
  await flush();

  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(state(holder).jobStatus, "CANCELLED");
  assert.deepEqual(row(holder, 1).status, ready);
});

test("aStreamThatEndsWithoutJobDoneIsReportedAsALostConnection", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();

  api.closeEvents();
  await flush();

  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(state(holder).error, "서버와의 연결이 끊어졌습니다.");
});

test("aJobThatCannotStartReturnsToThePreview", async () => {
  const { api, holder } = await resolved();
  api.startError = apiError("출력 폴더를 만들 수 없습니다");

  holder.startDownload();
  await flush();

  assert.equal(state(holder).phase, "PREVIEW");
  assert.equal(state(holder).error, "출력 폴더를 만들 수 없습니다");
  assert.deepEqual(row(holder, 1).status, ready);
});

test("startingWithoutAnOutputFolderIsRefused", async () => {
  const { api, holder } = await resolved();
  holder.onOutputDir("  ");

  holder.startDownload();
  await flush();

  assert.equal(api.jobRequests.length, 0);
  assert.equal(state(holder).error, "출력 폴더를 지정하세요.");
});

test("resetReturnsToAnEmptyScreenButKeepsSettings", async () => {
  const { holder } = await resolved();
  holder.onOutputDir("D:/Songs");

  holder.reset();

  const current = state(holder);
  assert.equal(current.phase, "IDLE");
  assert.equal(current.rows.length, 0);
  assert.equal(current.outputDir, "D:/Songs");
});

// ---- tools ----------------------------------------------------------------------------

test("refreshToolsLoadsTheStatus", async () => {
  const { api, holder } = rig();

  holder.refreshTools();
  await flush();

  assert.deepEqual(state(holder).tools, api.toolsStatus);
});

test("installingYtDlpReportsTheResultAndRefreshesTheStatus", async () => {
  const { api, holder } = rig();

  holder.installYtDlp();
  await flush();

  assert.equal(api.installCalls, 1);
  assert.equal(state(holder).toolMessage, "installed");
  assert.equal(state(holder).toolBusy, false);
  assert.deepEqual(state(holder).tools, api.toolsStatus);
});

// ---- job endings and failures ---------------------------------------------------------

test("rowsInFlightGoBackToReadyWhenACancelledJobEnds", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemStarted(1, "vid00000001", "001 A1 - T1.m4a"), progress(1, "DOWNLOADING", 40));
  await flush();
  assert.deepEqual(row(holder, 1).status, downloading(40));

  api.send(jobDone("CANCELLED", 0, 0, 0));
  api.closeEvents();
  await flush();

  assert.deepEqual(row(holder, 1).status, ready);
  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(state(holder).jobStatus, "CANCELLED");
});

test("rowsInFlightGoBackToReadyWhenTheStreamBreaks", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemStarted(1, "vid00000001", "001 A1 - T1.m4a"), progress(1, "FINISHING"));
  await flush();
  assert.deepEqual(row(holder, 1).status, finishing);

  api.closeEvents();
  await flush();

  assert.equal(state(holder).phase, "FINISHED");
  assert.equal(state(holder).error, "서버와의 연결이 끊어졌습니다.");
  assert.deepEqual(row(holder, 1).status, ready);
});

test("aRawExceptionDuringResolveDoesNotLeaveTheScreenResolving", async () => {
  const api = new FakeApi();
  api.resolveError = new Error("boom");
  const { holder } = await resolved({ api });

  assert.equal(state(holder).phase, "IDLE");
  assert.ok((state(holder).error ?? "").includes("boom"));
});

test("aRawExceptionFromToolActionsClearsTheBusyFlag", async () => {
  const api = new FakeApi();
  api.toolsError = new Error("boom");
  const { holder } = rig({ api });

  holder.installYtDlp();
  await flush();

  assert.equal(state(holder).toolBusy, false);
  assert.ok((state(holder).toolMessage ?? "").includes("boom"));

  holder.refreshTools();
  await flush();

  assert.ok((state(holder).error ?? "").includes("boom"));
});

test("aFailedStartFromFinishedKeepsThePreviousResults", async () => {
  const { api, holder } = await finishedWithOneFailure();
  api.startError = apiError("출력 폴더를 만들 수 없습니다");

  holder.retryFailed();
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "FINISHED");
  assert.deepEqual(row(holder, 3).status, failed("boom"));
  assert.deepEqual(row(holder, 1).status, done(null));
  assert.deepEqual(current.summary, { succeeded: 1, skipped: 0, failed: 1 });
  assert.equal(current.jobStatus, "COMPLETED");
  assert.deepEqual(failedRanks(current), [3]);
  assert.equal(current.error, "출력 폴더를 만들 수 없습니다");
});

test("aRawExceptionWhileStartingARetryRestoresThePreviousScreen", async () => {
  const { api, holder } = await finishedWithOneFailure();
  api.startError = new Error("boom");

  holder.retryFailed();
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "FINISHED");
  assert.deepEqual(failedRanks(current), [3]);
  assert.ok((current.error ?? "").includes("boom"));
});

test("aFailedSecondLookupKeepsThePreviewUsable", async () => {
  const { api, holder } = await resolved();
  const rowsBefore = state(holder).rows;
  api.resolveError = apiError("x");

  holder.onInput(playlistId);
  holder.resolve();
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "PREVIEW");
  assert.deepEqual(current.rows.map((r) => r.item.rank), rowsBefore.map((r) => r.item.rank));
  assert.deepEqual(current.rows.map((r) => r.status), rowsBefore.map((r) => r.status));
  assert.equal(current.error, "x");
});

test("aFailedSecondLookupShowsTheOldRowsWithTheOptionAsItIsNow", async () => {
  const { api, holder } = await resolved();
  assert.equal(row(holder, 1).fileName, "001 A1 - T1.m4a");
  api.resolveGate = new Deferred();
  api.resolveError = apiError("x");
  holder.onInput(playlistId);
  holder.resolve();
  await flush();
  assert.equal(state(holder).phase, "RESOLVING");

  holder.onIncludeRank(false); // while the lookup is held: nothing is rewritten yet
  api.resolveGate.resolve();
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "PREVIEW");
  assert.equal(current.error, "x");
  assert.equal(current.includeRank, false);
  assert.equal(row(holder, 1).fileName, "A1 - T1.m4a");
  assert.equal(row(holder, 3).fileName, "A3 - T3.m4a");
  assert.deepEqual(current.rows.map((r) => r.item.rank), [1, 2, 3]);
  assert.deepEqual(row(holder, 1).status, ready);
});

test("aFailedLookupAfterAFinishedJobKeepsTheResults", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();
  api.resolveError = apiError("x");

  holder.resolve();
  await flush();

  const current = state(holder);
  assert.equal(current.phase, "FINISHED");
  assert.deepEqual(current.summary, { succeeded: 1, skipped: 0, failed: 0 });
  assert.deepEqual(row(holder, 1).status, done("ONLINE"));
  assert.equal(current.error, "x");
});

// ---- not in the Kotlin file: what the port adds ----------------------------------------------

test("subscribersAreToldOfEveryChangeUntilTheyStop", () => {
  const { holder } = rig();
  const seen: string[] = [];
  const stop = holder.subscribe((current) => seen.push(current.input));

  holder.onInput("a");
  holder.onInput("ab");
  stop();
  holder.onInput("abc");

  assert.deepEqual(seen, ["a", "ab"]);
});

test("whatAJobReportsAfterTheScreenWasResetIsIgnored", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  holder.reset();

  api.send(itemDone(1, "001 A1 - T1.m4a", "ONLINE"), jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();

  assert.equal(state(holder).phase, "IDLE");
  assert.deepEqual(state(holder).rows, []);
  assert.equal(state(holder).summary, null);
  assert.equal(state(holder).error, null);
});

test("aFailureThatIsNotTheServersMessageSaysThatCommunicationFailed", async () => {
  const api = new FakeApi();
  api.resolveError = "invoke is not defined"; // a rejection that is only text
  const { holder } = await resolved({ api });

  assert.equal(state(holder).error, "서버와 통신 중 오류가 발생했습니다: invoke is not defined");
});

// ---- what the port adds after the review of stage 3 ------------------------------------------------

test("cancelBeforeTheNewJobHasAnIdDoesNotCancelThePreviousJob", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(jobDone("COMPLETED", 1, 0, 0));
  api.closeEvents();
  await flush();
  assert.equal(state(holder).phase, "FINISHED");

  api.startGate = new Deferred();
  holder.startDownload();
  await flush();
  assert.equal(state(holder).phase, "RUNNING");
  holder.cancel();
  await flush();

  assert.deepEqual(api.cancelled, [], "job-1 is over and job-2 has no id yet: there is nothing to cancel");
  assert.equal(state(holder).error, null);

  api.startGate.resolve();
  await flush();
  holder.cancel();
  await flush();

  assert.deepEqual(api.cancelled, ["job-2"]);
});

test("theEndOfAnOldWatchDoesNotMarkTheNewJobAsLost", async () => {
  const { api, holder } = await resolved();
  holder.startDownload();
  await flush();
  api.send(jobDone("COMPLETED", 1, 0, 0)); // the job has reported its end but its event stream is still open
  await flush();
  assert.equal(state(holder).phase, "FINISHED");

  holder.startDownload();
  await flush();
  assert.equal(state(holder).phase, "RUNNING");
  api.closeEventsOf(1); // the old stream ends only now
  await flush();

  assert.equal(state(holder).phase, "RUNNING", "the new job is still running");
  assert.equal(state(holder).error, null);

  api.send(jobDone("COMPLETED", 2, 0, 0));
  api.closeEvents();
  await flush();

  assert.equal(state(holder).jobStatus, "COMPLETED");
});

test("theElapsedTimeDoesNotFollowTheWallClock", async () => {
  mock.timers.enable({ apis: ["Date"], now: 0 });
  try {
    const api = new FakeApi();
    const holder = new AppStateHolder({ api, defaultOutputDir: "C:/Music/xGetSongs", scheduler: new FakeTimers() });
    holder.onInput(playlistId);
    holder.resolve();
    await flush();
    holder.startDownload();
    await flush();

    mock.timers.tick(3_600_000); // the wall clock jumps by an hour: a clock change or a time sync
    api.send(jobDone("COMPLETED", 1, 0, 0));
    api.closeEvents();
    await flush();

    const elapsed = state(holder).elapsedMs;
    assert.ok(elapsed !== null && elapsed < 60_000, `the job ran for well under a minute but the elapsed time is ${elapsed} ms`);
  } finally {
    mock.timers.reset();
  }
});
