import org.jetbrains.kotlin.gradle.plugin.mpp.apple.XCFramework

plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.android.library)
}

group = "com.scorbutics.rubyvm"
version = "1.0.0-SNAPSHOT"

kotlin {
    // Android target (uses JNI)
    androidTarget {
        compilations.all {
            kotlinOptions {
                jvmTarget = "1.8"
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

    // NOTE: All Kotlin/Native targets (iOS, macOS, Linux) are disabled
    // because the host platform (Linux ARM64) is not supported by Kotlin/Native.
    // This prevents Gradle from trying to download kotlin-native-prebuilt-linux-aarch64.
    // To re-enable these targets, build on a supported platform (macOS, Linux x64, or Windows).

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

    // Linux targets (uses cinterop) - DISABLED
    // linuxX64()
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

        // Android implementation (JNI-based)
        val androidMain by getting {
            dependencies {
                implementation(libs.kotlinx.coroutines.android)
            }
        }

        // JVM Desktop implementation (JNI-based)
        val desktopMain by getting {
            dependencies {
                // Same as Android, uses JNI
            }
        }

        // Native implementations (cinterop-based) - DISABLED
        // val nativeMain by creating {
        //     dependsOn(commonMain)
        // }

        // val iosMain by getting {
        //     dependsOn(nativeMain)
        // }

        // val macosX64Main by getting {
        //     dependsOn(nativeMain)
        // }

        // val macosArm64Main by getting {
        //     dependsOn(nativeMain)
        // }

        // val linuxX64Main by getting {
        //     dependsOn(nativeMain)
        // }

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

                    // Include directories for C headers
                    includeDirs.headerFilterOnly(
                        project.file("../core/ruby-vm"),
                        project.file("../core/logging")
                    )

                    // Link against the compiled native library
                    extraOpts("-libraryPath", project.file("libs/${target.konanTarget.name}").absolutePath)
                }
            }
        }
    }
}

// Android configuration
android {
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

    // Define where native libraries are built
    val nativeLibsSource = mapOf(
        "linux-x64" to "../build/linux-x64/lib/libembedded-ruby.so",
        "linux-arm64" to "../build/jvm/lib/libembedded-ruby.so",
        "macos-x64" to "../build/macos-x64/lib/libembedded-ruby.dylib",
        "macos-arm64" to "../build/macos-arm64/lib/libembedded-ruby.dylib",
        "windows-x64" to "../build/windows-x64/lib/embedded-ruby.dll"
    )

    // Copy each platform's library to resources
    nativeLibsSource.forEach { (platform, sourcePath) ->
        val sourceFile = file(sourcePath)
        if (sourceFile.exists()) {
            from(sourceFile) {
                into("natives/$platform")
            }
        }
    }

    into("src/desktopMain/resources")
}

// Make JAR task depend on native library packaging
tasks.named("desktopProcessResources") {
    dependsOn("packageNativeLibraries")
}

// ============================================================================
// CMake Integration Tasks
// ============================================================================

/**
 * Build native libraries using CMake for all required platforms.
 * These tasks run before cinterop to ensure libraries are available.
 */

// Helper function to run CMake
fun Project.runCMake(
    targetPlatform: String,
    cmakeArgs: List<String>,
    outputDir: File
) {
    val buildDir = file("$buildDir/cmake/$targetPlatform")
    buildDir.mkdirs()

    exec {
        workingDir = buildDir
        commandLine("cmake", *cmakeArgs.toTypedArray(), file("..").absolutePath)
    }

    exec {
        workingDir = buildDir
        commandLine("cmake", "--build", ".")
    }

    // Copy built libraries to libs directory
    outputDir.mkdirs()
    copy {
        from("$buildDir/lib")
        into(outputDir)
        include("*.so", "*.a", "*.dylib")
    }
}

// Task: Build for iOS (arm64 and simulator)
tasks.register("buildNativeLibsIOS") {
    description = "Build native Ruby VM library for iOS"
    group = "build"

    doLast {
        // iOS arm64 (device)
        runCMake(
            "ios-arm64",
            listOf(
                "-DCMAKE_SYSTEM_NAME=iOS",
                "-DCMAKE_OSX_ARCHITECTURES=arm64",
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
                "-DBUILD_JNI=OFF",
                "-DBUILD_TESTS=OFF"
            ),
            file("libs/ios_arm64")
        )

        // iOS Simulator (x64 and arm64)
        runCMake(
            "ios-simulator-arm64",
            listOf(
                "-DCMAKE_SYSTEM_NAME=iOS",
                "-DCMAKE_OSX_ARCHITECTURES=arm64",
                "-DCMAKE_OSX_SYSROOT=iphonesimulator",
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0",
                "-DBUILD_JNI=OFF",
                "-DBUILD_TESTS=OFF"
            ),
            file("libs/ios_simulator_arm64")
        )
    }
}

