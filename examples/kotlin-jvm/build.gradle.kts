plugins {
    kotlin("jvm") version "1.9.22"
    application
}

group = "com.scorbutics.rubyvm.examples"
version = "1.0.0"

repositories {
    mavenCentral()
}

// Get the path to the project root
val projectRoot = project.file("../..").absoluteFile

java {
    sourceCompatibility = JavaVersion.VERSION_1_8
    targetCompatibility = JavaVersion.VERSION_1_8
}

tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile> {
    kotlinOptions {
        jvmTarget = "1.8"
    }
}

dependencies {
    // Depend on the KMP desktop JAR
    implementation(files("${projectRoot}/kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar"))

    // Kotlin standard library
    implementation("org.jetbrains.kotlin:kotlin-stdlib:1.9.22")
}

application {
    mainClass.set("examples.JvmExampleKt")

    // Set java.library.path to find native libraries
    applicationDefaultJvmArgs = listOf(
        "-Djava.library.path=${projectRoot}/lib"
    )
}

// Task to build the KMP desktop JAR first
tasks.register<Exec>("buildDesktopJar") {
    group = "build"
    description = "Build the Ruby VM KMP desktop JAR"

    val jarFile = file("${projectRoot}/kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar")

    // Only rebuild if JAR doesn't exist
    onlyIf {
        !jarFile.exists()
    }

    doFirst {
        if (!jarFile.exists()) {
            println("Desktop JAR not found. Building...")
        }
    }

    workingDir = projectRoot
    commandLine("./gradlew", ":ruby-vm-kmp:desktopJar", "-PbuildType=Debug")
}

// Make compilation depend on the desktop JAR being built
tasks.named("compileKotlin") {
    dependsOn("buildDesktopJar")

    // Ensure JAR exists before compiling
    doFirst {
        val jarFile = file("${projectRoot}/kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar")
        if (!jarFile.exists()) {
            throw GradleException(
                "Desktop JAR not found at: ${jarFile.absolutePath}\n" +
                "Please build it first using the Docker container:\n" +
                "  ./docker-dev.sh shell\n" +
                "  ./gradlew :ruby-vm-kmp:desktopJar\n" +
                "  exit\n" +
                "  docker cp embedded-ruby-vm-dev:/workspace/kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar kmp/build/libs/"
            )
        }
    }
}

// Custom task to run the example
tasks.register("runExample") {
    group = "application"
    description = "Build and run the Kotlin/JVM Ruby VM example"

    dependsOn("run")
}

// Custom task to run the example with AddressSanitizer (ASAN)
tasks.register<JavaExec>("runExampleWithASAN") {
    group = "application"
    description = "Build and run the Kotlin/JVM Ruby VM example with AddressSanitizer for debugging"

    // Note: Build the native library with ASAN first using:
    // ./gradlew :ruby-vm-kmp:desktopJar -PbuildType=Debug -PenableASAN=true -PforceRebuild=true

    mainClass.set("examples.JvmExampleKt")
    classpath = sourceSets.main.get().runtimeClasspath
    workingDir = projectRoot

    // Find libasan.so path
    val asanLibPath = providers.exec {
        commandLine("gcc", "-print-file-name=libasan.so")
    }.standardOutput.asText.get().trim()

    // ASAN configuration
    environment("LD_PRELOAD", asanLibPath)
    environment("ASAN_OPTIONS", "detect_leaks=0:abort_on_error=1:fast_unwind_on_malloc=0:symbolize=1")

    // Try to find symbolizer
    val symbolizerPath = providers.exec {
        commandLine("sh", "-c", "which llvm-symbolizer 2>/dev/null || which addr2line 2>/dev/null || echo ''")
        isIgnoreExitValue = true
    }.standardOutput.asText.get().trim()

    if (symbolizerPath.isNotEmpty()) {
        environment("ASAN_SYMBOLIZER_PATH", symbolizerPath)
    }

    // Set java.library.path
    jvmArgs = listOf("-Djava.library.path=${projectRoot}/lib")

    doFirst {
        println("============================================")
        println("Running with AddressSanitizer (ASAN)")
        println("============================================")
        println("ASAN library: $asanLibPath")
        println("Symbolizer: ${if (symbolizerPath.isNotEmpty()) symbolizerPath else "Not found (stack traces may be limited)"}")
        println("")
        println("ASAN will detect:")
        println("  - Use-after-free")
        println("  - Heap/stack buffer overflows")
        println("  - Memory access violations")
        println("")
        println("Starting example...")
        println("============================================")
        println("")
    }

    doLast {
        println("")
        println("============================================")
        println("Example completed")
        println("============================================")
    }
}

// Set working directory for the run task to project root
tasks.named<JavaExec>("run") {
    workingDir = projectRoot
}
