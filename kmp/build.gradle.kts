import org.jetbrains.kotlin.gradle.plugin.mpp.apple.XCFramework

// Detect if Android SDK is available
val isAndroidAvailable = System.getenv("ANDROID_HOME") != null ||
                         System.getenv("ANDROID_SDK_ROOT") != null ||
                         findProperty("android.skip")?.toString()?.toBoolean() == false

plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.android.library) apply false
    `maven-publish`
}

if (isAndroidAvailable) {
    apply(plugin = libs.plugins.android.library.get().pluginId)
}

group = "com.scorbutics.rubyvm"
version = "1.0.0-SNAPSHOT"

// iOS/macOS targets require Xcode and iOS SDKs, only available on macOS hosts.
// Enabling them on unsupported hosts (e.g. Linux ARM64) would cause Gradle to fail
// trying to download missing kotlin-native-prebuilt packages.
val isMacOs = System.getProperty("os.name").startsWith("Mac")

// Configuration for the native library name
// This can be overridden via gradle.properties: nativeLibraryName=rgss_runtime
val nativeLibraryName: String = findProperty("nativeLibraryName")?.toString() ?: "embedded-ruby"
println("Native library name: $nativeLibraryName")

kotlin {
    // Android target (uses JNI) - only if Android SDK is available
    if (isAndroidAvailable) {
        androidTarget {
            compilations.all {
                kotlinOptions {
                    jvmTarget = "1.8"
                }
            }
        }
    }

    // JVM Desktop target (uses JNI)
    jvm("desktop") {
        compilations.all {
            kotlinOptions {
                jvmTarget = "11"
            }
        }
    }

    // iOS targets (uses cinterop) - requires macOS host with Xcode
    if (isMacOs) {
        val xcf = XCFramework()
        listOf(
            iosArm64(),
            iosSimulatorArm64()
        ).forEach { iosTarget ->
            iosTarget.binaries.framework {
                baseName = "RubyVM"
                xcf.add(this)
            }
        }
    }

    // macOS targets (uses cinterop) - DISABLED
    // listOf(
    //     macosX64(),
    //     macosArm64()
    // ).forEach { macosTarget ->
    //     macosTarget.binaries.framework {
    //         baseName = "RubyVM"
    //     }
    // }

    // Linux targets (uses cinterop)
    // Uses shared wrapper libraries (buildNativeLibsLinux forces this)
    linuxX64 {
        // No special configuration needed - shared libraries work out of the box
        // Individual examples configure their own linker options
    }

    // Source sets configuration
    sourceSets {
        // Common source set (platform-agnostic API)
        val commonMain by getting {
            dependencies {
                implementation(libs.kotlinx.coroutines.core)
            }
        }

        val commonTest by getting {
            dependencies {
                implementation(kotlin("test"))
            }
        }

        // JVM source set (shared between Android and Desktop)
        // Both platforms use JNI-based implementation
        val jvmMain by creating {
            dependsOn(commonMain)
        }

        // Android implementation (JNI-based) - only if Android SDK is available
        if (isAndroidAvailable) {
            val androidMain by getting {
                dependsOn(jvmMain)
                dependencies {
                    implementation(libs.kotlinx.coroutines.android)
                }
            }
        }

        // JVM Desktop implementation (JNI-based)
        val desktopMain by getting {
            dependsOn(jvmMain)
            resources.srcDir(layout.buildDirectory.dir("generated/natives"))
            dependencies {
                // Same as Android, uses JNI
            }
        }

        // Native implementations (cinterop-based)
        val nativeMain by creating {
            dependsOn(commonMain)
        }

        if (isMacOs) {
            val iosMain by creating {
                dependsOn(nativeMain)
            }
            val iosArm64Main by getting {
                dependsOn(iosMain)
            }
            val iosSimulatorArm64Main by getting {
                dependsOn(iosMain)
            }
        }

        // val macosX64Main by getting {
        //     dependsOn(nativeMain)
        // }

        // val macosArm64Main by getting {
        //     dependsOn(nativeMain)
        // }

        val linuxX64Main by getting {
            dependsOn(nativeMain)
        }

        // val linuxArm64Main by getting {
        //     dependsOn(nativeMain)
        // }
    }

    // Configure cinterop for native targets
    targets.withType<org.jetbrains.kotlin.gradle.plugin.mpp.KotlinNativeTarget> {
        compilations.getByName("main") {
            cinterops {
                val rubyVM by creating {
                    defFile(project.file("src/nativeInterop/cinterop/ruby_vm.def"))
                    packageName("com.scorbutics.rubyvm.native")

                    // Include directories for headers (using absolute paths)
                    // Note: Use rootProject.file() since this is a subproject
                    val coreRubyVmDir = project.rootProject.file("core/ruby-vm").absoluteFile
                    val assetsDir = project.rootProject.file("assets").absoluteFile

                    // Get the CMake build directory where ruby-api-loader.h is generated
                    // Map konanTarget.name to CMake directory naming convention
                    // konanTarget.name: "linux_x64" -> CMake dir: "linux-x86_64"
                    // konanTarget.name: "macos_arm64" -> CMake dir: "macos-arm64"
                    val konanTargetName = this@withType.konanTarget.name
                    val (platform, arch) = when {
                        konanTargetName.startsWith("linux_x64") -> "linux" to "x86_64"
                        konanTargetName.startsWith("linux_arm64") -> "linux" to "arm64"
                        konanTargetName.startsWith("macos_x64") -> "macos" to "x86_64"
                        konanTargetName.startsWith("macos_arm64") -> "macos" to "arm64"
                        konanTargetName == "ios_arm64" -> "ios" to "arm64"
                        konanTargetName == "ios_simulator_arm64" -> "ios-simulator" to "arm64"
                        konanTargetName == "ios_x64" -> "ios-simulator" to "x86_64"
                        else -> throw GradleException("Unsupported Konan target: $konanTargetName")
                    }
                    val cmakeBuildDir = file("${layout.buildDirectory.get()}/cmake/$platform-$arch/core/ruby-vm").absoluteFile
                    val libsOutputDir = file("libs/$konanTargetName")

                    // Add all directories to ALL include dir categories
                    includeDirs.apply {
                        allHeaders(coreRubyVmDir, assetsDir, cmakeBuildDir)
                        headerFilterOnly(coreRubyVmDir, assetsDir, cmakeBuildDir)
                    }

                    // Also pass as compiler options
                    compilerOpts(
                        "-I${coreRubyVmDir.absolutePath}",
                        "-I${assetsDir.absolutePath}",
                        "-I${cmakeBuildDir.absolutePath}"
                    )

                    // Static library linking is handled at the final link stage via binaries configuration
                    // The cinterop only needs headers, not library linking
                    // Libraries will be linked when creating the final Kotlin/Native binary
                }
            }
        }
    }
}

