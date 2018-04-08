#include "yuv_texture_frame_uploader.h"

#define LOG_TAG "YUVTextureFrameUploader"

YUVTextureFrameUploader::YUVTextureFrameUploader(){
	LOGI("TextureFrameUploader instance created");
}

YUVTextureFrameUploader::~YUVTextureFrameUploader() {
	LOGI("TextureFrameUploader instance destroyed");
}

bool YUVTextureFrameUploader::initialize() {
	TextureFrameUploader::initialize();
	//init decodeTexId
	textureFrame = new YUVTextureFrame();
	textureFrame->createTexture();
	textureFrameCopier = new YUVTextureFrameCopier();
	textureFrameCopier->init();
	return true;
}

void YUVTextureFrameUploader::destroy() {
	eglCore->makeCurrent(copyTexSurface);
	if (textureFrameCopier) {
		textureFrameCopier->destroy();
		delete textureFrameCopier;
		textureFrameCopier = NULL;
	}
	if (textureFrame) {
		textureFrame->dealloc();

		delete textureFrame;
		textureFrame = NULL;
	}
	TextureFrameUploader::destroy();
}


