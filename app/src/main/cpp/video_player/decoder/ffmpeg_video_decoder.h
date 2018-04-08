#ifndef FFMPEG_VIDEO_DECODER_H
#define FFMPEG_VIDEO_DECODER_H

#include "video_decoder.h"

#include "../texture_uploader/texture_frame_uploader.h"
#include "../texture_uploader/yuv_texture_frame_uploader.h"

class FFMPEGVideoDecoder : public VideoDecoder{
public:
	FFMPEGVideoDecoder();
	FFMPEGVideoDecoder(JavaVM *g_jvm, jobject obj);
    virtual ~FFMPEGVideoDecoder();

    virtual float updateTexImage(TextureFrame* textureFrame);

protected:
    virtual TextureFrameUploader* createTextureFrameUploader();
	virtual bool decodeVideoFrame(AVPacket packet, int* decodeVideoErrorState);
	virtual void flushVideoFrames(AVPacket packet, int* decodeVideoErrorState);
    virtual int initAnalyzeDurationAndProbesize(int* max_analyze_durations, int analyzeDurationSize, int probesize, bool fpsProbeSizeConfigured){
    		return 1;
    };
};
#endif // FFMPEG_VIDEO_DECODER_H
