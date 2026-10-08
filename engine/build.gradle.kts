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
    implementation(libs.jsoup)

    // The hidden web view of the Google lyrics lookup. Gradle does not pick the Windows build of JavaFX by itself here
    // (it would take the empty, platform-less jars), and the modules that javafx-web needs come without it too, so all
    // five are named with the `win` classifier. The app is Windows only.
    val javafxVersion = libs.versions.javafx.get()
    for (module in listOf("base", "graphics", "controls", "media", "web")) {
        implementation("org.openjfx:javafx-$module:$javafxVersion:win")
    }

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
    // The result depends on the installed tools and the network, which Gradle cannot see, so never skip a run.
    outputs.upToDateWhen { false }
    testClassesDirs = sourceSets.test.get().output.classesDirs
    classpath = sourceSets.test.get().runtimeClasspath
    useJUnitPlatform { includeTags("integration") }
    testLogging {
        showStandardStreams = true
        events("passed", "skipped", "failed")
    }
}
