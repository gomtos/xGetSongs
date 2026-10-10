rootProject.name = "xGetSongs"

pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
        google()
    }
}

dependencyResolutionManagement {
    repositories {
        mavenCentral()
        google()
    }
}

include(":shared")
include(":diagnostics")
include(":engine")
include(":server")
include(":app")
