#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#ifndef UINT64_C
#define UINT64_C(value)__CONCAT(value,ULL)
#endif

#ifndef INT64_MIN
#define INT64_MIN  (-9223372036854775807LL - 1)
#endif

#ifndef INT64_MAX
#define INT64_MAX	9223372036854775807LL
#endif
#include "CommonTools.h"
#include <list>
#include <vector>
using namespace std;

extern "C" {
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
#include "libavutil/dict.h"
#include "libswresample/swresample.h"
#include "libavutil/samplefmt.h"
#include "libavutil/timestamp.h"
#include "libavformat/avformat.h"
}

#include "./decoder_request_header.h"
#include "../common/circle_texture_queue.h"
#include "opengl_media/texture/texture_frame.h"
#include "opengl_media/texture/yuv_texture_frame.h"
#include "opengl_media/movie_frame.h"
#include "../texture_uploader/texture_frame_uploader.h"
#include "../texture_uploader/yuv_texture_frame_uploader.h"

#ifndef SUBSCRIBE_VIDEO_DATA_TIME_OUT
#define SUBSCRIBE_VIDEO_DATA_TIME_OUT 20 * 1000
#endif

#ifndef DECODE_PROBESIZE_DEFAULT_VALUE
#define DECODE_PROBESIZE_DEFAULT_VALUE 50 * 1024
#endif

typedef struct {
	long long beginOpen;				// 开始试图去打开一个直播流
	float successOpen;				// 成功打开流
	float firstScreenTimeMills;		// 首屏时间
	float failOpen;					// 流打开失败
	int failOpenType;				// 流打开失败类型
	float duration;
	vector<float> retryOpen;		// 重试
	vector<float> videoQueueFull;	// 解码缓冲区满
	vector<float> videoQueueEmpty;	// 解码缓冲区空
//	vector<float> videoLoading;		// 加载中，正在缓冲
//	vector<float> videoContinue;	// 缓冲结束，开始播放
//	float stopPlay;					// 结束
} BuriedPoint;

#define DECODER_HEADER_FORCE_ROTATE			"header_force_rotate"
#define DECODER_HEADER_FORCE_FPS			"header_force_fps"

/**
 * 视频解码
 */
class VideoDecoder {
protected:
	int subscribeTimeOutTimeMills;

	DecoderRequestHeader *requestHeader;
	float seek_seconds;
	int64_t audio_stream_duration;

	TextureFrameUploader* textureFrameUploader;
	pthread_mutex_t mLock;
	pthread_cond_t mCondition;

	UploaderCallback * mUploaderCallback;

	//保存需要读入的文件的格式信息，比如流的个数以及流数据等
	AVFormatContext *pFormatCtx;
    int openInput();
    bool hasAllCodecParameters();
    virtual int openFormatInput(char *videoSourceURI);
	virtual void initFFMpegContext();
	virtual bool isNeedRetry();
	virtual int initAnalyzeDurationAndProbesize(int* max_analyze_durations, int analyzeDurationSize, int probesize, bool fpsProbeSizeConfigured);

	bool isNetworkPath(char *videoSourceURI) {
		bool result = false;
		int length = 0;
		char* path = videoSourceURI;
		char c = ':';
		while (*path != '\0' && *path != c) {
			length++;
			++path;
		}
		if (*path == c) {
			char* scheme = new char[length];
			char* schemeTmp = scheme;
			while (length--) {
				*(schemeTmp++) = *(videoSourceURI++);
			}
			*(schemeTmp++) = '\0';
			if (!strcmp(scheme, "rtmp") || !strcmp(scheme, "RTMP")) {
				result = true;
			}
			delete[] scheme;
		}
		return result;
	};

public:
	//才开始连接的重试次数
	int connectionRetry = 0;

	/** 如果使用了快进或者快退命令，则先设置以下参数 **/
	bool seek_req;
	bool seek_resp;
//	FILE* audioFile;
	bool isSubscribe;
	bool isOpenInputSuccess;
	/** 超时的设置 **/
	AVIOInterruptCB int_cb;
	long long readLatestFrameTimemills;
	bool isTimeout;

	/** 总的解码变量 **/
	bool is_Network;
	bool is_eof;
    float position;

	/** 视频流解码变量 **/
	//保存了相应流的详细编码信息，比如视频的宽、高，编码类型等
	AVCodecContext *videoCodecCtx;
	//真正的编解码器，其中有编解码需要调用的函数
	AVCodec *videoCodec;
	AVFrame *videoFrame;
	int width;
	int height;
	int degress;
	float fps;
	float videoTimeBase;
	std::list<int>* videoStreams;
	int videoStreamIndex;

	/** 音频流解码变量 **/
	AVCodecContext *audioCodecCtx;
	AVCodec *audioCodec;
	AVFrame *audioFrame;
	std::list<int>* audioStreams;
	int audioStreamIndex;
	float audioTimeBase;
	SwrContext *swrContext;
	void *swrBuffer;
	int swrBufferSize;

	/** 字幕流解码变量 **/
	std::list<int>* subtitleStreams;
	int subtitleStreamIndex;
	int artworkStreamIndex;

