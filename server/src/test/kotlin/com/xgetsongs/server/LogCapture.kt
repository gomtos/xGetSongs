package com.xgetsongs.server

import ch.qos.logback.classic.Level
import ch.qos.logback.classic.Logger
import ch.qos.logback.classic.spi.ILoggingEvent
import ch.qos.logback.core.read.ListAppender
import org.slf4j.LoggerFactory

/**
 * Collects what the logger [name] gets (every logger when it is the root), at every level. [close] puts the logger back as
 * it was.
 */
class LogCapture(name: String = org.slf4j.Logger.ROOT_LOGGER_NAME) : AutoCloseable {
    private val logger = LoggerFactory.getLogger(name) as Logger
    private val appender = ListAppender<ILoggingEvent>().also { it.start() }
    private val previousLevel = logger.level

    init {
        logger.level = Level.TRACE
        logger.addAppender(appender)
    }

    /** A snapshot of the records so far, oldest first. */
    val events: List<ILoggingEvent> get() = synchronized(appender.list) { appender.list.toList() }

    fun at(level: Level): List<ILoggingEvent> = events.filter { it.level == level }

    override fun close() {
        logger.detachAppender(appender)
        logger.level = previousLevel
    }
}
