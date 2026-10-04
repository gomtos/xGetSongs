package com.xgetsongs.engine.ytdlp

import com.xgetsongs.engine.ResolveException
import com.xgetsongs.engine.testutil.FakeProcessRunner
import com.xgetsongs.engine.testutil.TEST_TOOLS
import com.xgetsongs.engine.testutil.toolsOf
import com.xgetsongs.shared.api.InputKind
import com.xgetsongs.shared.input.RejectReason
import kotlinx.coroutines.test.runTest
import java.io.IOException
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class YtDlpResolverTest {
    private val playlistId = "PL2HEDIx6Li8jGsqCiXUq9fzCqpH99qqHV"
    private val playlistUrl = "https://www.youtube.com/playlist?list=$playlistId"

    private val playlistJson = """
        {"_type":"playlist","id":"$playlistId","title":"Melon Daily Top 100","entries":[
          {"_type":"url","ie_key":"Youtube","id":"vid00000001","title":"소연 (SOYEON) '퇴사할게여 (Narr. 기안84)' Official Music Video","channel":"i-dle (아이들)","uploader":"i-dle (아이들)","availability":null},
          {"id":"vid00000002","title":"[Private video]","channel":null,"availability":"needs_auth"},
          {"id":"vid00000003","title":"[Deleted video]"},
          {"id":"vid00000004","title":"Dynamite","channel":"BTS - Topic"}
        ]}
    """.trimIndent()

    private fun runnerReturning(stdout: String, exitCode: Int = 0, stderr: List<String> = emptyList()) =
        FakeProcessRunner { _, onStdout, onStderr ->
            stdout.lines().forEach(onStdout)
            stderr.forEach(onStderr)
            exitCode
        }

    private fun resolver(runner: FakeProcessRunner, tools: com.xgetsongs.engine.tools.ToolPaths = TEST_TOOLS) =
        YtDlpResolver(runner, toolsOf(tools))

    @Test
    fun resolvesAPlaylistIntoRankedItems() = runTest {
        val runner = runnerReturning(playlistJson)

        val response = resolver(runner).resolve(playlistUrl)

        assertEquals(InputKind.PLAYLIST, response.kind)
        assertEquals("Melon Daily Top 100", response.playlistTitle)
        assertEquals(listOf(1, 2, 3, 4), response.items.map { it.rank })
        val first = response.items[0]
        assertEquals("vid00000001", first.videoId)
        assertEquals("소연 (SOYEON)", first.artist)
        assertEquals("퇴사할게여 (Narr. 기안84)", first.track)
        assertEquals("001 소연 (SOYEON) - 퇴사할게여 (Narr. 기안84).mp3", first.expectedFileName)
        assertFalse(first.lowConfidence)
        assertTrue(first.available)
    }

    @Test
    fun unavailableEntriesKeepTheirRankButHaveNoFileName() = runTest {
        val items = resolver(runnerReturning(playlistJson)).resolve(playlistUrl).items

        assertFalse(items[1].available)
        assertEquals("비공개 영상", items[1].unavailableReason)
        assertNull(items[1].expectedFileName)
        assertEquals("삭제된 영상", items[2].unavailableReason)
        assertEquals(3, items[2].rank)
    }

    @Test
    fun channelFallbackIsMarkedLowConfidence() = runTest {
        val item = resolver(runnerReturning(playlistJson)).resolve(playlistUrl).items[3]

        assertTrue(item.lowConfidence)
        assertEquals("BTS", item.artist)
        assertEquals("004 BTS - Dynamite.mp3", item.expectedFileName)
    }

    @Test
    fun playlistCommandUsesTheCanonicalUrlAndFlatListing() = runTest {
        val runner = runnerReturning(playlistJson)

        resolver(runner).resolve("  $playlistId  ")

        val command = runner.commands.single()
        assertTrue("--flat-playlist" in command)
        assertEquals(playlistUrl, command.last())
    }

    @Test
    fun playlistsLongerThan999AreTruncated() = runTest {
        val entries = (1..1001).joinToString(",") { """{"id":"v${it.toString().padStart(10, '0')}","title":"A - B$it"}""" }
        val runner = runnerReturning("""{"title":"Big","entries":[$entries]}""")

        val response = resolver(runner).resolve(playlistUrl)

        assertEquals(999, response.items.size)
        assertTrue(response.truncated)
        assertEquals(999, response.items.last().rank)
    }

    @Test
    fun watchUrlWithListRemembersTheVideoId() = runTest {
        val runner = runnerReturning(playlistJson)

        val response = resolver(runner).resolve("https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=$playlistId")

        assertEquals("dQw4w9WgXcQ", response.alsoVideoId)
    }

    @Test
    fun resolvesASingleVideoAsRankOne() = runTest {
        val runner = runnerReturning(
            """{"id":"dQw4w9WgXcQ","title":"Rick Astley - Never Gonna Give You Up (Official Video)","channel":"Rick Astley","artist":"Rick Astley","track":"Never Gonna Give You Up"}""",
        )

        val response = resolver(runner).resolve("https://youtu.be/dQw4w9WgXcQ")

        assertEquals(InputKind.VIDEO, response.kind)
        val item = response.items.single()
        assertEquals(1, item.rank)
        assertEquals("001 Rick Astley - Never Gonna Give You Up.mp3", item.expectedFileName)
        assertEquals("https://www.youtube.com/watch?v=dQw4w9WgXcQ", runner.commands.single().last())
    }

    @Test
    fun invalidInputIsRejectedWithoutRunningAnything() = runTest {
        val runner = runnerReturning("{}")

        val error = assertFailsWith<ResolveException> { resolver(runner).resolve("https://evil.com/x") }

        assertEquals(RejectReason.UNSUPPORTED_HOST.message, error.message)
        assertTrue(runner.commands.isEmpty())
    }

    @Test
    fun missingYtDlpGivesAHelpfulError() = runTest {
        val error = assertFailsWith<ResolveException> {
            resolver(runnerReturning("{}"), TEST_TOOLS.copy(ytDlp = null)).resolve(playlistUrl)
        }
        assertTrue(error.message!!.contains("yt-dlp"))
    }

    @Test
    fun ytDlpFailureBecomesAResolveException() = runTest {
        val runner = runnerReturning(
            "", exitCode = 1,
            stderr = listOf("ERROR: [youtube:tab] $playlistId: The playlist does not exist."),
        )

        val error = assertFailsWith<ResolveException> { resolver(runner).resolve(playlistUrl) }

        assertTrue(error.message!!.contains("playlist does not exist"))
    }

    @Test
    fun privateVideoErrorIsTranslated() = runTest {
        val runner = runnerReturning("", exitCode = 1, stderr = listOf("ERROR: [youtube] abc: Private video. Sign in if you've been granted access"))

        val error = assertFailsWith<ResolveException> { resolver(runner).resolve("dQw4w9WgXcQ") }

        assertEquals("비공개 영상", error.message)
    }

    @Test
    fun unreadableOutputIsAResolveException() = runTest {
        assertFailsWith<ResolveException> { resolver(runnerReturning("not json")).resolve(playlistUrl) }
    }

    @Test
    fun fetchReturnsFullMetadata() = runTest {
        val runner = runnerReturning("""{"id":"dQw4w9WgXcQ","title":"Dynamite","channel":"BTS - Topic","artist":"BTS","track":"Dynamite"}""")

        val meta = assertNotNull(resolver(runner).fetch("dQw4w9WgXcQ"))

        assertEquals("BTS", meta.artist)
        assertEquals("Dynamite", meta.track)
        assertEquals("BTS - Topic", meta.channel)
    }

    @Test
    fun fetchReturnsNullWhenYtDlpFails() = runTest {
        assertNull(resolver(runnerReturning("", exitCode = 1, stderr = listOf("ERROR: boom"))).fetch("dQw4w9WgXcQ"))
    }

    @Test
    fun nonPublicAvailabilityMakesAnItemUnavailableAndKeepsItsRank() = runTest {
        val runner = runnerReturning(
            """{"title":"T","entries":[
              {"id":"vid00000001","title":"Song A","availability":"premium_only"},
              {"id":"vid00000002","title":"Song B","availability":"subscriber_only"},
              {"id":"vid00000003","title":"Song C","availability":"needs_auth"},
              {"id":"vid00000004","title":"Artist - Song D","availability":"public"},
              {"id":"vid00000005","title":"Artist - Song E","availability":"unlisted"}
            ]}""",
        )

        val items = resolver(runner).resolve(playlistUrl).items

        assertEquals(listOf(1, 2, 3, 4, 5), items.map { it.rank })
        listOf("premium_only", "subscriber_only", "needs_auth").forEachIndexed { index, availability ->
            val item = items[index]
            assertFalse(item.available)
            assertNull(item.expectedFileName)
            assertTrue(item.unavailableReason!!.contains(availability))
        }
        listOf(items[3], items[4]).forEach { item ->
            assertTrue(item.available)
            assertNotNull(item.expectedFileName)
        }
    }

    @Test
    fun ytDlpThatCannotBeStartedBecomesAResolveException() = runTest {
        val runner = FakeProcessRunner { _, _, _ -> throw IOException("Cannot run program") }

        val error = assertFailsWith<ResolveException> { resolver(runner).resolve(playlistUrl) }

        assertTrue(error.message!!.contains("yt-dlp"))
    }

    @Test
    fun fetchReturnsNullWhenYtDlpCannotBeStarted() = runTest {
        val runner = FakeProcessRunner { _, _, _ -> throw IOException("Cannot run program") }

        assertNull(resolver(runner).fetch("dQw4w9WgXcQ"))
    }
}
