rootProject.name = "xGetSongs"

pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositories {
        mavenCentral()
    }
}

include(":shared")
include(":diagnostics")
include(":engine")
include(":server")
