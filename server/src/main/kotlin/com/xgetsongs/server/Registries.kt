package com.xgetsongs.server

import com.xgetsongs.engine.JobHandle
import com.xgetsongs.shared.api.ResolveResponse
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean

/** Remembers recent resolve results so `POST /jobs` can refer to them by ID instead of trusting client data. */
class ResolveCache(private val maxEntries: Int = 20) {
    private val entries = object : LinkedHashMap<String, ResolveResponse>(16, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<String, ResolveResponse>): Boolean =
            size > maxEntries
    }

    /** Stores [response] and returns it with a freshly assigned `resolveId`. */
    fun put(response: ResolveResponse): ResolveResponse {
        val stored = response.copy(resolveId = UUID.randomUUID().toString())
        synchronized(entries) { entries[stored.resolveId] = stored }
        return stored
    }

    fun get(id: String): ResolveResponse? = synchronized(entries) { entries[id] }
}

/** Running jobs by ID. A job's event stream can be claimed by one reader only. */
class JobRegistry {
    private class Entry(val handle: JobHandle) {
        val claimed = AtomicBoolean(false)
    }

    private val jobs = ConcurrentHashMap<String, Entry>()

    fun register(handle: JobHandle): String {
        val id = UUID.randomUUID().toString()
        jobs[id] = Entry(handle)
        return id
    }

    fun exists(id: String): Boolean = jobs.containsKey(id)

    /** Returns the handle to the first caller and null to everyone after (or when the job is unknown). */
    fun claim(id: String): JobHandle? = jobs[id]?.takeIf { it.claimed.compareAndSet(false, true) }?.handle

    fun cancel(id: String): Boolean {
        val entry = jobs[id] ?: return false
        entry.handle.cancel()
        return true
    }

    fun remove(id: String) {
        jobs.remove(id)
    }
}
