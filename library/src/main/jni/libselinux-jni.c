/*
 * Copyright (c) 2019 Hai Zhang <dreaming.in.code.zh@gmail.com>
 * All Rights Reserved.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <jni.h>

#include <android/log.h>

#include <selinux/selinux.h>

#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define LOG_TAG "libselinux-binding"

#undef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp) ({ \
    errno = 0; \
    __typeof__(exp) _rc; \
    do { \
        _rc = (exp); \
    } while (errno == EINTR); \
    _rc; })

static jclass findClass(JNIEnv *env, const char *name) {
    jclass localClass = (*env)->FindClass(env, name);
    if (!localClass) {
        ALOGE("Failed to find class '%s'", name);
        abort();
    }
    jclass globalClass = (*env)->NewGlobalRef(env, localClass);
    (*env)->DeleteLocalRef(env, localClass);
    if (!globalClass) {
        ALOGE("Failed to create a global reference for '%s'", name);
        abort();
    }
    return globalClass;
}

static jfieldID findField(JNIEnv *env, jclass clazz, const char *name, const char *signature) {
    jfieldID field = (*env)->GetFieldID(env, clazz, name, signature);
    if (!field) {
        ALOGE("Failed to find field '%s' '%s'", name, signature);
        abort();
    }
    return field;
}

static jmethodID findMethod(JNIEnv *env, jclass clazz, const char *name, const char *signature) {
    jmethodID method = (*env)->GetMethodID(env, clazz, name, signature);
    if (!method) {
        ALOGE("Failed to find method '%s' '%s'", name, signature);
        abort();
    }
    return method;
}

static jclass getErrnoExceptionClass(JNIEnv *env) {
    static jclass errnoExceptionClass = NULL;
    if (!errnoExceptionClass) {
        errnoExceptionClass = findClass(env, "android/system/ErrnoException");
    }
    return errnoExceptionClass;
}

static jclass getFileDescriptorClass(JNIEnv *env) {
    static jclass fileDescriptorClass = NULL;
    if (!fileDescriptorClass) {
        fileDescriptorClass = findClass(env, "java/io/FileDescriptor");
    }
    return fileDescriptorClass;
}

static jfieldID getFileDescriptorDescriptorField(JNIEnv *env) {
    static jclass fileDescriptorDescriptorField = NULL;
    if (!fileDescriptorDescriptorField) {
        fileDescriptorDescriptorField = findField(env, getFileDescriptorClass(env), "descriptor",
                                                  "I");
    }
    return fileDescriptorDescriptorField;
}

static void throwException(JNIEnv *env, jclass exceptionClass, jmethodID constructor3,
                           jmethodID constructor2, const char *functionName, int error) {
    jthrowable cause = NULL;
    if ((*env)->ExceptionCheck(env)) {
        cause = (*env)->ExceptionOccurred(env);
        (*env)->ExceptionClear(env);
    }
    jstring detailMessage = (*env)->NewStringUTF(env, functionName);
    if (!detailMessage) {
        // Not really much we can do here. We're probably dead in the water,
        // but let's try to stumble on...
        (*env)->ExceptionClear(env);
    }
    jobject exception;
    if (cause) {
        exception = (*env)->NewObject(env, exceptionClass, constructor3, detailMessage, error,
                                      cause);
    } else {
        exception = (*env)->NewObject(env, exceptionClass, constructor2, detailMessage, error);
    }
    (*env)->Throw(env, exception);
    (*env)->DeleteLocalRef(env, detailMessage);
}

static void throwErrnoException(JNIEnv* env, const char* functionName) {
    int error = errno;
    static jmethodID constructor3 = NULL;
    if (!constructor3) {
        constructor3 = findMethod(env, getErrnoExceptionClass(env), "<init>",
                                  "(Ljava/lang/String;ILjava/lang/Throwable;)V");
    }
    static jmethodID constructor2 = NULL;
    if (!constructor2) {
        constructor2 = findMethod(env, getErrnoExceptionClass(env), "<init>",
                                  "(Ljava/lang/String;I)V");
    }
    throwException(env, getErrnoExceptionClass(env), constructor3, constructor2, functionName,
                   error);
}

static char *mallocStringFromBytes(JNIEnv *env, jbyteArray javaBytes) {
    void *bytes = (*env)->GetByteArrayElements(env, javaBytes, NULL);
    jsize javaLength = (*env)->GetArrayLength(env, javaBytes);
    size_t length = (size_t) javaLength;
    char *string = malloc(length + 1);
    memcpy(string, bytes, length);
    (*env)->ReleaseByteArrayElements(env, javaBytes, bytes, JNI_ABORT);
    string[length] = '\0';
    return string;
}

static jbyteArray newBytesFromString(JNIEnv *env, const char *string) {
    size_t length = strlen(string);
    jsize javaLength = (jsize) length;
    jbyteArray javaBytes = (*env)->NewByteArray(env, javaLength);
    if (!javaBytes) {
        return NULL;
    }
    const void *stringBytes = string;
    (*env)->SetByteArrayRegion(env, javaBytes, 0, javaLength, stringBytes);
    return javaBytes;
}

JNIEXPORT jbyteArray JNICALL
Java_me_zhanghai_android_libselinux_SeLinux_fgetfilecon(
        JNIEnv *env, jclass clazz, jobject javaFd) {
    int fd = (*env)->GetIntField(env, javaFd, getFileDescriptorDescriptorField(env));
    security_context_t context = NULL;
    TEMP_FAILURE_RETRY(fgetfilecon(fd, &context));
    if (errno) {
        throwErrnoException(env, "fgetfilecon");
        return NULL;
    }
    jbyteArray javaContext = newBytesFromString(env, context);
    freecon(context);
    return javaContext;
}

JNIEXPORT void JNICALL
Java_me_zhanghai_android_libselinux_SeLinux_fsetfilecon(
        JNIEnv *env, jclass clazz, jobject javaFd, jbyteArray javaContext) {
    int fd = (*env)->GetIntField(env, javaFd, getFileDescriptorDescriptorField(env));
    security_context_t context = mallocStringFromBytes(env, javaContext);
    TEMP_FAILURE_RETRY(fsetfilecon(fd, context));
    free(context);
    if (errno) {
        throwErrnoException(env, "fsetfilecon");
    }
}

static jbyteArray doGetfilecon(JNIEnv *env, jbyteArray javaPath, bool isLgetfilecon) {
    char *path = mallocStringFromBytes(env, javaPath);
    security_context_t context = NULL;
    TEMP_FAILURE_RETRY((isLgetfilecon ? lgetfilecon : getfilecon)(path, &context));
    free(path);
    if (errno) {
        throwErrnoException(env, isLgetfilecon ? "lgetfilecon" : "getfilecon");
        return NULL;
    }
    jbyteArray javaContext = newBytesFromString(env, context);
    freecon(context);
    return javaContext;
}

JNIEXPORT jbyteArray JNICALL
Java_me_zhanghai_android_libselinux_SeLinux_getfilecon(
        JNIEnv *env, jclass clazz, jbyteArray javaPath) {
    return doGetfilecon(env, javaPath, false);
}

JNIEXPORT jboolean JNICALL
Java_me_zhanghai_android_libselinux_SeLinux_is_1selinux_1enabled(
        JNIEnv *env, jclass clazz) {
    int enabled = is_selinux_enabled();
    jboolean javaEnabled = (jboolean) (enabled ? JNI_TRUE : JNI_FALSE);
    return javaEnabled;
}

JNIEXPORT jbyteArray JNICALL
Java_me_zhanghai_android_libselinux_SeLinux_lgetfilecon(
        JNIEnv *env, jclass clazz, jbyteArray javaPath) {
    return doGetfilecon(env, javaPath, true);
}

static void doSetfilecon(JNIEnv *env, jbyteArray javaPath, jbyteArray javaContext,
        bool isLsetfilecon) {
    char *path = mallocStringFromBytes(env, javaPath);
    security_context_t context = mallocStringFromBytes(env, javaContext);
    TEMP_FAILURE_RETRY((isLsetfilecon ? lsetfilecon : setfilecon)(path, context));
    free(path);
    free(context);
    if (errno) {
        throwErrnoException(env, isLsetfilecon ? "lsetfilecon" : "setfilecon");
    }
}

JNIEXPORT void JNICALL
Java_me_zhanghai_android_libselinux_SeLinux_lsetfilecon(
        JNIEnv *env, jclass clazz, jbyteArray javaPath, jbyteArray javaContext) {
    doSetfilecon(env, javaPath, javaContext, true);
}

JNIEXPORT jboolean JNICALL
Java_me_zhanghai_android_libselinux_SeLinux_security_1getenforce(
        JNIEnv *env, jclass clazz) {
    int enforce = TEMP_FAILURE_RETRY(security_getenforce());
    if (enforce == -1 && !errno) {
        // The only case is sscanf() returning EOF in security_getenforce(), which we can treat as
        // an I/O error.
        errno = EIO;
    }
    if (errno) {
        throwErrnoException(env, "security_getenforce");
    }
    jboolean javaEnforce = (jboolean) (enforce ? JNI_TRUE : JNI_FALSE);
    return javaEnforce;
}

JNIEXPORT void JNICALL
Java_me_zhanghai_android_libselinux_SeLinux_setfilecon(
        JNIEnv *env, jclass clazz, jbyteArray javaPath, jbyteArray javaContext) {
    doSetfilecon(env, javaPath, javaContext, false);
}
