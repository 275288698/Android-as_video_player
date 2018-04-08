#ifndef VIDEO_PLAYER_AV_SYNCHRONIZER_
#define VIDEO_PLAYER_AV_SYNCHRONIZER_

#include "CommonTools.h"
#include "../decoder/video_decoder.h"
#include "../decoder/ffmpeg_video_decoder.h"
#include "opengl_media/render/video_gl_surface_render.h"
#include "../decoder/mediacodec_video_decoder.h"
#include <queue>
#include <list>
#include <string>
using namespace std;

// 以下是解码本地文件的时候的缓冲区的最小和最大值
#define LOCAL_MIN_BUFFERED_DURATION   			0.5
#define LOCAL_MAX_BUFFERED_DURATION   			0.8
#define LOCAL_AV_SYNC_MAX_TIME_DIFF         		0.05

#define FIRST_BUFFER_DURATION         			0.5
#define DEFAULT_AUDIO_BUFFER_DURATION_IN_SECS	0.03
//#define DEFAULT_AUDIO_BUFFER_DURATION_IN_SECS	0.05

#define SEEK_REQUEST_LIST_MAX_SIZE  			2	// max size of seek request list

class AVSynchronizer;

class UploaderCallbackImpl : public UploaderCallback {
public:
	void setParent(AVSynchronizer * pParent) {
		mParent = pParent;
	}

public:
	void processVideoFrame(GLuint inputTexId, int width, int height, float position);

	int processAudioData(short *sample, int size, float position, byte** buffer);

	void onSeekCallback(float seek_seconds);

    void initFromUploaderGLContext(EGLCore* eglCore);

	void destroyFromUploaderGLContext();

	void createEncoderOutputRender() { };

protected:
	AVSynchronizer * mParent;
};


class AVSynchronizer {
public:
	/** 当uploader创建后调用 **/
	virtual void OnInitFromUploaderGLContext(EGLCore* eglCore,
			int videoFrameWidth, int videoFrameHeight);

	/** 当uploader销毁前调用 **/
	virtual void onDestroyFromUploaderGLContext();

	virtual void onSeek(float seek_seconds);
	virtual void frameAvailable();

	virtual void processVideoFrame(GLuint inputTexId, int width, int height, float position);

	virtual int processAudioData(short *sample, int size, float position, byte** buffer);
	UploaderCallbackImpl mUploaderCallback;

	AVSynchronizer();
	virtual ~AVSynchronizer();

	bool init(DecoderRequestHeader *requestHeader, JavaVM *g_jvm, jobject obj,
			float minBufferedDuration, float maxBufferedDuration);
	bool validAudio();
	bool isValid();
	int getVideoFrameHeight();
	int getVideoFrameWidth();
	float getVideoFPS();
	int getAudioChannels();
	int getAudioSampleRate();

	void start();
	EGLContext getUploaderEGLContext();

	virtual void signalDecodeThread();
	bool checkPlayState();

	virtual void destroy();

	inline void interruptRequest(){
		if (decoder){
			decoder->interrupt();
		}
	}

	void initCircleQueue(int videoWidth, int videoHeight);

	/** 当客户端调用destroy方法之后 只为true **/
	bool isDestroyed;
	bool isOnDecoding;
	void clearFrameMeta();

	//当前缓冲区是否有数据
	bool buffered;
	bool isCompleted;

	int fillAudioData(byte* outData, int bufferSize);
	virtual FrameTexture* getCorrectRenderTexture(bool forceGetFrame);
	FrameTexture* getFirstRenderTexture();
	FrameTexture* getSeekRenderTexture();

	float getDuration();
	float getBufferedProgress();
	float getPlayProgress();

	//调用解码器进行解码特定长度frame
	virtual void decodeFrames();
	void pauseDecodeThread();

	EGLContext getTextureFrameUploaderEGLContext(){
		if(NULL != decoder){
			return decoder->getTextureFrameUploaderEGLContext();
		}
		return NULL;
	};

	virtual void renderToVideoQueue(GLuint inputTexId, int width, int height, float position);

	void clearCircleFrameTextureQueue();


	bool isPlayCompleted() {
		return isCompleted;
	}

	void destroyPassThorughRender();
	void seekToPosition(float position);

protected:
	EGLContext loadTextureContext;
protected:
	VideoGLSurfaceRender* passThorughRender;
	void processDecodingFrame(bool& good, float duration);
	void decode();

	inline bool canDecode(){
		return !pauseDecodeThreadFlag && !isDestroyed && decoder && (decoder->validVideo() || decoder->validAudio()) && !decoder->isEOF();
	}

	//init 方法中调用的私有方法
	VideoDecoder* decoder;
	int decodeVideoErrorState;
	/** 是否初始化解码线程 **/
	bool isInitializeDecodeThread;
	//回调客户端的方法比如书显示或者隐藏progressbar
	JavaVM *g_jvm;
	jobject obj;
	//表示客户端界面上是否显示loading progressbar
	bool isLoading;
	float syncMaxTimeDiff;
	float minBufferedDuration;//缓冲区的最短时长
	float maxBufferedDuration;//缓冲区的最长时长
	/** 解码出来的videoFrame与audioFrame的容器，以及同步操作信号量 **/
    pthread_mutex_t audioFrameQueueMutex;
	std::queue<AudioFrame*> *audioFrameQueue;
	CircleFrameTextureQueue* circleFrameTextureQueue;
	/** 这里是为了将audioFrame中的数据，缓冲到播放音频的buffer中，有可能需要积攒几个frame，所以记录位置以及保存当前frame **/
	AudioFrame* currentAudioFrame;
	int currentAudioFramePos;
	/** 当前movie的position，用于同步音画 **/
	double moviePosition;
	/** 根据缓冲区来控制是否需要编解码的变量 **/
	float bufferedDuration;//当前缓冲区时长

	bool isHWCodecAvaliable();
	virtual void createDecoderInstance();
	virtual void initMeta();
	void viewStreamMetaCallback(int videoWidth, int videoHeight, float duration);
	void closeDecoder();

	virtual void useForstatistic(int leftVideoFrames){};

	//start 中用到的变量以及方法
	//将frame加到对应的容器中 并且根据返回值决定是否需要继续解码
	bool addFrames(std::list<MovieFrame*>* frames);
	bool addFrames(float thresholdDuration, std::list<MovieFrame*>* frames);
	//由于在解码器中解码的时候有可能会由于网络原因阻塞，甚至超时，所以这个时候需要将解码过程放到新的线程中
	pthread_t videoDecoderThread;
	static void* startDecoderThread(void* ptr);
	/** 由于在解码线程中要用到以下几个值，所以访问控制符是public的 **/
	bool isDecodingFrames;
	bool pauseDecodeThreadFlag;
	pthread_mutex_t videoDecoderLock;
	pthread_cond_t videoDecoderCondition;
	/** 开启解码线程 **/
	virtual void initDecoderThread();
	/** 销毁解码线程 **/
	virtual void destroyDecoderThread();

	// 调用解码器解码指定position的frame
	void decodeFrameByPosition(float pos);

	void clearVideoFrameQueue();
	void clearAudioFrameQueue();

	//jni 调java层方法
	int showLoadingDialog();
	int onCompletion();
	int videoDecodeException();
	int hideLoadingDialog();
	int jniCallbackWithNoArguments(char* signature, char* params);
	int jniCallbackWithArguments(const char* signature, const char* params, ...);
};
#endif // VIDEO_PLAYER_AV_SYNCHRONIZER_
