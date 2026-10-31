package com.scorbutics.rubyvm

import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * The Kotlin side of VMHost, through JNI: [RubyInterpreter.setHostEventListener]
 * and [post]. A game-like loop runs on one interpreter, the host posts from
 * its own interpreter, the loop answers with VMHost.emit.
 */
class HostChannelTest {

    private val logListener = object : LogListener {
        override fun onLogMessage(logMessage: LogMessage) {
            if (logMessage.isError()) println("[Ruby ERROR] ${logMessage.message}")
        }
    }

    @Test
    fun testPostAndEmitRoundTrip() {
        val paths = RubyVMPaths.getDefaultPaths()
        val game = RubyInterpreter.create(".", paths.rubyBaseDir, paths.nativeLibsDir, logListener)
        val host = RubyInterpreter.create(".", paths.rubyBaseDir, paths.nativeLibsDir, logListener)
        val events = LinkedBlockingQueue<HostEvent>()
        try {
            game.setHostEventListener { events.add(it) }

            // The loop ends after 10 s anyway: a failure fails instead of hanging destroy()
            val loopEnded = LinkedBlockingQueue<Int>()
            game.execute(
                """
                done = false
                deadline = Time.now + 10
                until done || Time.now > deadline
                  VMHost.each_message('test.') do |topic, payload|
                    VMHost.emit('test.echo', "#{topic}=#{payload}")
                    done = payload == 'stop'
                  end
                  sleep(0.01)
                end
                """.trimIndent()
            ) { loopEnded.add(it) }

            // Multi-byte text (4-byte UTF-8 included) both ways, then an event
            // longer than a pipe write keeps whole
            assertEquals(0, host.post("test.msg", "héllo 😀"))
            assertEquals(0, host.post("test.msg", "x".repeat(10_000)))
            assertEquals(0, host.post("test.msg", "stop"))

            assertEquals(0, loopEnded.poll(15, TimeUnit.SECONDS))
            val first = assertNotNull(events.poll(5, TimeUnit.SECONDS))
            assertEquals(HostEvent("test.echo", "test.msg=héllo 😀"), first)
            assertEquals("test.msg=" + "x".repeat(10_000), assertNotNull(events.poll(5, TimeUnit.SECONDS)).payload)
            assertEquals("test.msg=stop", assertNotNull(events.poll(5, TimeUnit.SECONDS)).payload)

            assertFailsWith<IllegalArgumentException> { host.post("bad topic") }

            // Removed: the next event goes nowhere
            game.setHostEventListener(null)
            assertEquals(0, game.executeSync("VMHost.emit('test.after', '')"))
            assertNull(events.poll())
        } finally {
            game.destroy()
            host.destroy()
        }
    }

    private fun createInterpreter(): RubyInterpreter {
        val paths = RubyVMPaths.getDefaultPaths()
        return RubyInterpreter.create(".", paths.rubyBaseDir, paths.nativeLibsDir, logListener)
    }

    /** destroy() returns although the listener is stuck far behind (the dispatch thread waits for room). */
    @Test
    fun testDestroyWhileListenerIsBehind() {
        val game = createInterpreter()
        val release = CountDownLatch(1)
        val received = AtomicInteger(0)
        try {
            game.setHostEventListener {
                received.incrementAndGet()
                release.await()
            }
            game.execute("600.times { |i| VMHost.emit('test.flood', i.to_s) }") {}
            // The listener holds one event, the queue the next 256: the dispatch thread waits
            while (received.get() == 0) Thread.sleep(10)
            Thread.sleep(300)

            val destroyed = CountDownLatch(1)
            thread { game.destroy(); destroyed.countDown() }
            assertTrue(destroyed.await(15, TimeUnit.SECONDS), "destroy() hung behind the listener")
        } finally {
            release.countDown()
            game.destroy()
        }
    }

    /** A listener throwing an Error loses its event, not the next ones. */
    @Test
    fun testListenerThrowingAnError() {
        val game = createInterpreter()
        val events = LinkedBlockingQueue<HostEvent>()
        try {
            game.setHostEventListener {
                if (it.payload == "fail") throw AssertionError("listener failure")
                events.add(it)
            }
            assertEquals(0, game.executeSync("VMHost.emit('test.error', 'fail'); VMHost.emit('test.error', 'next')"))
            assertEquals("next", assertNotNull(events.poll(5, TimeUnit.SECONDS)).payload)
        } finally {
            game.destroy()
        }
    }

    /** A listener may destroy its own interpreter. */
    @Test
    fun testListenerDestroyingItsInterpreter() {
        val game = createInterpreter()
        val destroyed = CountDownLatch(1)
        try {
            game.setHostEventListener {
                game.destroy()
                destroyed.countDown()
            }
            game.execute("VMHost.emit('test.quit', '')") {}
            assertTrue(destroyed.await(15, TimeUnit.SECONDS), "destroy() from the listener hung")
        } finally {
            game.destroy()
        }
    }
}
