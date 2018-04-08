#include "circle_texture_queue.h"

#define LOG_TAG "CircleFrameTextureQueue"

CircleFrameTextureQueue::CircleFrameTextureQueue(const char* queueNameParam) {
	queueName = queueNameParam;
	int initLockCode = pthread_mutex_init(&mLock, NULL);
	int initConditionCode = pthread_cond_init(&mCondition, NULL);
	mAbortRequest = false;
	//组织循环链表
	isAvailable = false;

	firstFrame = NULL;
}

void CircleFrameTextureQueue::init(int width, int height, int queueSizeParam){
	LOGI("enter CircleFrameTextureQueue::init width is %d height is %d queueSize is %d", width, height, queueSizeParam);
	if(queueSizeParam < 5){
		LOGI("circle video packet queue min size is 5");
		return;
	}
	queueSize = queueSizeParam;
	int getLockCode = pthread_mutex_lock(&mLock);
	tail = new FrameTextureNode();
	tail->texture = buildFrameTexture(width, height, INVALID_FRAME_POSITION);
	int i = queueSize - 1;
	FrameTextureNode* nextCursor = tail;
	FrameTextureNode* curCursor = NULL;
	while (i > 0) {
		curCursor = new FrameTextureNode();
		curCursor->texture = buildFrameTexture(width, height, INVALID_FRAME_POSITION);
		curCursor->next = nextCursor;
		nextCursor = curCursor;
		i--;
	}
	head = curCursor;
	tail->next = head;
	pullCursor = head;
	pushCursor = head;
	pthread_mutex_unlock(&mLock);
//	LOGI("leave CircleFrameTextureQueue::init");

	// allocate first frame
	firstFrame = new FrameTexture();
	buildGPUFrame(firstFrame, width, height);
	firstFrame->position = 0.0f;
	firstFrame->width = width;
	firstFrame->height = height;

	isFirstFrame = false;
}

void CircleFrameTextureQueue::setIsFirstFrame(bool value) {
	isFirstFrame = value;
}

bool CircleFrameTextureQueue::getIsFirstFrame() {
	return isFirstFrame;
}

FrameTexture* CircleFrameTextureQueue::buildFrameTexture(int width, int height, float position) {
	FrameTexture* frameTexture = new FrameTexture();
	//生成纹理与FBO并且关联两者
	buildGPUFrame(frameTexture, width, height);
	frameTexture->position = position;
	frameTexture->width = width;
	frameTexture->height = height;
//	LOGI("buildFrameTexture:width is %d height is %d texId is %d", width, height, frameTexture->texId);
	return frameTexture;
}

void CircleFrameTextureQueue::buildGPUFrame(FrameTexture* frameTexture, int width, int height){
	GLuint texId = 0;
	glGenTextures(1, &texId);
	checkGlError("glGenTextures texId");
	glBindTexture(GL_TEXTURE_2D, texId);
	checkGlError("glBindTexture texId");
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	GLint internalFormat = GL_RGBA;
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, (GLsizei) width, (GLsizei) height, 0, internalFormat, GL_UNSIGNED_BYTE, 0);

	glBindTexture(GL_TEXTURE_2D, 0);
	frameTexture->texId = texId;
}

FrameTexture* CircleFrameTextureQueue::getFirstFrameFrameTexture() {
	return firstFrame;
}

FrameTexture* CircleFrameTextureQueue::lockPushCursorFrameTexture() {
//	LOGI("enter CircleFrameTextureQueue::lockPushCursorFrameTexture");
	if (mAbortRequest) {
		return NULL;
	}
	int getLockCode = pthread_mutex_lock(&mLock);
	return pushCursor->texture;
}

