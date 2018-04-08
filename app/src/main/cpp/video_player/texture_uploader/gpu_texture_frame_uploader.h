#ifndef GPU_TEXTURE_FRAME_UPLOADER_H
#define GPU_TEXTURE_FRAME_UPLOADER_H

#include "texture_frame_uploader.h"

class GPUTextureFrameUploader: public TextureFrameUploader {
public:
	GPUTextureFrameUploader();
	virtual ~GPUTextureFrameUploader();

	GLuint getDecodeTexId(){
		return decodeTexId;
	};

protected:
	GLuint decodeTexId;

	virtual bool initialize();
	virtual void destroy();
};

#endif // GPU_TEXTURE_FRAME_UPLOADER_H
