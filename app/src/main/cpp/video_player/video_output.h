#ifndef _VIDEO_PLAYER_VIDEO_OUTPUT_H_
#define _VIDEO_PLAYER_VIDEO_OUTPUT_H_

#include <unistd.h>
#include <android/native_window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "opengl_media/render/video_gl_surface_render.h"
#include "./common/circle_texture_queue.h"
#include "egl_core/egl_core.h"
#include "CommonTools.h"
#include "message_queue/handler.h"
#include "message_queue/message_queue.h"
typedef enum {
	VIDEO_OUTPUT_MESSAGE_CREATE_EGL_CONTEXT,
	VIDEO_OUTPUT_MESSAGE_CREATE_WINDOW_SURFACE,
	VIDEO_OUTPUT_MESSAGE_DESTROY_WINDOW_SURFACE,
	VIDEO_OUTPUT_MESSAGE_DESTROY_EGL_CONTEXT,
	VIDEO_OUTPUT_MESSAGE_RENDER_FRAME
} VideoOutputMSGType;

typedef int (*getTextureCallback)(FrameTexture** texture, void* ctx, bool forceGetFrame);
typedef int (*onPreDestroyCallback)(void* ctx);

class VideoOuputHandler;

class VideoOutput {
public:
	VideoOutput();
	virtual ~VideoOutput();
	/** 初始化Output **/
	bool initOutput(ANativeWindow* window, int screenWidth, int screenHeight,getTextureCallback produceDataCallback,  void* ctx);
	/** 绘制视频帧 **/
	void signalFrameAvailable();
	/** 重置视频绘制区域的大小 **/
	void resetRenderSize(int left, int top, int width, int height);
	/** 当surface创建的时候的调用 **/
	void onSurfaceCreated(ANativeWindow* window);
	/** 当surface销毁的时候调用 **/
	void onSurfaceDestroyed();
	/** 销毁Output **/
	void stopOutput();

	bool createEGLContext(ANativeWindow* window);
	void createWindowSurface(ANativeWindow* window);
	bool renderVideo();
	void destroyWindowSurface();
	void destroyEGLContext();

	int getScreenWidth(){
		return screenWidth;
	};
	int getScreenHeight(){
		return screenHeight;
	};

	bool eglHasDestroyed;

protected:
private:
	getTextureCallback produceDataCallback;
	void* ctx;

	VideoOuputHandler* handler;
	MessageQueue* queue;

	EGLCore* eglCore;
	EGLSurface renderTexSurface;
	ANativeWindow* surfaceWindow;
	VideoGLSurfaceRender* renderer;

	int screenWidth;
	int screenHeight;

	pthread_t _threadId;
	static void* threadStartCallback(void *myself);
	void processMessage();

	bool surfaceExists;
	bool isANativeWindowValid;
	bool forceGetFrame;


};

class VideoOuputHandler: public Handler {
	private:
		VideoOutput* videoOutput;
		bool initPlayerResourceFlag;
	public:
		VideoOuputHandler(VideoOutput* videoOutput, MessageQueue* queue) :
				Handler(queue) {
			this->videoOutput = videoOutput;
			initPlayerResourceFlag = false;
		}
		void handleMessage(Message* msg) {
			int what = msg->getWhat();
			ANativeWindow* obj;
			switch (what) {
			case VIDEO_OUTPUT_MESSAGE_CREATE_EGL_CONTEXT:
				if (videoOutput->eglHasDestroyed){
					break;
				}

				obj = (ANativeWindow*) (msg->getObj());
				initPlayerResourceFlag = videoOutput->createEGLContext(obj);
				break;
			case VIDEO_OUTPUT_MESSAGE_RENDER_FRAME:
				if (videoOutput->eglHasDestroyed) {
					break;
				}

				if(initPlayerResourceFlag){
					videoOutput->renderVideo();
				}
				break;
			case VIDEO_OUTPUT_MESSAGE_CREATE_WINDOW_SURFACE:
				if (videoOutput->eglHasDestroyed) {
					break;
				}
				if(initPlayerResourceFlag){
					obj = (ANativeWindow*) (msg->getObj());
					videoOutput->createWindowSurface(obj);
				}
				break;
			case VIDEO_OUTPUT_MESSAGE_DESTROY_WINDOW_SURFACE:
				if(initPlayerResourceFlag){
					videoOutput->destroyWindowSurface();
				}
				break;
			case VIDEO_OUTPUT_MESSAGE_DESTROY_EGL_CONTEXT:
				videoOutput->destroyEGLContext();
				break;
			}
		}
	};
#endif	//_VIDEO_PLAYER_VIDEO_OUTPUT_H_
