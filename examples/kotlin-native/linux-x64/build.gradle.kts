import org.jetbrains.kotlin.gradle.plugin.mpp.KotlinNativeTarget

plugins {
    kotlin("multiplatform") version "1.9.20"
}

group = "com.scorbutics.rubyvm.examples"
version = "1.0.0"

repositories {
    mavenCentral()
}

kotlin {
    // Linux x64 target
    linuxX64 {
        compilations.all {
            kotlinOptions {
                freeCompilerArgs += listOf("-opt-in=kotlin.ExperimentalStdlibApi")
            }
        }

        binaries {
            executable {
                entryPoint = "main"
                baseName = "ruby-vm-example"

                // Allow undefined symbols (will be resolved at runtime)
                freeCompilerArgs += listOf("-linker-option", "--allow-shlib-undefined")

                // Configure linker options with paths relative to project root
                val projectRoot = project.file("../../..").absoluteFile
                val libDir = project.file("${projectRoot}/kmp/libs/linux_x64").absoluteFile
                val rubyLibDir = project.file("${projectRoot}/core/external/lib/x86_64-linux-linux").absoluteFile

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
                    // Set RPATH so executable can find Ruby library
                    "-Wl,-rpath,\$ORIGIN",
                    "-Wl,-rpath,${rubyLibDir.absolutePath}"
                )
            }
        }

        compilations.getByName("main") {
            cinterops {
                val rubyVM by creating {
                    // Path to cinterop definition file
                    val projectRoot = project.file("../../..").absoluteFile
                    defFile(project.file("${projectRoot}/kmp/src/nativeInterop/cinterop/ruby_vm.def"))
                    packageName("com.scorbutics.rubyvm.native")

                    // Include directories for C headers
                    val coreRubyVm = project.file("${projectRoot}/core/ruby-vm").absoluteFile
                    val coreLogging = project.file("${projectRoot}/core/logging").absoluteFile

                    includeDirs.allHeaders(coreRubyVm, coreLogging)
                    compilerOpts("-I${coreRubyVm.absolutePath}", "-I${coreLogging.absolutePath}")

                    // Link against the compiled native library
                    val libDir = project.file("${projectRoot}/kmp/libs/linux_x64").absoluteFile
                    extraOpts("-libraryPath", libDir.absolutePath)
                }
            }
        }
    }

    sourceSets {
        // Get the auto-created commonMain and add KMP common sources
        val commonMain by getting {
            val projectRoot = project.file("../../..").absoluteFile
            kotlin.srcDir("${projectRoot}/kmp/src/commonMain/kotlin")
        }

        // Create nativeMain hierarchy source set for shared native code
        val nativeMain by creating {
            dependsOn(commonMain)
            val projectRoot = project.file("../../..").absoluteFile
            kotlin.srcDir("${projectRoot}/kmp/src/nativeMain/kotlin")
        }

        // linuxX64Main depends on nativeMain
        val linuxX64Main by getting {
            dependsOn(nativeMain)
            // Include example source code
            kotlin.srcDir("src")
            dependencies {
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.7.3")
            }
        }
    }
}

// Task to build native libraries first
tasks.register<Exec>("buildNativeLibs") {
    group = "build"
    description = "Build native Ruby VM libraries for Linux x64"

    val projectRoot = project.file("../../..").absoluteFile
    workingDir = projectRoot
    commandLine("./gradlew", ":ruby-vm-kmp:buildNativeLibsLinux", "-PtargetArch=x86_64")
}

// Make compilation depend on native libraries
tasks.matching { it.name.contains("compile") }.configureEach {
    dependsOn("buildNativeLibs")
}

// Custom task to run the example
tasks.register("runExample") {
    group = "application"
    description = "Build and run the Kotlin/Native Ruby VM example"

    dependsOn("linkDebugExecutableLinuxX64")

    doLast {
        val projectRoot = project.file("../../..").absoluteFile
        val executable = file("build/bin/linuxX64/debugExecutable/ruby-vm-example.kexe")

        if (!executable.exists()) {
            throw GradleException("Executable not found: ${executable.absolutePath}")
        }

        println("\n=== Running Kotlin/Native Ruby VM Example ===\n")

        // Run the executable from project root so paths work
        exec {
            workingDir = projectRoot
            commandLine(executable.absolutePath)
        }
    }
}
