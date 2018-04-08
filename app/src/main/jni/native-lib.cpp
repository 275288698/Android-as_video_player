#include <jni.h>
#include <string>

extern "C"
JNIEXPORT jstring

JNICALL
Java_media_ushow_as_1video_1player_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello From VideoPlayer >>>";
    return env->NewStringUTF(hello.c_str());
}