// Android configuration - only if Android SDK is available
if (isAndroidAvailable) {
    configure<com.android.build.gradle.LibraryExtension> {
        namespace = "com.scorbutics.rubyvm"
        compileSdk = 34

        defaultConfig {
            minSdk = 26  // Must match Android API level used to build Ruby (see ruby-for-android toolchain params)
        }

        compileOptions {
            sourceCompatibility = JavaVersion.VERSION_1_8
            targetCompatibility = JavaVersion.VERSION_1_8
        }

        sourceSets["main"].manifest.srcFile("src/androidMain/AndroidManifest.xml")
        sourceSets["main"].res.srcDirs("src/androidMain/res")

        // CMake configuration for Android-specific library wrapping
        // This is used only when building with external static libraries
        // Enable with: -PuseAndroidCMakeWrapper=true
        val useAndroidCMakeWrapper: Boolean = findProperty("useAndroidCMakeWrapper")?.toString()?.toBoolean() ?: false

        if (useAndroidCMakeWrapper && file("../kmp-publish/wrapper/CMakeLists.txt").exists()) {
            externalNativeBuild {
                cmake {
                    path = file("../kmp-publish/wrapper/CMakeLists.txt")
                    version = "3.22.1"
                }
            }

            defaultConfig {
                externalNativeBuild {
                    cmake {
                        val targetAbi = findProperty("targetArch")?.toString() ?: "x86_64"
                        arguments(
                            "-DTARGET_ABI=$targetAbi",
                            "-DANDROID_STL=c++_shared"
                        )
                        abiFilters(targetAbi)
                    }
                }
            }

            packaging {
                jniLibs {
                    useLegacyPackaging = false
                }
            }
        }
    }
}

