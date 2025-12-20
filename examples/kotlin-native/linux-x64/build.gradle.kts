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
        binaries {
            executable {
                entryPoint = "main"
                baseName = "ruby-vm-example"

                // Link configuration depends on whether we're building with shared or static libraries
                val projectRoot = project.file("../../..").absoluteFile
                val libDir = project.file("${projectRoot}/kmp/libs/linux_x64").absoluteFile

                // Always Dynamic linking - link against shared wrapper libraries.
                // For platforms like iOS, it will be full static.
                // But we don't support it right now.
                linkerOpts(
                    "-Wl,--allow-shlib-undefined",
                    "-L${libDir.absolutePath}",
                    "-lembedded-ruby",
                    "-lassets",
                    "-Wl,-rpath,${libDir.absolutePath}"
                )
                
            }
        }
    }

    sourceSets {
        val linuxX64Main by getting {
            kotlin.srcDir("src")
            dependencies {
                // Depend on the ruby-vm-kmp module from the parent composite build
                implementation("com.scorbutics.rubyvm:ruby-vm-kmp:1.0.0-SNAPSHOT")
            }
        }
    }
}

// Custom task to run the example
tasks.register("runExample") {
    group = "application"
    description = "Build and run the Kotlin/Native Ruby VM example"

    dependsOn("linkDebugExecutableLinuxX64")

    doLast {
        val executable = file("build/bin/linuxX64/debugExecutable/ruby-vm-example.kexe")

        if (!executable.exists()) {
            throw GradleException("Executable not found: ${executable.absolutePath}")
        }

        println("\n=== Running Kotlin/Native Ruby VM Example ===\n")

        // Run the executable
        exec {
            commandLine(executable.absolutePath)
        }
    }
}
