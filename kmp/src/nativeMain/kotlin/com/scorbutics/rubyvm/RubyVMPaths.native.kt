package com.scorbutics.rubyvm

import com.scorbutics.rubyvm.native.*
import kotlinx.cinterop.*
import platform.posix.*

/**
 * Utility object to get paths for extracted Ruby VM runtime and native libraries.
 *
 * The Ruby VM automatically extracts its runtime files (Ruby standard library,
 * native libraries like libruby.so, etc.) to a cache directory on first use.
 *
 * Use this object to get the correct paths when creating a RubyInterpreter.
 *
 * Example:
 * ```kotlin
 * val paths = RubyVMPaths.getDefaultPaths()
 * RubyInterpreter.create(
 *     appPath = ".",
 *     rubyBaseDir = paths.rubyBaseDir,
 *     nativeLibsDir = paths.nativeLibsDir,
 *     listener = myListener
 * )
 * ```
 */
@OptIn(ExperimentalForeignApi::class)
object RubyVMPaths {
    /**
     * Container for Ruby VM installation paths.
     */
    data class Paths(
        /** Base installation directory (e.g., "/home/user/.cache/embedded-ruby-vm") */
        val installDir: String,

        /** Ruby base directory - pass this to RubyInterpreter.create() as rubyBaseDir */
        val rubyBaseDir: String,

        /** Native libraries directory - pass this to RubyInterpreter.create() as nativeLibsDir */
        val nativeLibsDir: String
    )

    /**
     * Get the default paths for the extracted Ruby VM runtime.
     *
     * This function ensures that the native libraries are loaded and extracted,
     * then returns the correct paths to pass to RubyInterpreter.create().
     *
     * @param installDir Optional custom installation directory. If not provided, uses default cache location.
     * @return Paths object containing installDir, rubyBaseDir, and nativeLibsDir
     * @throws RuntimeException if bootstrap fails
     */
    fun getDefaultPaths(installDir: String? = null): Paths {
        // Use provided install dir or get default
        val targetInstallDir = installDir ?: getDefaultInstallDir()

        // Bootstrap the Ruby runtime (extract assets if needed, load native libs)
        memScoped {
            val error = alloc<AssetsError>()
            assets_error_init(error.ptr)

            val layout = assets_bootstrap(targetInstallDir, error.ptr)

            if (layout == null) {
                val errorMsg = error.message.toKString()
                val context = error.context.toKString()
                throw RuntimeException(
                    "Failed to bootstrap Ruby VM: $errorMsg" +
                    if (context.isNotEmpty()) " (Context: $context)" else ""
                )
            }

            try {
                val rubyStdlibPath = layout.pointed.ruby_stdlib_path.toKString()
                val nativeLibsPath = layout.pointed.native_libs_dir.toKString()

                // Copy libembedded-ruby.so and its .deps file to the native libs directory
                // This is needed for dynamic loading approach
                copyLibembeddedRuby(nativeLibsPath)
                copyLibembeddedRubyDeps(nativeLibsPath)

                return Paths(
                    installDir = targetInstallDir,
                    rubyBaseDir = rubyStdlibPath,
                    nativeLibsDir = nativeLibsPath
                )
            } finally {
                // Free the layout structure
                assets_free_layout(layout)
            }
        }
    }

    /**
     * Copy libembedded-ruby.so from the build directory to the native libs directory.
     * This is needed for the dynamic loading approach used by RubyInterpreter.
     */
    private fun copyLibembeddedRuby(nativeLibsDir: String) {
        val targetPath = "$nativeLibsDir/libembedded-ruby.so"

        // Skip if already exists
        if (access(targetPath, F_OK) == 0) {
            return
        }

        // Try to find libembedded-ruby.so in common build locations
        val possibleSources = listOf(
            "./libs/linux_x64/libembedded-ruby.so",
            "./kmp/libs/linux_x64/libembedded-ruby.so",
            "../kmp/libs/linux_x64/libembedded-ruby.so",
            "../../kmp/libs/linux_x64/libembedded-ruby.so",
            "../../../kmp/libs/linux_x64/libembedded-ruby.so"
        )

        var copied = false
        for (sourcePath in possibleSources) {
            // Check if source file exists
            if (access(sourcePath, F_OK) != 0) {
                // File doesn't exist, try next
            } else {
                // Try to copy the file
                copied = tryCopyFile(sourcePath, targetPath)
                if (copied) {
                    break
                }
            }
        }

        if (!copied) {
            // If we get here, we couldn't find the source file
            println("Warning: Could not find libembedded-ruby.so to copy. Searched locations:")
            possibleSources.forEach { println("  - $it") }
        }
    }