// ============================================================================
// Native Library Packaging
// ============================================================================

/**
 * Package native libraries into the JAR for self-contained distribution.
 * This allows users to use the JAR without setting java.library.path.
 *
 * NOTE: buildNativeLibsDesktop ALWAYS builds shared libraries,
 * so this task always runs (regardless of -PbuildWrapperShared property).
 */
tasks.register("packageNativeLibraries") {
    description = "Copy native libraries into JAR resources (Desktop always uses shared)"
    group = "build"
    dependsOn("buildNativeLibsDesktop")

    // Desktop target ALWAYS builds shared wrapper libraries for JAR packaging
    // (buildNativeLibsDesktop forces BUILD_WRAPPER_SHARED=ON)
    run {
        // Define where native libraries are built (SHARED versions)
        val nativeLibsSource = mapOf(
            "linux-x64" to Triple(
                "libs/linux_x86_64/libassets.so",
                "libs/linux_x86_64/libembedded-ruby.so",
                "../external/lib/x86_64-linux-linux"
            ),
            "linux-arm64" to Triple(
                "libs/linux_arm64/libassets.so",
                "libs/linux_arm64/libembedded-ruby.so",
                "../external/lib/aarch64-linux-linux"
            ),
            "macos-x64" to Triple(
                "libs/macos_x64/libassets.dylib",
                "libs/macos_x64/libembedded-ruby.dylib",
                "../external/lib/x86_64-macos-darwin"
            ),
            "macos-arm64" to Triple(
                "libs/macos_arm64/libassets.dylib",
                "libs/macos_arm64/libembedded-ruby.dylib",
                "../external/lib/aarch64-macos-darwin"
            ),
            "windows-x64" to Triple(
                "libs/windows_x64/assets.dll",
                "libs/windows_x64/embedded-ruby.dll",
                "../external/lib/x86_64-windows-mingw"
            )
        )

        // Configure outputs for proper up-to-date checking
        outputs.dir(layout.buildDirectory.dir("generated/natives"))

        doLast {
        val outputDir = layout.buildDirectory.dir("generated/natives").get().asFile

        // Copy each platform's libraries to resources
        nativeLibsSource.forEach { (platform, paths) ->
            val (assetsLibPath, embeddedRubyLibPath, rubyLibDir) = paths
            val assetsFile = file(assetsLibPath)
            val embeddedRubyFile = file(embeddedRubyLibPath)
            val rubyDir = file(rubyLibDir)

            println("Packaging libraries for $platform:")
            println("  - Assets lib: ${assetsFile.absolutePath} - exists: ${assetsFile.exists()}")
            println("  - Main lib: ${embeddedRubyFile.absolutePath} - exists: ${embeddedRubyFile.exists()}")

            // Copy libassets.so (or .dylib, .dll)
            if (assetsFile.exists()) {
                copy {
                    from(assetsFile)
                    into("$outputDir/natives/$platform")
                }
                println("  ✓ Copied assets library")
            } else {
                println("  ⚠ Assets library not found - will be built during CMake")
            }

            // Copy libembedded-ruby.so (or .dylib, .dll)
            if (embeddedRubyFile.exists()) {
                copy {
                    from(embeddedRubyFile)
                    into("$outputDir/natives/$platform")
                }
                println("  ✓ Copied embedded-ruby library")
            } else {
                println("  ⚠ Embedded-ruby library not found - will be built during CMake")
            }

            // NOTE: Ruby runtime libraries (libruby.so, libssl.so, etc.) are now
            // embedded in libassets.so and extracted at runtime. We no longer
            // package them in the JAR to reduce binary bloat.
            // The C asset extraction code handles them.
        }
        }
    }
}

// Ensure resources are processed after native libs are packaged
tasks.named("desktopProcessResources") {
    dependsOn("packageNativeLibraries")
}

