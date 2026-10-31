package com.scorbutics.rubyvm

import kotlin.concurrent.AtomicInt
import kotlin.concurrent.AtomicReference
import kotlin.native.concurrent.FutureState
import kotlin.native.concurrent.ObsoleteWorkersApi
import kotlin.native.concurrent.TransferMode
import kotlin.native.concurrent.Worker
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlinx.cinterop.ExperimentalForeignApi
import platform.posix.usleep

/**
 * The Kotlin/Native side of VMHost (cinterop, the StableRef log context, the
 * host event consumer thread): a loop runs on one interpreter, the host
 * posts from its own interpreter, the loop answers with VMHost.emit.
 */
@OptIn(ExperimentalForeignApi::class, ObsoleteWorkersApi::class)
class HostChannelTest {

    private val logListener = object : LogListener {
        override fun onLogMessage(logMessage: LogMessage) {
            if (logMessage.isError()) println("  [ERROR] ${logMessage.message}")
        }
    }

    private val events = AtomicReference<List<HostEvent>>(emptyList())

    private fun waitFor(condition: () -> Boolean) {
        repeat(1500) {
            if (condition()) return
            usleep(10_000u)
        }
    }

    @Test
    fun testPostAndEmitRoundTrip() {
        val paths = RubyVMPaths.getDefaultPaths()
        val game = RubyInterpreter.create(".", paths.rubyBaseDir, paths.nativeLibsDir, logListener)
        val host = RubyInterpreter.create(".", paths.rubyBaseDir, paths.nativeLibsDir, logListener)
        try {
            game.setHostEventListener { event ->
                while (true) {
                    val current = events.value
                    if (events.compareAndSet(current, current + event)) break
                }
            }

            // The loop ends after 10 s anyway: a failure fails instead of hanging destroy()
            val loopExitCode = AtomicInt(-1)
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
            ) { loopExitCode.value = it }

            assertEquals(0, host.post("test.msg", "héllo 😀"))
            assertEquals(0, host.post("test.msg", "x".repeat(10_000)))
            assertEquals(0, host.post("test.msg", "stop"))

            waitFor { loopExitCode.value != -1 && events.value.size >= 3 }
            assertEquals(0, loopExitCode.value)
            assertEquals(
                listOf("test.msg=héllo 😀", "test.msg=" + "x".repeat(10_000), "test.msg=stop"),
                events.value.map { it.payload }
            )
        } finally {
            game.destroy()
            host.destroy()
        }
    }

    /** destroy() returns although the listener is stuck far behind (the dispatch thread waits for room). */
    @Test
    fun testDestroyWhileListenerIsBehind() {
        val paths = RubyVMPaths.getDefaultPaths()
        val game = RubyInterpreter.create(".", paths.rubyBaseDir, paths.nativeLibsDir, logListener)
        val released = AtomicInt(0)
        val received = AtomicInt(0)
        val worker = Worker.start(name = "HostChannelTest-destroy")
        try {
            game.setHostEventListener {
                received.incrementAndGet()
                while (released.value == 0) usleep(1_000u)
            }
            game.execute("600.times { |i| VMHost.emit('test.flood', i.to_s) }") {}
            waitFor { received.value > 0 }
            usleep(300_000u)

            val destroyed = worker.execute(TransferMode.SAFE, { game }) { it.destroy(); true }
            waitFor { destroyed.state == FutureState.COMPUTED }
            assertEquals(FutureState.COMPUTED, destroyed.state, "destroy() hung behind the listener")
        } finally {
            released.value = 1
            worker.requestTermination().result
        }
    }

    private fun createInterpreter(): RubyInterpreter {
        val paths = RubyVMPaths.getDefaultPaths()
        return RubyInterpreter.create(".", paths.rubyBaseDir, paths.nativeLibsDir, logListener)
    }

    /** A listener throwing an Error loses its event, not the next ones (and the process lives). */
    @Test
    fun testListenerThrowingAnError() {
        val game = createInterpreter()
        val next = AtomicReference<String?>(null)
        try {
            game.setHostEventListener {
                if (it.payload == "fail") throw AssertionError("listener failure")
                next.value = it.payload
            }
            assertEquals(0, game.executeSync("VMHost.emit('test.error', 'fail'); VMHost.emit('test.error', 'next')"))
            waitFor { next.value != null }
            assertEquals("next", next.value)
        } finally {
            game.destroy()
        }
    }

    /** A listener may destroy its own interpreter. */
    @Test
    fun testListenerDestroyingItsInterpreter() {
        val game = createInterpreter()
        val destroyed = AtomicInt(0)
        try {
            game.setHostEventListener {
                game.destroy()
                destroyed.value = 1
            }
            game.execute("VMHost.emit('test.quit', '')") {}
            waitFor { destroyed.value == 1 }
            assertEquals(1, destroyed.value, "destroy() from the listener hung")
        } finally {
            game.destroy()
        }
    }
}
