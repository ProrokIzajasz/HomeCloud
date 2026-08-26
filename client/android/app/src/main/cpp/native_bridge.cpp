#include "homecloud_client/client.hpp"

#include <jni.h>

#include <string>

using namespace std;

extern "C" JNIEXPORT jstring JNICALL
Java_pl_homecloud_app_NativeBridge_baseUrl(JNIEnv* environment, jclass) {
    const string url = "https://your-homecloud.example/";
    return environment->NewStringUTF(url.c_str());
}