// ============================================================================
// CMake Integration Tasks
// ============================================================================

/**
 * Build native libraries using CMake for all required platforms.
 * These tasks run automatically before compilation.
 *
 * Architecture selection via Gradle properties:
 * - targetArch=x86_64|arm64|all (default: host architecture)
 * - buildType=Debug|Release (default: Release)
 *
 * Examples:
 *   ./gradlew build -PtargetArch=x86_64
 *   ./gradlew build -PtargetArch=arm64 -PbuildType=Debug
 *   ./gradlew build -PtargetArch=all
 */

// Get architecture from Gradle properties or detect host
val targetArch: String = findProperty("targetArch")?.toString() ?: run {
    val osArch = System.getProperty("os.arch")
    when {
        osArch.contains("aarch64") || osArch.contains("arm64") -> "arm64"
        osArch.contains("amd64") || osArch.contains("x86_64") -> "x86_64"
        else -> "x86_64" // default
    }
}

val buildType: String = findProperty("buildType")?.toString() ?: "Release"
val forceRebuild: Boolean = findProperty("forceRebuild")?.toString()?.toBoolean() ?: false
val enableASAN: Boolean = findProperty("enableASAN")?.toString()?.toBoolean() ?: false

// Two-flag system for build control:
// - buildWrapperShared: Controls output type of libassets and libembedded-ruby (.so vs .a)
// - buildSharedLibs: Controls which Ruby libraries to link against (libruby.so vs libruby-static.a)
val buildWrapperShared: Boolean = findProperty("buildWrapperShared")?.toString()?.toBoolean() ?: false
val buildSharedLibs: Boolean = findProperty("buildSharedLibs")?.toString()?.toBoolean() ?: false

println("CMake Build Configuration:")
println("  Target Architecture: $targetArch")
println("  Build Type: $buildType")
println("  Build Wrapper Shared: $buildWrapperShared (libassets/libembedded-ruby as .so)")
println("  Build Shared Libs: $buildSharedLibs (link against libruby.so)")
if (forceRebuild) {
    println("  Force Rebuild: ENABLED (will clean before building)")
}
if (enableASAN) {
    println("  AddressSanitizer: ENABLED (memory error detection)")
}

