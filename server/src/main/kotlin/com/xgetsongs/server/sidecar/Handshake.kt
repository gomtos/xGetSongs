package com.xgetsongs.server.sidecar

/**
 * The one line the sidecar prints on stdout once its server listens: `XGS-READY <port> <token>`. The shell that started
 * the process reads it to learn where the server is. Nothing else may be written to stdout (the logs go to stderr).
 */
object Handshake {
    const val PREFIX = "XGS-READY"

    fun line(port: Int, token: String): String {
        require(port in 1..65535) { "port out of range: $port" }
        // The message must not repeat the token: it is a secret.
        require(token.isNotEmpty() && token.none { it.isWhitespace() }) { "the token must be one word" }
        return "$PREFIX $port $token"
    }
}