// Task: Build for macOS
tasks.register("buildNativeLibsMacOS") {
    description = "Build native Ruby VM library for macOS"
    group = "build"

    doLast {
        // macOS arm64 (Apple Silicon)
        runCMake(
            "macos-arm64",
            listOf(
                "-DCMAKE_OSX_ARCHITECTURES=arm64",
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0",
                "-DBUILD_JNI=OFF",
                "-DBUILD_TESTS=OFF"
            ),
            file("libs/macos_arm64")
        )

        // macOS x64 (Intel)
        runCMake(
            "macos-x64",
            listOf(
                "-DCMAKE_OSX_ARCHITECTURES=x86_64",
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=10.15",
                "-DBUILD_JNI=OFF",
                "-DBUILD_TESTS=OFF"
            ),
            file("libs/macos_x64")
        )
    }
}

// Task: Build for Linux
tasks.register("buildNativeLibsLinux") {
    description = "Build native Ruby VM library for Linux (x64 and arm64)"
    group = "build"

    doLast {
        // Linux x64
        runCMake(
            "linux-x64",
            listOf(
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_JNI=OFF",
                "-DBUILD_TESTS=OFF"
            ),
            file("libs/linux_x64")
        )

        // Linux arm64 (aarch64)
        runCMake(
            "linux-arm64",
            listOf(
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_JNI=OFF",
                "-DBUILD_TESTS=OFF"
            ),
            file("libs/linux_arm64")
        )
    }
}

// Task: Build for current Linux architecture
tasks.register("buildNativeLibsLinuxNative") {
    description = "Build native Ruby VM library for current Linux architecture"
    group = "build"

    doLast {
        val arch = System.getProperty("os.arch")
        val (targetName, outputDir) = when {
            arch.contains("aarch64") || arch.contains("arm64") -> "linux-arm64" to "linux_arm64"
            arch.contains("amd64") || arch.contains("x86_64") -> "linux-x64" to "linux_x64"
            else -> throw GradleException("Unsupported architecture: $arch")
        }

        println("Building for current architecture: $arch -> $targetName")
        runCMake(
            targetName,
            listOf(
                "-DCMAKE_BUILD_TYPE=Release",
                "-DBUILD_JNI=OFF",
                "-DBUILD_TESTS=OFF"
            ),
            file("libs/$outputDir")
        )
    }
}

// Task: Build for Android (via existing build system)
tasks.register("buildNativeLibsAndroid") {
    description = "Build native Ruby VM library for Android"
    group = "build"

    doLast {
        // Android libraries should be built using NDK
        // This is typically done through Android Gradle Plugin
        runCMake(
            "android-arm64",
            listOf(
                "-DCMAKE_SYSTEM_NAME=Android",
                "-DCMAKE_ANDROID_ARCH_ABI=arm64-v8a",
                "-DCMAKE_ANDROID_NDK=${System.getenv("ANDROID_NDK_HOME")}",
                "-DCMAKE_SYSTEM_VERSION=24",
                "-DBUILD_JNI=ON",
                "-DBUILD_JNI_ANDROID_LOG=ON",
                "-DBUILD_TESTS=OFF"
            ),
            file("libs/android")
        )
    }
}

// Make cinterop depend on CMake builds
tasks.matching { it.name.contains("cinterop", ignoreCase = true) }.configureEach {
    when {
        name.contains("Ios", ignoreCase = true) -> dependsOn("buildNativeLibsIOS")
        name.contains("Macos", ignoreCase = true) -> dependsOn("buildNativeLibsMacOS")
        name.contains("Linux", ignoreCase = true) -> dependsOn("buildNativeLibsLinux")
    }
}

// Convenience task to build all native libraries
tasks.register("buildAllNativeLibs") {
    description = "Build native libraries for all platforms"
    group = "build"

    dependsOn(
        "buildNativeLibsIOS",
        "buildNativeLibsMacOS",
        "buildNativeLibsLinux",
        "buildNativeLibsAndroid"
    )
}
