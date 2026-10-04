package com.xgetsongs.shared.title

import kotlin.test.Test
import kotlin.test.assertEquals

class TitleParserTest {
    private fun assertParsed(
        raw: String,
        artist: String,
        title: String,
        confidence: Confidence = Confidence.HIGH,
        channel: String? = null,
        metaArtist: String? = null,
        metaTrack: String? = null,
    ) {
        val parsed = TitleParser.parse(raw, channel, metaArtist, metaTrack)
        assertEquals(ParsedTrack(artist, title, confidence), parsed, "title: $raw")
    }

    // ---- fixtures taken from the reference playlist "Melon Daily Top 100" ----------------

    @Test
    fun quotedTitleWithOfficialMusicVideoSuffix() =
        assertParsed(
            "소연 (SOYEON) '퇴사할게여 (Narr. 기안84)' Official Music Video",
            "소연 (SOYEON)", "퇴사할게여 (Narr. 기안84)",
        )

    @Test
    fun curlyQuotes() =
        assertParsed("RESCENE (리센느) ‘LOVE ATTACK’ Official MV", "RESCENE (리센느)", "LOVE ATTACK")

    @Test
    fun curlyQuotesWithApostropheInside() =
        assertParsed("ILLIT (아일릿) ‘It’s Me’ Official MV", "ILLIT (아일릿)", "It’s Me")

    @Test
    fun dashSeparatorWithTrailingMv() =
        assertParsed("아이오아이 (I.O.I) - 갑자기 (Suddenly) MV", "아이오아이 (I.O.I)", "갑자기 (Suddenly)")

    @Test
    fun dashSeparatorWithQuotedTitle() =
        assertParsed("ATEEZ(에이티즈) - 'BAD' Official MV", "ATEEZ(에이티즈)", "BAD")

    @Test
    fun leadingMvTagAndUnderscoreSeparator() =
        assertParsed(
            "[MV] 태연 (TAEYEON)_ 만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1",
            "태연 (TAEYEON)", "만찬가 (晩餐歌 / BANSANKA) : J-POP REMAKE Vol.1",
        )

    @Test
    fun spacedUnderscoreSeparator() =
        assertParsed(
            "[MV] Woody(우디) _ Sadder Than Yesterday(어제보다 슬픈 오늘)",
            "Woody(우디)", "Sadder Than Yesterday(어제보다 슬픈 오늘)",
        )

    @Test
    fun liveClipSuffix() = assertParsed("WOODZ 'Drowning' Live Clip", "WOODZ", "Drowning")

    @Test
    fun leadingEmojiAndProgramTailAfterPipe() =
        assertParsed(
            "🎤진영&최유리 - 생각을 멈추다 보면 | 들어봐! 유리의 숲2 EP.01 진영 편",
            "진영&최유리", "생각을 멈추다 보면",
        )

    @Test
    fun trailingBracketBlockAndPipeTail() =
        assertParsed(
            "성시경 - 너의 모든 순간 [유희열의 스케치북/You Heeyeol’s Sketchbook] | KBS 210528 방송",
            "성시경", "너의 모든 순간",
        )

    @Test
    fun apostropheInsideArtistNameIsNotAQuote() =
        assertParsed(
            "Girls' Generation-HRS 소녀시대-효리수 'Skibidi' Performance Video",
            "Girls' Generation-HRS 소녀시대-효리수", "Skibidi",
        )

    @Test
    fun textAfterClosingQuoteIsDiscarded() =
        assertParsed(
            "볼빨간사춘기 BOL4 '여름아 부탁해' Special Clip (with 적재)",
            "볼빨간사춘기 BOL4", "여름아 부탁해",
        )

    @Test
    fun backtickInsideQuotedTitleIsKept() =
        assertParsed(
            "AKMU - '어떻게 이별까지 사랑하겠어, 널 사랑하는 거지(How can I love the heartbreak, you`re the one I love)' M/V",
            "AKMU", "어떻게 이별까지 사랑하겠어, 널 사랑하는 거지(How can I love the heartbreak, you`re the one I love)",
        )

    @Test
    fun otherReferenceTitles() {
        assertParsed("BIGBANG - ‘BiiiG’ M/V", "BIGBANG", "BiiiG")
        assertParsed("[MV] 한로로 (HANRORO) - 사랑하게 될 거야 (Landing in Love)", "한로로 (HANRORO)", "사랑하게 될 거야 (Landing in Love)")
        assertParsed("Hearts2Hearts 하츠투하츠 'RUDE!' MV", "Hearts2Hearts 하츠투하츠", "RUDE!")
        assertParsed("IU '이 별로부터(Unknown Planet)' MV", "IU", "이 별로부터(Unknown Planet)")
        assertParsed("aespa 에스파 'LEMONADE' MV", "aespa 에스파", "LEMONADE")
        assertParsed("다비치 (DAVICHI) '타임캡슐' Official Music Video", "다비치 (DAVICHI)", "타임캡슐")
        assertParsed("YENA(최예나) - '캐치 캐치' M/V", "YENA(최예나)", "캐치 캐치")
        assertParsed("도경수 Doh Kyung Soo 'Popcorn' MV", "도경수 Doh Kyung Soo", "Popcorn")
    }

    // ---- synthetic edge cases -------------------------------------------------------------

    @Test
    fun leadingParenthesisInArtistNameIsKept() =
        assertParsed("(G)I-DLE - 'TOMBOY' Official Music Video", "(G)I-DLE", "TOMBOY")

    @Test
    fun apostropheBeforeSeparator() = assertParsed("Girls' Generation - Gee", "Girls' Generation", "Gee")

