// compiles on MacOS X with: g++ -DTESTING VideoRecorder.cpp -o v -lavcodec -lavformat -lavutil -lswscale -lx264 -g

#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"VideoRecorder",__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"VideoRecorder",__VA_ARGS__)

#include "VideoRecorder.h"

extern "C" {
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <pthread.h>

}

// Do not use C++ exceptions, templates, or RTTI

namespace AVR {

class VideoRecorderImpl : public VideoRecorder {
public:
	VideoRecorderImpl();
	~VideoRecorderImpl();
	
	bool SetVideoOptions(VideoFrameFormat fmt, int width, int height, unsigned long bitrate, int framerate);
	bool SetAudioOptions(AudioSampleFormat fmt, int channels, unsigned long samplerate, unsigned long bitrate);

	bool Open(const char *mp4file, const char *format, bool hasAudio, bool dbg);
	bool Close();
	
	bool Start();

	void SupplyVideoFrame(const void *frame, unsigned long numBytes, long long timestamp);
	void SupplyAudioSamples(const void *samples, unsigned long numSamples);

private:	
	AVStream *add_audio_stream(enum AVCodecID codec_id);
	void open_audio();	
	void write_audio_frame(AVStream *st);
	
	AVStream *add_video_stream(enum AVCodecID codec_id);
	AVFrame *alloc_picture(enum PixelFormat pix_fmt, int width, int height);
	void open_video();
	void write_video_frame(AVStream *st);
	pthread_mutex_t mtx ;  
	
	// audio related vars
	long long framecount;
	AVFrame *frame;
	int16_t *samples;
	uint8_t *audio_outbuf;
	int audio_outbuf_size;
	int audio_input_frame_size;
	AVStream *audio_st;
	AVOutputFormat *oformat;
	AVCodec *video_codec, *audio_codec;
	int video_framerate;
	int       src_nb_samples;
	uint8_t **src_samples_data;
	int       src_samples_linesize;
	//AVAudioResampleContext *swr_ctx;
	int max_dst_nb_samples;
	uint8_t **dst_samples_data;
	int       dst_samples_linesize;
	int       dst_samples_size;
	
	unsigned long audio_input_leftover_samples;
	
	int audio_channels;				// number of channels (2)
	unsigned long audio_bit_rate;		// codec's output bitrate
	unsigned long audio_sample_rate;		// number of samples per second
	int audio_sample_size;					// size of each sample in bytes (16-bit = 2)
	AVSampleFormat audio_sample_format;

	// video related vars
	uint8_t *video_outbuf;
	int video_outbuf_size;
	AVStream *video_st;
	
	int video_width;
	int video_height;
	unsigned long video_bitrate;
	PixelFormat video_pixfmt;
	AVPicture dst_picture, src_picture;			// video frame after being converted to x264-friendly YUV420P
	AVFrame *tmp_picture;		// video frame before conversion (RGB565)
	SwsContext *img_convert_ctx;
	
	unsigned long timestamp_base;
	
