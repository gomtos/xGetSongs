plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.serialization)
    application
}

application {
    mainClass.set("com.xgetsongs.server.sidecar.SidecarMainKt")
    applicationName = "xgs-server"
    // stdout is the handshake channel of the sidecar, so its logs go to a file and stderr (see logback-sidecar.xml).
    applicationDefaultJvmArgs = listOf("-Dlogback.configurationFile=logback-sidecar.xml")
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    api(project(":engine"))
    implementation(project(":diagnostics"))
    implementation(libs.ktor.server.core)
    implementation(libs.ktor.server.netty)
    implementation(libs.ktor.server.content.negotiation)
    implementation(libs.ktor.server.sse)
    implementation(libs.ktor.server.status.pages)
    implementation(libs.ktor.serialization.kotlinx.json)
    implementation(libs.slf4j.api)
    runtimeOnly(libs.logback.classic)

    testImplementation(kotlin("test"))
    testImplementation(libs.logback.classic) // ListAppender: the tests read the job log
    testImplementation(libs.kotlinx.coroutines.test)
    testImplementation(libs.ktor.server.test.host)
    testImplementation(libs.ktor.client.cio)
    testImplementation(libs.ktor.client.content.negotiation)
    testImplementation(libs.ktor.serialization.kotlinx.json)
}

tasks.test {
    // The image tests need the image: they run through sidecarImageTest.
    useJUnitPlatform { excludeTags("image") }
}

// The shell starts the sidecar from the image below; the archives of the application plugin are not used.
tasks.distZip { enabled = false }
tasks.distTar { enabled = false }

/** Where the runtime to bundle comes from: `-Pxgs.runtimeDir=<a JDK or runtime folder>`, else the JDK 21 toolchain. */
val runtimeHome = providers.gradleProperty("xgs.runtimeDir").map { file(it) }
    .orElse(javaToolchains.launcherFor(java.toolchain).map { it.metadata.installationPath.asFile })

val sidecarImage by tasks.registering(Sync::class) {
    description = "Builds the folder the shell starts the sidecar from: the jars of the server and a runtime (java)."
    group = "distribution"
    into(layout.buildDirectory.dir("sidecar-image"))
    into("lib") {
        from(tasks.jar)
        from(configurations.runtimeClasspath)
        // A sidecar that only runs on Windows does not need the natives of the other platforms.
        exclude("*-linux-*", "*-osx-*")
    }
    into("runtime") {
        from(runtimeHome)
        exclude("lib/src.zip", "jmods/**", "include/**")
    }
}

val sidecarImageTest by tasks.registering(Test::class) {
    description = "Runs the tests that start the sidecar from the image (bundled runtime, trimmed lib)."
    group = "verification"
    dependsOn(sidecarImage)
    // The result depends on the image, which Gradle cannot see completely, so never skip a run.
    outputs.upToDateWhen { false }
    testClassesDirs = sourceSets.test.get().output.classesDirs
    classpath = sourceSets.test.get().runtimeClasspath
    useJUnitPlatform { includeTags("image") }
    systemProperty("xgs.sidecarImageDir", layout.buildDirectory.dir("sidecar-image").get().asFile.absolutePath)
    testLogging {
        events("passed", "skipped", "failed")
    }
}
