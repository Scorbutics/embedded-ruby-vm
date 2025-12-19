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

                // Check if building with shared libraries (via -PbuildWrapperShared=true)
                val buildWrapperShared = project.findProperty("buildWrapperShared")?.toString()?.toBoolean() ?: false

                if (buildWrapperShared) {
                    // Dynamic linking - link against shared wrapper libraries
                    linkerOpts(
                        "-L${libDir.absolutePath}",
                        "-lembedded-ruby",
                        "-lassets",
                        "-Wl,-rpath,${libDir.absolutePath}"
                    )
                } else {
                    // Static linking - link all dependencies
                    val rubyLibDir = project.file("${projectRoot}/external/lib/x86_64-linux-gnu/static").absoluteFile
                    linkerOpts(
                        "-L${libDir.absolutePath}",
                        "-L${rubyLibDir.absolutePath}",
                        // Start with a linker group to handle circular dependencies
                        "-Wl,--start-group",
                        // Application libraries
                        "-lruby-vm",
                        "-llogging",
                        "-lassets",
                        "-lminizip",
                        // Ruby libraries
                        "-lruby-static",
                        "-lruby-ext",
                        // System libraries (within the group for circular dependency resolution)
                        // Note: libhistory is not needed as libreadline.a contains history functions
                        "-lreadline", "-lncurses", "-lncurses++", "-lpanel", "-lmenu", "-lform",
                        "-lgdbm", "-lgdbm_compat",
                        "-lssl", "-lcrypto",
                        "-lgmp",
                        "-lz",
                        "-Wl,--end-group",
                        // Final system libraries that have no dependencies
                        "-lm", "-lpthread", "-ldl", "-lcrypt", "-lrt"
                    )
                }
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
