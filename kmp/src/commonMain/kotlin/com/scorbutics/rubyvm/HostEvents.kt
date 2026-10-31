package com.scorbutics.rubyvm

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.IO
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlin.io.encoding.Base64
import kotlin.io.encoding.ExperimentalEncodingApi

/**
 * Messages between the host and the Ruby scripts (the scripts' side is the
 * `VMHost` module, loaded in every VM).
 *
 * - Ruby -> host: a script calls `VMHost.emit(topic, payload)`; the
 *   [HostEventListener] of the interpreter running it receives a [HostEvent]
 *   (see [RubyInterpreter.setHostEventListener]).
 * - Host -> Ruby: `post(topic, payload)` on an interpreter of the host's
 *   own; the scripts read the message with `VMHost.each_message(prefix)`.
 *   Each post runs a small script: fine for occasional messages, not for
 *   every frame.
 *
 * A topic is 1 to 128 characters among `[A-Za-z0-9_.:-]`. Consumers share
 * the VM, so each one uses its own prefix ("overlay.", "loader.").
 */
data class HostEvent(
    val topic: String,
    /** UTF-8 text in practice (JSON...) */
    val payload: String
)

/**
 * Receives the [HostEvent]s of an interpreter's scripts, in order, one at a
 * time, off the logging threads: it may take its time, and create or
 * destroy interpreters. When it falls far behind (256 events queued here,
 * 256 more in the C dispatch queue, then the socket buffer), the emitting
 * script waits, and so does all logging, `post` and `executeSync`
 * included: a listener that far behind must not call those, they would
 * wait for it until their timeout.
 */
fun interface HostEventListener {
    fun onHostEvent(event: HostEvent)
}

private val TOPIC_PATTERN = Regex("[A-Za-z0-9_.:\\-]{1,128}")

/** LOG_STREAM_HOST_EVENT (log-listener.h): a log message that is a host event */
internal const val HOST_EVENT_SOURCE = 6

/**
 * The script `post` runs: `VMHost.deliver(topic, payload)`, the payload as a
 * base64 literal, so no text of the caller ever reaches the Ruby source.
 */
@OptIn(ExperimentalEncodingApi::class)
internal fun hostDeliverScript(topic: String, payload: String): String {
    require(TOPIC_PATTERN.matches(topic)) { "Invalid VMHost topic: $topic" }
    return "VMHost.deliver('$topic', '${Base64.encode(payload.encodeToByteArray())}'.unpack1('m0'))"
}

/** How many host events wait for the listener, as in the C dispatch queue */
private const val HOST_EVENT_QUEUE_CAPACITY = 256

/**
 * Turns an interpreter's LOG_STREAM_HOST_EVENT messages ("<topic> <base64>")
 * into [HostEvent]s for its listener, one at a time on a pool thread: the
 * logging dispatch thread only queues them. The queue is bounded: while
 * [HOST_EVENT_QUEUE_CAPACITY] events wait, the dispatch thread waits too,
 * which slows the emitting script down instead of growing the queue.
 */
@OptIn(ExperimentalCoroutinesApi::class)
internal class HostEventRouter {
    private val messages = Channel<String>(HOST_EVENT_QUEUE_CAPACITY)
    private val listener = MutableStateFlow<HostEventListener?>(null)

    /** Started with the first listener */
    private val consumer = CoroutineScope(Dispatchers.IO.limitedParallelism(1)).launch(start = CoroutineStart.LAZY) {
        for (message in messages) deliver(message)
    }

    /** Takes effect at once: after it returns, the previous listener only finishes the call it may be in. */
    fun setListener(listener: HostEventListener?) {
        this.listener.value = listener
        if (listener != null) consumer.start()
    }

    /** Called on the logging dispatch thread; nothing escapes it (a native callback). */
    fun onHostEventMessage(message: String) {
        if (listener.value == null) return
        try {
            runBlocking { messages.send(message) }
        } catch (e: Throwable) {
            // Cancelled: the interpreter is being destroyed
        }
    }

    /** Before the interpreter is destroyed: releases a dispatch thread waiting for room. */
    fun close() {
        messages.cancel()
        consumer.cancel()
    }

    private fun deliver(message: String) {
        val event = parse(message)
        if (event == null) {
            println("[RubyVM] Malformed host event dropped: ${message.take(64)}")
            return
        }
        try {
            listener.value?.onHostEvent(event)
        } catch (e: Throwable) {
            println("[RubyVM] HostEventListener failed on ${event.topic}: ${e.stackTraceToString()}")
        }
    }

    @OptIn(ExperimentalEncodingApi::class)
    private fun parse(message: String): HostEvent? {
        val separator = message.indexOf(' ')
        if (separator <= 0) return null
        val payload = runCatching { Base64.decode(message.substring(separator + 1)) }.getOrNull() ?: return null
        return HostEvent(message.substring(0, separator), payload.decodeToString())
    }
}
