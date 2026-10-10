plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.serialization)
    application
}

application {
    mainClass.set("com.xgetsongs.server.sidecar.SidecarMainKt")
    applicationName = "xgs-server"
    // stdout is the handshake channel of the sidecar, so its logs go to stderr (see logback-sidecar.xml).
    applicationDefaultJvmArgs = listOf("-Dlogback.configurationFile=logback-sidecar.xml")
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    api(project(":engine"))
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
    useJUnitPlatform()
}
