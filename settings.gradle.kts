rootProject.name = "embedded-ruby-vm"

// Enable Gradle version catalogs
enableFeaturePreview("TYPESAFE_PROJECT_ACCESSORS")

// Include the Kotlin Multiplatform module
include(":kmp")

// Optional: Rename the module for cleaner API
project(":kmp").name = "ruby-vm-kmp"

pluginManagement {
    repositories {
        google()
        gradlePluginPortal()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}
