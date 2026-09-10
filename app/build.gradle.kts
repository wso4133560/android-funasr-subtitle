plugins {
    id("com.android.application")
}

android {
    namespace = "com.wso4133560.funasrsubtitle"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.wso4133560.funasrsubtitle"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-O3")
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    ndkVersion = "27.2.12479018"
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}
