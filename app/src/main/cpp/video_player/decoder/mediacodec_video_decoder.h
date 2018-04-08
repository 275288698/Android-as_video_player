#ifndef MEDIACODEC_VIDEO_DECODER_H
#define MEDIACODEC_VIDEO_DECODER_H

#include "video_decoder.h"

#include "../texture_uploader/texture_frame_uploader.h"
#include "../texture_uploader/gpu_texture_frame_uploader.h"

class MediaCodecVideoDecoder : public VideoDecoder{
public:
	MediaCodecVideoDecoder();
	MediaCodecVideoDecoder(JavaVM *g_jvm, jobject obj);
    virtual ~MediaCodecVideoDecoder();

    virtual float updateTexImage(TextureFrame* textureFrame);

protected:
    virtual TextureFrameUploader* createTextureFrameUploader();
	virtual void seek_frame();
	virtual bool decodeVideoFrame(AVPacket packet, int* decodeVideoErrorState);
	virtual void flushVideoFrames(AVPacket packet, int* decodeVideoErrorState);
    virtual void closeVideoStream();
    virtual int initAnalyzeDurationAndProbesize(int* max_analyze_durations, int analyzeDurationSize, int probesize, bool fpsProbeSizeConfigured){
    		return 1;
    };

private:
    bool isMediaCodecInit;
	GLuint decodeTexId;
	jbyteArray inputBuffer;

	typedef struct {
		uint8_t* data;
		int size;
	} FramePacket;

	void convertPacket(FramePacket* packet);

	bool initializeMediaCodec();
	bool initializeMediaCodec(FramePacket * packet);

	void flushMediaCodecBuffers();
	void destroyMediaCodec();

	uint32_t findStartCode(uint8_t* in_pBuffer, uint32_t in_ui32BufferSize,
			uint32_t in_ui32Code, uint32_t& out_ui32ProcessedBytes);
	void parseH264SequenceHeader(uint8_t* in_pBuffer, uint32_t in_ui32Size,
			uint8_t** inout_pBufferSPS, int& inout_ui32SizeSPS,
			uint8_t** inout_pBufferPPS, int& inout_ui32SizePPS);
};


#endif // MEDIACODEC_VIDEO_DECODER_H
