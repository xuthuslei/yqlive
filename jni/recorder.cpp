#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <android/log.h>
#include <pthread.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}
#include "VideoRecorder.h"
#include "recorder.h"
int isStop = 1;
AVR::VideoRecorder *recorder = NULL;

static int decode_interrupt_cb(void *ctx)
{
    return isStop;
}

const AVIOInterruptCB int_cb = { decode_interrupt_cb, NULL };
void log_callback(void* ptr, int level, const char* fmt, va_list vl)
{
    static int print_prefix = 1;
    char line[1024] ;
    av_log_format_line(ptr, level, fmt, vl, line, 1024, &print_prefix);
    __android_log_print(ANDROID_LOG_INFO, "FFMPEG", "%s", line);
}

jint Java_com_yiqingart_live_MainActivity_initRecorder(JNIEnv* env,
        jobject thiz) {
    __android_log_print(ANDROID_LOG_INFO, "JNIMsg", "initRecorder");
    av_log_set_callback(log_callback);
    av_register_all();
    //avcodec_register_all();
    avformat_network_init();


    return 0;
}

jint Java_com_yiqingart_live_MainActivity_startRecorder( JNIEnv* env, jobject thiz )
{
    __android_log_print(ANDROID_LOG_INFO, "JNIMsg", "startRecorder");
    isStop = 0;
    recorder = AVR::VideoRecorder::New();

    recorder->SetAudioOptions(AVR::AudioSampleFormatS16, 2, 8000, 16000);
    recorder->SetVideoOptions(AVR::VideoFrameFormatNV21, 320, 240, 80000, 10);
    //recorder->Open("/mnt/sdcard/test.m3u8", "hls", true, true);
    //recorder->Open("/mnt/sdcard/test.ts", NULL, true, true);
    recorder->Open("osd://192.168.1.105:8080/file/video/test.m3u8", "hls", true, true);

    return 0;
}

jint Java_com_yiqingart_live_MainActivity_stopRecorder(JNIEnv* env,
        jobject thiz) {
    __android_log_print(ANDROID_LOG_INFO, "JNIMsg", "stopRecorder");
    //stopWorker();
    isStop = 1;
    if(recorder){
        recorder->Close();
    }
    return 0;
}
jint Java_com_yiqingart_live_MainActivity_SupplyAudioSamples( JNIEnv* env, jobject thiz, jshortArray buffer, jlong len )
{
    short * inbuffer = NULL;
    __android_log_print(ANDROID_LOG_INFO, "JNIMsg", "SupplyAudioSamples");
    inbuffer = ( short *)env->GetShortArrayElements(buffer, 0);
    if(recorder){
        recorder->SupplyAudioSamples(inbuffer, (long)len);
    }
    env->ReleaseShortArrayElements(buffer, (jshort*)inbuffer, 0);
    return 0;
}
jint Java_com_yiqingart_live_MainActivity_SupplyVideoFrame( JNIEnv* env, jobject thiz, jbyteArray buffer, jlong len, jlong timestamp)
{
    char * inbuffer = NULL;
    //__android_log_print(ANDROID_LOG_INFO, "JNIMsg", "SupplyVideoFrame len:%lld time:%lld getlen:%d", len, timestamp,  env->GetArrayLength(buffer));
    inbuffer = ( char *)env->GetByteArrayElements(buffer, 0);

    if(recorder){
        recorder->SupplyVideoFrame(inbuffer, (long)len, (long)timestamp);
    }
    env->ReleaseByteArrayElements(buffer, (jbyte*)inbuffer, 0);
    return 0;
}

