package com.xgetsongs.app.diagnostics

import ch.qos.logback.classic.Level
import ch.qos.logback.classic.Logger
import ch.qos.logback.classic.spi.ILoggingEvent
import ch.qos.logback.core.read.ListAppender
import org.slf4j.LoggerFactory

/**
 * Collects what the class [owner] logs, at every level, and keeps it off the console while it is attached. [close] puts
 * the logger back as it was.
 */
internal class LogCapture(owner: Class<*>) : AutoCloseable {
    private val logger = LoggerFactory.getLogger(owner) as Logger
    private val appender = ListAppender<ILoggingEvent>().also { it.start() }
    private val previousLevel = logger.level
    private val previousAdditive = logger.isAdditive

    init {
        logger.level = Level.TRACE
        logger.isAdditive = false
        logger.addAppender(appender)
    }

    /** A snapshot of the records so far, oldest first. */
    val events: List<ILoggingEvent> get() = synchronized(appender.list) { appender.list.toList() }

    fun at(level: Level): List<ILoggingEvent> = events.filter { it.level == level }

    override fun close() {
        logger.detachAppender(appender)
        logger.level = previousLevel
        logger.isAdditive = previousAdditive
    }
}
