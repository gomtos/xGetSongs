# Playlist Folder and ID3 Tags Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Save a playlist's mp3 files in `<output folder>\<playlist name>\` and write ID3 tags (title, artist, album artist, album, track number, comment, cover) into every file.

**Architecture:** `shared` gets a pure-Kotlin folder-name rule. `engine` gets an `Id3Tagger` that runs after yt-dlp: one ffmpeg stream-copy pass fed by an ffmetadata file (values never go through the command line). `server` picks the sub-folder and passes the album; `app` shows where files will go.

**Tech Stack:** unchanged (Kotlin 2.4.20, Ktor 3.6.0, Compose Multiplatform 1.12.1, JUnit 5 through `kotlin("test")`). No new dependencies.

Design spec: `docs/superpowers/specs/2026-10-04-xgetsongs-design.md`, section 6.4 (already written; this plan implements it).

## Global Constraints

- Commit directly on `main`, no branches. Every commit message ends with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- `shared/src/commonMain` stays pure Kotlin (no `java.*`); the Gradle task `checkCommonPurity` enforces it.
- External programs are started only through `ProcessRunner` with argument lists, never through a shell. No tag value may appear on a command line (ffmetadata file instead).
- `app` talks to the server only through the HTTP API; it must not import `engine` or `server`.
- User-visible texts are Korean; code, identifiers and commit messages are English.
- `TitleParser.kt` and `TitleParserTest.kt` contain typographic quotes: do not touch them. Edit any file with non-ASCII characters in a way that keeps UTF-8 intact (the Edit tool is fine; verify with `git diff`).
- Tests: `.\gradlew.bat check` must stay green. Current totals: shared 70, engine 94, server 30, app 47.
- Real-tool tests are tagged `integration` and run only with `.\gradlew.bat :engine:integrationTest`; they skip themselves when the tool is missing.

## File Structure

- `shared/.../filename/FilenameFormatter.kt` (modify): `folderName`.
- `engine/.../tags/TrackTags.kt`, `Ffmetadata.kt`, `Id3Tagger.kt` (create): tag model, ffmetadata rendering, the ffmpeg pass.
- `engine/.../ytdlp/YtDlpCommands.kt` (modify): thumbnail flags; new `FfmpegCommands.kt` (create) in the same package.
- `engine/.../job/ItemDownloader.kt`, `Services.kt`, `DefaultDownloadService.kt` (modify): carry artist/track/album, run the tagger.
- `server/.../Application.kt` (modify): sub-folder and album.
- `app/.../state/Labels.kt`, `ui/OptionsPanel.kt` (modify): destination line.

---

### Task 1: Folder name rule

**Files:**
- Modify: `shared/src/commonMain/kotlin/com/xgetsongs/shared/filename/FilenameFormatter.kt`
- Test: `shared/src/commonTest/kotlin/com/xgetsongs/shared/filename/FilenameFormatterTest.kt`

**Interfaces:**
- Produces: `FilenameFormatter.folderName(title: String?): String`, `const val MAX_FOLDER_LENGTH = 80`, `const val UNTITLED_PLAYLIST = "재생목록"` (both public in `FilenameFormatter`).
- Consumes: the existing private `truncate` and public `sanitize` in the same object.

Rules for `folderName` (spec 6.4), applied in this order:
1. `null` or blank input gives `UNTITLED_PLAYLIST`.
2. `sanitize(title)` (forbidden characters to full-width, control characters dropped, trimmed), then collapse nothing else.
3. Cut to `MAX_FOLDER_LENGTH` UTF-16 units with the existing `truncate` (ends with `…`, never splits a surrogate pair).
4. Remove trailing `.` and space characters (`trimEnd('.', ' ')`); remove leading spaces.
5. If the result is empty, return `UNTITLED_PLAYLIST`.
6. Reserved Windows device names: take the text before the first `.` (call it `stem`, with trailing spaces trimmed). If `stem` equals, ignoring case, one of `CON PRN AUX NUL COM1..COM9 LPT1..LPT9`, insert `_` right after `stem` (`CON` becomes `CON_`, `con.txt` becomes `con_.txt`, `COM1.` cannot occur because of step 4). `Console`, `CON TEST` and `NULL` are not reserved.

- [ ] **Step 1: Write failing tests** in `FilenameFormatterTest` (same style as the existing tests, kotlin.test). Cover at least: `null`, `""` and `"   "` give `재생목록`; `Melon Daily Top 100` is unchanged; `Best: Hits? <2024>` becomes full-width (`Best： Hits？ ＜2024＞`); trailing dots/spaces removed (`Mix... `); `"..."` gives `재생목록`; control characters dropped; an 81-character title gives exactly 80 UTF-16 units ending in `…`; a title whose cut would land inside a surrogate pair (use an emoji such as `"🎵"` repeated) never ends in a lone high surrogate; reserved names `CON`, `nul`, `Com1`, `LPT9`, `con.txt` get the `_`; `Console`, `CON TEST`, `NULL` are unchanged; a Korean title is unchanged.
- [ ] **Step 2: Run and see them fail**

Run: `.\gradlew.bat :shared:jvmTest --tests "*FilenameFormatterTest*"`
Expected: FAIL (unresolved reference `folderName`).

- [ ] **Step 3: Implement** `folderName` and the two constants in `FilenameFormatter`; keep the file's existing style and KDoc density.
- [ ] **Step 4: Run** `.\gradlew.bat :shared:check`
Expected: BUILD SUCCESSFUL, `FilenameFormatterTest` passes, `checkCommonPurity` passes.
- [ ] **Step 5: Commit**

```powershell
git add shared
git commit -m "feat(shared): add folder name rule for playlist folders" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: ID3 tagging in the engine

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/tags/TrackTags.kt`, `Ffmetadata.kt`, `Id3Tagger.kt`
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/FfmpegCommands.kt`
- Modify: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/YtDlpCommands.kt`, `job/ItemDownloader.kt`, `Services.kt`, `job/DefaultDownloadService.kt`
- Modify tests: `engine/src/test/kotlin/com/xgetsongs/engine/testutil/Fakes.kt`, `ytdlp/YtDlpCommandsTest.kt`, `job/DefaultDownloadServiceTest.kt`, `integration/RealYtDlpIntegrationTest.kt`
- Create tests: `tags/FfmetadataTest.kt`, `tags/Id3TaggerTest.kt`, `ytdlp/FfmpegCommandsTest.kt`, `integration/RealFfmpegTaggingIntegrationTest.kt`

**Interfaces:**
- Consumes: `ProcessRunner.run(command, onStdout, onStderr): Int`, `ToolPathProvider.current(): ToolPaths` (with `ffmpeg: Path?`), `Failure(kind: FailureKind, message: String)` and `FailureKind {TRANSIENT, UNAVAILABLE, FATAL, OTHER}` from `engine/ytdlp`, `ParsedInput.Video(id).canonicalUrl`.
- Produces:
  - `data class TrackTags(val title: String, val artist: String, val album: String?, val albumArtist: String, val trackNumber: Int, val comment: String?)`
  - `object Ffmetadata { fun render(tags: TrackTags): String }`
  - `object FfmpegCommands { fun tag(ffmpeg: Path, input: Path, cover: Path?, metadataFile: Path, output: Path): List<String> }`
  - `class Id3Tagger(runner: ProcessRunner, tools: ToolPathProvider) { suspend fun tag(file: Path, cover: Path?, tags: TrackTags): Failure? }` (null on success)
  - `DownloadRequest` gets a last parameter `val album: String? = null`.
  - `PreparedItem(item, fileName, artist, track, album)` (artist/track are the parsed originals, album the playlist title or null) and `ItemDownloader.prepare(item: ResolvedItem, album: String? = null)`. `ItemDownloader.download` keeps its signature.

**Behaviour to implement**

1. `YtDlpCommands.download` adds `--write-thumbnail`, `--convert-thumbnails`, `jpg` (before `-o`). yt-dlp then leaves `<videoId>.jpg` next to `<videoId>.mp3` in the work directory.
2. `Ffmetadata.render`: first line `;FFMETADATA1`, then one `key=value` line per tag in this order: `title`, `artist`, `album_artist`, `album` (only when not null and not blank), `track` (the number as a plain integer), `comment` (only when not null and not blank). Values are escaped per ffmpeg's ffmetadata rules: a backslash is put before `=`, `;`, `#`, `\` and before a line break (the line break itself is kept after the backslash). Lines end with `\n`; the string is written as UTF-8.
3. `FfmpegCommands.tag` builds exactly this list (the optional parts only when `cover != null`):

```
[ffmpeg, -hide_banner, -loglevel, error, -nostdin, -y,
 -i, input, (-i, cover,) -f, ffmetadata, -i, metadataFile,
 -map, 0:a, (-map, 1:v,) -map_chapters, -1, -map_metadata, <index of the ffmetadata input: 1 without cover, 2 with cover>,
 -c:a, copy,
 (-c:v, mjpeg, -q:v, 2, -vf, crop=min(iw\,ih):min(iw\,ih), -disposition:v, attached_pic,
  -metadata:s:v, title=Album cover, -metadata:s:v, comment=Cover (front),)
 -id3v2_version, 3, output]
```
   `output` must end in `.mp3`. The `-vf` value is a single argument without quotes (`\,` is the filter-graph escape for a comma; do not "fix" it).
