plugins {
    id("com.android.application")
    id("kotlin-android")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

val datafriendAbis = (project.findProperty("target-platform") as? String
    ?: "android-arm64,android-x64").split(",").mapNotNull {
    when (it) {
        "android-arm64" -> "arm64-v8a"
        "android-x64" -> "x86_64"
        else -> null
    }
}
require(datafriendAbis.isNotEmpty()) { "Datafriend requires android-arm64 or android-x64" }

android {
    namespace = "dev.drawdo.drawdo"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = JavaVersion.VERSION_17.toString()
    }

    defaultConfig {
        // TODO: Specify your own unique Application ID (https://developer.android.com/studio/build/application-id.html).
        applicationId = if (System.getenv("DATAFRIEND_PARTICLE_DEMO") == "1")
            "dev.drawdo.drawdo.particles" else "dev.drawdo.drawdo"
        manifestPlaceholders["appLabel"] = if (System.getenv("DATAFRIEND_PARTICLE_DEMO") == "1")
            "Cell particles" else "DrawDo"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        versionCode = flutter.versionCode
        versionName = flutter.versionName
        ndk { abiFilters.addAll(datafriendAbis) }
    }

    externalNativeBuild {
        cmake {
            path = if (System.getenv("DATAFRIEND_PARTICLE_COMPARE") == "1")
                file("../../benchmarks/CMakeLists.txt")
                else file("../../../../datafriend/CMakeLists.txt")
        }
    }

    sourceSets.getByName("main").java.srcDir("../../../../datafriend/platform/android/src/main/java")

    buildTypes {
        // Override Flutter's build-type defaults, which also include 32-bit ARM.
        configureEach {
            ndk.abiFilters.clear()
            ndk.abiFilters.addAll(datafriendAbis)
            // AGP clears CMake's configuration flags, and Flutter's "profile"
            // configuration has no CMake defaults. Optimize the native engine
            // explicitly in both AOT configurations, leaving debug unchanged.
            if (name == "profile" || name == "release") {
                externalNativeBuild.cmake {
                    cFlags += listOf("-O3", "-DNDEBUG")
                    cppFlags += listOf("-O3", "-DNDEBUG")
                }
            }
        }
        release {
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "../../../../datafriend/platform/android/proguard-rules.pro")
            // TODO: Add your own signing config for the release build.
            // Signing with the debug keys for now, so `flutter run --release` works.
            signingConfig = signingConfigs.getByName("debug")
        }
    }
}

flutter {
    source = "../.."
}