	// common
	AVFormatContext *oc;
};

VideoRecorder::VideoRecorder()
{
	
}

VideoRecorder::~VideoRecorder()
{
	
}

VideoRecorderImpl::VideoRecorderImpl()
{
	samples = NULL;
	audio_outbuf = NULL;
	audio_st = NULL;

	audio_input_leftover_samples = 0;
	//swr_ctx = NULL;

	video_outbuf = NULL;
	video_st = NULL;

	tmp_picture = NULL;
	img_convert_ctx = NULL;
	oformat = NULL;

	oc = NULL;
	pthread_mutex_init(&mtx, NULL);
}

VideoRecorderImpl::~VideoRecorderImpl()
{
	
}

bool VideoRecorderImpl::Open(const char *mp4file, const char *format, bool hasAudio, bool dbg)
{	
	//av_register_all();
	AVDictionary *opts = NULL;

	oformat = NULL;

	if(format != NULL)
	{
		oformat = av_guess_format(format, mp4file, NULL);
	}
	
	avformat_alloc_output_context2(&oc, oformat, NULL, mp4file);
	if (!oc) {
		LOGE("could not deduce output format from file extension\n");
		return false;
	}
	
	video_st = add_video_stream(AV_CODEC_ID_H264);
	
	if(hasAudio)
		audio_st = add_audio_stream(AV_CODEC_ID_AAC);
	
	if(dbg)
		av_dump_format(oc, 0, mp4file, 1);
	
	open_video();
	
	if(hasAudio)
		open_audio();
	if((oc->oformat->flags & AVFMT_NOFILE) == 0)
	{
		if (avio_open(&oc->pb, mp4file, AVIO_FLAG_WRITE) < 0) {
			LOGE("could not open '%s'\n", mp4file);
			return false;
		}
	}
	av_dict_set(&opts, "hls_time", "10", 0);
	av_dict_set(&opts, "hls_list_size", "3", 0);
	
	if(avformat_write_header(oc, &opts)<0)
	{
	    return false;
	}
	av_dict_free(&opts);
	return true;
}

AVStream *VideoRecorderImpl::add_audio_stream(enum AVCodecID codec_id)
{
	AVCodecContext *c;
	AVStream *st;

	audio_codec = avcodec_find_encoder(codec_id);
	
	if (!audio_codec) {
		LOGE("could not found audio_codec\n");
		return NULL;
	}

	st = avformat_new_stream(oc, audio_codec);
	if (!st) {
		LOGE("could not alloc stream\n");
		return NULL;
	}
	st->id = oc->nb_streams-1;
	c = st->codec;
	c->codec_id = codec_id;
	c->codec_type = AVMEDIA_TYPE_AUDIO;
	c->sample_fmt = audio_sample_format;
	c->bit_rate = audio_bit_rate;
	c->sample_rate = audio_sample_rate;
	c->channel_layout = AV_CH_LAYOUT_MONO;//select_channel_layout(*codec);
	c->time_base = (AVRational){1, c->sample_rate};	
	//c->channels = audio_channels;
	c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
	c->profile = FF_PROFILE_AAC_LOW;

	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return st;
}
#if 0
void VideoRecorderImpl::open_audio()
{
	AVCodecContext *c;
	int ret;
	c = audio_st->codec;

	if (avcodec_open2(c, audio_codec, NULL) < 0) {
		LOGE("could not open audio codec\n");
		return;
	}
#if 0
	audio_outbuf_size = 256*1024; // XXX TODO
	audio_outbuf = (uint8_t *)av_malloc(audio_outbuf_size);
#endif
	audio_input_frame_size = c->frame_size;
	audio_outbuf_size = av_samples_get_buffer_size(NULL, c->channels, c->frame_size,
												 c->sample_fmt, 0);
	frame = avcodec_alloc_frame();	

	samples = (int16_t *)av_malloc(audio_outbuf_size);

	
	audio_input_leftover_samples = 0;

}
#endif
void VideoRecorderImpl::open_audio()
{
    AVCodecContext *c;
    AVCodec *codec;
    int ret;
    c = audio_st->codec;
    c->strict_std_compliance = -2;
    /* open it */
    ret = avcodec_open2(c, audio_codec, NULL);
    if (ret == AVERROR_EXPERIMENTAL) {
    	 LOGE( "experimental codec\n");
	}
    if (ret < 0) {
       LOGE( "could not open codec\n");
        return;
    }

	framecount = 0;

    audio_outbuf_size = 10000;
    audio_outbuf = (uint8_t *)av_malloc(audio_outbuf_size);

    /* ugly hack for PCM codecs (will be removed ASAP with new PCM
       support to compute the input frame size in samples */
    if (c->frame_size <= 1) {
        audio_input_frame_size = audio_outbuf_size / c->channels;
        switch(audio_st->codec->codec_id) {
        case AV_CODEC_ID_PCM_S16LE:
        case AV_CODEC_ID_PCM_S16BE:
        case AV_CODEC_ID_PCM_U16LE:
        case AV_CODEC_ID_PCM_U16BE:
            audio_input_frame_size >>= 1;
            break;
        default:
            break;
        }
    } else {
        audio_input_frame_size = c->frame_size;
    }


    // init resampling
    src_nb_samples = c->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE ?10000 : c->frame_size;

	ret = av_samples_alloc_array_and_samples(&src_samples_data,
			&src_samples_linesize, c->channels, src_nb_samples, AV_SAMPLE_FMT_S16,0);
	if (ret < 0) {
		LOGE("Could not allocate source samples\n");
		return;
	}

       /* create resampler context */
       if (c->sample_fmt != AV_SAMPLE_FMT_S16) {
	   	LOGE("Could not be here\n");
		return;
	   	#if 0
           swr_ctx =  avresample_alloc_context();
           if (!swr_ctx) {
        	   LOGE("Could not allocate resampler context\n");
               return;
           }

           /* set options */
           av_opt_set_int       (swr_ctx, "in_channel_count",   c->channels,       0);
           av_opt_set_int       (swr_ctx, "in_channel_layout",   c->channel_layout,       0);
           av_opt_set_int       (swr_ctx, "in_sample_rate",     c->sample_rate,    0);
           av_opt_set_int		(swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
           av_opt_set_int       (swr_ctx, "out_channel_count",  c->channels,       0);
           av_opt_set_int       (swr_ctx, "out_sample_rate",    c->sample_rate,    0);
           av_opt_set_int       (swr_ctx, "out_channel_layout",   c->channel_layout,       0);
           av_opt_set_int		(swr_ctx, "out_sample_fmt",     c->sample_fmt, 0);//av_opt_set_sample_fmt


           /* initialize the resampling context */
           if ((ret =  avresample_open(swr_ctx)) < 0) {
        	   LOGE("Failed to initialize the resampling context\n");
               return;
           }
		   #endif
       }

       /* compute the number of converted samples: buffering is avoided
        * ensuring that the output buffer will contain at least all the
        * converted input samples */
       max_dst_nb_samples = src_nb_samples;
       ret = av_samples_alloc_array_and_samples(&dst_samples_data, &dst_samples_linesize, c->channels,
                                                max_dst_nb_samples, c->sample_fmt, 0);
       if (ret < 0) {
           LOGE("Could not allocate destination samples\n");
           return;
       }
       dst_samples_size = av_samples_get_buffer_size(NULL, c->channels, max_dst_nb_samples,
                                                     c->sample_fmt, 0);

}


AVStream *VideoRecorderImpl::add_video_stream(enum AVCodecID codec_id)
{
	AVCodecContext *c;
	AVStream *st;

	video_codec = avcodec_find_encoder(codec_id);

	if (!video_codec) {
		LOGE("could not found video_codec\n");
		return NULL;
	}

	st = avformat_new_stream(oc, video_codec);
	if (!st) {
		LOGE("could not alloc stream\n");
		return NULL;
	}
	st->id = oc->nb_streams-1;

	c = st->codec;
	av_opt_set(c->priv_data, "preset", "superfast", 0);
	av_opt_set(c->priv_data, "x264opts", "rc_lookahead=0", 0);
	c->codec_id = codec_id;
	c->codec_type = AVMEDIA_TYPE_VIDEO;

	/* put sample parameters */
	c->bit_rate = video_bitrate;
	c->width = video_width;
	c->height = video_height;
	c->time_base.num = 1;
	c->time_base.den = video_framerate;
	c->pix_fmt = PIX_FMT_YUV420P;		// we convert everything to PIX_FMT_YUV420P
	c->gop_size = 5*video_framerate;
	c->max_b_frames = 0;

	/* h264 specific stuff */
/*	c->coder_type = 0;	// coder = 0
	c->me_cmp |= 1;	// cmp=+chroma, where CHROMA = 1
	c->partitions |= X264_PART_I8X8 + X264_PART_I4X4 + X264_PART_P8X8 + X264_PART_B8X8; // partitions=+parti8x8+parti4x4+partp8x8+partb8x8
	c->me_method = ME_HEX;	// me_method=hex
	c->me_subpel_quality = 7;	// subq=7
	c->me_range = 16;	// me_range=16
	c->gop_size = 250;	// g=250
	c->keyint_min = 25; // keyint_min=25
	c->scenechange_threshold = 40;	// sc_threshold=40
	c->i_quant_factor = 0.71; // i_qfactor=0.71
	c->b_frame_strategy = 1;  // b_strategy=1
	c->qcompress = 0.6; // qcomp=0.6
	c->qmin = 10;	// qmin=10
	c->qmax = 51;	// qmax=51
	c->max_qdiff = 4;	// qdiff=4
	c->max_b_frames = 0;	// bf=0
	c->refs = 3;	// refs=3
	c->directpred = 1;	// directpred=1
	c->trellis = 1; // trellis=1
	c->weighted_p_pred = 2; // wpredp=2

	c->flags |= CODEC_FLAG_LOOP_FILTER + CODEC_FLAG_GLOBAL_HEADER;
	c->flags2 |= CODEC_FLAG2_BPYRAMID + CODEC_FLAG2_MIXED_REFS + CODEC_FLAG2_WPRED + CODEC_FLAG2_8X8DCT + CODEC_FLAG2_FASTPSKIP;	// flags2=+bpyramid+mixed_refs+wpred+dct8x8+fastpskip
	c->flags2 |= CODEC_FLAG2_8X8DCT;
	c->flags2 ^= CODEC_FLAG2_8X8DCT;*/
#if 0
	/*x264 ultrafast preset*/
	c->aq_mode = 0; // aq-mode = 0
	// b-adapt = 0
	c->max_b_frames = 0; // bframes = 0
	// no cabac
	// no deblock
	c->me_method = ME_HEX; // me = dia !!!
	c->partitions = 0; // partitions = none
	c->rc_lookahead = 0; // rc-lookahead = 0
	c->refs = 1; // ref = 1
	// scenecut = 0
	c->scenechange_threshold = 40;
	// subme = 0
	c->trellis = 0; // trellis = 0
	c->weighted_p_pred = 0; // weightp = 0 ??
	c->coder_type = 0;
	c->me_subpel_quality = 4;
	c->me_range = 16;
	c->gop_size = 250;
	c->keyint_min = 25;
	c->i_quant_factor = 0.71;
	c->b_frame_strategy = 0;
	c->qcompress = 0.6;
	c->qmin = 10;
	c->qmax = 51;
	c->max_qdiff = 4;
	c->directpred = 0;
	c->flags |= CODEC_FLAG_LOOP_FILTER + CODEC_FLAG_GLOBAL_HEADER;
	c->flags2 |= CODEC_FLAG2_8X8DCT; c->flags2 ^= CODEC_FLAG2_8X8DCT; // no 8x8dct
	c->flags2 |= CODEC_FLAG2_MIXED_REFS; c->flags2 ^= CODEC_FLAG2_MIXED_REFS; // no mixed refs
	c->flags2 |= CODEC_FLAG2_MBTREE; c->flags2 ^= CODEC_FLAG2_MBTREE; // no mbtree
	c->flags2 |= CODEC_FLAG2_WPRED; c->flags2 ^= CODEC_FLAG2_WPRED; // no weightb ??
#endif
	c->profile = FF_PROFILE_H264_BASELINE;
	//c->level = 30;

	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return st;
}

AVFrame *VideoRecorderImpl::alloc_picture(enum PixelFormat pix_fmt, int width, int height)
{
	AVFrame *pict;
	uint8_t *picture_buf;
	int size;

	pict = avcodec_alloc_frame();
	if (!pict) {
		LOGE("could not allocate picture frame\n");
		return NULL;
	}
	
	size = avpicture_get_size(pix_fmt, width, height);
	picture_buf = (uint8_t *)av_malloc(size);
	if (!picture_buf) {
		av_free(pict);
		LOGE("could not allocate picture frame buf\n");
		return NULL;
	}
	avpicture_fill((AVPicture *)pict, picture_buf,
				   pix_fmt, width, height);
	return pict;
}

void VideoRecorderImpl::open_video()
{
	AVCodecContext *c;
	int ret;
	timestamp_base = 0;
	
	if(!video_st) {
		LOGE("tried to open_video without a valid video_st (add_video_stream must have failed)\n");
		return;
	}
	
	c = video_st->codec;

	if (avcodec_open2(c, video_codec, NULL) < 0) {
		LOGE("could not open codec\n");
		return;
	}
#if 0
	video_outbuf = NULL;
	if (!(oc->oformat->flags & AVFMT_RAWPICTURE)) {
		video_outbuf_size = c->width * c->height * 8; // We assume the encoded frame will be smaller in size than an equivalent raw frame in RGBA8888 format ... a pretty safe assumption!
		video_outbuf = (uint8_t *)av_malloc(video_outbuf_size);
		if(!video_outbuf) {
			LOGE("could not allocate video_outbuf\n");
			return;
		}
	}
#endif
	ret = avpicture_alloc(&dst_picture, c->pix_fmt, c->width, c->height);
	if (ret < 0) {
		LOGE("Could not allocate picture\n");
		return;
	}

	tmp_picture = avcodec_alloc_frame();
	if(!tmp_picture) {
		LOGE("Could not allocate temporary picture\n");
		return;
	}

	*((AVPicture *)tmp_picture) = dst_picture;
	
	if (video_pixfmt != AV_PIX_FMT_YUV420P) {
		ret = avpicture_alloc(&src_picture, video_pixfmt, c->width, c->height);
        if (ret < 0) {
            LOGE("Could not initialize sws context\n");
			return;
        }
		img_convert_ctx = sws_getContext(video_width, video_height, video_pixfmt, c->width, c->height, AV_PIX_FMT_YUV420P, /*SWS_BICUBIC*/SWS_FAST_BILINEAR, NULL, NULL, NULL);
		if(img_convert_ctx==NULL) {
			LOGE("Could not initialize sws context\n");
			return;
		}
	}

	tmp_picture->pts = 0;
}

bool VideoRecorderImpl::Close()
{
	AVPacket pkt;
	int ret, got_output;
	AVCodecContext *c;
	LOGE("closeing %d\n",__LINE__);
	if(video_st)
	{
		av_init_packet(&pkt);
		pkt.data = NULL;	// packet data will be allocated by the encoder
		pkt.size = 0;
		c = video_st->codec;
		for (got_output = 1; got_output; ) {
	        ret = avcodec_encode_audio2(c, &pkt, NULL, &got_output);
	        if (ret < 0) {
	            LOGE("Error encoding frame\n");
	            return false;
	        }

	        if (got_output) {
				pkt.pts = av_rescale_q(pkt.pts, video_st->codec->time_base, video_st->time_base);
				pkt.dts = av_rescale_q(pkt.dts, video_st->codec->time_base, video_st->time_base);
			
	            pkt.stream_index = video_st->index;
            	/* Write the compressed frame to the media file. */
            	ret = av_interleaved_write_frame(oc, &pkt);
	            av_free_packet(&pkt);
	        }
	    }
	}
	LOGE("closeing %d\n",__LINE__);
	if(audio_st)
	{
		av_init_packet(&pkt);
		pkt.data = NULL;	// packet data will be allocated by the encoder
		pkt.size = 0;
		c = audio_st->codec;
		for (got_output = 1; got_output; ) {
	        ret = avcodec_encode_audio2(c, &pkt, NULL, &got_output);
	        if (ret < 0) {
	            LOGE("Error encoding frame\n");
	            return false;
	        }

	        if (got_output) {
				pkt.pts = av_rescale_q(pkt.pts, audio_st->codec->time_base, audio_st->time_base);
				pkt.dts = av_rescale_q(pkt.dts, audio_st->codec->time_base, audio_st->time_base);
			
	            pkt.stream_index = audio_st->index;
            	/* Write the compressed frame to the media file. */
            	ret = av_interleaved_write_frame(oc, &pkt);
	            av_free_packet(&pkt);
	        }
	    }
	}
	LOGE("closeing %d\n",__LINE__);
	if(oc) {
		av_write_trailer(oc);
	}
	LOGE("closeing %d\n",__LINE__);
	
	if(video_st){
		if(video_st->codec->pix_fmt != AV_PIX_FMT_YUV420P )
		{
			av_free(src_picture.data[0]);
		}
		LOGE("closeing %d\n",__LINE__);
  
		avcodec_close(video_st->codec);
	}
	LOGE("closeing %d\n",__LINE__);

	  av_free(dst_picture.data[0]);
	LOGE("closeing %d\n",__LINE__);
	avcodec_free_frame(&tmp_picture);
	LOGE("closeing %d\n",__LINE__);
	
	if(img_convert_ctx) {
		sws_freeContext(img_convert_ctx);
	}
	LOGE("closeing %d\n",__LINE__);
#if 0	
	if(video_outbuf)
		av_free(video_outbuf);
#endif	
	if(audio_st)
		avcodec_close(audio_st->codec);
	LOGE("closeing %d\n",__LINE__);	
	if(samples)
		av_free(samples);
	LOGE("closeing %d\n",__LINE__);
#if 0		
	if(audio_outbuf)
		av_free(audio_outbuf);
#endif
	//if (!(oformat->flags & AVFMT_NOFILE))
	//		/* Close the output file. */
	//		avio_close(oc->pb);
	LOGE("closeing %d\n",__LINE__);

	avformat_free_context(oc);
	LOGE("closeing %d\n",__LINE__);

	return true;
}

bool VideoRecorderImpl::SetVideoOptions(VideoFrameFormat fmt, int width, int height, unsigned long bitrate, int framerate)
{
	switch(fmt) {
		case VideoFrameFormatYUV420P: video_pixfmt=AV_PIX_FMT_YUV420P; break;
		case VideoFrameFormatNV12: video_pixfmt=AV_PIX_FMT_NV12; break;
		case VideoFrameFormatNV21: video_pixfmt=AV_PIX_FMT_NV21; break;
		case VideoFrameFormatRGB24: video_pixfmt=AV_PIX_FMT_RGB24; break;
		case VideoFrameFormatBGR24: video_pixfmt=AV_PIX_FMT_BGR24; break;
		case VideoFrameFormatARGB: video_pixfmt=AV_PIX_FMT_ARGB; break;
		case VideoFrameFormatRGBA: video_pixfmt=AV_PIX_FMT_RGBA; break;
		case VideoFrameFormatABGR: video_pixfmt=AV_PIX_FMT_ABGR; break;
		case VideoFrameFormatBGRA: video_pixfmt=AV_PIX_FMT_BGRA; break;
		case VideoFrameFormatRGB565LE: video_pixfmt=AV_PIX_FMT_RGB565LE; break;
		case VideoFrameFormatRGB565BE: video_pixfmt=AV_PIX_FMT_RGB565BE; break;
		case VideoFrameFormatBGR565LE: video_pixfmt=AV_PIX_FMT_BGR565LE; break;
		case VideoFrameFormatBGR565BE: video_pixfmt=AV_PIX_FMT_BGR565BE; break;
		default: LOGE("Unknown frame format passed to SetVideoOptions!\n"); return false;
	}
	video_width = width;
	video_height = height;
	video_bitrate = bitrate;
	video_framerate = framerate;
	return true;
}

bool VideoRecorderImpl::SetAudioOptions(AudioSampleFormat fmt, int channels, unsigned long samplerate, unsigned long bitrate)
{
	switch(fmt) {
		case AudioSampleFormatU8: audio_sample_format=AV_SAMPLE_FMT_U8; audio_sample_size=1; break;
		case AudioSampleFormatS16: audio_sample_format=AV_SAMPLE_FMT_S16; audio_sample_size=2; break;
		case AudioSampleFormatS32: audio_sample_format=AV_SAMPLE_FMT_S32; audio_sample_size=4; break;
		case AudioSampleFormatFLT: audio_sample_format=AV_SAMPLE_FMT_FLT; audio_sample_size=4; break;
		case AudioSampleFormatDBL: audio_sample_format=AV_SAMPLE_FMT_DBL; audio_sample_size=8; break;
		default: LOGE("Unknown sample format passed to SetAudioOptions!\n"); return false;
	}
	audio_channels = channels;
	audio_bit_rate = bitrate;
	audio_sample_rate = samplerate;
	return true;
}

bool VideoRecorderImpl::Start()
{
	return true;
}
#if 0
void VideoRecorderImpl::SupplyAudioSamples(const void *sampleData, unsigned long len)
{
	// check whether there is any audio stream (hasAudio=true)
	if(audio_st == NULL) {
		LOGE("tried to supply an audio frame when no audio stream was present\n");
		return;
	}
	//AVFrame *frame = avcodec_alloc_frame();
	AVCodecContext *c = audio_st->codec;
	int got_packet, ret;
	unsigned long  numSamples, dst_nb_samples;

	numSamples = len / (audio_sample_size * audio_channels);
	LOG("SupplyAudioSamples len:%ld numSamples:%ld\n", len,numSamples);
	

	uint8_t *samplePtr = (uint8_t *)sampleData;		// using a byte pointer
	
	// numSamples is supplied by the codec.. should be c->frame_size (1024 for AAC)
	// if it's more we go through it c->frame_size samples at a time
	while(numSamples) {
		static AVPacket pkt;
		LOG("SupplyAudioSamples numSamples:%ld left:%ld\n", numSamples, audio_input_leftover_samples);
		// if we have enough samples for a frame, we write out c->frame_size number of samples (ie: one frame) to the output context
		if( (numSamples + audio_input_leftover_samples) >= c->frame_size) {
			// audio_input_leftover_samples contains the number of samples already in our "samples" array, left over from last time
			// we copy the remaining samples to fill up the frame to the complete frame size
			int num_new_samples = c->frame_size - audio_input_leftover_samples;
			LOG("SupplyAudioSamples num_new_samples:%d \n",num_new_samples);
			memcpy((uint8_t *)samples + (audio_input_leftover_samples * audio_sample_size * audio_channels), samplePtr, num_new_samples * audio_sample_size * audio_channels);
			numSamples -= num_new_samples;
			samplePtr += (num_new_samples * audio_sample_size * audio_channels);
			audio_input_leftover_samples = 0;

			frame->nb_samples = c->frame_size;
			frame->format = c->sample_fmt;  
		    frame->channels = c->channels;  
		    frame->channel_layout = c->channel_layout;  
		    LOG("frame:%d\n", frame->nb_samples);
			avcodec_fill_audio_frame(frame, c->channels, c->sample_fmt,
										 (uint8_t*)samples, audio_outbuf_size, 0);
			LOG("frame:%d\n", frame->nb_samples);

			av_init_packet(&pkt);	// need to init packet every time so all the values (such as pts) are re-initialized
			pkt.data = NULL;	// packet data will be allocated by the encoder
			pkt.size = 0;				

			ret = avcodec_encode_audio2(c, &pkt, frame, &got_packet);
		    if (ret < 0) {
		        LOGE("Error avcodec_encode_audio2 audio frame\n");
				//avcodec_free_frame(&frame);
				return;
		    }
			LOG("got_packet:%d size:%d\n", got_packet, pkt.size);

			if(got_packet)
			{
				pkt.pts = av_rescale_q(pkt.pts, audio_st->codec->time_base, audio_st->time_base);
				pkt.dts = av_rescale_q(pkt.dts, audio_st->codec->time_base, audio_st->time_base);
			
				pkt.stream_index = audio_st->index;
				LOG("write  got_packet:%d size:%d\n", got_packet, pkt.size);
				ret = av_interleaved_write_frame(oc, &pkt);
			    if (ret != 0) {
			        LOGE("Error while writing audio frame\n");
					//avcodec_free_frame(&frame);
					return;
			    }
				LOG("free  got_packet:%d size:%d\n", got_packet, pkt.size);
				av_free_packet(&pkt);
			}
			LOG("over  got_packet:%d size:%d\n", got_packet, pkt.size);
			
		}
		else {
			// if we didn't have enough samples for a frame, we copy over however many we had and update audio_input_leftover_samples
			int num_new_samples = c->frame_size - audio_input_leftover_samples;
			if(numSamples < num_new_samples)
				num_new_samples = numSamples;
				
			memcpy((uint8_t *)samples + (audio_input_leftover_samples * audio_sample_size * audio_channels), samplePtr, num_new_samples * audio_sample_size * audio_channels);
			numSamples -= num_new_samples;
			samplePtr += (num_new_samples * audio_sample_size * audio_channels);
			audio_input_leftover_samples += num_new_samples;
			LOG("SupplyAudioSamples num_new_samples2:%d \n",num_new_samples);
		}
	}
	//avcodec_free_frame(&frame);
	return;
}
#endif
void VideoRecorderImpl::SupplyAudioSamples(const void *sampleData, unsigned long len)
{
	int got_packet, ret, dst_nb_samples;

	AVStream *st = audio_st;
	AVPacket pkt;
	AVCodecContext *c = st->codec;

	AVFrame *audioframe = av_frame_alloc();
	audioframe->nb_samples = c->frame_size;
	audioframe->format = c->sample_fmt;
	audioframe->channel_layout = c->channel_layout;

	int16_t* bufferShortPtr = (int16_t *)sampleData;
	uint8_t * bufferPtr = (uint8_t *) bufferShortPtr;

	int buffer_size = av_samples_get_buffer_size(NULL, c->channels,
			c->frame_size, c->sample_fmt, 0);

	memcpy((int16_t *) src_samples_data[0], bufferShortPtr, len * 2);

	/* convert samples from native format to destination codec format, using the resampler */
#if 0
	if (swr_ctx) {
		LOGE("can't be here");
		return;

		/* compute destination number of samples */
		dst_nb_samples = av_rescale_rnd(
				avresample_get_delay(swr_ctx) + src_nb_samples,
				c->sample_rate, c->sample_rate, AV_ROUND_UP);
		if (dst_nb_samples > max_dst_nb_samples) {
			av_free(dst_samples_data[0]);
			ret = av_samples_alloc(dst_samples_data, &dst_samples_linesize,
					c->channels, dst_nb_samples, c->sample_fmt, 0);
			if (ret < 0)
				return;
			max_dst_nb_samples = dst_nb_samples;
			dst_samples_size = av_samples_get_buffer_size(NULL, c->channels,
					dst_nb_samples, c->sample_fmt, 0);
		}

		/* convert to destination format */
		ret = avresample_convert(swr_ctx, dst_samples_data, 0,
				dst_nb_samples, (uint8_t **) src_samples_data, 0,
				src_nb_samples);

		if (ret < 0) {
			LOGE("Error while converting\n");
			return;
		}

	} else
    #endif
	    {
		dst_samples_data[0] = src_samples_data[0];
		dst_nb_samples = src_nb_samples;
	}
	framecount+= dst_nb_samples;
	audioframe->pts = framecount;

	audioframe->nb_samples = dst_nb_samples;
	/* setup the data pointers in the AVFrame */
	ret = avcodec_fill_audio_frame(audioframe, c->channels, c->sample_fmt,
			dst_samples_data[0], dst_samples_size, 0);

	av_init_packet(&pkt);
	pkt.data = NULL; // packet data will be allocated by the encoder
	pkt.size = 0;

	if (avcodec_encode_audio2(c, &pkt, audioframe, &got_packet) < 0) {
		LOGE("Error encoding audio frame");
		return;;
	}
	//LOG("audio audioframe->pts:%lld count:%lld pkt.pts:%lld", audioframe->pts, framecount,  pkt.pts);

	if (got_packet) {
		pkt.stream_index = st->index;
		if (pkt.pts != AV_NOPTS_VALUE)
			pkt.pts = av_rescale_q(pkt.pts, st->codec->time_base,st->time_base);
		if (pkt.dts != AV_NOPTS_VALUE)
			pkt.dts = av_rescale_q(pkt.dts, st->codec->time_base,st->time_base);
		//LOG("audio pkt.pts:%lld pkt.dts:%lld", pkt.pts, pkt.dts);

		/* Write the compressed frame to the media file. */
		pthread_mutex_lock(&mtx);
		ret = av_interleaved_write_frame(oc, &pkt);
		pthread_mutex_unlock(&mtx); 
		av_free_packet(&pkt);
	}else{
		//LOG("Audio Frame Buffered");
	}
	//	av_freep(&samples);
	av_frame_free(&audioframe);
}

void VideoRecorderImpl::SupplyVideoFrame(const void *frameData, unsigned long numBytes, long long timestamp)
{
	static AVPacket pkt;
	int ret, got_packet = 0;
	long long tempPts;
		
	if(!video_st) {
		LOGE("tried to SupplyVideoFrame when no video stream was present\n");
		return;
	}
	tempPts = timestamp*video_framerate/1000000;
	if(tmp_picture->pts >=tempPts){
		LOGE("SupplyVideoFrame pts old\n");
		return;
	}

	//LOG("SupplyVideoFrame len:%ld\n", numBytes);

	AVCodecContext *c = video_st->codec;
	
	if(video_pixfmt != AV_PIX_FMT_YUV420P) {
        avpicture_fill(&src_picture, (uint8_t *)frameData, video_pixfmt, c->width, c->height);
		sws_scale(img_convert_ctx, (const uint8_t * const *)src_picture.data, src_picture.linesize, 0, c->height, dst_picture.data, dst_picture.linesize);
	}else
	{
        avpicture_fill((AVPicture *)tmp_picture, (uint8_t *)frameData, video_pixfmt, c->width, c->height);
        //picture.linesize(0, step);
    }

	if (oc->oformat->flags & AVFMT_RAWPICTURE) {
        /* Raw video case - directly store the picture in the packet */
        av_init_packet(&pkt);

        pkt.flags        |= AV_PKT_FLAG_KEY;
        pkt.stream_index  = video_st->index;
        pkt.data          = dst_picture.data[0];
        pkt.size          = sizeof(AVPicture);

        ret = av_interleaved_write_frame(oc, &pkt);
    } else {
        av_init_packet(&pkt);
		pkt.data = NULL;	// packet data will be allocated by the encoder
		pkt.size = 0;
		
        /* encode the image */
        ret = avcodec_encode_video2(c, &pkt, tmp_picture, &got_packet);
        if (ret < 0) {
            LOGE("avcodec_encode_video2 error %d\n", ret);
			return;
        }
		/* If size is zero, it means the image was buffered. */
        //LOG("avcodec_encode_video2 ret:%d got_packet:%d size:%d pts:%lld tempPts:%lld\n", ret, got_packet, pkt.size, tmp_picture->pts, tempPts);
		tmp_picture->pts++;
		if (got_packet) {
			if (pkt.pts != AV_NOPTS_VALUE) {
				pkt.pts = av_rescale_q(tempPts, video_st->codec->time_base, video_st->time_base);
			}
			if (pkt.dts != AV_NOPTS_VALUE) {
				pkt.dts = av_rescale_q(tempPts, video_st->codec->time_base, video_st->time_base);
			}
			//pkt.pts = av_rescale_q(c->coded_frame->pts, c->time_base, video_st->time_base);
			//LOG("video pkt.pts:%lld pkt.dts:%lld", pkt.pts, pkt.dts);
			
            pkt.stream_index = video_st->index;

            /* Write the compressed frame to the media file. */
			pthread_mutex_lock(&mtx);
            ret = av_interleaved_write_frame(oc, &pkt);
			pthread_mutex_unlock(&mtx); 
			av_free_packet(&pkt);
        } else {
            ret = 0;
        }
    }
	
	return;
}

VideoRecorder* VideoRecorder::New()
{
	return (VideoRecorder*)(new VideoRecorderImpl);
}

} // namespace AVR

#ifdef TESTING

float t = 0;
float tincr = 2 * M_PI * 110.0 / 44100;
float tincr2 = 2 * M_PI * 110.0 / 44100 / 44100;

void fill_audio_frame(int16_t *samples, int frame_size, int nb_channels)
{
	int j, i, v;
	int16_t *q;

	q = samples;
	for (j = 0; j < frame_size; j++) {
		v = (int)(sin(t) * 10000);
		for(i = 0; i < nb_channels; i++)
			*q++ = v;
		t += tincr;
		tincr += tincr2;
	}
}

void fill_yuv_image(AVFrame *pict, int frame_index, int width, int height)
{
	int x, y, i;

	i = frame_index;

	/* Y */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;
		}
	}

	/* Cb and Cr */
	for (y = 0; y < height/2; y++) {
		for (x = 0; x < width/2; x++) {
			pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
			pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
		}
	}
}