void CircleFrameTextureQueue::unLockPushCursorFrameTexture(){
//	LOGI("enter CircleFrameTextureQueue::unLockPushCursorFrameTexture");
	if (mAbortRequest) {
		return;
	}
	if(pushCursor == pullCursor){
		if(!isAvailable){
			isAvailable = true;
			isFirstFrame = false;
//			LOGI("pthread_cond_signal ~~~");
			pthread_cond_signal(&mCondition);
		} else{
			pullCursor = pullCursor->next;
		}
	}
	pushCursor = pushCursor->next;
	pthread_mutex_unlock(&mLock);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
int CircleFrameTextureQueue::front(FrameTexture **frameTexture) {
	if (mAbortRequest) {
		return -1;
	}
	int ret = 1;
	int getLockCode = pthread_mutex_lock(&mLock);
	if(!isAvailable){
		//如果isAvailable 说明还没有推送进来 直到等到signal信号才进入下面
		pthread_cond_wait(&mCondition, &mLock);
	}
	FrameTextureNode* frontCursor = pullCursor;
	if(frontCursor->next != pushCursor){
		//如果没有追上pushCursor 则将pullCursor向后推移 否则直接进行复制视频帧操作
		frontCursor = frontCursor->next;
	} else{
		ret = -1;
	}
	*frameTexture = frontCursor->texture;
	pthread_mutex_unlock(&mLock);
	return ret;
}

int CircleFrameTextureQueue::pop(){
	if (mAbortRequest) {
		return -1;
	}
	int ret = 1;
	int getLockCode = pthread_mutex_lock(&mLock);
	if(!isAvailable){
		//如果isAvailable 说明还没有推送进来 直到等到signal信号才进入下面
		pthread_cond_wait(&mCondition, &mLock);
	}
	if(pullCursor->next != pushCursor){
		//如果没有追上pushCursor 则将pullCursor向后推移 否则直接进行复制视频帧操作
		pullCursor = pullCursor->next;
	}
	pthread_mutex_unlock(&mLock);
	return ret;
}

void CircleFrameTextureQueue::clear(){
//	LOGI("enter CircleFrameTextureQueue::clear");
	pthread_mutex_lock(&mLock);
	isAvailable = false;
	pullCursor = head;
	pushCursor = head;
	pthread_mutex_unlock(&mLock);
//	LOGI("leave CircleFrameTextureQueue::clear");
}

int CircleFrameTextureQueue::getValidSize(){
	if (mAbortRequest || !isAvailable) {
		return 0;
	}
	int size = 0;

	int getLockCode = pthread_mutex_lock(&mLock);
	FrameTextureNode* beginCursor = pullCursor;
	FrameTextureNode* endCursor = pushCursor;

	if(beginCursor->next == endCursor){
		size = 1;
	} else{
		while(beginCursor->next != endCursor){
			size++;
			beginCursor = beginCursor->next;
		}
	}
	pthread_mutex_unlock(&mLock);
	return size;
}

//int CircleFrameTextureQueue::getValidSize(){
//	int size = 1;
//	int getLockCode = pthread_mutex_lock(&mLock);
//	FrameTextureNode* beginCursor = pullCursor;
//	FrameTextureNode* endCursor = pushCursor;
//	LOGI("pullCursor texId : %d pushCursor texId : %d", pullCursor->texture->texId, pushCursor->texture->texId);
//	if(beginCursor == endCursor){
//		size = 0;
//	} else{
//		while(beginCursor->next != endCursor){
//			size++;
//			beginCursor = beginCursor->next;
//		}
//	}
//	pthread_mutex_unlock(&mLock);
//	return size;
//}

bool CircleFrameTextureQueue::checkGlError(const char* op) {
	GLint error;
	for (error = glGetError(); error; error = glGetError()) {
		LOGI("error::after %s() glError (0x%x)\n", op, error);
		return true;
	}
	return false;
}

void CircleFrameTextureQueue::abort() {
	pthread_mutex_lock(&mLock);
	mAbortRequest = true;
	pthread_cond_signal(&mCondition);
	pthread_mutex_unlock(&mLock);
}

CircleFrameTextureQueue::~CircleFrameTextureQueue() {
//	LOGI("%s ~CircleFrameTextureQueue ....", queueName);
	flush();
	pthread_mutex_destroy(&mLock);
	pthread_cond_destroy(&mCondition);
}

void CircleFrameTextureQueue::flush() {
//	LOGI("\n %s flush .... and this time the queue size is %d \n", queueName, queueSize);
	pthread_mutex_lock(&mLock);
//	LOGI("success get a lock in CircleFrameTextureQueue flush method...");
	FrameTextureNode *node = head;
	FrameTextureNode *tempNode;
	FrameTexture *frameTexture;
	while(node != tail){
		tempNode = node->next;
		frameTexture = node->texture;
		if (NULL != frameTexture) {
			delete frameTexture;
		}
		node->next = NULL;
		delete node;
		node = tempNode;
	}
	frameTexture = tail->texture;
	if (NULL != frameTexture) {
		delete frameTexture;
	}
	tail->next = NULL;
	delete tail;

	head = NULL;
	tail = NULL;
	pullCursor = NULL;
	pushCursor = NULL;

	// delete the cached first frame
	delete firstFrame;
	firstFrame = NULL;

	isFirstFrame = false;

	pthread_mutex_unlock(&mLock);
}
