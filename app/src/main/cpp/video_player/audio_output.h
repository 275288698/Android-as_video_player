#ifndef _VIDEO_PLAYER_AUDIO_OUTPUT_H_
#define _VIDEO_PLAYER_AUDIO_OUTPUT_H_

#include "CommonTools.h";
#include "opensl_media/opensl_es_util.h"
#include "opensl_media/opensl_es_context.h"
#include <unistd.h>

#define DEFAULT_AUDIO_BUFFER_DURATION_IN_SECS 0.03

typedef int(*audioPlayerCallback)(byte* , size_t, void* ctx);

#define PLAYING_STATE_STOPPED (0x00000001)
#define PLAYING_STATE_PLAYING (0x00000002)

class AudioOutput {
public:
	AudioOutput();
	virtual ~AudioOutput();

    int playingState;

	SLresult initSoundTrack(int channels, int accompanySampleRate, audioPlayerCallback, void* ctx);
	SLresult start();
	SLresult play();
	SLresult pause();
	SLresult stop();
	bool isPlaying();
	long getCurrentTimeMills();
	void destroyContext();
	audioPlayerCallback produceDataCallback;
	void* ctx;

private:
	SLEngineItf engineEngine;
	SLObjectItf outputMixObject;
	SLObjectItf audioPlayerObject;
	SLAndroidSimpleBufferQueueItf audioPlayerBufferQueue;
	SLPlayItf audioPlayerPlay;

	byte* buffer;
	size_t bufferSize;
	//初始化OpenSL要播放的buffer
	void initPlayerBuffer();
	//释放OpenSL播放的buffer
	void freePlayerBuffer();
	//实例化一个对象
	SLresult realizeObject(SLObjectItf object);
	//销毁这个对象以及这个对象下面的接口
	void destroyObject(SLObjectItf& object);
	//创建输出对象
	SLresult createOutputMix();
	//创建OpenSL的AudioPlayer对象
	SLresult createAudioPlayer(int channels, int accompanySampleRate);
	//获得AudioPlayer对象的bufferQueue的接口
	SLresult getAudioPlayerBufferQueueInterface();
	//获得AudioPlayer对象的play的接口
	SLresult getAudioPlayerPlayInterface();
	//设置播放器的状态为播放状态
	SLresult setAudioPlayerStatePlaying();
	//设置播放器的状态为暂停状态
	SLresult setAudioPlayerStatePaused();
	//以下三个是进行注册回调函数的
	//当OpenSL播放完毕给他的buffer数据之后，就会回调playerCallback
	SLresult registerPlayerCallback();
	//playerCallback中其实会调用我们的实例方法producePacket
	static void playerCallback(SLAndroidSimpleBufferQueueItf bq, void *context);
	//这里面会调用在初始化的时候注册过来的回调函数，回调它，由这个回调函数填充数据（这个回调函数还负责音画同步）
	void producePacket();
};
#endif	//_VIDEO_PLAYER_AUDIO_OUTPUT_H_