    @Test
    fun separatorInsideQuotesBelongsToTheTitle() =
        assertParsed("IU 'Love - Poem' MV", "IU", "Love - Poem")

    @Test
    fun apostropheInsideQuotedTitle() =
        assertParsed("Artist 'Don't Stop' MV", "Artist", "Don't Stop")

    @Test
    fun enDashSeparator() = assertParsed("Artist – Title", "Artist", "Title")

    @Test
    fun nestedLeadingTags() = assertParsed("[Official MV] [MV] Artist - Song", "Artist", "Song")

    @Test
    fun leadingEmoji() = assertParsed("🎶 Artist - Song", "Artist", "Song")

    @Test
    fun noiseMustBeASeparateWord() = assertParsed("Artist - HAMV", "Artist", "HAMV")

    @Test
    fun noiseInParentheses() = assertParsed("Artist - Song (Official Video)", "Artist", "Song")

    @Test
    fun parenthesesThatAreNotNoiseAreKept() =
        assertParsed("Artist - Song (feat. Someone)", "Artist", "Song (feat. Someone)")

    // ---- fallbacks ------------------------------------------------------------------------

    @Test
    fun metadataIsUsedWhenTitleHasNoPattern() =
        assertParsed(
            "Dynamite", "BTS", "Dynamite", Confidence.MEDIUM,
            channel = "BTS - Topic", metaArtist = "BTS", metaTrack = "Dynamite",
        )

    @Test
    fun topicChannelTitleIsNeverSplit() =
        assertParsed(
            "Song - Remix", "Artist", "Song - Remix", Confidence.MEDIUM,
            channel = "Artist - Topic", metaArtist = "Artist", metaTrack = "Song - Remix",
        )

    @Test
    fun topicChannelWithoutMetadataFallsBackToChannelName() =
        assertParsed("Dynamite", "BTS", "Dynamite", Confidence.LOW, channel = "BTS - Topic")

    @Test
    fun vevoSuffixIsRemovedFromChannelName() =
        assertParsed("7 rings (Official Video)", "ArianaGrande", "7 rings", Confidence.LOW, channel = "ArianaGrandeVevo")

    @Test
    fun unknownArtistWhenNothingIsAvailable() =
        assertParsed("Some Song", TitleParser.UNKNOWN_ARTIST, "Some Song", Confidence.LOW)

    // ---- ASCII symbols at the start of an artist name are not junk -------------------------

    @Test
    fun ampersandStaysInArtistName() = assertParsed("&TEAM 'Go in Blind' MV", "&TEAM", "Go in Blind")

    @Test
    fun asteriskStaysInArtistName() = assertParsed("*NSYNC - Bye Bye Bye", "*NSYNC", "Bye Bye Bye")

    @Test
    fun dollarSignsStayInArtistName() = assertParsed("\$uicideboy\$ - Song", "\$uicideboy\$", "Song")

    @Test
    fun exclamationMarksAloneAreAnArtistName() = assertParsed("!!! - Song", "!!!", "Song")

    @Test
    fun emojiWithVariationSelectorIsStillStripped() = assertParsed("\u2764\uFE0F Artist - Song", "Artist", "Song")

    // ---- any leading [..] tag is dropped, not only known ones ------------------------------

    @Test
    fun anyLeadingBracketTagIsDropped_mama() =
        assertParsed(
            "[#2024MAMA] G-DRAGON - HOME SWEET HOME (feat. Taeyang, Daesung) | Mnet 241123",
            "G-DRAGON", "HOME SWEET HOME (feat. Taeyang, Daesung)",
        )

    @Test
    fun anyLeadingBracketTagIsDropped_live() =
        assertParsed(
            "[LIVE] 이창섭 - 천상연 (선녀외전 OST) 라이브 (Full. ver)",
            "이창섭", "천상연 (선녀외전 OST) 라이브 (Full. ver)",
        )

    @Test
    fun anyLeadingBracketTagIsDropped_liveWithShowName() =
        assertParsed(
            "[LIVE] Car, the garden - 그대 작은 나의 세상이 되어 | 2026 단독공연 'BLUE HEART'",
            "Car, the garden", "그대 작은 나의 세상이 되어",
        )

    @Test
    fun anyLeadingBracketTagIsDropped_programName() =
        assertParsed(
            "[DJ티비씨] 폴킴(Paul Kim) - 모든 날, 모든 순간 \u266C #비긴어게인3 #DJ티비씨",
            "폴킴(Paul Kim)", "모든 날, 모든 순간 \u266C #비긴어게인3 #DJ티비씨",
        )

    @Test
    fun anyLeadingBracketTagIsDropped_genre() =
        assertParsed(
            "[Ballad] 임현정 - 사랑은 봄비처럼... 이별은 겨울비처럼...",
            "임현정", "사랑은 봄비처럼... 이별은 겨울비처럼...",
        )

    @Test
    fun anyLeadingBracketTagIsDropped_preRelease() =
        assertParsed(
            "[선공개] PLAVE - 이 밤을 빌려 말해요 MV (영화 '오늘 밤, 세계에서 이 사랑이 사라진다 해도')",
            "PLAVE", "이 밤을 빌려 말해요 MV (영화 '오늘 밤, 세계에서 이 사랑이 사라진다 해도')",
        )

    @Test
    fun aTitleThatIsOnlyABracketBlockIsKept() {
        val parsed = TitleParser.parse("[MV]", "Some Channel")
        assertEquals("Some Channel", parsed.artist)
        assertEquals("[MV]", parsed.title)
        assertEquals(Confidence.LOW, parsed.confidence)
    }
}