// Helper function to run CMake
fun Project.runCMake(
    targetPlatform: String,
    architecture: String,
    cmakeArgs: List<String>,
    outputDir: File
) {
    val cmakeBuildDir = file("${layout.buildDirectory.get()}/cmake/$targetPlatform-$architecture")
    cmakeBuildDir.mkdirs()

    println("Building native library for $targetPlatform-$architecture")

    // Start with build type, then add task-specific args (which may override defaults)
    val allCMakeArgs = mutableListOf("-DCMAKE_BUILD_TYPE=$buildType")

    // Smart merging: Gradle properties override task defaults only when explicitly set
    val hasWrapperSharedProperty = project.hasProperty("buildWrapperShared")
    val hasSharedLibsProperty = project.hasProperty("buildSharedLibs")

    // If properties are explicitly set, filter out conflicting task args
    val filteredCmakeArgs = if (hasWrapperSharedProperty || hasSharedLibsProperty) {
        cmakeArgs.filter { arg ->
            val shouldFilter = (hasWrapperSharedProperty && arg.startsWith("-DBUILD_WRAPPER_SHARED=")) ||
                              (hasSharedLibsProperty && arg.startsWith("-DBUILD_SHARED_LIBS="))
            !shouldFilter
        }
    } else {
        cmakeArgs
    }
    allCMakeArgs.addAll(filteredCmakeArgs)

    // Add wrapper/shared lib flags only if not already present in filtered args
    if (!allCMakeArgs.any { it.startsWith("-DBUILD_WRAPPER_SHARED=") }) {
        allCMakeArgs.add("-DBUILD_WRAPPER_SHARED=${if (buildWrapperShared) "ON" else "OFF"}")
    }
    if (!allCMakeArgs.any { it.startsWith("-DBUILD_SHARED_LIBS=") }) {
        allCMakeArgs.add("-DBUILD_SHARED_LIBS=${if (buildSharedLibs) "ON" else "OFF"}")
    }

    // Add AddressSanitizer flags if enabled
    if (enableASAN) {
        println("  → Enabling AddressSanitizer instrumentation")
        allCMakeArgs.addAll(listOf(
            "-DCMAKE_C_FLAGS=-fsanitize=address -fno-omit-frame-pointer -g -O1",
            "-DCMAKE_CXX_FLAGS=-fsanitize=address -fno-omit-frame-pointer -g -O1",
            "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address",
            "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address"
        ))
    }

    // Debug: Print all CMake args
    println("  CMake args: ${allCMakeArgs.joinToString(" ")}")

    // Only run configure if cache doesn't exist or CMakeLists.txt changed
    val cmakeCache = File(cmakeBuildDir, "CMakeCache.txt")
    val rootCMakeLists = file("../CMakeLists.txt")
    println("  Configuring CMake...")
    exec {
        workingDir = cmakeBuildDir
        commandLine("cmake", *allCMakeArgs.toTypedArray(), file("..").absolutePath)
    }


    // Always run build (CMake handles incremental builds)
    exec {
        workingDir = cmakeBuildDir
        val buildCommand = mutableListOf("cmake", "--build", ".")

        // Add --clean-first if force rebuild is requested
        if (forceRebuild) {
            buildCommand.add("--clean-first")
            println("  Force rebuilding (cleaning first)...")
        }

        buildCommand.add("--config")
        buildCommand.add(buildType)

        commandLine(*buildCommand.toTypedArray())
    }

    // Copy built libraries to output directory
    // Static wrapper builds (.a), shared wrapper builds (.so/.dylib/.dll), and deps files
    outputDir.mkdirs()

    // Determine if we're building shared or static wrappers from the actual CMake args
    val isSharedWrapper = allCMakeArgs.any { it == "-DBUILD_WRAPPER_SHARED=ON" }

    copy {
        from("$cmakeBuildDir/lib")
        into(outputDir)
        if (isSharedWrapper) {
            include("*.so", "*.dylib", "*.dll", "*.deps")
        } else {
            include("*.a", "*.deps")
        }
    }

    println("Native library built successfully: ${outputDir.absolutePath}")
}

// Task: Build for iOS (arm64 and simulator)
tasks.register("buildNativeLibsIOS") {
    description = "Build native Ruby VM library for iOS (respects -PtargetArch)"
    group = "build"

    doLast {
        val architectures = when (targetArch) {
            "arm64" -> listOf("arm64")
            "x86_64" -> listOf("x86_64") // Simulator only
            "all" -> listOf("arm64", "x86_64")
            else -> listOf(targetArch)
        }

        architectures.forEach { arch ->
            if (arch == "arm64") {
                // iOS arm64 (device)
                runCMake(
                    "ios",
                    "arm64",
                    listOf(
                        "-DCMAKE_SYSTEM_NAME=iOS",
                        "-DCMAKE_OSX_ARCHITECTURES=arm64",
                        "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
                        "-DBUILD_JNI=OFF"
                    ),
                    file("libs/ios_arm64")
                )

                // iOS Simulator arm64
                runCMake(
                    "ios-simulator",
                    "arm64",
                    listOf(
                        "-DCMAKE_SYSTEM_NAME=iOS",
                        "-DCMAKE_OSX_ARCHITECTURES=arm64",
                        "-DCMAKE_OSX_SYSROOT=iphonesimulator",
                        "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
                        "-DBUILD_JNI=OFF"
                    ),
                    file("libs/ios_simulator_arm64")
                )
            }

            if (arch == "x86_64") {
                // iOS Simulator x64
                runCMake(
                    "ios-simulator",
                    "x86_64",
                    listOf(
                        "-DCMAKE_SYSTEM_NAME=iOS",
                        "-DCMAKE_OSX_ARCHITECTURES=x86_64",
                        "-DCMAKE_OSX_SYSROOT=iphonesimulator",
                        "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
                        "-DBUILD_JNI=OFF"
                    ),
                    file("libs/ios_simulator_x64")
                )
            }
        }
    }
}