	int openVideoStream();
	int openVideoStream(int streamIndex);
	int openAudioStream();
	int openAudioStream(int streamIndex);
	//判断codecContext的sampleFmt是否是AV_SAMPLE_FMT_S16的
	bool audioCodecIsSupported(AVCodecContext *audioCodecCtx);
	std::list<int> *collectStreams(enum AVMediaType codecType);
	void avStreamFPSTimeBase(AVStream *st, float defaultTimeBase, float *pFPS, float *pTimeBase);

	VideoFrame * handleVideoFrame();
	void copyFrameData(uint8_t * dst, uint8_t * src, int width, int height, int linesize);
	AudioFrame * handleAudioFrame();

	void closeSubtitleStream();
	void closeAudioStream();
public:
	VideoDecoder();

	VideoDecoder(JavaVM *g_jvm, jobject obj);

	/** 开启解码线程 **/
	virtual ~VideoDecoder();
	void uploadTexture();
	void signalDecodeThread();
	/** 打开网络文件需要传递 探针的参数以及重试策略 **/
	virtual int openFile(DecoderRequestHeader *requestHeader);
	/** 如果打开视频流成功，那么启动decoder的uploader部分 **/
	void startUploader(UploaderCallback * pUploaderCallback);
	/** 解码足够长度的视频和音频frame **/
	std::list<MovieFrame*>* decodeFrames(float minDuration);
	std::list<MovieFrame*>* decodeFrames(float minDuration, int* decodeVideoErrorState);
	//仅仅解码一帧视频帧
	bool decodeVideoTexIdByPosition(float position);
	bool hasSeekReq(){
		return seek_req;
	};
	bool hasSeekResp(){
		return seek_resp;
	};

	EGLContext getTextureFrameUploaderEGLContext(){
		if(NULL != textureFrameUploader){
			return textureFrameUploader->getEGLContext();
		}
		return NULL;
	};

	void setSeekReq(bool seekReqParam){
		seek_req = seekReqParam;
		if(seek_req){
			seek_resp = false;
		}
	};
	bool isOpenInput(){
		return isOpenInputSuccess;
	};
	void stopSubscribe(){
		subscribeTimeOutTimeMills = -1;
		isSubscribe = false;
	};
	bool isSubscribed(){
		return isSubscribe;
	};

	/** 关于超时的设置 **/
	static int interrupt_cb(void *ctx);
	int detectInterrupted();

	inline void interrupt(){
		subscribeTimeOutTimeMills = -1;
	}

	bool isEOF(){
//		return is_eof;
		return isVideoOutputEOF;
	};
	virtual bool isNetwork(){
		is_Network = false;
		return is_Network;
	};
	bool validAudio(){
	    return audioStreamIndex != -1;
	};
	bool validVideo(){
	    return videoStreamIndex != -1;
	};
	bool validSubtitles(){
	    return subtitleStreamIndex != -1;
	};

	/** 设置到播放到什么位置，单位是秒，但是后边3位小数，其实是精确到毫秒 **/
	void setPosition(float seconds);
	/** 关闭文件 **/
	void closeFile();
	/** 当前解码到什么位置了，单位是秒，但是后边3位小数，其实是精确到毫秒 **/
	float getPosition(){
		return position;
	};

	/** 当前源的大小，单位是秒，但是后边3位小数，其实是精确到毫秒 **/
	float getDuration(){
		if(!pFormatCtx){
			return 0;
		}
	    if (pFormatCtx->duration == AV_NOPTS_VALUE){
	        return -1;
	    }
	    return (float)pFormatCtx->duration / AV_TIME_BASE;
	};

	/** 获得视频帧的宽度 **/
	int getVideoRotateHint(){
		return degress;
	};
	int getVideoFrameWidth(){
		if(videoCodecCtx){
			return videoCodecCtx->width;
		}
		return -1;
	};
	float getVideoFPS(){
		return fps;
	};
	/** 获得视频帧的高度 **/
	int getVideoFrameHeight(){
		if(videoCodecCtx){
			return videoCodecCtx->height;
		}
		return -1;
	};

	/** 获得Audio信道的声道数 **/
	int getAudioChannels(){
		if(audioCodecCtx){
			return audioCodecCtx->channels;
		}
		return -1;
	};

	/** 获得Audio信道的采样率 **/
	int getAudioSampleRate(){
		if(audioCodecCtx){
			return audioCodecCtx->sample_rate;
		}
		return -1;
	};

	/** 解码音频 **/
	bool decodeAudioFrames(AVPacket* packet, std::list<MovieFrame*> * result, float& decodedDuration, float minDuration, int* decodeVideoErrorState);

	/** flush音频 **/
	void flushAudioFrames(AVPacket* audioPacket, std::list<MovieFrame*> * result, float minDuration, int* decodeVideoErrorState);

protected:
	virtual TextureFrameUploader* createTextureFrameUploader() = 0;
	virtual void seek_frame();
	virtual bool decodeVideoFrame(AVPacket packet, int* decodeVideoErrorState) = 0;
	virtual void flushVideoFrames(AVPacket packet, int* decodeVideoErrorState) = 0;
	virtual void closeVideoStream();

public:
	virtual float updateTexImage(TextureFrame* textureFrame) = 0;

public:
	JavaVM* g_jvm;
	jobject obj;
	bool isVideoOutputEOF;
	bool isAudioOutputEOF;
};

#endif //VIDEO_DECODER_H

