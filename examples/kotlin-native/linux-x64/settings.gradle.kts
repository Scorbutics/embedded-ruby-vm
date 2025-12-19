rootProject.name = "kotlin-native-ruby-vm-example"

// Include the parent project as a composite build
includeBuild("../../..")

// Enable version catalogs from parent project if available
dependencyResolutionManagement {
    @Suppress("UnstableApiUsage")
    repositories {
        mavenCentral()
    }
}
