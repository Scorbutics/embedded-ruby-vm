// ============================================================================
// Maven Publishing Configuration
// ============================================================================
// NOTE: This file is loaded via apply(from = ...) so Kotlin DSL type-safe
// accessors (e.g. `publishing { }`) are NOT available. Use explicit
// configure<T> / the<T>() calls instead.

import org.gradle.api.publish.PublishingExtension
import org.gradle.api.publish.maven.MavenPublication
import org.gradle.api.artifacts.repositories.MavenArtifactRepository

configure<PublishingExtension> {
    publications {
        // Configure all publications
        withType(MavenPublication::class) {
            pom {
                name.set("Ruby VM KMP")
                description.set("Kotlin Multiplatform bindings for embedded Ruby VM with configurable fat library support")
                url.set("https://github.com/Scorbutics/litergss-everywhere")

                licenses {
                    license {
                        name.set("MIT License")
                        url.set("https://opensource.org/licenses/MIT")
                    }
                }

                developers {
                    developer {
                        id.set("scorbutics")
                        name.set("Scorbutics")
                        email.set("brice.bulgarelli@gmail.com")
                    }
                }

                scm {
                    connection.set("scm:git:git://github.com/Scorbutics/litergss-everywhere.git")
                    developerConnection.set("scm:git:ssh://github.com:Scorbutics/litergss-everywhere.git")
                    url.set("https://github.com/Scorbutics/litergss-everywhere")
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

        // Local Maven repository for testing
        mavenLocal()
    }
}

// Resolve nativeLibraryName from the parent build script's property
val nativeLibraryName: String = findProperty("nativeLibraryName")?.toString() ?: "embedded-ruby"

// Task to package static libraries for Android
tasks.register<Copy>("packageStaticLibsForAndroid") {
    description = "Package static fat libraries for Android into jniLibs structure"
    group = "publishing"

    // Source: Fat library from CMake build (parent project)
    val fatLibSource = file("../../build/staging/usr/local/lib/lib${nativeLibraryName}.a")

    // Destination: jniLibs directory for Android AAR packaging
    val jniLibsDir = file("src/androidMain/jniLibs")

    // Android ABIs to include
    val abis = listOf("arm64-v8a", "x86_64")

    doFirst {
        if (!fatLibSource.exists()) {
            logger.warn("Fat library not found at ${fatLibSource}. Run CMake build first.")
            logger.warn("Expected location: ${fatLibSource.absolutePath}")
        }
    }

    from(fatLibSource)

    abis.forEach { abi ->
        into("$jniLibsDir/$abi") {
            rename { "lib${nativeLibraryName}.a" }
        }
    }

    doLast {
        if (fatLibSource.exists()) {
            abis.forEach { abi ->
                logger.lifecycle("Packaged ${fatLibSource.name} for $abi")
            }
        }
    }
}

// Ensure static libs are packaged before Android AAR is built
tasks.matching { it.name.contains("mergeReleaseJniLibFolders") || it.name.contains("mergeDebugJniLibFolders") }.configureEach {
    dependsOn("packageStaticLibsForAndroid")
}

// Add publishing documentation task
tasks.register("publishingInfo") {
    description = "Display information about publishing configuration"
    group = "help"

    doLast {
        val pubExt = the<PublishingExtension>()
        println("""
        ================================================================================
        Publishing Information
        ================================================================================

        Group ID: $group
        Artifact ID: kmp (or platform-specific)
        Version: $version
        Native Library Name: $nativeLibraryName

        Available publications:
        ------------------------
        """.trimIndent())

        pubExt.publications.forEach { pub ->
            if (pub is MavenPublication) {
                println("  - ${pub.name}: ${pub.groupId}:${pub.artifactId}:${pub.version}")
            }
        }

        println("""

        Repositories:
        -------------
        """.trimIndent())

        pubExt.repositories.forEach { repo ->
            if (repo is MavenArtifactRepository) {
                println("  - ${repo.name}: ${repo.url}")
            }
        }

        println("""

        Usage in consumer projects:
        ---------------------------

        build.gradle.kts:

        repositories {
            maven { url = uri("https://maven.pkg.github.com/Scorbutics/litergss-everywhere") }
            // or
            mavenLocal()
        }

        dependencies {
            implementation("$group:kmp:$version")
        }

        ================================================================================

        Publishing commands:
        --------------------
        ./gradlew publishToMavenLocal           # Publish to local Maven repository
        ./gradlew publish                        # Publish to all configured repositories
        ./gradlew publishAllPublicationsToGitHubPackagesRepository  # Publish to GitHub Packages

        """.trimIndent())
    }
}
