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

        // Resolve the build libs directory to an absolute path BEFORE bootstrap,
        // because assets_bootstrap() may change the working directory.
        val buildLibsDir = findBuildLibsDir()

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
                copyLibembeddedRuby(nativeLibsPath, buildLibsDir)
                copyLibembeddedRubyDeps(nativeLibsPath, buildLibsDir)

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
     * Find the build libs directory using an absolute path.
     * Must be called BEFORE assets_bootstrap() which may change the working directory.
     */
    private fun findBuildLibsDir(): String? {
        val possibleDirs = listOf(
            "./libs/linux_x64",
            "./kmp/libs/linux_x64",
            "../kmp/libs/linux_x64",
            "../../kmp/libs/linux_x64",
            "../../../kmp/libs/linux_x64"
        )
        for (dir in possibleDirs) {
            val soPath = "$dir/libembedded-ruby.so"
            if (access(soPath, F_OK) == 0) {
                // Resolve to absolute path using realpath
                val resolved = realpath(soPath, null)
                if (resolved != null) {
                    val absPath = resolved.toKString()
                    free(resolved)
                    // Return the directory (strip the filename)
                    return absPath.substringBeforeLast('/')
                }
            }
        }
        return null
    }

    /**
     * Copy libembedded-ruby.so from the build directory to the native libs directory.
     * This is needed for the dynamic loading approach used by RubyInterpreter.
     */
    private fun copyLibembeddedRuby(nativeLibsDir: String, buildLibsDir: String?) {
        val targetPath = "$nativeLibsDir/libembedded-ruby.so"

        // Skip if already exists
        if (access(targetPath, F_OK) == 0) {
            return
        }

        if (buildLibsDir != null) {
            val sourcePath = "$buildLibsDir/libembedded-ruby.so"
            if (tryCopyFile(sourcePath, targetPath)) {
                return
            }
        }

        println("Warning: Could not find libembedded-ruby.so to copy." +
            if (buildLibsDir != null) " Tried: $buildLibsDir/libembedded-ruby.so" else " No build libs directory found.")
    }

    /**
     * Copy libembedded-ruby.deps from the build directory to the native libs directory.
     * This file contains the list of library dependencies that need to be preloaded.
     */
    private fun copyLibembeddedRubyDeps(nativeLibsDir: String, buildLibsDir: String?) {
        val targetPath = "$nativeLibsDir/libembedded-ruby.deps"

        // Skip if already exists
        if (access(targetPath, F_OK) == 0) {
            return
        }

        if (buildLibsDir != null) {
            val sourcePath = "$buildLibsDir/libembedded-ruby.deps"
            if (tryCopyFile(sourcePath, targetPath, makeExecutable = false)) {
                return
            }
        }

        println("Warning: Could not find libembedded-ruby.deps to copy." +
            if (buildLibsDir != null) " Tried: $buildLibsDir/libembedded-ruby.deps" else " No build libs directory found.")
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