// Task: Build for macOS
tasks.register("buildNativeLibsMacOS") {
    description = "Build native Ruby VM library for macOS (respects -PtargetArch)"
    group = "build"

    doLast {
        val architectures = when (targetArch) {
            "arm64" -> listOf("arm64")
            "x86_64" -> listOf("x86_64")
            "all" -> listOf("arm64", "x86_64")
            else -> listOf(targetArch)
        }

        architectures.forEach { arch ->
            val (cmakeArch, deploymentTarget, outputName) = when (arch) {
                "arm64" -> Triple("arm64", "11.0", "macos_arm64")
                "x86_64" -> Triple("x86_64", "10.15", "macos_x64")
                else -> throw GradleException("Unsupported macOS architecture: $arch")
            }

            runCMake(
                "macos",
                arch,
                listOf(
                    "-DCMAKE_OSX_ARCHITECTURES=$cmakeArch",
                    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$deploymentTarget",
                    "-DBUILD_JNI=OFF"
                ),
                file("libs/$outputName")
            )
        }
    }
}

// Task: Build for Linux
// Uses SHARED wrapper with STATIC Ruby for Kotlin/Native
// This avoids complex static linking issues while keeping Ruby statically linked
tasks.register("buildNativeLibsLinux") {
    description = "Build native Ruby VM library for Linux (respects -PtargetArch)"
    group = "build"

    doLast {
        val architectures = when (targetArch) {
            "arm64" -> listOf("arm64")
            "x86_64" -> listOf("x86_64")
            "all" -> listOf("arm64", "x86_64")
            else -> listOf(targetArch)
        }

        architectures.forEach { arch ->
            val outputName = when (arch) {
                "arm64" -> "linux_arm64"
                "x86_64" -> "linux_x64"
                else -> throw GradleException("Unsupported Linux architecture: $arch")
            }

            runCMake(
                "linux",
                arch,
                listOf(
                    "-DBUILD_JNI=OFF",
                    // Use shared wrapper to avoid Kotlin/Native static linking complexity
                    // Ruby itself remains statically linked for portability
                    "-DBUILD_WRAPPER_SHARED=ON",
                    "-DBUILD_SHARED_LIBS=OFF"
                ),
                file("libs/$outputName")
            )
        }
    }
}

// Task: Build for JVM Desktop (Linux, macOS, Windows)
// ALWAYS uses shared wrapper libraries for JAR packaging
tasks.register("buildNativeLibsDesktop") {
    description = "Build native Ruby VM library for JVM Desktop with JNI (respects -PtargetArch)"
    group = "build"

    doLast {
        val os = System.getProperty("os.name").lowercase()
        val architectures = when (targetArch) {
            "arm64" -> listOf("arm64")
            "x86_64" -> listOf("x86_64")
            "all" -> listOf("arm64", "x86_64")
            else -> listOf(targetArch)
        }

        architectures.forEach { arch ->
            val (platformName, libExtension) = when {
                os.contains("linux") -> "linux" to "so"
                os.contains("mac") -> "macos" to "dylib"
                os.contains("win") -> "windows" to "dll"
                else -> throw GradleException("Unsupported OS: $os")
            }

            val outputName = "${platformName}_${arch}"
            val cmakeArgs = mutableListOf(
                "-DBUILD_JNI=ON",
                // Force shared wrapper for Desktop JVM (overrides project properties)
                "-DBUILD_WRAPPER_SHARED=ON",
                "-DBUILD_SHARED_LIBS=OFF"
            )

            // Add platform-specific CMake arguments
            when {
                os.contains("mac") -> {
                    val (cmakeArch, deploymentTarget) = when (arch) {
                        "arm64" -> "arm64" to "11.0"
                        "x86_64" -> "x86_64" to "10.15"
                        else -> throw GradleException("Unsupported macOS architecture: $arch")
                    }
                    cmakeArgs.add("-DCMAKE_OSX_ARCHITECTURES=$cmakeArch")
                    cmakeArgs.add("-DCMAKE_OSX_DEPLOYMENT_TARGET=$deploymentTarget")
                }
                os.contains("win") -> {
                    if (arch == "x86_64") {
                        cmakeArgs.add("-A")
                        cmakeArgs.add("x64")
                    }
                }
            }

            runCMake(
                "desktop-$platformName",
                arch,
                cmakeArgs,
                file("libs/$outputName")
            )
        }
    }
}

