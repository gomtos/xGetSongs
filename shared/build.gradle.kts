plugins {
    alias(libs.plugins.kotlin.multiplatform)
    alias(libs.plugins.kotlin.serialization)
}

kotlin {
    jvmToolchain(21)
    jvm()

    sourceSets {
        commonMain.dependencies {
            implementation(libs.kotlinx.serialization.json)
        }
        commonTest.dependencies {
            implementation(kotlin("test"))
        }
    }
}

// shared must stay pure Kotlin so it can later compile for wasmJs. With only the jvm() target the
// compiler does not enforce this, so fail the build if commonMain mentions java.* / javax.*.
val checkCommonPurity by tasks.registering {
    val sources = fileTree("src/commonMain") { include("**/*.kt") }
    inputs.files(sources)
    doLast {
        val forbidden = Regex("""\bjavax?\.""")
        val offenders = sources.files.flatMap { file ->
            file.readLines().withIndex()
                .filter { forbidden.containsMatchIn(it.value) }
                .map { "${file.name}:${it.index + 1}: ${it.value.trim()}" }
        }
        if (offenders.isNotEmpty()) {
            throw GradleException("shared/commonMain must not use java.* APIs:\n" + offenders.joinToString("\n"))
        }
    }
}

tasks.matching { it.name == "jvmTest" || it.name == "check" }.configureEach {
    dependsOn(checkCommonPurity)
}
