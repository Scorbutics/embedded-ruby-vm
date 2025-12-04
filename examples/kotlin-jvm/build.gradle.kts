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
    // Default to running the improved API example
    // Can be overridden with: -PexampleClass=JvmExample or -PexampleClass=ImprovedApiExample
    val exampleClass = project.findProperty("exampleClass")?.toString() ?: "ImprovedApiExample"
    mainClass.set("examples.${exampleClass}Kt")

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
    description = "Build and run the Kotlin/JVM Ruby VM example (default: ImprovedApiExample)"

    doFirst {
        val exampleClass = project.findProperty("exampleClass")?.toString() ?: "ImprovedApiExample"
        println("==============================================")
        println("Running example: $exampleClass")
        println("==============================================")
        println()
        println("Available examples:")
        println("  1. ImprovedApiExample (default) - Shows new batch execution, builder pattern, metrics")
        println("  2. JvmExample - Original manual CountDownLatch example")
        println()
        println("To run a specific example:")
        println("  ./gradlew runExample -PexampleClass=JvmExample")
        println("  ./gradlew runExample -PexampleClass=ImprovedApiExample")
        println()
        println("==============================================")
        println()
    }

    dependsOn("run")
}

// Task to run both examples sequentially
tasks.register("runAllExamples") {
    group = "application"
    description = "Run all available examples sequentially"

    doLast {
        println()
        println("==============================================")
        println("Running JvmExample...")
        println("==============================================")
        println()

        javaexec {
            mainClass.set("examples.JvmExampleKt")
            classpath = sourceSets.main.get().runtimeClasspath
            workingDir = projectRoot
            jvmArgs = listOf("-Djava.library.path=${projectRoot}/lib")
        }

        println()
        println("==============================================")
        println("Running ImprovedApiExample...")
        println("==============================================")
        println()

        javaexec {
            mainClass.set("examples.ImprovedApiExampleKt")
            classpath = sourceSets.main.get().runtimeClasspath
            workingDir = projectRoot
            jvmArgs = listOf("-Djava.library.path=${projectRoot}/lib")
        }
    }

    dependsOn("classes")
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