    /**
     * Copy libembedded-ruby.deps from the build directory to the native libs directory.
     * This file contains the list of library dependencies that need to be preloaded.
     */
    private fun copyLibembeddedRubyDeps(nativeLibsDir: String) {
        val targetPath = "$nativeLibsDir/libembedded-ruby.deps"

        // Skip if already exists
        if (access(targetPath, F_OK) == 0) {
            return
        }

        // Try to find libembedded-ruby.deps in common build locations
        val possibleSources = listOf(
            "./libs/linux_x64/libembedded-ruby.deps",
            "./kmp/libs/linux_x64/libembedded-ruby.deps",
            "../kmp/libs/linux_x64/libembedded-ruby.deps",
            "../../kmp/libs/linux_x64/libembedded-ruby.deps",
            "../../../kmp/libs/linux_x64/libembedded-ruby.deps"
        )

        var copied = false
        for (sourcePath in possibleSources) {
            // Check if source file exists
            if (access(sourcePath, F_OK) != 0) {
                // File doesn't exist, try next
            } else {
                // Try to copy the file (no need to chmod for .deps file)
                copied = tryCopyFile(sourcePath, targetPath, makeExecutable = false)
                if (copied) {
                    break
                }
            }
        }

        if (!copied) {
            // If we get here, we couldn't find the source file
            println("Warning: Could not find libembedded-ruby.deps to copy. Searched locations:")
            possibleSources.forEach { println("  - $it") }
        }
    }

    /**
     * Try to copy a file from source to destination.
     * Returns true on success, false on failure.
     */
    private fun tryCopyFile(sourcePath: String, targetPath: String, makeExecutable: Boolean = true): Boolean {
        return memScoped {
            val src = fopen(sourcePath, "rb") ?: return@memScoped false

            val dst = fopen(targetPath, "wb")
            if (dst == null) {
                fclose(src)
                return@memScoped false
            }

            val buffer = allocArray<ByteVar>(8192)
            var success = true

            while (true) {
                val bytesRead = fread(buffer, 1u, 8192u, src)
                if (bytesRead == 0uL) {
                    break
                }
                val bytesWritten = fwrite(buffer, 1u, bytesRead, dst)
                if (bytesWritten != bytesRead) {
                    success = false
                    break
                }
            }

            fclose(dst)
            fclose(src)

            if (success && makeExecutable) {
                // Make executable (0755)
                chmod(targetPath, 0x1EDu)
            }

            success
        }
    }

    /**
     * Get default installation directory based on environment.
     *
     * Follows XDG Base Directory specification:
     * - Uses $EMBEDDED_RUBY_INSTALL_DIR if set (highest priority, useful for Android)
     * - Uses $XDG_CACHE_HOME/embedded-ruby-vm if set
     * - Falls back to $HOME/.cache/embedded-ruby-vm
     * - Falls back to /tmp/embedded-ruby-vm as last resort
     *
     * Note: The EMBEDDED_RUBY_INSTALL_DIR variable is particularly useful on Android
     * where it's set to the app's internal data directory by the Kotlin layer.
     */
    private fun getDefaultInstallDir(): String {
        // First check EMBEDDED_RUBY_INSTALL_DIR (set by platform-specific code, e.g., Android)
        getenv("EMBEDDED_RUBY_INSTALL_DIR")?.toKString()?.let { installDir ->
            if (installDir.isNotEmpty()) {
                return installDir
            }
        }

        // Try XDG_CACHE_HOME
        getenv("XDG_CACHE_HOME")?.toKString()?.let { cacheHome ->
            if (cacheHome.isNotEmpty()) {
                return "$cacheHome/embedded-ruby-vm"
            }
        }

        // Try HOME/.cache
        getenv("HOME")?.toKString()?.let { home ->
            if (home.isNotEmpty()) {
                return "$home/.cache/embedded-ruby-vm"
            }
        }

        // Fall back to /tmp
        return "/tmp/embedded-ruby-vm"
    }
}
