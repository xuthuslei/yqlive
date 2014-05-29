#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
JavaVM * gJavaVM;
jobject gJavaObj;
AVR::VideoRecorder *recorder = NULL;

static int decode_interrupt_cb(void *ctx)
{
    return isStop;
}



const AVIOInterruptCB int_cb = { decode_interrupt_cb, NULL };
void ChangeHttpInfo(int level, const char * info)
{
	JNIEnv *env;  
	jclass cls;
	jmethodID callbackID;
	
	gJavaVM->AttachCurrentThread(&env, NULL);
	cls = env->GetObjectClass(gJavaObj);  
	 if(cls == NULL)  
    {  
	     __android_log_print(ANDROID_LOG_ERROR,"JNIMsg","GetObjectClass is failed" );
		 return;
    }  

	if(level==49){
		callbackID = env->GetMethodID(cls , "onHttpInfoCallback" , "(Ljava/lang/String;)V") ;//或得该回调方法句柄  
	}
	else{
		callbackID = env->GetMethodID(cls , "onFmpegInfoCallback" , "(Ljava/lang/String;)V") ;//或得该回调方法句柄  
	}
    if(callbackID == NULL)  
    {  
        __android_log_print(ANDROID_LOG_ERROR,"JNIMsg","getMethodId is failed" ) ;
		 return;
    }  
    
    jstring native_desc = env->NewStringUTF(info);  
  
    env->CallVoidMethod(gJavaObj , callbackID , native_desc); //回调该方法，并且传递参数值  
    gJavaVM->DetachCurrentThread();
}
void log_callback(void* ptr, int level, const char* fmt, va_list vl)
{
    static int print_prefix = 1;
    char line[1024] ;
    av_log_format_line(ptr, level, fmt, vl, line, 1024, &print_prefix);

    __android_log_print(ANDROID_LOG_INFO, "JNIMsg", "%s", line);
    if((level == 49)||(level == 50)){
		ChangeHttpInfo(level, line);
    }
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

jint Java_com_yiqingart_live_MainActivity_startRecorder( JNIEnv* env, jobject thiz, jstring url, jstring videoSpeed, jint videoWidth, jint videoHeight, jint videoBitrate )
{
	const char* x264Speed;  
	const char* posturl;
    __android_log_print(ANDROID_LOG_INFO, "JNIMsg", "startRecorder");

	x264Speed = env->GetStringUTFChars(videoSpeed, 0);
	posturl = env->GetStringUTFChars(url, 0);
	env->GetJavaVM(&gJavaVM);
	gJavaObj=env->NewGlobalRef(thiz);
    isStop = 0;
    recorder = AVR::VideoRecorder::New();

    recorder->SetAudioOptions(AVR::AudioSampleFormatS16, 2, 8000, 16000);
    //recorder->SetVideoOptions(AVR::VideoFrameFormatNV21, 640, 480, 140000, 10);
    recorder->SetVideoOptions(AVR::VideoFrameFormatNV21, (int)videoWidth, (int)videoHeight, (int)videoBitrate, 10, x264Speed);
    //recorder->Open("/mnt/sdcard/test.m3u8", "hls", true, true);
    //recorder->Open("/mnt/sdcard/test.ts", NULL, true, true);
    //recorder->Open("osd://live.yiqingart.com/file/video/test.m3u8", "hls", true, true);
    recorder->Open(posturl, "hls", true, true);
    env->ReleaseStringUTFChars(videoSpeed, x264Speed);
    env->ReleaseStringUTFChars(url, posturl);
    return 0;
}

jint Java_com_yiqingart_live_MainActivity_stopRecorder(JNIEnv* env,
        jobject thiz) {
    __android_log_print(ANDROID_LOG_INFO, "JNIMsg", "stopRecorder");
    //stopWorker();
    if(recorder){
        recorder->Close();
    }
    isStop = 1;
    delete recorder;
    recorder = NULL;
    return 0;
}
jint Java_com_yiqingart_live_MainActivity_SupplyAudioSamples( JNIEnv* env, jobject thiz, jshortArray buffer, jlong len )
{
    short * inbuffer = NULL;
    //__android_log_print(ANDROID_LOG_INFO, "JNIMsg", "SupplyAudioSamples");
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
	jint pts = 1;
    
    //__android_log_print(ANDROID_LOG_INFO, "JNIMsg", "SupplyVideoFrame len:%lld time:%lld getlen:%d", len, timestamp,  env->GetArrayLength(buffer));
    inbuffer = ( char *)env->GetByteArrayElements(buffer, 0);

    if(recorder){
        recorder->SupplyVideoFrame(inbuffer, (long)len, timestamp);
		pts = (jint)(recorder->getPts());
    }
    env->ReleaseByteArrayElements(buffer, (jbyte*)inbuffer, 0);
    return pts;
}

