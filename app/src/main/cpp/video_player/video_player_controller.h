#ifndef VIDEO_PLAYER_CONTROLLER_H
#define VIDEO_PLAYER_CONTROLLER_H

#include "CommonTools.h"
#include <queue>
#include <unistd.h>
#include <pthread.h>
#include <android/native_window.h>
#include "./sync/av_synchronizer.h"
#include "./audio_output.h"
#include "opengl_media/render/video_gl_surface_render.h"
#include "./video_output.h"
#include "egl_core/egl_share_context.h"

/**
 * Video Player Controller
 */
class VideoPlayerController {
public:
	VideoPlayerController();
	virtual ~VideoPlayerController();

	/** 初始化播放器 **/
	bool init(char *srcFilenameParam, JavaVM *g_jvm, jobject obj, int* max_analyze_duration, int analyzeCnt, int probesize, bool fpsProbeSizeConfigured, float minBufferedDuration, float maxBufferedDuration);

	/** 继续播放 **/
	void play();
	/** seek到某个位置 **/
	void seekToPosition(float position);

	/** 暂停播放 **/
	void pause();
	/** 销毁播放器 **/
	virtual void destroy();
	/** 以下是对视频的操作参数，单位都是秒 但是后边保留三位小数相当于 精度到毫秒 **/
	//获得总时长
	float getDuration();
	int getVideoFrameWidth();
	int getVideoFrameHeight();
	//获得缓冲进度
	float getBufferedProgress();
	//获得播放进度
	float getPlayProgress();
	/** 重置播放区域的大小,比如横屏或者根据视频的ratio来调整 **/
	void resetRenderSize(int left, int top, int width, int height);
	int getScreenWidth(){
		if(NULL != videoOutput){
			return videoOutput->getScreenWidth();
		}
		return 0;
	};
	int getScreenHeight(){
		if(NULL != videoOutput){
			return videoOutput->getScreenHeight();
		}
		return 0;
	};
	/** 关键的回调方法 **/
	//当音频播放器播放完毕一段buffer之后，会回调这个方法，这个方法要做的就是用数据将这个buffer再填充起来
	static int audioCallbackFillData(byte* buffer, size_t bufferSize, void* ctx);
	int consumeAudioFrames(byte* outData, size_t bufferSize);
	//当视频播放器接受到要播放视频的时候，会回调这个方法，这个方法
	static int videoCallbackGetTex(FrameTexture** frameTex, void* ctx, bool forceGetFrame);
	virtual int getCorrectRenderTexture(FrameTexture** frameTex, bool forceGetFrame);
	/** 当output初始化结束之后调用 **/
	static void outputOnInitialized(EGLContext eglContext, void* ctx);
	virtual bool startAVSynchronizer();

	void onSurfaceCreated(ANativeWindow* window, int widht, int height);
	void onSurfaceDestroyed();

	void signalOutputFrameAvailable();

	EGLContext getUploaderEGLContext();

protected:
	ANativeWindow* window;
	int screenWidth;
	int screenHeight;

	EGLContext mSharedEGLContext;

	/** 整个movie是否在播放 **/
	bool isPlaying;
	/** 保存临时参数在新的线程中启动 **/
	DecoderRequestHeader* requestHeader;
	float minBufferedDuration;
	float maxBufferedDuration;

	/** 用于回调Java层 **/
	JavaVM *g_jvm;
	jobject obj;
	/** 当初始化完毕之后 回调给客户端 **/
	void setInitializedStatus(bool initCode);

	/** 3个最主要的成员变量 **/
	AVSynchronizer* synchronizer;
	VideoOutput* videoOutput;
	AudioOutput* audioOutput;
	bool initAudioOutput();
	virtual int getAudioChannels();

	int getAudioSampleRate(){
		if(synchronizer){
			return synchronizer->getAudioSampleRate();
		}
		return -1;
	};

	virtual bool initAVSynchronizer();

	bool userCancelled; //用户取消拉流

	pthread_t initThreadThreadId;

	static void* initThreadCallback(void *myself);

	static void* initVideoOutputThreadCallback(void *myself);
	void initVideoOutput(ANativeWindow* window);
};
#endif //VIDEO_PLAYER_CONTROLLER_H
