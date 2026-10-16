package com.xgetsongs.server

import com.xgetsongs.shared.api.ApiJson
import com.xgetsongs.shared.api.BrowserAnswer
import com.xgetsongs.shared.api.BrowserCommand
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.Test
import kotlin.test.assertEquals

/**
 * `contract/browser-protocol.json` holds the commands the server hands to the shell's hidden browser and the answers it gets back.
 * The Rust agent (`sidecar-host/src/browser.rs`) reads the same file: neither side can change a field without this test and that one
 * saying so.
 */
class BrowserProtocolTest {
    private val json = ApiJson.instance

    // Gradle runs the tests of a module with the module's folder as the working directory.
    private val contract = Json.parseToJsonElement(Files.readString(Path.of("../contract/browser-protocol.json"))).jsonObject

    @Test
    fun everyCommandOfTheContractFileDecodesAndEncodesBackToItself() {
        val expected = listOf(
            BrowserCommand.Load("c-1", "https://www.google.com/search?q=IU+Love+poem+lyrics&hl=en"),
            BrowserCommand.Evaluate("c-2", "document.title"),
            BrowserCommand.Reset("c-3"),
        )
        val entries = contract.getValue("commands").jsonArray

        assertEquals(expected.size, entries.size)
        entries.zip(expected).forEach { (entry, command) ->
            assertEquals(command, json.decodeFromJsonElement(BrowserCommand.serializer(), entry))
            assertEquals(entry, json.encodeToJsonElement(BrowserCommand.serializer(), command))
        }
    }

    @Test
    fun everyAnswerOfTheContractFileDecodesAndEncodesBackToItself() {
        val expected = listOf(
            BrowserAnswer("c-1", ok = true),
            BrowserAnswer("c-2", ok = true, value = "한글 \"title\"\n"),
            BrowserAnswer("c-3", ok = false, error = "window gone"),
        )
        val entries = contract.getValue("answers").jsonArray

        assertEquals(expected.size, entries.size)
        entries.zip(expected).forEach { (entry, answer) ->
            assertEquals(answer, json.decodeFromJsonElement(BrowserAnswer.serializer(), entry))
            assertEquals(entry, json.encodeToJsonElement(BrowserAnswer.serializer(), answer))
        }
    }
}
