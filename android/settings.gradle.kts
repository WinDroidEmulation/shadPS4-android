// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Android port contributors
// SPDX-License-Identifier: GPL-2.0-or-later

pluginManagement {
    repositories {
        gradlePluginPortal()
        google()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "shadPS4"

include(":app")