4. `Id3Tagger.tag(file, cover, tags)`: read `tools.current().ffmpeg`; if null return `Failure(FailureKind.FATAL, "ffmpeg를 찾을 수 없습니다.")`. Otherwise write the ffmetadata file (`<name>.ffmeta` in `file`'s directory) and run the command with output `<name>.tagged.mp3` in the same directory, collecting stderr lines. On exit code 0 and an existing output file, move the output over `file` with `REPLACE_EXISTING`; return null. On a non-zero exit code, or when the output file is missing, return `Failure(FailureKind.OTHER, "ID3 태그를 쓰지 못했습니다: " + <last non-blank stderr line, at most 200 chars, or "알 수 없는 오류">)`. Catch `IOException` around the file work and return the same kind of failure (message with `e.message`). Never catch `CancellationException`. Always delete the `.ffmeta` and `.tagged.mp3` temp files in a `finally`.
5. `ItemDownloader`: `prepare` stores the parsed artist, parsed track (the same values that feed `FilenameFormatter.format`, including the low-confidence second parse) and the album in `PreparedItem`. After yt-dlp succeeds and `<videoId>.mp3` exists, `download` looks for `<videoId>.jpg` in the work directory (cover = that path when it is a regular file, else null), builds `TrackTags(title = track, artist = artist, album = album, albumArtist = artist, trackNumber = item.rank, comment = canonicalUrl)` and calls an internal `Id3Tagger(runner, tools)`; a returned `Failure` becomes `DownloadResult.Failed`. `ItemDownloader`'s constructor does not change.
6. `DownloadRequest.album` is passed by `DefaultDownloadService` as `downloader.prepare(item, request.album)`.

**Tests**

- [ ] **Step 1: Write failing tests**
  - `FfmetadataTest`: header and key order; album/comment omitted when null or blank; escaping of `=`, `;`, `#`, `\`, and a newline; Korean text and quotes (`"Golden" 'x'`) pass through unchanged.
  - `FfmpegCommandsTest`: the exact list with and without cover (compare whole lists).
  - `Id3TaggerTest` with `FakeProcessRunner`: success replaces the original file with the fake output and removes the temp files (the fake handler writes the file named by the last argument and, inside the handler, captures the ffmetadata file's text so the test can assert it); a command with a cover passes `-i <cover>`; exit code 1 with stderr gives `OTHER` with the last stderr line in the message; exit code 0 without an output file gives `OTHER`; ffmpeg null gives `FATAL` and runs nothing.
  - `YtDlpCommandsTest`: update expectations for the thumbnail flags (and add one test that `--write-thumbnail` comes before `-o`).
  - `DefaultDownloadServiceTest`: update the fake handlers so an ffmpeg command (first element equals the `TEST_TOOLS.ffmpeg` path) produces the tagged output instead of failing; add tests that (a) the tagging command runs once per item after the yt-dlp command, (b) the ffmetadata content captured in the handler has the item's parsed artist/title, `track=<rank>` and, for a request with `album = "My List"`, `album=My List` and no album line when `album` is null, (c) a tagging failure makes the item `ItemFailed` with the tagger's message and the job continues with the other items.
  - Update `Fakes.kt` helpers as needed (keep `writeFakeMp3` working; add a helper for the tagged output).
- [ ] **Step 2: Run to see them fail:** `.\gradlew.bat :engine:test`
- [ ] **Step 3: Implement** items 1-6.
- [ ] **Step 4: Run** `.\gradlew.bat :engine:check` and `.\gradlew.bat check`
  Expected: BUILD SUCCESSFUL, no test fails, no test is skipped (a `@Test` function must return `Unit`).
- [ ] **Step 5: Real-tool tests** (ffmpeg and ffprobe are installed on this machine; they are found next to each other, `ToolLocator` locates ffmpeg):
  - `RealFfmpegTaggingIntegrationTest` (`@Tag("integration")`, skipped when ffmpeg or ffprobe is missing, same skipping style as `RealYtDlpIntegrationTest`): generate a 1-second silent mp3 and a 640x360 red jpg with ffmpeg (`-f lavfi -i anullsrc=r=44100:cl=mono -t 1 ... .mp3`, `-f lavfi -i color=c=red:s=640x360 -frames:v 1 ... .jpg`), run the real `Id3Tagger` with `SystemProcessRunner` on a title containing Korean text and double and single quotes, an album, and the cover. Read the result with `ffprobe -v error -of json -show_entries format_tags:stream=codec_type,codec_name,width,height,disposition`. Assert: `title`, `artist`, `album_artist`, `album`, `track`, `comment` equal the inputs exactly; there is exactly one audio stream (mp3) and one video stream (mjpeg, 360x360, `attached_pic` = 1). A second test without a cover: only the audio stream.
  - Extend the existing download test in `RealYtDlpIntegrationTest` so the downloaded file is read with ffprobe and has `title=Me at the zoo`, `artist=jawed`, `album=Test Album` (pass it through `DownloadRequest.album`), `track=1`, a comment starting with `https://www.youtube.com/watch?v=`, and one attached-picture video stream. Run: `.\gradlew.bat :engine:integrationTest` (it reaches YouTube and downloads one 19-second video; this was approved for this project). If the thumbnail file name is not `<videoId>.jpg`, fix the lookup to what yt-dlp really writes and report it.
- [ ] **Step 6: Commit** (one or two commits, tests with the code they cover)

```powershell
git add engine
git commit -m "feat(engine): write ID3 tags and cover art after each download" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Playlist folder in the server and destination line in the app

**Files:**
- Modify: `engine/src/main/kotlin/com/xgetsongs/engine/output/LocalFolderSink.kt` (make `directory` a public read-only property: `val directory: Path`)
- Modify: `server/src/main/kotlin/com/xgetsongs/server/Application.kt`, `server/src/test/kotlin/com/xgetsongs/server/RoutesTest.kt` (and `TestSupport.kt` if needed)
- Modify: `app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt`, `app/src/commonMain/kotlin/com/xgetsongs/app/ui/OptionsPanel.kt`, `app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt`
- Modify: `README.md`

**Interfaces:**
- Consumes: `FilenameFormatter.folderName` (Task 1), `DownloadRequest.album` (Task 2), `ResolveResponse.kind/playlistTitle`, `JobOptions.outputDir`.
- Produces: `fun destinationLabel(state: UiState): String?` in `Labels.kt`.

**Behaviour**

1. In `POST /jobs` (LOCAL mode): when `resolved.kind == InputKind.PLAYLIST`, the sink directory is `<outputDir>/<FilenameFormatter.folderName(resolved.playlistTitle)>` and the request's `album` is `resolved.playlistTitle` (the original text, not the folder name; null stays null). For `InputKind.VIDEO` the sink directory is the output directory itself and `album` is null. The existing validation of `outputDir` (absolute, creatable) is unchanged; the sub-folder is created by `LocalFolderSink`'s constructor. A retry (`ranks` set) of a playlist job uses the same folder because it uses the same `resolveId`.
2. `destinationLabel(state)`: null when nothing is resolved or `state.outputDir` is blank. For a resolved video: `저장 위치: <outputDir>`. For a resolved playlist: `저장 위치: <outputDir>\<folderName>` where `<outputDir>` has trailing `\` and `/` removed and `<folderName>` is `FilenameFormatter.folderName(state.resolved.playlistTitle)`.
3. `OptionsPanel` shows that label (small text, one line, ellipsis) under the output-folder row when it is not null.
4. `README.md`: replace the file-name section's implied "all files in one folder" with the layout (playlist: folder named after the playlist; single video: output folder) and add a short "ID3 태그" paragraph listing the fields (title, artist, album artist, album, track number, comment URL, cover). Remove ID3/cover from any "not supported" wording if present.

**Tests**
- [ ] **Step 1: Write failing tests**
  - `RoutesTest` (use the existing fake `DownloadService` that captures the `DownloadRequest`): a playlist job gets a `LocalFolderSink` whose `directory` equals `<tempOut>/<playlist title>`, the folder exists on disk, and `album` equals the playlist title; a title with `:` and `?` produces the full-width folder name; a null playlist title gives `재생목록`; a single-video job gets `directory == <tempOut>` and `album == null`; a retry with `ranks` uses the same directory; `outputDir` validation errors are unchanged (existing tests stay green).
  - `LabelsTest`: `destinationLabel` for no resolve, blank output dir, video, playlist (with a trailing backslash in the output dir, and a title that needs sanitizing).
- [ ] **Step 2: Run to see them fail:** `.\gradlew.bat :server:test :app:desktopTest`
- [ ] **Step 3: Implement** behaviours 1-4.
- [ ] **Step 4: Run** `.\gradlew.bat check`
  Expected: BUILD SUCCESSFUL; counts go up, nothing is skipped or failing.
- [ ] **Step 5: Commit**

```powershell
git add server app engine README.md
git commit -m "feat: save playlists in a folder named after them and show the destination" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Option to leave the rank out of the file name

Added after the user's request "파일명에 순번(001 ~ 999) 포함 여부를 옵션으로 처리해". Default stays "include" (nothing changes unless the user turns it off).

**Files:**
- Modify: `shared/src/commonMain/kotlin/com/xgetsongs/shared/filename/FilenameFormatter.kt`, `shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt`
- Modify: `engine/src/main/kotlin/com/xgetsongs/engine/Services.kt`, `engine/.../job/ItemDownloader.kt`, `engine/.../job/DefaultDownloadService.kt`
- Modify: `server/src/main/kotlin/com/xgetsongs/server/Application.kt`
- Modify: `app/src/commonMain/kotlin/com/xgetsongs/app/state/UiState.kt`, `.../state/AppStateHolder.kt`, `.../ui/OptionsPanel.kt`
- Modify tests next to each changed class; Modify: `README.md`

**Interfaces:**
- Produces: `FilenameFormatter.format(rank: Int, artist: String, title: String, includeRank: Boolean = true): String`; `JobOptions.includeRank: Boolean = true` (last parameter); `DownloadRequest.includeRank: Boolean = true` (last parameter); `ItemDownloader.prepare(item: ResolvedItem, album: String? = null, includeRank: Boolean = true)`; `UiState.includeRank: Boolean = true`; `AppStateHolder.onIncludeRank(value: Boolean)`.

**Behaviour (spec 6.3, already written)**

1. `format(..., includeRank = false)` returns `{artist} - {title}.mp3`: no rank, no leading space. The artist is cut at `MAX_ARTIST_LENGTH`, the base name (without extension) at `MAX_BASE_LENGTH`, the title budget is `MAX_BASE_LENGTH - "{artist} - ".length`, an empty title still becomes `untitled`, trailing dots/spaces are trimmed, forbidden characters are mapped as before. `rank` is still validated with `require` (1..999) even when it is not printed (callers always have a valid rank). With `includeRank = true` the result is byte-for-byte what it is today.
2. `JobOptions.includeRank` travels in `POST /jobs`; `Application.kt` copies it into `DownloadRequest.includeRank`; `DefaultDownloadService` calls `downloader.prepare(item, request.album, request.includeRank)`. The ID3 track number (TRCK) is unchanged: it is still the rank, whatever the option says. The `resolve` response (`ResolvedItem.expectedFileName`) is unchanged (computed with the rank).
3. App: `UiState.includeRank` (default true, kept by `reset()` like `overwrite`), `AppStateHolder.onIncludeRank(value)` updates it and, when the screen shows a preview (`phase == PREVIEW`), recomputes the displayed `fileName` of every available row with `FilenameFormatter.format(item.rank, item.artist, item.track, includeRank)`; rows of unavailable items keep their text. Resolving (`doResolve`/`previewRow`) and the single-video rank change (`withRank`) must use the current `includeRank` too. `jobOptions(state)` passes it. `OptionsPanel` gets a checkbox with the label `파일명에 순번 포함` next to the overwrite checkbox, disabled while a job runs. The rank column of the preview list stays (it is the position, also used for the tag).
4. Duplicate names: without the rank, two entries of one playlist can produce the same file name. Establish with tests what `DefaultDownloadService` does today when two items of one job get the same `fileName` (use a fake sink or `LocalFolderSink` on a temp dir): with `overwrite = false` exactly one file is written and the other item ends as `ItemSkipped` (not `ItemFailed`, no crash, no job abort) and with `overwrite = true` the job completes with one file on disk and both items done. If the current code does something else (for example the second item fails with a raw exception), fix it minimally so the outcomes above hold, keeping every existing test green, and report it. Note the retry flow (`ranks`) is unaffected.
5. `README.md`: in the file-name section add one sentence about the option and the duplicate-name note.

- [ ] **Step 1: Write failing tests**
  - `FilenameFormatterTest`: `includeRank = false` for a simple case, a Korean case (`소연 (SOYEON)` / `퇴사할게여 (Narr. 기안84)` gives `소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).mp3`), forbidden characters, a title long enough to be cut (total base length exactly 180 UTF-16 units, no leading rank), an 81+ character artist, an empty title; plus a guard that the default call still returns the old result (`001 ...`). Update `ApiModelsTest` for the new `JobOptions` field if it enumerates fields (default true, survives a round trip, missing key decodes to true).
  - `DefaultDownloadServiceTest`: `includeRank = false` yields the rank-less name passed to the sink and in `ItemDone`/`ItemStarted`, the tag's `track` is still the rank; the duplicate-name cases from item 4.
  - `RoutesTest`: the option reaches `DownloadRequest.includeRank` (true by default, false when sent).
  - `AppStateHolderTest` (+ `FakeApi`): toggling recomputes preview names, resolving with the option off shows rank-less names, single-video rank change keeps the option, `startDownload` sends `includeRank`, `reset` keeps the option, toggling while a job runs does not rewrite rows of the running job (the checkbox is disabled then, but the holder must also ignore the recompute outside `PREVIEW`).
- [ ] **Step 2: Run to see them fail:** `.\gradlew.bat check` (compile errors count as failing)
- [ ] **Step 3: Implement** behaviours 1-5.
- [ ] **Step 4: Run** `.\gradlew.bat check`
  Expected: BUILD SUCCESSFUL, no failures, none skipped (test methods return `Unit`).
- [ ] **Step 5: Commit**

```powershell
git add shared engine server app README.md
git commit -m "feat: let the user leave the rank out of file names" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Remember the last options

Added after the user's request "마지막 옵션은 앱이 기억하도록 해". The app restores the options it was last used with: output folder, overwrite, rank in file name, concurrency. It does not remember the typed playlist/video address or the single-video rank.

**Files:**
- Create: `app/src/commonMain/kotlin/com/xgetsongs/app/settings/UserSettings.kt`, `.../settings/SettingsStore.kt`
- Create: `app/src/desktopMain/kotlin/com/xgetsongs/app/JsonSettingsStore.kt`
- Modify: `app/src/commonMain/kotlin/com/xgetsongs/app/state/AppStateHolder.kt`, `app/src/desktopMain/kotlin/com/xgetsongs/app/Main.kt`, `app/src/commonMain/kotlin/com/xgetsongs/app/ui/OptionsPanel.kt`
- Tests: create `app/src/desktopTest/kotlin/com/xgetsongs/app/JsonSettingsStoreTest.kt`; modify `app/src/commonTest/kotlin/com/xgetsongs/app/state/AppStateHolderTest.kt` (and `FakeApi.kt` / a new fake store next to it if needed)
- Modify: `README.md`

**Interfaces:**
- Produces:
  - `@Serializable data class UserSettings(val outputDir: String? = null, val overwrite: Boolean = false, val includeRank: Boolean = true, val concurrency: Int = 2)` in `com.xgetsongs.app.settings`.
  - `interface SettingsStore { fun load(): UserSettings; fun save(settings: UserSettings) }` and `object NoSettingsStore : SettingsStore` (loads defaults, saves nothing) in the same package. `app/commonMain` stays free of `java.*` (the store is a capability; the web client will later bring its own).
  - `class JsonSettingsStore(private val file: java.nio.file.Path) : SettingsStore` in `desktopMain`.
  - `AppStateHolder(api, scope, defaultOutputDir, settings: SettingsStore = NoSettingsStore)` and `fun flushSettings()`.

**Behaviour**

1. `JsonSettingsStore.load()`: a missing, unreadable, empty or corrupt file, or a JSON of the wrong shape, gives `UserSettings()` (never throws); unknown keys are ignored (use `ApiJson.instance`-style `ignoreUnknownKeys` with `encodeDefaults = true`); `concurrency` is coerced into 1..4 after reading. `save()`: creates the parent directory, writes UTF-8 JSON to a temp file in the same directory and moves it over the target with `REPLACE_EXISTING` (so a crash never leaves half a file), deletes the temp file in `finally`, and never throws (an `IOException` is swallowed; the app must keep working with a read-only profile). The file is `appDataDirectory().resolve("settings.json")` (wired in `Main.kt`).
2. `AppStateHolder` starts from `settings.load()`: `outputDir = stored.outputDir?.takeIf { it.isNotBlank() } ?: defaultOutputDir`, `overwrite`, `includeRank`, `concurrency` (clamped 1..4) from the file.
3. Saving: every change of one of the four options (through `onOutputDir`, `onOverwrite`, `onIncludeRank`, `onConcurrency`) schedules a save of the CURRENT four values after a 400 ms quiet period: keep a `saveJob`; each change cancels the pending one and launches `scope.launch { delay(400); settings.save(...) }` (finite coroutines, so tests with a `TestScope` finish). A change that does not alter the four values (same value set again) schedules nothing. `flushSettings()` cancels any pending save and saves the current values immediately (synchronously); `Main.kt` calls it in `onCloseRequest` before `uiScope.cancel()`. Nothing is saved at startup, and `reset()` does not touch the settings.
4. `OptionsPanel`: the label `동시 다운로드` must stay on one line (`maxLines = 1`, `softWrap = false`, width no smaller than it needs: use `Modifier.widthIn(min = 100.dp)` instead of the fixed `width(100.dp)`; it currently wraps to two lines).
5. `README.md`: one sentence in the usage part: the last options are remembered in `%APPDATA%\xGetSongs\settings.json`.

- [ ] **Step 1: Write failing tests**
  - `JsonSettingsStoreTest` (desktopTest, temp directory per test, cleaned up): missing file; round trip with a Korean path and all fields; corrupt JSON (`{`), empty file, JSON of the wrong shape (`[]`), unknown keys; `concurrency` 9 gives 4 and 0 gives 1; blank stored `outputDir` is kept as is by the store (the holder applies the default); `save` creates the parent directory, leaves no temp file, replaces an existing file; `save` into a location that cannot be written (the parent path is a regular file) does not throw.
  - `AppStateHolderTest` (virtual time; use a recording fake store): initial state comes from the store, and a blank stored folder falls back to the default folder; changing each of the four options saves the right values after 400 ms and not before (`advanceTimeBy(399)` then `(1)`); three quick changes give exactly one save with the last values; setting the same value again saves nothing; `flushSettings()` saves immediately and cancels the pending one (no second save afterwards); nothing saved at startup; `reset()` keeps the options and saves nothing; the single-video rank and the input text are never part of the saved values.
- [ ] **Step 2: Run to see them fail:** `.\gradlew.bat :app:desktopTest`
- [ ] **Step 3: Implement** behaviours 1-5.
- [ ] **Step 4: Run** `.\gradlew.bat check`
  Expected: BUILD SUCCESSFUL, no failures, none skipped (test methods return `Unit`).
- [ ] **Step 5: Commit**

```powershell
git add app README.md
git commit -m "feat(app): remember the last options" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Album tag from the video's own album

Added after the user's request "앨범명은 폴더명으로 하지 말고 각 음원별 앨범명으로 따로 저장해, 못찾으면 폴더명으로". The album tag becomes the video's own album (yt-dlp's `album` field, which YouTube fills for many official tracks, for example `Love poem` or `Palette`; most official MVs have none). When the video has no album, the playlist title is used as before (the folder name is derived from it); a single video without an album gets no album tag.

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/VideoInfoFile.kt`
- Modify: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/YtDlpCommands.kt`, `engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt` (KDoc of `PreparedItem.album` and of `DownloadRequest.album` in `Services.kt` too)
- Tests: create `engine/src/test/kotlin/com/xgetsongs/engine/ytdlp/VideoInfoFileTest.kt`; modify `ytdlp/YtDlpCommandsTest.kt`, `job/DefaultDownloadServiceTest.kt`, `testutil/Fakes.kt` (as needed), `integration/RealYtDlpIntegrationTest.kt`
- Modify: `README.md`

**Interfaces:**
- Produces: `object VideoInfoFile { fun readAlbum(file: java.nio.file.Path): String? }`.
- Consumes: `PreparedItem.album` (now only the FALLBACK: the playlist title or null), `TrackTags.album`.

**Behaviour**
1. `YtDlpCommands.download` adds `--write-info-json` (before `-o`). yt-dlp then writes `<videoId>.info.json` into the item's work directory next to `<videoId>.mp3`. (The `--no-playlist` flag already present keeps it to one video.)
2. `VideoInfoFile.readAlbum(file)`: returns the `album` string of the JSON object in `file`, trimmed, or null when the file is missing/unreadable/not UTF-8/not valid JSON, the root is not an object, `album` is absent/null/not a string, or the trimmed text is empty. It also returns null for the literal text `NA` (yt-dlp's placeholder in printed output). It never throws and must not keep the file open. Use `kotlinx.serialization.json` (already used by `YtDlpResolver`); the file can be several hundred KB because of the formats list, which is fine.
3. `ItemDownloader.download`: after yt-dlp succeeded and `<videoId>.mp3` exists, `album = VideoInfoFile.readAlbum(workDir.resolve("$videoId.info.json")) ?: prepared.album` and that value goes into `TrackTags.album`. Everything else about tagging is unchanged (album artist stays the per-track artist, the cover logic is unchanged). A missing or broken info file never fails the item.
4. `README.md`: in the ID3 paragraph say the album is the video's own album when YouTube knows it, else the playlist name.

- [ ] **Step 1: Write failing tests**
  - `VideoInfoFileTest` (temp files, cleaned up): album present; trimmed; Korean album; missing key; `null` value; number value; blank string; `NA`; invalid JSON; empty file; JSON array root; non-existent file; invalid UTF-8 bytes; a large file (200 KB of other fields) still works.
  - `YtDlpCommandsTest`: `--write-info-json` is present and before `-o`.
  - `DefaultDownloadServiceTest` (the fake yt-dlp handler may write `<id>.info.json` into the item dir; capture the ffmetadata text inside the fake ffmpeg handler as the existing tag tests do): info album `Palette` with `request.album = "My List"` gives `album=Palette`; no info file gives `album=My List`; info file without album gives `album=My List`; info album blank gives `album=My List`; single video (`request.album = null`) with an info album gives `album=Palette`; single video without any album has no `album=` line; a corrupt info file gives the fallback and the item still succeeds.
- [ ] **Step 2: Run to see them fail:** `.\gradlew.bat :engine:test`
- [ ] **Step 3: Implement** behaviours 1-4.
- [ ] **Step 4: Run** `.\gradlew.bat check` (green, none skipped; test methods return `Unit`).
- [ ] **Step 5: Real yt-dlp check** (integration, the user approved real downloads for this project): in `RealYtDlpIntegrationTest` add (a) a test that runs the real `YtDlpCommands.download(...)` command for the 19-second test video (`jNQXAC9IVRw`, the same video the existing test uses) into a temp directory with the real `SystemProcessRunner` and asserts that `<id>.info.json` exists next to `<id>.mp3`, parses as a JSON object whose `id` is the video id, and that `VideoInfoFile.readAlbum` returns null for it (that video has no album); (b) a METADATA-ONLY test (no audio download): the same command with `--skip-download` inserted before the `--` separator, for video `kcx0a2OAhN0`, asserting `<id>.info.json` exists and `VideoInfoFile.readAlbum` equals `Love poem` (add a comment that this depends on the music metadata YouTube currently shows for that video). Run `.\gradlew.bat :engine:integrationTest`. If the real info file name differs from `<videoId>.info.json`, fix the lookup and report it.
- [ ] **Step 6: Commit**

```powershell
git add engine README.md
git commit -m "feat(engine): use the video's own album for the album tag" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: "탐색기에서 보기" button

Added after the user's request "'저장 위치' 라벨 제일 오른쪽 조회 버튼 아래에 '탐색기에서 보기' 버튼 추가".

**Files:**
- Modify: `app/src/commonMain/kotlin/com/xgetsongs/app/state/Labels.kt`, `app/src/commonMain/kotlin/com/xgetsongs/app/ui/OptionsPanel.kt`, `app/src/commonMain/kotlin/com/xgetsongs/app/ui/App.kt`
- Create: `app/src/desktopMain/kotlin/com/xgetsongs/app/ExplorerOpener.kt`; modify `app/src/desktopMain/kotlin/com/xgetsongs/app/Main.kt`
- Tests: `app/src/commonTest/kotlin/com/xgetsongs/app/state/LabelsTest.kt`; create `app/src/desktopTest/kotlin/com/xgetsongs/app/ExplorerOpenerTest.kt`
- Modify: `README.md`

**Interfaces:**
- Produces: `fun destinationPath(state: UiState): String?` in `Labels.kt` (the path part of the existing label); `destinationLabel(state)` becomes `destinationPath(state)?.let { "저장 위치: $it" }` (same text as now). `App(holder, pickFolder, openFolder: (String) -> Unit)` (a platform capability like `pickFolder`; the default for tests/previews may be a no-op). `internal fun openInExplorer(path: String)` and `internal fun existingFolderFor(path: String): java.nio.file.Path?` in `desktopMain`.

**Behaviour**
1. `OptionsPanel`: the line that shows the destination label becomes a `Row(Modifier.fillMaxWidth(), verticalAlignment = CenterVertically)`: the label `Text` (weight 1f, one line, ellipsis, as now) and, at the far right of the row (so it sits under the 폴더 선택 and 조회 buttons), an `OutlinedButton` with the text `탐색기에서 보기`. The button is shown only when `destinationPath(state)` is not null, is always enabled (also while a job runs), and calls `openFolder(destinationPath(state))`. Keep the button compact (smaller content padding, `bodySmall` text) so the row does not grow much taller than now.
2. `existingFolderFor(path)`: null for a blank path or one the platform rejects (`InvalidPathException`); otherwise the first existing DIRECTORY on the way up from the path (the path itself when it is a directory, the parent when it is a regular file, then the parents, up to the root); null when none exists. It never throws.
3. `openInExplorer(path)`: opens `existingFolderFor(path)` in the system file manager: `java.awt.Desktop.getDesktop().open(folder.toFile())` when `Desktop.isDesktopSupported()` and `Desktop.getDesktop().isSupported(Desktop.Action.OPEN)`, otherwise `ProcessBuilder("explorer.exe", folder.toString())` (argument list, no shell). It does nothing when there is no folder. It runs on its own daemon thread so the UI never waits for the file manager, and swallows every `Exception` (opening the folder is a convenience). `Main.kt` passes `openFolder = ::openInExplorer` to `App`.
4. `README.md`: one sentence in the usage part about the button.

- [ ] **Step 1: Write failing tests**
  - `LabelsTest`: `destinationPath` for no resolve, blank output dir, video, playlist (trailing separators trimmed, title sanitised); `destinationLabel` still returns exactly the old texts (`저장 위치: ...`).
  - `ExplorerOpenerTest` (temp directory per test, cleaned up): an existing directory returns itself; a missing child of an existing directory returns the parent; several missing levels return the nearest existing ancestor; a regular file returns its parent; a blank path and a path with an illegal character (for example one containing `\u0000`) return null; none of these throws. Do not start a file manager in tests.
- [ ] **Step 2: Run to see them fail:** `.\gradlew.bat :app:desktopTest`
- [ ] **Step 3: Implement** behaviours 1-4.
- [ ] **Step 4: Run** `.\gradlew.bat check` (green, none skipped).
- [ ] **Step 5: Commit**

```powershell
git add app README.md
git commit -m "feat(app): add a button that shows the destination folder in the file manager" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Lyrics from the video description into the ID3 lyrics frame

Added after the user's request "비디오 설명란에 가사가 있으면 id3tag 가사란에 입력해줘" (example: https://www.youtube.com/watch?v=7mDDM0eBWR0, whose description has a `[Lyrics]` heading, the lyrics, and `=======` separator lines around the block). The lyrics go into an ID3v2.3 `USLT` (unsynchronised lyrics) frame. Nothing is written when the description holds no lyrics.

**Files:**
- Create: `shared/src/commonMain/kotlin/com/xgetsongs/shared/lyrics/LyricsExtractor.kt`
- Modify: `engine/src/main/kotlin/com/xgetsongs/engine/ytdlp/VideoInfoFile.kt`, `engine/.../job/ItemDownloader.kt`, `engine/.../tags/TrackTags.kt`, `engine/.../tags/Id3Tagger.kt`, and generalise `engine/.../tags/Id3Comment.kt` into `Id3Frames.kt` (`git mv`; see behaviour 4)
- Tests: create `shared/src/commonTest/kotlin/com/xgetsongs/shared/lyrics/LyricsExtractorTest.kt`; modify `engine/.../ytdlp/VideoInfoFileTest.kt`, `engine/.../tags/Id3CommentTest.kt` (becomes `Id3FramesTest.kt`), `Id3TaggerTest.kt`, `job/DefaultDownloadServiceTest.kt`, `testutil/Id3v2Tag.kt`, `integration/RealFfmpegTaggingIntegrationTest.kt`, `integration/RealYtDlpIntegrationTest.kt`
- Modify: `README.md`

**IMPORTANT (copyright):** all fixtures in tests are SYNTHETIC lyrics (made-up lines such as `첫 번째 줄`, `La la la`, `Line one`). Do not paste real song lyrics or a real description into the repository. The real-description integration test (behaviour 7) asserts only structure, never lyric text.

**Interfaces:**
- Produces:
  - `object LyricsExtractor { fun extract(description: String?): String? }` in `com.xgetsongs.shared.lyrics` (pure Kotlin, no `java.*`).
  - `VideoInfo(val album: String?, val description: String?)` and `VideoInfoFile.read(file: Path): VideoInfo` (never throws; all-null for any unreadable/invalid file); `VideoInfoFile.readAlbum(file)` stays and delegates (existing callers/tests keep working).
  - `TrackTags.lyrics: String? = null` (last parameter, default null).
  - `object Id3Frames { fun add(file: Path, comment: String?, lyrics: String?) }` replacing `Id3Comment.addComment(file, text)` (same tag-rewrite logic, one pass for both frames).

**Behaviour**

1. `LyricsExtractor.extract(description)` returns the lyrics block as plain text (lines joined with `\n`, no CR) or null.
   - Normalise the input first: `\r\n` and `\r` to `\n`, strip a BOM.
   - **Marker line:** a line that is only a lyrics heading. Test it by removing from the line every character that is not a letter or digit (Unicode letters, including Hangul/CJK/kana, count as letters; brackets, punctuation, symbols, emoji, spaces are removed), lowercasing, and comparing with exactly these ten strings and nothing else: `lyrics`, `lyric`, `가사`, `歌詞`, `歌词`, `lyrics가사`, `가사lyrics`, `lyricskorean`, `lyricskor`, `lyricsoriginal`. So `[Lyrics]`, `Lyrics:`, `■ LYRICS ■`, `【가사】`, `[Lyrics / 가사]`, `Lyrics (Korean)` all match, while `Lyrics by 소연`, `Lyrics: some words`, `Lyricsmith` do not. The FIRST marker line wins; later markers (for example a Romanized or English block) are ignored.
   - **Block:** the lines after the marker line. Skip blank lines and separator lines (a line of 3 or more of the same character from `=-_*~#.+` or `━─═` after trimming) that come directly after the marker. The block then runs until the first terminator line or the end of the text. A terminator line is any of: a separator line; a line containing `http://` or `https://` or `www.`; a line starting (after trimming) with `©`, `ⓒ`, `(c)`, `copyright` (case-insensitive), `all rights reserved`; a line starting with `#` followed by a non-space character (a hashtag line); a line that is only a bracketed heading (whole line enclosed in `[...]` or `【...】` or `(...)`) whose letters/digits-only lowercase form starts with one of: `rom`, `eng`, `translat`, `credit`, `info`, `staff`, `link`, `sns`, `follow`, `번역`, `해석`, `영문`, `로마`, `크레딧`, `정보`, or is itself a marker (so a second `[Lyrics]` heading ends the first block); a line whose letters/digits-only lowercase form starts with `provided`, `releasedon`, `autogenerated`. Bracketed lines that look like song-structure tags (`[Verse 1]`, `[Chorus]`, `[Bridge: Name]`, `[후렴]`) are lyrics and are kept.
   - **Clean-up:** trim trailing whitespace of every line; drop leading and trailing blank lines of the block; collapse runs of 3 or more consecutive blank lines to 2; remove control characters other than `\n` and `\t`; the text is cut at 30000 characters (at a line boundary) as a safety limit.
   - **Validity:** the cleaned block must have at least 3 non-blank lines, else null. A description without a marker line, or null/blank input, gives null.
2. `VideoInfoFile.read(file)` parses the root JSON object once and returns the trimmed `album` (same rules as today: null for absent/null/non-string/blank/`NA`) and the `description` (string only; null when absent/null/non-string/blank; NOT trimmed beyond dropping a leading/trailing blank run; keep inner newlines). Only the ROOT keys count.
3. `ItemDownloader.download`: from the same `<videoId>.info.json` that gives the album, `lyrics = LyricsExtractor.extract(info.description)`; `TrackTags(..., lyrics = lyrics)`. A missing or broken info file means no lyrics and never fails the item.
4. `Id3Frames.add(file, comment, lyrics)` generalises today's COMM writer: the same ID3v2.3 tag rewrite (same header checks, flags mask, size handling, temp file + move, cleanup) but it can append two frames after the existing frames, COMM first (when `comment` is not blank) then USLT (when `lyrics` is not blank); with both null/blank it does nothing and does not touch the file. `Id3Tagger` calls it with `tags.comment` and `tags.lyrics` (an `IOException` still becomes `Failure(OTHER, ...)`). `Ffmetadata` does NOT carry the lyrics (ffmpeg would write a `TXXX` frame).
   - **USLT frame:** id `USLT`, size 4 bytes big-endian (not syncsafe), flags `00 00`; payload = encoding byte, 3-byte language, content descriptor (empty) + its terminator, then the lyrics text. Always use encoding 1 (UTF-16 with BOM): descriptor `FF FE 00 00`, text `FF FE` followed by UTF-16LE bytes; no terminator after the text. Language: `kor` when the text contains at least one Hangul syllable (U+AC00..U+D7A3), otherwise `eng`. Line breaks in the written text are `\r\n` (convert `\n` to `\r\n`; the extractor returns `\n`).
   - Keep the COMM frame exactly as it is today.
5. The tag-writing order and the cover logic are unchanged. The album artist etc. unchanged.
6. `README.md`: in the ID3 paragraph add that lyrics found in the video description (a `[Lyrics]`/`가사` section) are written to the lyrics frame.
7. Real check (integration; yt-dlp metadata only, no audio download; the user approved real downloads/queries for this project): in `RealYtDlpIntegrationTest` add a test like the existing `kcx0a2OAhN0` one for video `7mDDM0eBWR0` (command with `--skip-download` before `--`): read `<id>.info.json` with `VideoInfoFile.read`, run `LyricsExtractor.extract` on its description and assert only structure: not null, at least 20 non-blank lines, contains no `http`, no line starts with `i-dle Official`, no line equals `=======`. A comment must say it depends on that video's current description. In `RealFfmpegTaggingIntegrationTest` add a test that tags a generated mp3 with a synthetic Korean + English multi-line lyrics text through the real `Id3Tagger`, reads the raw frames with the test helper (exactly one `USLT`, encoding byte 1, language `kor`, the text round-trips with CRLF line breaks and no extra BOM inside the text) and ffprobe (`format_tags` shows a `lyrics` tag; ffprobe may name it `lyrics` or `lyrics-kor`: accept either; compare ignoring CR) and that the other tags and the cover are unchanged.

- [ ] **Step 1: Write failing tests**
  - `LyricsExtractorTest` (synthetic lyrics): the structure of the real example (heading `[Lyrics]`, separator lines before and after the block, social links and copyright and hashtag lines after); every marker spelling listed above, and non-markers (`Lyrics by X`, `Lyrics: text`, `Lyricsmith`); first marker wins (second block ignored); block ends at each terminator kind (separator, URL, `©`, hashtag line, `[Rom]`, a second `[Lyrics]`); song-structure bracket tags kept; no terminator (block runs to the end); separator directly after the marker skipped; fewer than 3 non-blank lines gives null; no marker gives null; null/blank input gives null; CRLF input; blank-line collapsing; trailing whitespace trimmed; control characters removed; the 30000-character cut at a line boundary; Korean, emoji and other non-BMP text survives.
  - `VideoInfoFileTest`: `read` returns album and description together; description absent/null/number/blank gives null; a nested `description` is ignored; keep all existing tests.
  - `Id3FramesTest` (the old `Id3CommentTest` cases, adapted, still pass unchanged in meaning) plus: USLT only; COMM + USLT in one pass (literal expected bytes for both frames, header size updated, audio and other frames unchanged); Hangul text gives `kor` and Latin-only text gives `eng`; `\n` becomes `\r\n`; a non-BMP character is written as a surrogate pair in UTF-16LE; both null/blank leaves the file byte-identical (same last-modified is not required, but contents are); unsupported header flags still throw `IOException`.
  - `Id3TaggerTest` and `DefaultDownloadServiceTest`: the lyrics from the fake `<id>.info.json` description reach the tagger/`TrackTags` (assert the final file bytes contain a USLT frame with the text, or the captured `TrackTags`, whichever the existing fakes allow); no description, a description without lyrics, and a corrupt info file give no USLT and a successful item.
- [ ] **Step 2: Run to see them fail:** `.\gradlew.bat :shared:jvmTest :engine:test`
- [ ] **Step 3: Implement** behaviours 1-6.
- [ ] **Step 4: Run** `.\gradlew.bat check` (green, none skipped; test methods return `Unit`).
- [ ] **Step 5: Real tools:** run `.\gradlew.bat :engine:integrationTest` once (behaviour 7).
- [ ] **Step 6: Commit** (one commit, or two: shared + engine)

```powershell
git add shared engine README.md
git commit -m "feat: write lyrics found in the video description to the ID3 lyrics frame" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Look lyrics up on the internet when the description has none

Added after the user's requests "설명에서 가사 추출이 불가하면 폴백으로 ... 인터넷에서 가사를 자동으로 검색하여 태그에 넣어줘" and "인터넷 검색에도 가사가 없으면 태그의 가사를 지워주도록". Musicat's tag editor only opens a Google results page (query `lyrics + title + artist`) for the user to copy from; a page that cannot be read reliably or permissibly by a program. The app therefore reuses the same query idea (title + artist, plus album/duration to match) against LRCLIB (https://lrclib.net), an open lyrics database with a public JSON API meant for programs (no key; asks for a descriptive `User-Agent`). The lookup is an option (default on) because it sends the artist, title, album and length of every downloaded song to that service. When neither the description nor LRCLIB has lyrics, NO lyrics frame is written, so the file never carries lyrics left over from anywhere else.

**IMPORTANT (copyright):** every lyrics fixture in tests is SYNTHETIC. Real responses of LRCLIB must never be printed, logged, asserted on, or copied into the repository or reports: real-service tests assert STRUCTURE only (non-null, line counts). Do not paste real lyrics anywhere.

**Files:**
- Create: `engine/src/main/kotlin/com/xgetsongs/engine/lyrics/LyricsProvider.kt` (interface, query type, `NoLyricsProvider`), `LyricsMatcher.kt` (pure matching logic), `LrclibLyricsProvider.kt` (HTTP)
- Modify: `shared/.../lyrics/LyricsExtractor.kt` (add `tidy`), `shared/.../api/ApiModels.kt` (`JobOptions.searchLyricsOnline`)
- Modify: `engine/.../ytdlp/VideoInfoFile.kt` (`VideoInfo.duration`), `engine/.../Services.kt` (`DownloadRequest.searchLyricsOnline`), `engine/.../job/ItemDownloader.kt`, `engine/.../job/DefaultDownloadService.kt`
- Modify: `server/.../Application.kt`, `server/.../LocalServer.kt` (wire `LrclibLyricsProvider`)
- Modify: `app/.../settings/UserSettings.kt`, `app/.../state/UiState.kt`, `app/.../state/AppStateHolder.kt`, `app/.../ui/OptionsPanel.kt`
- Tests: create `engine/src/test/.../lyrics/LyricsMatcherTest.kt`, `LrclibLyricsProviderTest.kt`, `integration/RealLrclibIntegrationTest.kt`; modify `shared/.../LyricsExtractorTest.kt`, `engine/.../ytdlp/VideoInfoFileTest.kt`, `job/DefaultDownloadServiceTest.kt`, `tags/Id3TaggerTest.kt`, `integration/RealFfmpegTaggingIntegrationTest.kt`, `integration/RealYtDlpIntegrationTest.kt`, `server/.../RoutesTest.kt`, `app/.../AppStateHolderTest.kt`, `app/.../JsonSettingsStoreTest.kt`, `ApiModelsTest`
- Modify: `README.md`

**Interfaces:**
- Produces:
  - `data class LyricsQuery(val artist: String, val title: String, val album: String?, val durationSeconds: Int?)`; `interface LyricsProvider { suspend fun find(query: LyricsQuery): String? }` (returns tidy lyrics or null; NEVER throws except `CancellationException`); `object NoLyricsProvider : LyricsProvider` (always null).
  - `LyricsExtractor.tidy(text: String?): String?` in shared: the clean-up and validity rules of `extract` applied to a whole text (normalise line breaks, drop control chars except `\n`/`\t`, trim line ends, drop leading/trailing blank lines, collapse 3+ blank lines to 2, cut at 30000 chars on a line boundary without splitting a surrogate pair, at least 3 non-blank lines else null). `extract` uses it internally (no behaviour change).
  - `JobOptions.searchLyricsOnline: Boolean = true` (last parameter); `DownloadRequest.searchLyricsOnline: Boolean = false` (last parameter; the engine default stays offline, the server passes the option); `PreparedItem.searchLyricsOnline`; `ItemDownloader.prepare(item, album, includeRank, searchLyricsOnline = false)`; `ItemDownloader(runner, tools, metadata, lyrics: LyricsProvider = NoLyricsProvider)`; `VideoInfo.duration: Int?` (seconds, rounded, from the root `duration` number in the info file; null when absent/not a number/not positive).
  - `class LrclibLyricsProvider(client: java.net.http.HttpClient = default, baseUrl: String = "https://lrclib.net/api", userAgent: String = "xGetSongs/1.0 (https://github.com/gomtos/xGetSongs)", connectTimeout: Duration = 10.seconds, requestTimeout: Duration = 15.seconds, maxRequests: Int = 8, maxConcurrent: Int = 2) : LyricsProvider`.
  - `UserSettings.searchLyricsOnline: Boolean = true`; `UiState.searchLyricsOnline = true`; `AppStateHolder.onSearchLyricsOnline(value)`.

**Behaviour**

1. Lyrics chain in `ItemDownloader.download` (after yt-dlp succeeded; it already reads `<videoId>.info.json`): `lyrics = LyricsExtractor.extract(info.description)`. When that is null and `prepared.searchLyricsOnline` is true: `lyrics = lyricsProvider.find(LyricsQuery(artist = prepared.artist, title = prepared.track, album = <the album used for the tag, own album or fallback>, durationSeconds = info.duration))`, called inside a `try` that turns any `Exception` except `CancellationException` into null. The description's lyrics always win and then the provider is NOT called. No lyrics from either source gives `TrackTags.lyrics = null`, hence no USLT frame. A failing lookup never fails or delays the item beyond the provider's own time limits.
2. `LyricsMatcher` (pure Kotlin object, unit-testable without network):
   - `titleVariants(title)`: the title; the title without bracketed parts (`(...)`, `[...]`, `{...}`, `<...>`) with spaces collapsed; and that text without a trailing credit like ` feat. X`, ` ft. X`, ` with X`, ` prod. X`, ` Narr. X` (case-insensitive); and without quote characters (`'`, `"`, typographic quotes). Distinct, non-blank, in that order.
   - `artistVariants(artist)`: the artist; the text outside its parentheses; each parenthesised part (`소연 (SOYEON)` gives `소연 (SOYEON)`, `소연`, `SOYEON`); distinct, non-blank, at most 3 (the order above).
   - `normalize(text)`: lowercase and keep only letters and digits (Unicode letters, Hangul included).
   - `isMatch(query, candidate)`: the candidate (an LRCLIB record: trackName, artistName, albumName, duration (seconds, may be fractional), instrumental, plainLyrics) is acceptable when `plainLyrics` is not blank, `instrumental` is not true, some normalised title variant of the query equals some normalised title variant of the candidate's `trackName`, some normalised artist variant of the query is contained in (or contains) some normalised artist variant of the candidate's `artistName` (both at least 2 characters), and, when both durations are known, `abs(query.durationSeconds - candidate.duration) <= 8`.
   - `pick(query, candidates)`: the acceptable candidate with the smallest duration difference (unknown difference counts as the largest; ties keep the first), or null.
3. `LrclibLyricsProvider.find(query)`:
   - At most `maxRequests` HTTP requests per call, in this order, stopping at the first accepted result: (a) `GET {base}/get?artist_name=<artist>&track_name=<title>[&album_name=<album>][&duration=<seconds>]` with the unmodified query (artist, title); its JSON object is accepted when `isMatch` (the server's own match is exact but keep the same safety checks); (b) for each (artist variant x title variant) pair in order, `GET {base}/search?track_name=<t>&artist_name=<a>`, whose JSON array is passed to `pick`. All values URL-encoded as UTF-8 (`URLEncoder`, spaces as `%20`/`+` both fine). Header `User-Agent: <userAgent>`; `Accept: application/json`.
   - Anything other than HTTP 200 with a parseable body (404, 429, 5xx, bad JSON, timeout, connection error) counts as "no result" for that request and the next request is still tried, except that a 429 or a connect/timeout failure stops the whole call (return null) so a down or throttling service is not hammered.
   - The result is `LyricsExtractor.tidy(candidate.plainLyrics)`; null when tidy gives null.
   - Concurrency: a `Semaphore(maxConcurrent)` held per HTTP request (shared by the instance); the blocking `HttpClient.send` runs in `runInterruptible(Dispatchers.IO)` so cancelling the coroutine interrupts it; `CancellationException` propagates; all other exceptions are swallowed into "no result". Uses `kotlinx.serialization.json` (ignore unknown keys; `duration` may be an integer or a decimal).
4. Clearing: when no lyrics were found the written tag has no lyrics: `Id3Tagger` already builds the tag only from the ffmetadata file (`-map_metadata`) and `Id3Frames` writes USLT only for non-blank lyrics. Prove it with a real ffmpeg test (see tests): an input mp3 that already carries a USLT frame (made with `Id3Frames.add` on a generated file) tagged with `lyrics = null` comes out without any USLT frame and without a `lyrics`/`lyrics-*` tag in ffprobe.
5. Option plumbing (same pattern as `includeRank`): `Application.kt` passes `options.searchLyricsOnline` into `DownloadRequest`; `DefaultDownloadService` passes `request.searchLyricsOnline` to `prepare`; `LocalServer` builds `ItemDownloader(runner, locator, resolver, LrclibLyricsProvider())`. The app keeps the option in `UiState` (default true, kept by `reset()`), `onSearchLyricsOnline` goes through `changeOptions` (so it is saved and restored like the other options; `UserSettings.options()`/`lastKnown` comparisons include it; an old settings file without the key loads as true), `jobOptions(state)` sends it, and `OptionsPanel` shows a third checkbox `가사가 없으면 인터넷에서 검색` next to the other two (disabled while a job runs). The three checkboxes must wrap instead of overflowing on a narrow window (use `FlowRow` with the needed `@OptIn(ExperimentalLayoutApi::class)`, or an equivalent that compiles on this Compose version).
6. `README.md`: a short paragraph: lyrics come from the video description when it has a `[Lyrics]`/`가사` section, otherwise (option on) from LRCLIB by artist, title, album and length; nothing found means no lyrics tag; the lookup sends those four values to lrclib.net and can be switched off with the checkbox; lyrics are copyrighted works, for personal use.

- [ ] **Step 1: Write failing tests**
  - `LyricsExtractorTest`: `tidy` rules (null/blank, CRLF, control characters, blank-line collapsing, fewer than 3 non-blank lines, 30000 cut at a line boundary, surrogate pairs); `extract` results unchanged.
  - `VideoInfoFileTest`: `duration` integer, decimal (rounded), absent, null, string, zero/negative, nested ignored.
  - `LyricsMatcherTest`: variants (the Korean `소연 (SOYEON)`, `LOVE ATTACK (LOVE ATTACK)`, `Title (feat. X)`, `Title [Live]`, quotes), normalisation, `isMatch` (title equal after normalisation; artist contains both ways; duration edges 8 s accepted and 9 s rejected; unknown durations accepted; instrumental, blank lyrics, title mismatch, artist mismatch, 1-character artist rejected), `pick` (smallest duration difference, ties keep the first, none acceptable gives null).
  - `LrclibLyricsProviderTest` with a local fake server (`com.sun.net.httpserver.HttpServer` on `127.0.0.1` port 0, stopped in `@AfterTest`; synthetic JSON): `/get` hit returns without calling `/search`; `/get` 404 then `/search` picks the best candidate; the exact request paths and query strings (Korean artist and title percent-encoded as UTF-8, album, duration present or absent) and the `User-Agent`/`Accept` headers; artist/title variants are tried in order; the request cap; 429 stops everything; 500 on one request still lets the next be tried; bad JSON, an empty array, an instrumental record and a mismatching record all give null; a read timeout (handler sleeps longer than a small configured `requestTimeout`) gives null; cancelling the coroutine while the server sleeps returns promptly with `CancellationException`; never throws for a refused connection (stopped server).
  - `DefaultDownloadServiceTest`/`ItemDownloader` tests with a fake `LyricsProvider`: description lyrics present gives the provider no call; none and option on gives one call with the right query (parsed artist/title, the tag album, the info file's duration) and its lyrics end up in the USLT frame; option off gives no call; provider returns null gives no USLT; provider throws `RuntimeException` gives no USLT and a successful item; a `CancellationException` from the provider cancels the job as usual.
  - `Id3TaggerTest`/`RoutesTest`/`ApiModelsTest`/`AppStateHolderTest`/`JsonSettingsStoreTest`: the option travels (default true; absent key decodes to true; server passes it to `DownloadRequest`; holder default, toggle, saved after 400 ms like the others, restored from the store, kept by `reset()`, sent in the job request; an old settings JSON without the key loads as true).
- [ ] **Step 2: Run to see them fail:** `.\gradlew.bat check` (compile errors count as failing)
- [ ] **Step 3: Implement** behaviours 1-6.
- [ ] **Step 4: Run** `.\gradlew.bat check` (green, none skipped; test methods return `Unit`).
- [ ] **Step 5: Real tools** (integration; the user wants this feature and approved real queries/downloads for the project; LRCLIB is a public API): `RealLrclibIntegrationTest` (`@Tag("integration")`, `assumeTrue` the service answers): `find(LyricsQuery("IU", "Love poem", null, 258))` is non-null with at least 10 non-blank lines (assert nothing else about the text and never print it); a made-up title (`zz-no-such-song-xgetsongs-0000`, artist `zz-nobody`) gives null; a query whose title carries a parenthetical credit still finds it (`"Love poem (feat. nobody)"`). In `RealFfmpegTaggingIntegrationTest` add the "clearing" test of behaviour 4. In `RealYtDlpIntegrationTest`'s real download test (the 19-second video, which has no lyrics anywhere) run the job with `searchLyricsOnline = true` and the real `LrclibLyricsProvider` and assert that the downloaded file has no `lyrics*` tag in ffprobe and no USLT frame, while the other tags and the cover are as before. Run `.\gradlew.bat :engine:integrationTest` once.
- [ ] **Step 6: Commit** (a few commits are fine: shared+engine, server+app, docs)

```powershell
git add shared engine server app README.md
git commit -m "feat: search lyrics on the internet when the description has none" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

**Amendments to Task 9 after review (the code is authoritative):** the trailing-credit rule of `titleVariants` does NOT include a bare ` with X` (a credit is only recognised as ` feat.`, ` ft.`, ` prod.`, ` Narr.`; `(with X)` is handled by bracket removal). Normalisation applies NFKC before lowercasing. Candidate-side shortening never removes a version marker (`ver`, `version`, `japanese`, `english`, `remix`, `live`, `inst`, `acoustic`, `cover`, `edit`, `mix`, `remaster`, `demo` and Korean equivalents; matched as whole words, the Korean ones anywhere). Artist containment compares whole words (`Rain` does not match `Rainbow`). `pick` ranks by title match quality, then album match, then duration difference. Records whose lyrics `tidy` rejects are dropped before matching. A 429 or a 502/503/504 or a connect failure or a timeout ends the call and starts a 5-minute cool-down of the provider instance (`coolDown`, `timeSource` parameters); an answer body is read up to 2 MiB. A parenthesised line ends the description's lyrics block only for a section name (`english`, `translation`, `romanized`, `번역`, `해석`, `영문`, `로마자`, `credit(s)`).

---

### Task 10: Diagnostic log and UI freeze watchdog

Added after the user's request "진단 로그와 UI 멈춤 감시기 추가해줘". Background: on 2026-10-07 the user's running app vanished mid-download; the only evidence was a Windows "stopped responding" record and exit value 143, because the app writes no log and prints Netty DEBUG noise to the console. After this task a problem leaves evidence in files: a rolling log, a record of HOW the JVM was asked to exit (the user's own window close versus an external termination), and, when the UI thread stops answering, a thread dump of every thread.

**Files:**
- Create: `app/src/desktopMain/resources/logback.xml`, `app/src/desktopTest/resources/logback-test.xml`
- Create: `app/src/desktopMain/kotlin/com/xgetsongs/app/diagnostics/` : `HangDetector.kt`, `ThreadDump.kt`, `UiWatchdog.kt`, `ExitLogger.kt`, `Diagnostics.kt` (the one entry point `Diagnostics.start(logDir)` used by `Main.kt`)
- Modify: `app/src/desktopMain/kotlin/com/xgetsongs/app/Main.kt`, `app/build.gradle.kts`, `gradle/libs.versions.toml` (only if an SLF4J entry is needed)
- Modify: `server/src/main/kotlin/com/xgetsongs/server/Application.kt` (job logging), `server/src/main/kotlin/com/xgetsongs/server/LocalServer.kt` (startup record), `server/build.gradle.kts` (SLF4J API if needed)
- Tests: create `app/src/desktopTest/kotlin/com/xgetsongs/app/diagnostics/{HangDetectorTest,ThreadDumpTest,UiWatchdogTest,ExitLoggerTest,LogbackConfigTest}.kt`, `server/src/test/kotlin/com/xgetsongs/server/JobLogTest.kt`
- Modify: `README.md` (a short "문제가 생겼을 때" section)

**Rules for this task:** the engine module stays free of logging dependencies (it reports through `ItemFailed`/events and the server logs them). Never log the token (`X-XGS-Token`), request headers, lyrics, or full titles; log ranks, video ids, statuses, reasons and exception class names. Run Gradle only with `--no-daemon` (see the project's memory note: a shared Gradle daemon killed by another tool once took the user's running app with it). Do not touch `run.bat` (the user's, untracked).

**Behaviour**

1. **Log configuration (`logback.xml` in `app/src/desktopMain/resources`)**
   - Root level `INFO`; `io.netty` and `io.ktor` at `WARN`/`INFO` respectively so the console is quiet (no more Netty DEBUG flood); `com.xgetsongs` at `DEBUG`.
   - Two appenders: `CONSOLE` (pattern `%d{HH:mm:ss.SSS} %-5level [%thread] %logger{20} - %msg%n`, level threshold INFO) and `FILE`: `RollingFileAppender` writing `${xgs.logDir}/xgetsongs.log` with `SizeAndTimeBasedRollingPolicy` (file name pattern `xgetsongs.%d{yyyy-MM-dd}.%i.log`, `maxFileSize` 5MB, `maxHistory` 7, `totalSizeCap` 50MB), UTF-8, `immediateFlush` true (a crash or kill must not lose the last lines), full date in the pattern (`%d{yyyy-MM-dd HH:mm:ss.SSS}`).
   - The directory comes from the system property `xgs.logDir` with the fallback `${java.io.tmpdir}/xgetsongs-logs`; `Main.kt` sets the property to `<appDataDirectory>/logs` as the very FIRST statement of `main` (before any class that creates a logger is touched; check there is no top-level logger in `Main.kt`/its file facade).
   - `app/src/desktopTest/resources/logback-test.xml`: console only, WARN, so tests never write into the user's folders.
2. **`Diagnostics.start(logDir: Path)`** (called first in `main`, after setting `xgs.logDir`): creates the log directory (a failure is only reported on stderr, never thrown), installs a default uncaught-exception handler that logs thread name and stack at ERROR (then delegates to the previous handler), logs one startup record at INFO (app name/version if known, Java version + vendor + VM name, OS name/version/arch, processors, max heap, working directory, log directory, `GraphicsEnvironment.isHeadless`), registers the exit logger (3) and starts the watchdog (4). It returns a small handle with `markUserExit()` (the window close handler calls it) and `stop()`.
3. **Exit logger (`ExitLogger`)**: a JVM shutdown hook that logs at INFO/WARN `JVM 종료 시작` with: uptime, whether the exit was asked by the user (`markUserExit()` was called before) or NOT (`외부 종료 요청: 콘솔 종료, SIGTERM, 로그오프·시스템 종료 등`, logged at WARN), the number of running download jobs when the server can tell (pass a `() -> Int` supplier; `LocalServer`/registry exposes it or the supplier returns -1 when unknown), and heap use. This line is what would have identified the 2026-10-07 incident. The decision text is a pure function `describeExit(userRequested, runningJobs, uptime)` so it is unit-testable. The hook must never throw and must not block (no I/O besides the log call).
4. **UI watchdog**
   - `HangDetector` (pure state machine, no threads, no clock of its own; every method takes `nowMs: Long`): `onUiBeat(nowMs)` records that the UI thread ran the heartbeat task; `onTick(nowMs): HangEvent?` is called by the watchdog thread once per interval and returns `Hung(silentMs, repeat: Boolean)` when no beat arrived for more than `thresholdMs` (default 5000; a repeat report at most every `repeatMs` = 30000 while the hang lasts), `Recovered(totalMs)` when a beat comes after a reported hang, else null. Suspend/throttle safety: if the gap between two consecutive `onTick` calls is larger than `maxTickGapMs` (default 4 x interval) the machine was probably asleep or the watchdog thread itself starved: reset the baseline (treat the last beat as `nowMs`), report nothing for that tick, and remember it as `gapsIgnored`.
   - `UiWatchdog` (thin thread wiring around `HangDetector`): a daemon thread named `xgs-ui-watchdog` ticks every `intervalMs` (default 1000): it posts a heartbeat via an injected `postToUi: (Runnable) -> Unit` (production: `java.awt.EventQueue::invokeLater`) that calls `onUiBeat(now)`, and calls `onTick(now)`. On `Hung` it writes a thread dump (5) to the log at ERROR (`UI 스레드가 N초 동안 응답하지 않음`) and to a separate file `<logDir>/ui-hang-yyyyMMdd-HHmmss.txt`; on repeat it appends/creates another dump section; on `Recovered` it logs at WARN `UI 응답 회복 (총 N초)`. At most 20 `ui-hang-*.txt` files are kept (delete the oldest). It never throws out of its loop and stops cleanly (`stop()` interrupts the thread). All times are measured with `System.nanoTime()`-based milliseconds; a `TimeSource`/lambda is injected so tests control time, and the loop body is a single `tick()` function that the tests call directly without sleeping.
   - The watchdog starts before `application { ... }` in `Main.kt`.
5. **`ThreadDump.render(...)`**: a pure function (testable with synthetic `Map<Thread, Array<StackTraceElement>>` plus the thread states) that renders: a header line with the time, the silent duration and the reason; a memory line (used/total/max heap in MB) and the GC names with counts and times (`ManagementFactory`); then the AWT event-queue thread FIRST (the thread whose name starts with `AWT-EventQueue`), then all other threads sorted by name, each as `"name" daemon? state` + `at ...` lines (cut at 60 frames per thread). It must not include environment variables or system properties other than the ones already listed in the startup record.
6. **Job logging in the server** (`Application.kt`): `POST /jobs` logs at INFO the job id, kind (playlist/video), the number of items and the options (overwrite, includeRank, concurrency, searchLyricsOnline; the output directory is allowed); the SSE handler logs every `ItemDone` (rank only), `ItemSkipped` and `ItemFailed` (rank and reason text), `ItemStarted` at DEBUG (rank, no title), and the `JobDone` summary (status and the three counts) at INFO; `DELETE /jobs/{id}` logs the cancellation. Never log titles, file names, lyrics, tokens or headers. The text lines come from a pure function `JobLog.describe(event)` (server module) that tests cover. `LocalServer.start` logs once at INFO the server port and the tool paths it found (yt-dlp, ffmpeg, JS runtime, or `없음`), no token.
7. **README**: a "문제가 생겼을 때" section: where the log lives (`%APPDATA%\xGetSongs\logs\xgetsongs.log`, rotated, 7 days / 50MB), what `ui-hang-*.txt` is, what a `JVM 종료 시작 ... 외부 종료 요청` line means, and the advice to send the newest log file when reporting a problem.

**Tests (write them first)**
- `HangDetectorTest`: no hang while beats arrive; `Hung(first)` after more than the threshold without a beat (boundary: exactly the threshold is not yet a hang, one ms more is); no second report before `repeatMs`, a `repeat = true` report after it; `Recovered` with the right total after a late beat, then silence; a gap between ticks larger than `maxTickGapMs` resets the baseline and reports nothing (and a real hang after the reset is still detected from the new baseline); a beat that arrives exactly when the threshold passes; `gapsIgnored` counts.
- `ThreadDumpTest`: the AWT-EventQueue thread comes first, the others are sorted, frames are cut at 60, the header and memory/GC lines are present, daemon/state shown, no environment variables.
- `UiWatchdogTest`: with fake time and a `postToUi` that never runs the heartbeat, repeated `tick()` calls produce one dump callback (a file `ui-hang-*.txt` in a temp dir with the EDT-first dump) and one ERROR log record, then a recovery log when the heartbeat starts running; with a `postToUi` that runs the heartbeat immediately no dump ever; the 20-file retention; `stop()` ends the thread (smoke test with the real thread, a tiny interval and a generous timeout, no timing assertions on the dump).
- `ExitLoggerTest`: `describeExit` for user exit (no running jobs, with jobs), external exit (WARN text mentions `외부 종료 요청`), unknown jobs (-1); the shutdown hook registration function is testable without actually exiting (call the hook's body).
- `LogbackConfigTest`: load the shipped `logback.xml` into a fresh `LoggerContext` with `xgs.logDir` pointing at a temp dir (use `JoranConfigurator` and `context.putProperty`), log INFO/DEBUG/WARN lines of different loggers, then assert: the file `xgetsongs.log` exists in that dir and holds the INFO line and a `com.xgetsongs` DEBUG line but NOT a `io.netty` DEBUG line, the console appender receives no DEBUG line, and the rolling policy settings (max history 7, total cap 50MB, 5MB file size) are present in the configured appender. Stop the context afterwards.
- `JobLogTest` (server): `describe` for every `JobEvent` type; lines contain rank/ids/counts, never a title or file name (use events whose names are recognisable markers and assert they are absent), and a test with a logback `ListAppender` proving `POST /jobs` + SSE through the existing test app logs the job start and the `JobDone` summary and logs no `X-XGS-Token` value.

- [ ] **Step 1: Write failing tests**
- [ ] **Step 2: Run to see them fail:** `.\gradlew.bat --no-daemon check` (compile errors count as failing)
- [ ] **Step 3: Implement** behaviours 1-7.
- [ ] **Step 4: Run** `.\gradlew.bat --no-daemon check` (green, none skipped; test methods return `Unit`).
- [ ] **Step 5: Real run check** (no network, no UI): write a throwaway `main` (NOT committed; delete it afterwards) that calls `Diagnostics.start(<a temp dir>)`, keeps a fake UI executor blocked for about 7 seconds so the watchdog fires, then prints the size and the first lines of the produced `ui-hang-*.txt` and ends with `System.exit(0)` so the exit logger writes its `JVM 종료 시작` line classified as `외부 종료 요청`. Report the observed lines (structure only; there are no secrets involved).
- [ ] **Step 6: Commit** in Korean (see the project rule: Korean commit messages, trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`), a few commits are fine (app diagnostics, server logging, docs).

---

### Task 11: Show the lyrics result of every file in the list

Added after the user's request "가사 저장 성공 여부를 각각의 파일에 표시해줘". Every finished row of the file list says where its lyrics came from or why it has none, so the user sees at a glance which files got lyrics and which did not. The lyrics are written in the same tagging step that already fails the item when the tag cannot be written, so a finished item with a lyrics outcome of DESCRIPTION or ONLINE really has the lyrics frame in its mp3.

**Files:**
- Modify: `shared/src/commonMain/kotlin/com/xgetsongs/shared/api/ApiModels.kt` (new `LyricsOutcome`, `JobEvent.ItemDone.lyrics`)
- Modify: `engine/src/main/kotlin/com/xgetsongs/engine/job/ItemDownloader.kt` (`DownloadResult.Downloaded.lyrics`), `engine/.../job/DefaultDownloadService.kt`
- Modify: `server/src/main/kotlin/com/xgetsongs/server/JobLog.kt`
- Modify: `app/src/commonMain/kotlin/com/xgetsongs/app/state/UiState.kt`, `AppStateHolder.kt`, `Labels.kt`; `app/.../ui/PreviewList.kt` only if the label needs layout care
- Tests: `shared/.../ApiModelsTest.kt`, `engine/.../job/DefaultDownloadServiceTest.kt`, `server/.../JobLogTest.kt`, `app/.../LabelsTest.kt`, `app/.../AppStateHolderTest.kt`, `app/src/desktopTest/.../EndToEndTest.kt` and every test that compares with `ItemStatus.Done`
- Modify: `README.md`, and one sentence of spec 6.4/7 (the controller edits the spec: do not touch `docs/superpowers`)

**Interfaces:**
- Produces: `@Serializable enum class LyricsOutcome { DESCRIPTION, ONLINE, NOT_FOUND, SEARCH_OFF }` in `com.xgetsongs.shared.api`; `JobEvent.ItemDone(rank: Int, fileName: String, lyrics: LyricsOutcome? = null)` (the new field is last and nullable with default null: an event without it decodes to null and means "not known"); `DownloadResult.Downloaded(file: Path, lyrics: LyricsOutcome)`; `ItemStatus.Done(val lyrics: LyricsOutcome? = null)` (was `data object Done`: it becomes `data class Done`).

**Behaviour**

1. **Outcome rules** (`ItemDownloader.download`, after the tag step succeeded): `DESCRIPTION` when the lyrics came from the video description; else `ONLINE` when the provider found lyrics; else `SEARCH_OFF` when the lyrics lookup was off for this item (`prepared.searchLyricsOnline` false) and the description had none; else `NOT_FOUND` (the lookup ran, or failed, and gave nothing). The outcome must describe what was WRITTEN: if the tag step fails the item fails as before and no outcome is reported. A provider exception counts as `NOT_FOUND`.
2. **Event:** `DefaultDownloadService` sends `ItemDone(rank, fileName, outcome)`. Items skipped because the file already existed (`ItemSkipped` "이미 존재") and failed items carry no outcome; nothing else about events changes.
3. **Server:** no route change (the SSE JSON simply carries the new field; keep `encodeDefaults` behaviour as is). `JobLog.describe(ItemDone)` appends the outcome name when present: `항목 완료: 순위 3, 가사 ONLINE` (no title, no file name, no lyrics).
4. **App state:** the holder maps `ItemDone` to `ItemStatus.Done(event.lyrics)`; everything else that treats `Done` (counting, retry, reset of in-flight rows, summaries) keeps working; update the code and tests that used `ItemStatus.Done` as an object.
5. **Labels:** `statusLabel(Done(lyrics))` returns `완료` for null, `완료 · 가사 ✓ 설명란` for DESCRIPTION, `완료 · 가사 ✓ 인터넷` for ONLINE, `완료 · 가사 없음` for NOT_FOUND, `완료 · 가사 없음 (검색 끔)` for SEARCH_OFF. The status cell is 220.dp wide: check the longest label fits on one line in `StatusCell` at the current font (if it would wrap, allow two lines or shorten the label to `완료 · 가사 없음 (검색 끔)` -> `완료 · 가사 없음(검색 끔)`; do not truncate). Optionally colour the lyrics part (a check mark in the primary colour, "없음" in the outline colour) only if it is simple with the existing `StatusCell`; the plain text labels are the requirement.
6. **README:** one sentence in the lyrics part: every finished row shows `가사 ✓ 설명란`, `가사 ✓ 인터넷`, `가사 없음` or `가사 없음 (검색 끔)`.

**Tests (write them first)**
- `ApiModelsTest`: `ItemDone` round trip with each outcome and with null; JSON of an old-style event without the field decodes to `lyrics == null`; the SSE name stays `item-done`.
- `DefaultDownloadServiceTest` (existing fakes: fake yt-dlp info file with/without a lyrics section in the description, fake `LyricsProvider`): description lyrics give `ItemDone(..., DESCRIPTION)` and the provider is not called; no description lyrics + provider returns text gives ONLINE; provider returns null gives NOT_FOUND; provider throws gives NOT_FOUND and the item still succeeds; lookup off (`searchLyricsOnline = false`) with no description lyrics gives SEARCH_OFF; lookup off but the description has lyrics gives DESCRIPTION; a tag failure gives `ItemFailed` and no `ItemDone`; an existing-file skip gives `ItemSkipped`.
- `JobLogTest`: the new log line for each outcome and for null; still no title/file name.
- `LabelsTest`: all five labels. `AppStateHolderTest`: the event updates the row to `Done(outcome)` for each outcome and the fileName; a retry run replaces it; reset clears. `EndToEndTest` and the other tests that asserted `ItemStatus.Done` are adapted.

- [ ] **Step 1: Write failing tests**
- [ ] **Step 2: Run to see them fail:** `.\gradlew.bat --no-daemon check --console=plain` (compile errors count as failing)
- [ ] **Step 3: Implement** behaviours 1-6.
- [ ] **Step 4: Run** `.\gradlew.bat --no-daemon check --console=plain` (green, none skipped; test methods return `Unit`).
- [ ] **Step 5: Commit** in Korean (trailer `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`), a few commits are fine (shared+engine, server+app, docs).
