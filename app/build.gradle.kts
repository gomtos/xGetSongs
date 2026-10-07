import org.jetbrains.compose.desktop.application.dsl.TargetFormat

plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.compose.multiplatform)
    alias(libs.plugins.kotlin.serialization)
}

kotlin {
    jvmToolchain(21)
    jvm("desktop")

    sourceSets {
        commonMain.dependencies {
            implementation(project(":shared"))
            implementation(libs.kotlinx.coroutines.core)
            implementation(libs.kotlinx.serialization.json)
            implementation(libs.compose.runtime)
            implementation(libs.compose.foundation)
            implementation(libs.compose.material3)
            implementation(libs.compose.ui)
            implementation(libs.ktor.client.core)
            implementation(libs.ktor.client.content.negotiation)
            implementation(libs.ktor.serialization.kotlinx.json)
        }
        commonTest.dependencies {
            implementation(kotlin("test"))
            implementation(libs.kotlinx.coroutines.test)
        }
        val desktopMain by getting {
            dependencies {
                implementation(compose.desktop.currentOs)
                implementation(project(":server"))
                implementation(libs.ktor.client.cio)
                implementation(libs.kotlinx.coroutines.swing)
                // The diagnostics code logs through SLF4J; logback itself comes along at run time through :server.
                implementation(libs.slf4j.api)
            }
        }
        val desktopTest by getting {
            dependencies {
                // The diagnostics tests configure logback directly and read what it writes.
                implementation(libs.logback.classic)
                implementation(libs.ktor.server.test.host)
                implementation(libs.ktor.server.sse)
                implementation(libs.ktor.client.cio)
                implementation(libs.ktor.client.mock)
            }
        }
    }
}

compose.desktop {
    application {
        mainClass = "com.xgetsongs.app.MainKt"
        nativeDistributions {
            targetFormats(TargetFormat.Msi)
            packageName = "xGetSongs"
            packageVersion = "1.0.0"
            // The default jlink runtime lacks these: java.net.http (yt-dlp download) and jdk.unsupported (Netty).
            // java.management is for the garbage collector figures in the thread dump of a frozen UI (it is left out when missing).
            modules("java.net.http", "jdk.unsupported", "java.management")
        }
    }
}
