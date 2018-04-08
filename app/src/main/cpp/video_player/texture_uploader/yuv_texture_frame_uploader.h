#ifndef YUV_TEXTURE_FRAME_UPLOADER_H
#define YUV_TEXTURE_FRAME_UPLOADER_H

#include "texture_frame_uploader.h"

class YUVTextureFrameUploader: public TextureFrameUploader {
public:
	YUVTextureFrameUploader();
	virtual ~YUVTextureFrameUploader();

protected:
	virtual bool initialize();
	virtual void destroy();
};

#endif // YUV_TEXTURE_FRAME_UPLOADER_H
