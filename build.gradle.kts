plugins {
    // Apply Kotlin multiplatform plugin to all subprojects
    alias(libs.plugins.kotlin.multiplatform) apply false
    alias(libs.plugins.android.library) apply false
}

// Root project configuration
group = "com.scorbutics.rubyvm"
version = "1.0.0-SNAPSHOT"

tasks.register("clean", Delete::class) {
    delete(rootProject.buildDir)
}
