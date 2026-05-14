package examples

import com.scorbutics.rubyvm.LogListener
import com.scorbutics.rubyvm.LogMessage
import com.scorbutics.rubyvm.LogSource
import com.scorbutics.rubyvm.RubyInterpreter
import com.scorbutics.rubyvm.execute
import java.io.FileOutputStream
import java.io.PrintStream
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Demonstrates the remote line-eval console (sibling of
 * [JvmRemoteDebugExample], simpler than DAP).
 *
 * What happens:
 *  1. enableRemoteEval(port, token) is called BEFORE the first script.
 *     This eagerly boots the VM with a TCP listener armed in
 *     remote_eval.rb.
 *  2. We enqueue a script that defines a `$shared` global and registers
 *     a Binding under name `:probe` so a remote-console user can
 *     `attach probe` to evaluate against that scope.
 *  3. The example then waits up to 5 minutes for you to poke around
 *     interactively from another terminal:
 *
 *        rlwrap nc 127.0.0.1 7777
 *
 *     (or plain `nc` if you don't have rlwrap). At the greeting line,
 *     paste the cookie shown at startup, then press Enter. You'll get
 *     a `rubyvm[TOPLEVEL]> ` prompt; type expressions like:
 *
 *        $shared
 *        $shared = 'hello from remote'
 *        scopes
 *        attach probe
 *        secret
 *        detach
 *        exit
 *
 * To run:
 *     ../../gradlew runExample -PexampleClass=JvmRemoteEvalExample
 *
 * Logging note: once the VM boots, the logging system dup2's fd 1 and
 * fd 2 onto its own pipes. Any println from main() — or worse from the
 * LogListener callback — would feed back into the logging thread and
 * deadlock on a pipe write. So we (a) print all banners BEFORE the VM
 * boots and (b) have the LogListener write to a real file rather than
 * System.out/System.err. Same pattern as JvmRemoteDebugExample.
 */
fun main() {
    val port = 7777
    val token = "demo-cookie-change-me"
    val logFilePath = "/tmp/jvm-remote-eval-example.log"

    println("=== Ruby VM Remote Eval Console Example ===\n")

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
    println(" About to arm the line-eval listener on 127.0.0.1:$port")
    println(" Cookie: $token")
    println()
    println(" Connect from another terminal:")
    println("     rlwrap nc 127.0.0.1 $port      (or: nc 127.0.0.1 $port)")
    println()
    println(" After the greeting line, paste the cookie above and press Enter.")
    println(" Type `help` at the rubyvm[...]> prompt for command list.")
    println("=================================================================")
    println()

    RubyInterpreter.create(
        appPath = ".",
        rubyBaseDir = paths.rubyBaseDir,
        nativeLibsDir = paths.nativeLibsDir,
        listener = listener
    ).use { interpreter ->
        // After this point the VM is booted and fd 1/2 are pipes — keep
        // any further main-thread console output minimal.
        val rc = interpreter.enableRemoteEval(
            host = "127.0.0.1",
            port = port,
            token = token,
            sessionName = "jvm-remote-eval-example",
        )
        rubyLog.println("[example] enableRemoteEval returned: $rc")
        if (rc != 0) {
            rubyLog.println("[example] FAILED — see VM error in $logFilePath")
            return
        }

        // Give the user a moment to start tailing the log before script
        // output starts flowing.
        Thread.sleep(2000)

        val latch = CountDownLatch(1)
        interpreter.execute(
            scriptContent = """
                # Set a global so the remote user can see and mutate it.
                ${'$'}shared = 'initial-value'
                puts "[script] ${'$'}shared = #{${'$'}shared.inspect}"

                # Register a local Binding under :probe so `attach probe`
                # works from the remote console.
                secret = 1337
                fruits = %w[apple banana cherry]
                RemoteEval.expose(:probe, binding)
                puts "[script] exposed :probe (locals: secret, fruits)"

                # Keep the script alive for 5 minutes so the binding stays
                # in scope (Binding objects keep their locals reachable).
                puts "[script] holding scope alive for 5 minutes…"
                sleep 300
            """.trimIndent(),
            latch = latch,
        ) { exitCode ->
            rubyLog.println("[example] script exit code: $exitCode")
        }

        rubyLog.println("[example] Waiting (max 5 min) for script to finish…")
        val finished = latch.await(5, TimeUnit.MINUTES)
        if (!finished) {
            rubyLog.println("[example] Timed out — the script's sleep(300) is still running")
        }
    }

    println("\nDone. See $logFilePath for the full transcript.")
}