#define RGB565(r,g,b) (uint16_t)( ((red & 0x1F) << 11) | ((green & 0x3F) << 5) | (blue & 0x1F) ) 

void fill_rgb_image(uint8_t *pixels, int i, int width, int height)
{	
	int x, y;
	
	for(y = 0; y < height; y++) {
		for(x = 0; x < width; x++) {
			
			uint8_t red = x + y + i * 3;
			uint8_t green = x + y + i * 3;
			uint8_t blue = x + y + i * 3;

			uint16_t pixel = RGB565(red, green, blue);

			// assume linesize is width*2
			pixels[y * (width*2) + x*2 + 0] = (uint8_t)(pixel);		// lower order bits
			pixels[y * (width*2) + x*2 + 1] = (uint8_t)(pixel >> 8);	// higher order bits
		}
	}
}

#include <iostream>

int main()
{
	AVR::VideoRecorder *recorder = new AVR::VideoRecorderImpl();

	recorder->SetAudioOptions(AVR::AudioSampleFormatS16, 2, 44100, 64000);
	recorder->SetVideoOptions(AVR::VideoFrameFormatRGB565LE, 640, 480, 400000);
	recorder->Open("testing.mp4", NULL, true, true);

	int16_t *sound_buffer = new int16_t[2048 * 2];
	uint8_t *video_buffer = new uint8_t[640 * 480 * 2];
	for(int i = 0; i < 200; i++) {
		fill_audio_frame(sound_buffer, 900, 2);
		recorder->SupplyAudioSamples(sound_buffer, 900);

		fill_rgb_image(video_buffer, i, 640, 480);
		recorder->SupplyVideoFrame(video_buffer, 640*480*2, (25 * i)+1);
	}
	
	delete video_buffer;
	delete sound_buffer;

	recorder->Close();

	std::cout << "Done" << std::endl;

	delete recorder;
	
	return 0;
}

#endif /* TESTING */
