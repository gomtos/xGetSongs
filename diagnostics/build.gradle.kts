plugins {
    alias(libs.plugins.kotlin.jvm)
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    implementation(project(":shared"))
    implementation(libs.slf4j.api)

    testImplementation(kotlin("test"))
    testImplementation(libs.logback.classic) // ListAppender: the tests read what is logged
}

tasks.test {
    useJUnitPlatform()
}

// The diagnostics run in every process of the app, including the sidecar, whose runtime need not have java.desktop: no AWT
// and no Swing in here. The UI thread is handed in by the caller (see Diagnostics.start).
val checkNoAwt by tasks.registering {
    val sources = fileTree("src/main/kotlin") { include("**/*.kt") }
    inputs.files(sources)
    doLast {
        val forbidden = Regex("""\bjava\.awt\.|\bjavax\.swing\.""")
        val offenders = sources.files.flatMap { file ->
            file.readLines().withIndex()
                .filter { forbidden.containsMatchIn(it.value) }
                .map { "${file.name}:${it.index + 1}: ${it.value.trim()}" }
        }
        if (offenders.isNotEmpty()) {
            throw GradleException("diagnostics must not use AWT or Swing:\n" + offenders.joinToString("\n"))
        }
    }
}

tasks.matching { it.name == "test" || it.name == "check" }.configureEach {
    dependsOn(checkNoAwt)
}
