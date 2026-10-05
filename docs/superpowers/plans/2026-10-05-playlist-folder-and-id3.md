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
