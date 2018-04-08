#ifndef TEXTURE_FRAME_UPLOADER_H
#define TEXTURE_FRAME_UPLOADER_H

#include <unistd.h>
#include <pthread.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "egl_core/egl_core.h"
#include "opengl_media/texture/texture_frame.h"
#include "opengl_media/texture/gpu_texture_frame.h"
#include "opengl_media/texture/yuv_texture_frame.h"
#include "opengl_media/texture_copier/texture_frame_copier.h"
#include "opengl_media/texture_copier/gpu_texture_frame_copier.h"
#include "opengl_media/texture_copier/yuv_texture_frame_copier.h"

#define OPENGL_VERTEX_COORDNATE_CNT			8

static GLfloat DECODER_COPIER_GL_VERTEX_COORDS[8] = {
		-1.0f, -1.0f,	// 0 top left
		1.0f, -1.0f,	// 1 bottom left
		-1.0f, 1.0f,  // 2 bottom right
		 1.0f, 1.0f,	// 3 top right
	};

static GLfloat DECODER_COPIER_GL_TEXTURE_COORDS_NO_ROTATION[8] = {
		0.0f, 0.0f,
		1.0f, 0.0f,
		0.0f, 1.0f,
		1.0f, 1.0f
	};

static GLfloat DECODER_COPIER_GL_TEXTURE_COORDS_ROTATED_90[8] = {
		0.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 1.0f,
		1.0f, 0.0f
	};

static GLfloat DECODER_COPIER_GL_TEXTURE_COORDS_ROTATED_180[8] = {
		1.0, 1.0,
		0.0, 1.0,
		1.0, 0.0,
		0.0, 0.0,
	};
static GLfloat DECODER_COPIER_GL_TEXTURE_COORDS_ROTATED_270[8] = {
		1.0f, 0.0f,
		1.0f, 1.0f,
		0.0f, 0.0f,
		0.0f, 1.0f
	};

// callback from VideoPlayerController
class UploaderCallback
{
public:
	virtual void processVideoFrame(GLuint inputTexId, int width, int height, float position) = 0;

	virtual int processAudioData(short *sample, int size, float position, byte** buffer) = 0;

	virtual void onSeekCallback(float seek_seconds) = 0;

	virtual void initFromUploaderGLContext(EGLCore* eglCore) = 0;

	virtual void destroyFromUploaderGLContext() = 0;
};

class TextureFrameUploader {
public:
	TextureFrameUploader();
	virtual ~TextureFrameUploader();

	bool start(int videoWidth, int videoHeight, int degress);

	void signalFrameAvailable();

	void stop();

    EGLContext getEGLContext(){
    		if(NULL != eglCore){
    			return eglCore->getContext();
    		}
    		return NULL;
    };

	/** 声明更新纹理的方法 **/
	typedef float (*update_tex_image_callback)(TextureFrame* textureFrame, void *context);
	typedef void (*signal_decode_thread_callback)(void *context);
	virtual void registerUpdateTexImageCallback(float (*update_tex_image_callback)(TextureFrame* textureFrame, void *context), void (*signal_decode_thread_callback)(void *context), void* context);

	void setUploaderCallback(UploaderCallback * pUploaderCallback) {
		mUploaderCallback = pUploaderCallback;
	}

protected:
	bool isInitial;
    EGLCore* eglCore;
	EGLSurface copyTexSurface;
	int videoWidth;
	int videoHeight;

	GLfloat* vertexCoords;
	GLfloat* textureCoords;

	UploaderCallback * mUploaderCallback;

	TextureFrameCopier* textureFrameCopier;
	/** videoEffectProcessor相关的 **/
	GLuint outputTexId;

	/** 操作纹理的FBO **/
    GLuint mFBO;

    /** 解码线程
     * 	拷贝以及处理线程
     * 	当解码出一帧之后会signal拷贝线程（自己wait住）,拷贝线程会利用updateTexImageCallback来更新解码线程最新解码出来的Frame
     * 	然后拷贝线程会把这个Frame进行拷贝以及通过VideoEffectProcessor进行处理
     * 	最终会给解码线程发送signal信号（自己wait住）
     **/
	TextureFrame* textureFrame;
	update_tex_image_callback updateTexImageCallback;
	signal_decode_thread_callback signalDecodeThreadCallback;
	void* updateTexImageContext;

	enum RenderThreadMessage {
		MSG_NONE = 0, MSG_WINDOW_SET, MSG_RENDER_LOOP_EXIT
	};

	pthread_t _threadId;
	pthread_mutex_t mLock;
	pthread_cond_t mCondition;
	enum RenderThreadMessage _msg;
	// Helper method for starting the thread
	static void* threadStartCallback(void *myself);
	// RenderLoop is called in a rendering thread started in start() method
	// It creates rendering context and renders scene until stop() is called
	void renderLoop();
	virtual void destroy();

	virtual bool initialize();
	virtual void drawFrame();
	float updateTexImage();
	void signalDecodeThread();
};

#endif // TEXTURE_FRAME_UPLOADER_H
