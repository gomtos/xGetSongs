plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.serialization)
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    api(project(":shared"))
    api(libs.kotlinx.coroutines.core)
    implementation(libs.kotlinx.serialization.json)

    testImplementation(kotlin("test"))
    testImplementation(libs.kotlinx.coroutines.test)
}

tasks.test {
    // Tests that need the real yt-dlp and the network run only through integrationTest.
    useJUnitPlatform { excludeTags("integration") }
}

val integrationTest by tasks.registering(Test::class) {
    description = "Runs the tests that talk to the real YouTube with the real yt-dlp and ffmpeg."
    group = "verification"
    testClassesDirs = sourceSets.test.get().output.classesDirs
    classpath = sourceSets.test.get().runtimeClasspath
    useJUnitPlatform { includeTags("integration") }
    testLogging {
        showStandardStreams = true
        events("passed", "skipped", "failed")
    }
}
