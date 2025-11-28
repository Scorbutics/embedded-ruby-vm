package com.scorbutics.rubyvm

/**
 * JVM Desktop implementation of RubyInterpreter using JNI.
 *
 * This is essentially the same as Android implementation,
 * but with different native library loading strategy.
 */
actual class RubyInterpreter private constructor(
    private val interpreterPtr: Long,
    private val listener: LogListener
) {
    private var isDestroyed = false

    actual fun enqueue(script: RubyScript, onComplete: (exitCode: Int) -> Unit) {
        check(!isDestroyed) { "Interpreter has been destroyed" }

        val callback = object : CompletionCallback {
            override fun complete(exitCode: Int) {
                onComplete(exitCode)
            }
        }

        RubyVMNative.enqueueScript(interpreterPtr, script.scriptPtr, callback)
    }

    actual fun destroy() {
        if (!isDestroyed) {
            RubyVMNative.destroyInterpreter(interpreterPtr)
            isDestroyed = true
        }
    }

    actual companion object {
        init {
            try {
                loadNativeLibrary()
            } catch (e: Exception) {
                // Fallback to standard loading if extraction fails
                try {
                    System.loadLibrary("embedded-ruby")
                } catch (e2: UnsatisfiedLinkError) {
                    throw RuntimeException("Failed to load native library. \n" +
                            "Extraction error: ${e.message}\n" +
                            "Standard load error: ${e2.message}", e)
                }
            }
        }

        private fun loadNativeLibrary() {
            val os = System.getProperty("os.name").lowercase()
            val arch = System.getProperty("os.arch").lowercase()

            val platform = when {
                os.contains("linux") -> "linux"
                os.contains("mac") -> "macos"
                os.contains("win") -> "windows"
                else -> throw UnsupportedOperationException("Unsupported OS: $os")
            }

            val architecture = when {
                arch.contains("amd64") || arch.contains("x86_64") -> "x64"
                arch.contains("aarch64") || arch.contains("arm64") -> "arm64"
                else -> throw UnsupportedOperationException("Unsupported architecture: $arch")
            }

            val libName = when {
                os.contains("win") -> "embedded-ruby.dll"
                os.contains("mac") -> "libembedded-ruby.dylib"
                else -> "libembedded-ruby.so"
            }

            val resourcePath = "/natives/$platform-$architecture/$libName"
            val resourceStream = RubyInterpreter::class.java.getResourceAsStream(resourcePath)
                ?: throw java.io.FileNotFoundException("Native library not found in resources: $resourcePath")

            val tempDir = java.io.File(System.getProperty("java.io.tmpdir"), "rubyvm_natives")
            if (!tempDir.exists()) tempDir.mkdirs()

            val tempFile = java.io.File(tempDir, "rubyvm_${System.currentTimeMillis()}_$libName")
            tempFile.deleteOnExit()

            java.io.FileOutputStream(tempFile).use { out ->
                resourceStream.copyTo(out)
            }

            System.load(tempFile.absolutePath)
        }

        actual fun create(
            appPath: String,
            rubyBaseDir: String,
            nativeLibsDir: String,
            listener: LogListener
        ): RubyInterpreter {
            val jniListener = object : JNILogListener {
                override fun accept(message: String) {
                    listener.onLog(message)
                }

                override fun onLogError(message: String) {
                    listener.onError(message)
                }
            }

            val interpreterPtr = RubyVMNative.createInterpreter(
                appPath,
                rubyBaseDir,
                nativeLibsDir,
                jniListener
            )

            require(interpreterPtr != 0L) { "Failed to create Ruby interpreter" }

            return RubyInterpreter(interpreterPtr, listener)
        }
    }
}
