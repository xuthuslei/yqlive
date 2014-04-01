/*
 * ffmpeg.h
 *
 *  Created on: 2014-3-6
 *      Author: dell
 */

#ifndef RECORDER_H_
#define RECORDER_H_

#ifdef __cplusplus
extern "C" {
#endif

jint Java_com_yiqingart_live_MainActivity_initRecorder( JNIEnv* env, jobject thiz );
jint Java_com_yiqingart_live_MainActivity_startRecorder( JNIEnv* env, jobject thiz );
jint Java_com_yiqingart_live_MainActivity_stopRecorder( JNIEnv* env, jobject thiz );
jint Java_com_yiqingart_live_MainActivity_SupplyAudioSamples( JNIEnv* env, jobject thiz, jshortArray buffer,  jlong len );
jint Java_com_yiqingart_live_MainActivity_SupplyVideoFrame( JNIEnv* env, jobject thiz, jbyteArray buffer,  jlong len, jlong timestamp );
int startWorker(int fd);
int stopWorker();
void log_callback(void* ptr, int level, const char* fmt, va_list vl);
static int decode_interrupt_cb(void *ctx);
extern const AVIOInterruptCB int_cb;
#ifdef __cplusplus
}
#endif

#endif /* RECORDER_H_ */
