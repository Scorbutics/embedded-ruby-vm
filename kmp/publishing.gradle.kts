// ============================================================================
// Maven Publishing Configuration
// ============================================================================

publishing {
    publications {
        // Configure all publications
        withType<MavenPublication> {
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
                logger.lifecycle("✓ Packaged ${fatLibSource.name} for $abi")
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
        
        publishing.publications.forEach { pub ->
            if (pub is MavenPublication) {
                println("  - ${pub.name}: ${pub.groupId}:${pub.artifactId}:${pub.version}")
            }
        }
        
        println("""
        
        Repositories:
        -------------
        """.trimIndent())
        
        publishing.repositories.forEach { repo ->
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
        
        Kotlin code:
        
        import com.scorbutics.rubyvm.LibraryConfig
        import com.scorbutics.rubyvm.RubyVMNative
        
        // Configure library name before first use
        LibraryConfig.libraryName = "$nativeLibraryName"
        
        // Initialize and use Ruby VM
        RubyVMNative.initialize()
        val result = RubyVMNative.eval("puts 'Hello from Ruby!'")
        
        ================================================================================
        
        Publishing commands:
        --------------------
        ./gradlew publishToMavenLocal           # Publish to local Maven repository
        ./gradlew publish                        # Publish to all configured repositories
        ./gradlew publishAllPublicationsToGitHubPackagesRepository  # Publish to GitHub Packages
        
        """.trimIndent())
    }
}
