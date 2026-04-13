import org.jetbrains.kotlin.gradle.plugin.mpp.apple.XCFramework

plugins {
    kotlin("multiplatform") version "1.9.22"
    id("com.android.library") version "8.2.2" apply false
    `maven-publish`
}

group = "com.scorbutics.rubyvm"
version = findProperty("version")?.toString()?.takeIf { it != "unspecified" }
    ?: (file("../NEXT_VERSION").readText().trim() + "-SNAPSHOT")

val nativeLibraryName: String = findProperty("nativeLibraryName")?.toString() ?: "rgss_runtime"
println("Native library name: $nativeLibraryName")
println("Publishing version: $version")

// Detect available platforms based on what native libs have been pre-staged
val stagedJniLibs = file("src/main/jniLibs")
val stagedDesktopLibs = file("src/main/desktopLibs")
val stagedIosDevice = file("src/main/iosLibs/ios_arm64")
val stagedIosSimulator = file("src/main/iosLibs/ios_simulator_arm64")
val stagedLinuxNativeLibs = file("src/main/linuxNativeLibs/linux_x64")

val hasAndroid = stagedJniLibs.exists() && stagedJniLibs.listFiles()?.isNotEmpty() == true
val hasDesktop = stagedDesktopLibs.exists() && stagedDesktopLibs.listFiles()?.isNotEmpty() == true
val hasIos = stagedIosDevice.exists() && stagedIosDevice.listFiles()?.isNotEmpty() == true
val hasLinuxNative = stagedLinuxNativeLibs.exists() && stagedLinuxNativeLibs.listFiles()?.isNotEmpty() == true
val hasJvm = hasAndroid || hasDesktop
val hasNative = hasIos || hasLinuxNative

println("Staged platforms: android=$hasAndroid, desktop=$hasDesktop, ios=$hasIos, linuxNative=$hasLinuxNative")

if (!hasAndroid && !hasDesktop && !hasIos && !hasLinuxNative) {
    throw GradleException("No staged platform libraries found. Stage native libs before publishing.")
}

// Only apply the Android plugin when we actually have Android libs to publish
if (hasAndroid) {
    apply(plugin = "com.android.library")
}

