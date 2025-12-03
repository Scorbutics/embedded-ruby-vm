plugins {
    java
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

dependencies {
    // Depend on the KMP desktop JAR
    implementation(files("${projectRoot}/kmp/build/libs/ruby-vm-kmp-desktop-1.0.0-SNAPSHOT.jar"))

    // Kotlin standard library is needed for Function1 and other Kotlin types used by the KMP library
    implementation("org.jetbrains.kotlin:kotlin-stdlib:1.9.22")
}

application {
    mainClass.set("examples.SimpleJavaExample")

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
    commandLine("./gradlew", ":ruby-vm-kmp:desktopJar")
}

// Make compilation depend on the desktop JAR being built
tasks.named("compileJava") {
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
    description = "Build and run the Java Ruby VM example"

    dependsOn("run")
}

// Set working directory for the run task to project root
tasks.named<JavaExec>("run") {
    workingDir = projectRoot
}
