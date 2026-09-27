// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Android port contributors
// SPDX-License-Identifier: GPL-2.0-or-later

import java.util.Base64

plugins {
    id("com.android.application")
}

fun envOr(prop: String, env: String): String? =
    if (project.hasProperty(prop)) project.property(prop) as String else System.getenv(env)

// Optional release signing material, normally provided by GitHub Actions
// secrets (see .github/workflows/android-build.yml). When absent the release
// build simply stays unsigned and can be signed manually afterwards.
val keyStoreBase64 = System.getenv("ANDROID_KEYSTORE_BASE64")
val keyStoreFile = envOr("androidStoreFile", "ANDROID_KEYSTORE_FILE")
val keyStorePassword = envOr("androidStorePassword", "ANDROID_KEYSTORE_PASSWORD")
val keyAlias = envOr("androidKeyAlias", "ANDROID_KEY_ALIAS")
val keyPassword = envOr("androidKeyPassword", "ANDROID_KEY_PASSWORD")

android {
    namespace = "com.shadps4.emu"
    compileSdk = 35
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.shadps4.emu"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.18.1-android.1"

        // The PS4 memory map and the current port only support 64-bit ARM.
        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        // Keep the APK aligned with 16 KB page devices (Android 15+).
        packaging {
            jniLibs {
                useLegacyPackaging = false
            }
        }

        externalNativeBuild {
            // Intentionally empty: the native side is driven by
            // android/build_native.sh (or the CI job), which invokes CMake
            // directly and stages the resulting libraries into jniLibs.
        }
    }

    if (keyStoreBase64 != null || (keyStoreFile != null && keyStorePassword != null)) {
        signingConfigs {
            create("github") {
                if (keyStoreBase64 != null) {
                    // Decode a base64 keystore handed over by CI.
                    val ksFile = File(rootProject.projectDir, "release.keystore")
                    if (!ksFile.exists()) {
                        ksFile.writeBytes(Base64.getDecoder().decode(keyStoreBase64))
                    }
                    storeFile = ksFile
                } else {
                    storeFile = File(keyStoreFile!!)
                }
                storePassword = keyStorePassword
                keyAlias = keyAlias
                keyPassword = keyPassword
            }
        }
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
            isDebuggable = true
        }
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            if (signingConfigs.findByName("github") != null) {
                signingConfig = signingConfigs.getByName("github")
            }
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    sourceSets {
        getByName("main") {
            // SDL3 ships its Java glue as plain sources; compile them in-tree
            // instead of depending on a prebuilt AAR. Paths are relative to
            // this module's project dir (android/app).
            java.srcDir("../../externals/sdl3/android-project/app/src/main/java")
            // Native libraries staged here by android/build_native.sh.
            jniLibs.srcDir("src/main/jniLibs")
        }
    }

    lint {
        abortOnError = false
    }
}

dependencies {
    // Deliberately empty: the port uses only the Android framework and the
    // in-tree SDL3 sources.
}