kotlin {
    if (hasAndroid) {
        // Android target (uses JNI — .so files pre-staged in jniLibs/)
        androidTarget {
            compilations.all {
                kotlinOptions {
                    jvmTarget = "1.8"
                }
            }
            publishLibraryVariants("release")
        }
    }

    if (hasDesktop) {
        // Desktop JVM target (uses JNI — .so files pre-staged in desktopLibs/)
        jvm("desktop") {
            compilations.all {
                kotlinOptions {
                    jvmTarget = "11"
                }
            }
        }
    }

    if (hasIos) {
        val isMacOs = System.getProperty("os.name").startsWith("Mac")
        val iosTargets = listOf(
            iosArm64(),
            iosSimulatorArm64()
        )
        iosTargets.forEach { iosTarget ->
            // iOS device (arm64) supports 13.0+, but the arm64 iOS Simulator
            // only exists since iOS 14.0 (introduced with Apple Silicon).
            // Setting 13.0 for simulator arm64 is invalid — Clang silently
            // bumps compiled objects to 14.0, but the linker still sees 13.0,
            // causing a version mismatch error.
            val minVersion = if (iosTarget.konanTarget.name == "ios_simulator_arm64") "14.0" else "13.0"
            iosTarget.compilations.all {
                compilerOptions.configure {
                    freeCompilerArgs.add("-Xoverride-konan-properties=osVersionMin.${iosTarget.konanTarget.name}=$minVersion")
                }
            }
        }
        if (isMacOs) {
            val xcf = XCFramework("RubyVM")
            iosTargets.forEach { iosTarget ->
                iosTarget.binaries.framework {
                    baseName = "RubyVM"
                    xcf.add(this)
                    // Link the pre-staged fat static library
                    val libDir = when (iosTarget.konanTarget.name) {
                        "ios_arm64" -> stagedIosDevice
                        "ios_simulator_arm64" -> stagedIosSimulator
                        else -> stagedIosDevice
                    }
                    linkerOpts(
                        "-force_load", "${libDir.absolutePath}/lib${nativeLibraryName}.a",
                        // Apple system frameworks required by OpenAL-Soft (CoreAudio backend)
                        // Note: AudioUnit APIs are part of AudioToolbox on iOS
                        "-framework", "AudioToolbox",
                        "-framework", "CoreAudio",
                        // SFML iOS rendering and input
                        "-framework", "OpenGLES",
                        "-framework", "QuartzCore",
                        "-framework", "CoreMotion",
                        "-framework", "UIKit",
                        "-framework", "Foundation",
                        "-framework", "CoreGraphics",
                        // General dependencies
                        "-framework", "CoreFoundation",
                        // Compression and encoding libraries
                        "-lcompression",
                        "-liconv",
                        "-lbz2",
                        "-lz"
                    )
                }
            }
        }
    }

    if (hasLinuxNative) {
        linuxX64()
    }

    sourceSets {
        // Common source set (platform-agnostic API)
        val commonMain by getting {
            // Reference shared Kotlin code from kmp project (no duplication)
            kotlin.srcDirs("../kmp/src/commonMain/kotlin")
            dependencies {
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.7.3")
            }
        }

        if (hasJvm) {
            // JVM source set (shared between Android and Desktop)
            val jvmMain by creating {
                dependsOn(commonMain)
                kotlin.srcDirs("../kmp/src/jvmMain/kotlin")
            }

            if (hasAndroid) {
                // Android implementation
                val androidMain by getting {
                    dependsOn(jvmMain)
                    kotlin.srcDirs("../kmp/src/androidMain/kotlin")
                    dependencies {
                        implementation("androidx.core:core-ktx:1.12.0")
                        implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
                    }
                }
            }

            if (hasDesktop) {
                // Desktop JVM implementation
                val desktopMain by getting {
                    dependsOn(jvmMain)
                    kotlin.srcDirs("../kmp/src/desktopMain/kotlin")
                    // Include the pre-staged .so files as resources
                    resources.srcDir("src/main/desktopLibs")
                }
            }
        }

        if (hasNative) {
            // Native implementations (cinterop-based: iOS, Linux)
            val nativeMain by creating {
                dependsOn(commonMain)
                kotlin.srcDirs("../kmp/src/nativeMain/kotlin")
            }

            if (hasIos) {
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

            if (hasLinuxNative) {
                val linuxX64Main by getting {
                    dependsOn(nativeMain)
                }
            }
        }
    }

    // Configure cinterop for all native targets (iOS, Linux)
    if (hasNative) {
        targets.withType<org.jetbrains.kotlin.gradle.plugin.mpp.KotlinNativeTarget> {
            compilations.getByName("main") {
                cinterops {
                    val rubyVM by creating {
                        defFile(project.file("../kmp/src/nativeInterop/cinterop/ruby_vm.def"))
                        packageName("com.scorbutics.rubyvm.native")

                        val privateHeadersDir = project.file("../include/private").absoluteFile
                        val publicHeadersDir = project.file("../include/public").absoluteFile

                        includeDirs.apply {
                            allHeaders(privateHeadersDir, publicHeadersDir)
                            headerFilterOnly(privateHeadersDir, publicHeadersDir)
                        }

                        compilerOpts(
                            "-I${privateHeadersDir.absolutePath}",
                            "-I${publicHeadersDir.absolutePath}"
                        )
                    }
                }
            }
        }
    }
}

if (hasAndroid) {
    configure<com.android.build.gradle.LibraryExtension> {
        namespace = "com.scorbutics.rubyvm"
        compileSdk = 34

        defaultConfig {
            minSdk = 26  // Must match Android API level used to build Ruby
        }

        buildToolsVersion = "34.0.0"

        compileOptions {
            sourceCompatibility = JavaVersion.VERSION_1_8
            targetCompatibility = JavaVersion.VERSION_1_8
        }

        // Don't compress native libraries in AAR
        packaging {
            jniLibs {
                useLegacyPackaging = false
            }
        }

        // Point to the AndroidManifest in androidMain
        sourceSets["main"].manifest.srcFile("src/androidMain/AndroidManifest.xml")
    }
}

// Publishing configuration
publishing {
    publications {
        withType<MavenPublication> {
            groupId = "com.scorbutics.rubyvm"
            version = project.version.toString()

            pom {
                name.set("LiteRGSS Runtime")
                description.set("Kotlin Multiplatform wrapper for LiteRGSS runtime (Ruby VM + game engine)")
                url.set("https://github.com/Scorbutics/litergss-everywhere")

                licenses {
                    license {
                        name.set("MIT License")
                        url.set("https://opensource.org/licenses/MIT")
                    }
                }
            }
        }
    }

    repositories {
        maven {
            name = "GitHubPackages"
            url = uri("https://maven.pkg.github.com/Scorbutics/litergss-everywhere")
            credentials {
                username = findProperty("gpr.user")?.toString() ?: System.getenv("GITHUB_ACTOR")
                password = findProperty("gpr.token")?.toString() ?: System.getenv("GITHUB_TOKEN")
            }
        }

        mavenLocal()
    }
}
