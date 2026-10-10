package com.xgetsongs.server.sidecar

import java.util.Base64
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class HandshakeTest {
    @Test
    fun theLineHasThePrefixThePortAndTheToken() {
        assertEquals("XGS-READY 51234 abc_DEF-123", Handshake.line(51234, "abc_DEF-123"))
    }

    @Test
    fun aTokenOfTheServerFitsInOneWord() {
        // LocalServer makes its tokens with URL-safe Base64 and no padding.
        val token = Base64.getUrlEncoder().withoutPadding().encodeToString(ByteArray(32) { it.toByte() })

        assertEquals("XGS-READY 80 $token", Handshake.line(80, token))
    }

    @Test
    fun aTokenThatIsEmptyOrHoldsWhitespaceIsRefused() {
        assertFailsWith<IllegalArgumentException> { Handshake.line(80, "") }
        assertFailsWith<IllegalArgumentException> { Handshake.line(80, "two words") }
        assertFailsWith<IllegalArgumentException> { Handshake.line(80, "line\nbreak") }
    }

    @Test
    fun aPortOutsideTheRangeIsRefused() {
        assertFailsWith<IllegalArgumentException> { Handshake.line(0, "t") }
        assertFailsWith<IllegalArgumentException> { Handshake.line(65536, "t") }
    }
}
