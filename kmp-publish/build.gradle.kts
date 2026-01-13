plugins {
    kotlin("multiplatform") version "1.9.22"
    id("com.android.library") version "8.2.2"
    `maven-publish`
}

group = "com.scorbutics.rubyvm"
version = "1.0.0-SNAPSHOT"

val nativeLibraryName: String = findProperty("nativeLibraryName")?.toString() ?: "rgss_runtime"
println("Native library name: $nativeLibraryName")

kotlin {
    androidTarget {
        compilations.all {
            kotlinOptions {
                jvmTarget = "1.8"
            }
        }

        publishLibraryVariants("release")
    }

    sourceSets {
        val commonMain by getting {
            // Reference shared Kotlin code from kmp project (no duplication)
            kotlin.srcDirs("../kmp/src/commonMain/kotlin")

            dependencies {
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.7.3")
            }
        }

        val androidMain by getting {
            // Reference Android-specific Kotlin code from kmp project (no duplication)
            // Android uses JVM implementation (JNI-based), so we include both jvmMain and androidMain
            kotlin.srcDirs(
                "../kmp/src/jvmMain/kotlin",
                "../kmp/src/androidMain/kotlin"
            )

            dependencies {
                implementation("androidx.core:core-ktx:1.12.0")
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
            }
        }
    }
}

android {
    namespace = "com.scorbutics.rubyvm"
    compileSdk = 34

    defaultConfig {
        minSdk = 26  // Must match Android API level used to build Ruby (see ruby-for-android toolchain params)
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

    // Use external CMake build to convert static library to shared library
    externalNativeBuild {
        cmake {
            path = file("wrapper/CMakeLists.txt")
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
}

// Publishing configuration
publishing {
    publications {
        withType<MavenPublication> {
            groupId = "com.scorbutics.rubyvm"
            // Note: KMP plugin will append "-android" to the project name for Android variants
            // So the final artifact ID will be "kmp-publish-android"
            version = "1.0.0-SNAPSHOT"

            pom {
                name.set("Embedded Ruby VM for Android")
                description.set("Kotlin Multiplatform wrapper for embedded Ruby VM on Android")
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
}

// CMake will automatically handle the library conversion via externalNativeBuild
