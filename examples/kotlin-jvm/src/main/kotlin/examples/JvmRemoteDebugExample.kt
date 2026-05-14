package examples

import com.scorbutics.rubyvm.LogListener
import com.scorbutics.rubyvm.LogMessage
import com.scorbutics.rubyvm.LogSource
import com.scorbutics.rubyvm.RubyInterpreter
import com.scorbutics.rubyvm.execute
import java.io.PrintStream
import java.io.FileOutputStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Demonstrates the remote DAP debugger.
 *
 * What happens:
 *  1. enableRemoteDebug() is called BEFORE the first script. This eagerly
 *     boots the VM with Ruby 3.1's stdlib `debug` gem listening on a TCP
 *     port in non-stop mode.
 *  2. We enqueue a script that calls `binding.break` (the `debug` gem's
 *     breakpoint pragma). The Ruby thread suspends and waits for a DAP
 *     client to drive it.
 *  3. While the script is paused, attach a debugger from another terminal:
 *
 *        rdbg --attach 127.0.0.1 57883
 *        (enter the cookie when prompted: see TOKEN below)
 *
 *     Or in VS Code: install the "VSCode rdbg Ruby Debugger" extension,
 *     run "Debug: Attach with rdbg", choose "Attach with TCP/IP",
 *     host=127.0.0.1, port=57883, debug-cookie=<TOKEN>.
 *
 *  4. From the debugger, use `info`, `step`, `next`, `continue` etc. When
 *     you `continue`, the script finishes and the example exits.
 *
 * To run:
 *     ../../gradlew runExample -PexampleClass=JvmRemoteDebugExample
 *
 * Logging note: once the VM is booted, the logging system dup2's fd 1 and
 * fd 2 onto its own pipes. Any println from main() — or worse, from the
 * LogListener callback — would feed back into the logging thread and
 * deadlock on a pipe write. So we (a) print all banners BEFORE booting the
 * VM and (b) have the LogListener write to a real file, not System.out.
 */
fun main() {
    val port = 57883
    val token = "demo-cookie-change-me"
    val logFilePath = "/tmp/jvm-remote-debug-example.log"

    println("=== Ruby VM Remote DAP Debugger Example ===\n")

    /* Open a real-file PrintStream for the LogListener so its writes never
     * touch fd 1/2 (which get redirected to the logging pipes when the VM
     * boots — see comment above). */
    val rubyLog = PrintStream(FileOutputStream(logFilePath, false), true)
    val listener = object : LogListener {
        override fun onLogMessage(logMessage: LogMessage) {
            val tag = when (logMessage.source) {
                LogSource.RUBY_STDERR, LogSource.NATIVE_STDERR -> "Ruby:err"
                LogSource.VMLOGGER -> "VM"
                else -> "Ruby"
            }
            rubyLog.println("[$tag] ${logMessage.message}")
        }
    }

    val paths = com.scorbutics.rubyvm.RubyVMPaths.getDefaultPaths()
    println("Ruby paths:")
    println("  installDir   = ${paths.installDir}")
    println("  rubyBaseDir  = ${paths.rubyBaseDir}")
    println("  nativeLibsDir= ${paths.nativeLibsDir}")
    println("Ruby/VM log:   $logFilePath  (tail -f this in another terminal)")
    println()
    println("=================================================================")
    println(" About to arm the rdbg/DAP listener on 127.0.0.1:$port")
    println(" Cookie: $token")
    println()
    println(" Attach from another terminal:")
    println("     rdbg --attach 127.0.0.1 $port")
    println(" (enter the cookie above when prompted)")
    println()
    println(" Or in VS Code: 'Debug: Attach with rdbg' -> TCP/IP")
    println("     host=127.0.0.1  port=$port  debug-cookie=$token")
    println("=================================================================")
    println()

    RubyInterpreter.create(
        appPath = ".",
        rubyBaseDir = paths.rubyBaseDir,
        nativeLibsDir = paths.nativeLibsDir,
        listener = listener
    ).use { interpreter ->
        // After this point the VM is booted, fd 1/2 are redirected to the
        // logging pipes, and any println on this thread risks feeding back
        // into the logging thread. Keep main-thread writes minimal.
        val rc = interpreter.enableRemoteDebug(
            host = "127.0.0.1",
            port = port,
            token = token,
            sessionName = "jvm-remote-debug-example",
        )
        rubyLog.println("[example] enableRemoteDebug returned: $rc")
        if (rc != 0) {
            rubyLog.println("[example] FAILED — see VM error in $logFilePath")
            return
        }

        Thread.sleep(3000)  // grace period so a tail -f on the log catches up

        val latch = CountDownLatch(1)
        interpreter.execute(
            scriptContent = """
                require 'debug'  # ensures binding.break is available even if
                                 # the listener didn't already require it

                puts "[script] computing some state..."
                items = (1..5).map { |i| i * i }
                target = "the answer"

                # Hit a breakpoint. Until a DAP client `continue`s this, the
                # Ruby thread stays paused and this example does not exit.
                binding.break

                puts "[script] resumed; items=#{items.inspect} target=#{target.inspect}"
            """.trimIndent(),
            latch = latch,
        ) { exitCode ->
            rubyLog.println("[example] script exit code: $exitCode")
        }

        rubyLog.println("[example] Waiting (max 5 min) for the script to complete...")
        val finished = latch.await(5, TimeUnit.MINUTES)
        if (!finished) {
            rubyLog.println("[example] Timed out — did you forget to attach and `continue`?")
        }
    }

    println("\nDone. See $logFilePath for the full transcript.")
}
