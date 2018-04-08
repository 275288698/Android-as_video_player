#ifndef HW_ENCODER_CIRCLE_TEXTURE_QUEUE_H
#define HW_ENCODER_CIRCLE_TEXTURE_QUEUE_H

#include <pthread.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "CommonTools.h"

#define DEFAULT_QUEUE_SIZE 10

#define INVALID_FRAME_POSITION -1.0

typedef void(*onSignalFrameAvailableCallback)(void* ctx);

typedef struct FrameTexture {
	GLuint texId;
	float position;
	int width;
	int height;
	FrameTexture() {
		texId = 0;
		position = INVALID_FRAME_POSITION;
		width = 0;
		height = 0;
	}
	~FrameTexture() {
		if (texId) {
			glDeleteTextures(1, &texId);
		}
	}
} FrameTexture;

typedef struct FrameTextureNode {
	FrameTexture *texture;
	struct FrameTextureNode *next;
	FrameTextureNode(){
		texture = NULL;
		next = NULL;
	}
} FrameTextureNode;

/**
 *	⬇ pushCursor
 *  ------       ------       ------          ------       ------
 * | Node | --> | Node | --> | Node | -...-> | Node | --> | Node | --
 *  ------       ------       ------          ------       ------    |
 *    ^                                                              |
 *    |--------------------------------------------------------------
 *	⬆ pullCursor
 *	1、最开始初始化的时候
 *		初始化length的长度的循环链表;
 *		pushCursor指向第一个节点,pullCursor也指向第一个节点;
 *		最后一个节点(tail)的Next指向第一个节点(head)构造循环链表;
 *	2、当生产者向queue里面push一个元素的时候，首先上锁，把数据memcpy到pushCursor当前节点上，并且吧pushCursor移到下一个节点，最后解锁;
 *	3、当消费者来queue里面pull一个元素的时候，首先上锁，把pullCursor移到下一个节点，指向的节点的数据memcpy到目标buffer，最后解锁;
 *	4、特殊case:
 *		(1)如果pullCursor追上了pushCursor怎么办?
 *		判断pullCursor的下一个节点是不是pushCursor指向的节点
 *			如果是那么就不用移动直接取出,
 *			如果不是，正常先移动到下一个节点，再取出来
 *		(2)如果pushCursor追上了pullCursor怎么办?
 *		首先memcpy数据到这个节点然后判断pushCursor当前指向的节点是不是pullCursor当前指向的节点
 *			如果是那么就同时把pullCursor与pushCursor同时移动到下一个节点
 *			如果不是 pushCursor移动下一个节点
 *		(3)最开始的时候,如何进行pull或者push?
 *		首先有一个全局变量是isAvailable默认值是false,当第一帧push进来之后, 我们会把isAvailable设置为true，发出signal信号;
 *			注意:此时不移动pullCursor
 *		当pull的时候，我们会首先判断isAvailable是否为true，如果为true则真正进入取的逻辑，否则wait直到等到signal信号
 *		则继续进行pull操作.
 *
 */
class CircleFrameTextureQueue {
public:
	/** 在AVSynchronizer中调用 **/
	CircleFrameTextureQueue(const char* queueNameParam);
	~CircleFrameTextureQueue();

	/** 在Uploader中的EGL Thread中调用 **/
	void init(int width, int height, int queueSize);

	/**
	 * 当视频解码器解码出一帧视频
	 * 	1、锁住pushCursor所在的FrameTexture;
	 * 	2、客户端自己向FrameTexture做拷贝或者上传纹理操作
	 * 	3、解锁pushCursor所在的FrameTexture并且移动
	 */
	FrameTexture* lockPushCursorFrameTexture();
	void unLockPushCursorFrameTexture();

	/**
	 * return < 0 if aborted,
	 * 			0 if no packet
	 *		  > 0 if packet.
	 */
	int front(FrameTexture **frameTexture);
	int pop();
	/** 清空queue **/
	void clear();
	/** 获取queue中有效的数据参数 **/
	int getValidSize();

	void abort();

	bool getAvailable() {
		return isAvailable;
	}

	FrameTexture* getFirstFrameFrameTexture();

	void setIsFirstFrame(bool value);

	bool getIsFirstFrame();

private:
	FrameTextureNode* head;
	FrameTextureNode* tail;
	FrameTextureNode* pullCursor;
	FrameTextureNode* pushCursor;
	FrameTexture* firstFrame;

	int queueSize;
	bool isAvailable;
	bool mAbortRequest;
	bool isFirstFrame;
	pthread_mutex_t mLock;
	pthread_cond_t mCondition;
	const char* queueName;

	void flush();
	FrameTexture* buildFrameTexture(int width, int height, float position);

	bool checkGlError(const char* op);
	void buildGPUFrame(FrameTexture* frameTexture, int width, int height);
};

#endif // HW_ENCODER_CIRCLE_TEXTURE_QUEUE_H
