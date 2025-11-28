import org.jetbrains.kotlin.gradle.plugin.mpp.apple.XCFramework

// Detect if Android SDK is available
val isAndroidAvailable = System.getenv("ANDROID_HOME") != null ||
                         System.getenv("ANDROID_SDK_ROOT") != null ||
                         findProperty("android.skip")?.toString()?.toBoolean() == false

plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.android.library) apply false
}

if (isAndroidAvailable) {
    apply(plugin = libs.plugins.android.library.get().pluginId)
}

group = "com.scorbutics.rubyvm"
version = "1.0.0-SNAPSHOT"

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

    // NOTE: Almost all Kotlin/Native targets (iOS, macOS, Linux) are disabled.
    // This prevents Gradle from trying to download kotlin-native-prebuilt-linux-aarch64.
    // If you want to enable these targets, build on a supported platform (macOS, Linux x64, or Windows).

    // iOS targets (uses cinterop) - DISABLED
    // val xcf = XCFramework()
    // listOf(
    //     iosX64(),
    //     iosArm64(),
    //     iosSimulatorArm64()
    // ).forEach { iosTarget ->
    //     iosTarget.binaries.framework {
    //         baseName = "RubyVM"
    //         xcf.add(this)
    //     }
    // }

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
    linuxX64 {
        binaries {
            executable {
                entryPoint = "main"
                baseName = "ruby-vm-test"

                freeCompilerArgs += listOf("-linker-option", "--allow-shlib-undefined")

                // Configure linker options with absolute paths
                val libDir = project.file("libs/linux_x64").absoluteFile
                val rubyLibDir = project.file("../core/external/lib/x86_64-linux-linux").absoluteFile

                // Link static libraries and Ruby
                linkerOpts(
                    "-L${libDir.absolutePath}",
                    "-L${rubyLibDir.absolutePath}",
                    // Our static libraries (order matters - dependents BEFORE dependencies!)
                    "${libDir.absolutePath}/libruby-vm.a",
                    "${libDir.absolutePath}/liblogging.a",
                    "${libDir.absolutePath}/libassets.a",
                    "${libDir.absolutePath}/libminizip.a",
                    // Ruby interpreter
                    "-lruby",
                    // System libraries (math and crypt for Ruby)
                    "-lm", "-lz", "-lpthread", "-ldl", "-lcrypt", "-lrt",
                    // Set RPATH so executable can find Ruby library in same directory
                    "-Wl,-rpath,\$ORIGIN",
                    "-Wl,-rpath,${rubyLibDir.absolutePath}"
                )
            }
        }
    }
    // linuxArm64() // Kotlin/Native doesn't support Linux ARM64

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

        // val iosMain by getting {
        //     dependsOn(nativeMain)
        // }

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

                    // Include directories for C headers (using absolute paths)
                    val coreRubyVm = project.file("../core/ruby-vm").absoluteFile
                    val coreLogging = project.file("../core/logging").absoluteFile

                    includeDirs.allHeaders(coreRubyVm, coreLogging)

                    // Also add compiler options with absolute paths
                    compilerOpts("-I${coreRubyVm.absolutePath}", "-I${coreLogging.absolutePath}")

                    // Link against the compiled native library (using absolute paths)
                    val libDir = project.file("libs/${target.konanTarget.name}").absoluteFile
                    extraOpts("-libraryPath", libDir.absolutePath)
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
            minSdk = 24
        }

        compileOptions {
            sourceCompatibility = JavaVersion.VERSION_1_8
            targetCompatibility = JavaVersion.VERSION_1_8
        }

        sourceSets["main"].manifest.srcFile("src/androidMain/AndroidManifest.xml")
        sourceSets["main"].res.srcDirs("src/androidMain/res")
    }
}

// ============================================================================
// Native Library Packaging
// ============================================================================

/**
 * Package native libraries into the JAR for self-contained distribution.
 * This allows users to use the JAR without setting java.library.path.
 */
tasks.register<Copy>("packageNativeLibraries") {
    description = "Copy native libraries into JAR resources"
    group = "build"
    dependsOn("buildNativeLibsDesktop")

    // Define where native libraries are built
    val nativeLibsSource = mapOf(
        "linux-x64" to "libs/linux_x86_64/libembedded-ruby.so",
        "linux-arm64" to "libs/linux_arm64/libembedded-ruby.so",
        "macos-x64" to "libs/macos_x64/libembedded-ruby.dylib",
        "macos-arm64" to "libs/macos_arm64/libembedded-ruby.dylib",
        "windows-x64" to "libs/windows_x64/embedded-ruby.dll"
    )

    // Copy each platform's library to resources
    nativeLibsSource.forEach { (platform, sourcePath) ->
        val sourceFile = file(sourcePath)
        // We don't check for existence here so it fails if the lib is missing (which is good)
        // But since we might be building on a specific host, we should only copy what exists
        // OR we rely on the fact that we only build for the current host usually?
        // The user wants the JAR to carry everything needed.
        // But we can only build for the current host (mostly).
        // Let's keep the existence check but log a warning?
        // Actually, for now, let's just copy what we have.
        if (sourceFile.exists()) {
            from(sourceFile) {
                into("natives/$platform")
            }
        }
    }

    into(layout.buildDirectory.dir("generated/natives"))
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

println("CMake Build Configuration:")
println("  Target Architecture: $targetArch")
println("  Build Type: $buildType")

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

    val allCMakeArgs = mutableListOf(
        "-DCMAKE_BUILD_TYPE=$buildType",
        "-DBUILD_TESTS=OFF"
    ) + cmakeArgs

    // Only run configure if cache doesn't exist or CMakeLists.txt changed
    val cmakeCache = File(cmakeBuildDir, "CMakeCache.txt")
    val rootCMakeLists = file("../CMakeLists.txt")
    val needsReconfigure = !cmakeCache.exists() ||
                          cmakeCache.lastModified() < rootCMakeLists.lastModified()

    if (needsReconfigure) {
        println("  Configuring CMake...")
        exec {
            workingDir = cmakeBuildDir
            commandLine("cmake", *allCMakeArgs.toTypedArray(), file("..").absolutePath)
        }
    } else {
        println("  CMake cache is up-to-date, skipping configure")
    }

    // Always run build (CMake handles incremental builds)
    exec {
        workingDir = cmakeBuildDir
        commandLine("cmake", "--build", ".", "--config", buildType)
    }

    // Copy built libraries to output directory
    outputDir.mkdirs()
    copy {
        from("$cmakeBuildDir/lib")
        into(outputDir)
        include("*.so", "*.a", "*.dylib", "*.dll")
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
                    "-DBUILD_JNI=OFF"
                ),
                file("libs/$outputName")
            )
        }
    }
}

// Task: Build for JVM Desktop (Linux, macOS, Windows)
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
            val cmakeArgs = mutableListOf("-DBUILD_JNI=ON")

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
