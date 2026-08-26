plugins { id("com.android.application") }

android {
    namespace = "pl.homecloud.app"
    compileSdk = 36
    ndkVersion = "29.0.14206865"
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    defaultConfig {
        applicationId = "pl.homecloud.app"
        minSdk = 26
        targetSdk = 36
        versionCode = 8
        versionName = "0.5.2"
        externalNativeBuild { cmake { cppFlags += listOf("-std=c++20", "-Wall", "-Wextra", "-Wpedantic") } }
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
    }
    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.31.6" }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
}