// Task: Build for Android
tasks.register("buildNativeLibsAndroid") {
    description = "Build native Ruby VM library for Android (respects -PtargetArch)"
    group = "build"

    doLast {
        val ndkHome = System.getenv("ANDROID_NDK_HOME")
            ?: throw GradleException("ANDROID_NDK_HOME environment variable not set")

        val architectures = when (targetArch) {
            "arm64" -> listOf("arm64-v8a")
            "x86_64" -> listOf("x86_64")
            "all" -> listOf("arm64-v8a", "x86_64", "armeabi-v7a", "x86")
            else -> {
                // Map common arch names to Android ABIs
                when (targetArch) {
                    "armv7" -> listOf("armeabi-v7a")
                    "x86" -> listOf("x86")
                    else -> listOf(targetArch)
                }
            }
        }

        architectures.forEach { abi ->
            runCMake(
                "android",
                abi,
                listOf(
                    "-DCMAKE_SYSTEM_NAME=Android",
                    "-DCMAKE_ANDROID_ARCH_ABI=$abi",
                    "-DCMAKE_ANDROID_NDK=$ndkHome",
                    "-DCMAKE_SYSTEM_VERSION=24",
                    "-DBUILD_JNI=ON",
                    "-DBUILD_JNI_ANDROID_LOG=ON"
                ),
                file("libs/android/$abi")
            )
        }
    }
}

// ============================================================================
// Automatic Build Integration
// ============================================================================

/**
 * Automatically build native libraries before compilation tasks.
 * This makes the build fully automated - just run ./gradlew build!
 */

// Hook into Android compilation
tasks.matching { it.name.startsWith("compile") && it.name.contains("Android") }.configureEach {
    dependsOn("buildNativeLibsAndroid")
}

// Hook into JVM Desktop compilation
tasks.matching { it.name.startsWith("compileKotlinDesktop") }.configureEach {
    dependsOn("buildNativeLibsDesktop")
}

// Hook into native cinterop tasks
tasks.matching { it.name.contains("cinterop", ignoreCase = true) }.configureEach {
    when {
        name.contains("Ios", ignoreCase = true) -> dependsOn("buildNativeLibsIOS")
        name.contains("Macos", ignoreCase = true) -> dependsOn("buildNativeLibsMacOS")
        name.contains("Linux", ignoreCase = true) && !name.contains("Desktop") -> dependsOn("buildNativeLibsLinux")
    }

    // Disable caching for cinterop tasks to ensure they pick up library changes
    // This prevents stale cached linking when C libraries are rebuilt
    outputs.cacheIf { false }
}

// Configure all Kotlin/Native linking tasks to track library changes (platform-agnostic)
tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinNativeLink>().configureEach {
    // Get the Kotlin/Native target name from the task's binary
    val konanTargetName = binary.target.konanTarget.name

    // Map konanTarget names to library directory paths
    // konanTarget.name examples: "linux_x64", "ios_arm64", "macos_x64", "macos_arm64"
    val libDir = file("libs/$konanTargetName")

    // Only track library files if the directory exists (platform might not be built yet)
    if (libDir.exists()) {
        // Make the linking task depend on the actual library files
        // This ensures re-linking when C libraries are rebuilt
        inputs.files(fileTree(libDir) {
            include("*.a", "*.so", "*.dylib", "*.dll")
        })

        logger.info("Linking task $name will track library changes in: $libDir")
    }

    // Disable caching to ensure fresh linking when libraries change
    outputs.cacheIf { false }
}

// Convenience task to build all native libraries
tasks.register("buildAllNativeLibs") {
    description = "Build native libraries for all platforms (respects -PtargetArch)"
    group = "build"

    dependsOn(
        "buildNativeLibsIOS",
        "buildNativeLibsMacOS",
        "buildNativeLibsLinux",
        "buildNativeLibsAndroid",
        "buildNativeLibsDesktop"
    )
}

// ============================================================================
// Maven Publishing Configuration
// ============================================================================
apply(from = "publishing.gradle.kts")
