#include "mediacodec_video_decoder.h"

#define LOG_TAG "MediaCodecVideoDecoder"

#define is_start_code(code)	(((code) & 0x0ffffff) == 0x01)

// returned match error value from MediaCodecDecoder.java
#define ERROR_OK        0
#define ERROR_EOF       1
#define ERROR_FAIL      2
#define ERROR_UNUSUAL   3

MediaCodecVideoDecoder::MediaCodecVideoDecoder() {
	isMediaCodecInit = false;
	inputBuffer = NULL;
}

MediaCodecVideoDecoder::MediaCodecVideoDecoder(JavaVM *g_jvm, jobject obj)
					: VideoDecoder(g_jvm, obj) {
	decodeTexId = -1;
	isMediaCodecInit = false;
}

MediaCodecVideoDecoder::~MediaCodecVideoDecoder() {
}

TextureFrameUploader* MediaCodecVideoDecoder::createTextureFrameUploader() {
	TextureFrameUploader* textureFrameUploader = new GPUTextureFrameUploader();
	return textureFrameUploader;
}

float MediaCodecVideoDecoder::updateTexImage(TextureFrame* textureFrame) {
	JNIEnv *env = 0;
	int status = 0;
	bool needAttach = false;
	status = g_jvm->GetEnv((void **) (&env), JNI_VERSION_1_4);
	// don't know why, if detach directly, will crash
	if (status < 0) {
		if (g_jvm->AttachCurrentThread(&env, NULL) != JNI_OK) {
			LOGE("%s: AttachCurrentThread() failed", __FUNCTION__);
			return 0.0f;
		}
		needAttach = true;
	}
	jclass jcls = env->GetObjectClass(obj);
	jmethodID updateTexImageFunc = env->GetMethodID(jcls, "updateTexImageFromNative", "()J");
	int64_t pos = (int64_t) env->CallLongMethod(obj, updateTexImageFunc);
	float position = (float) pos;
	position /= AV_TIME_BASE;
	if (needAttach) {
		if (g_jvm->DetachCurrentThread() != JNI_OK) {
			LOGE("%s: DetachCurrentThread() failed", __FUNCTION__);
		}
	}
	return position;
}

void MediaCodecVideoDecoder::flushVideoFrames(AVPacket packet, int* decodeVideoErrorState) {
	JNIEnv *env = 0;
	int status = 0;
	bool needAttach = false;
	status = g_jvm->GetEnv((void **) (&env), JNI_VERSION_1_4);
	// don't know why, if detach directly, will crash
	if (status < 0) {
		if (g_jvm->AttachCurrentThread(&env, NULL) != JNI_OK) {
			LOGE("%s: AttachCurrentThread() failed", __FUNCTION__);
			return;
		}

		needAttach = true;
	}
	jclass jcls = env->GetObjectClass(obj);

	jmethodID decodeFrameFunc = env->GetMethodID(jcls, "decodeFrameFromNative", "([BIJ)I");
	status = (int) env->CallIntMethod(obj, decodeFrameFunc, NULL, 0, 0, 0);

	if (status == ERROR_OK) {
		LOGI("flush video");
		this->uploadTexture();
	} else if (status == ERROR_FAIL) {
		LOGE("flush MediaCodec failed decode");
	} else {
		LOGI("output EOF");
		isVideoOutputEOF = true;
	}

	if (needAttach) {
		if (g_jvm->DetachCurrentThread() != JNI_OK) {
			LOGE("%s: DetachCurrentThread() failed", __FUNCTION__);
		}
	}
}

bool MediaCodecVideoDecoder::initializeMediaCodec(FramePacket * packet) {
	JNIEnv *env;
	int status = 0;
	bool needAttach = false;
	status = g_jvm->GetEnv((void **) (&env), JNI_VERSION_1_4);
	// don't know why, if detach directly, will crash
	if (status < 0) {
		if (g_jvm->AttachCurrentThread(&env, NULL) != JNI_OK) {
			LOGE("%s: AttachCurrentThread() failed", __FUNCTION__);
			return false;
		}

		needAttach = true;
	}
	jclass jcls = env->GetObjectClass(obj);

	// 1 allocate decoder input packet memory
	jbyteArray tempInputBuffer = env->NewByteArray(width * height * 3 / 2);	// big enough

	inputBuffer = static_cast<jbyteArray>(env->NewGlobalRef(tempInputBuffer));
	env->DeleteLocalRef(tempInputBuffer);

	decodeTexId =
			((GPUTextureFrameUploader *) textureFrameUploader)->getDecodeTexId();

	// 2 get SPS and PPS, set to decoder to create the MediaFormat
	uint8_t* bufSPS = 0;
	uint8_t* bufPPS = 0;

	int sizeSPS = 0;
	int sizePPS = 0;

	parseH264SequenceHeader(packet->data, packet->size, &bufSPS, sizeSPS,
			&bufPPS, sizePPS);

	jbyteArray sps = env->NewByteArray(sizeSPS);
	env->SetByteArrayRegion(sps, 0, sizeSPS, (jbyte*) bufSPS);

	jbyteArray pps = env->NewByteArray(sizePPS);
	env->SetByteArrayRegion(pps, 0, sizePPS, (jbyte*) bufPPS);

	jmethodID createVideoDecoderFunc = env->GetMethodID(jcls,
			"createVideoDecoderFromNative", "(III[BI[BI)Z");
	bool suc = (bool) env->CallBooleanMethod(obj, createVideoDecoderFunc, width,
			height, (int) decodeTexId, sps, sizeSPS, pps, sizePPS);

	if (!suc) {
		LOGE("Create MediaCodec decoder failed, use FFMPEG decoder instead");
	}

	env->DeleteLocalRef(sps);
	env->DeleteLocalRef(pps);

	if (needAttach) {
		if (g_jvm->DetachCurrentThread() != JNI_OK) {
			LOGE("%s: DetachCurrentThread() failed", __FUNCTION__);
		}
	}

	return suc;
}

bool MediaCodecVideoDecoder::decodeVideoFrame(AVPacket packet, int* decodeVideoErrorState) {
	bool suc = false;

	JNIEnv *env = 0;
	int status = 0;
	bool needAttach = false;
	status = g_jvm->GetEnv((void **) (&env), JNI_VERSION_1_4);

	// don't know why, if detach directly, will crash
	if (status < 0) {
		if (g_jvm->AttachCurrentThread(&env, NULL) != JNI_OK) {
			LOGE("%s: AttachCurrentThread() failed", __FUNCTION__);
			return false;
		}
		needAttach = true;
	}

	jclass jcls = env->GetObjectClass(obj);

	FramePacket packetTemp;
	packetTemp.data = packet.data;
	packetTemp.size = packet.size;

	// 先从第一帧去拿SPS和PPS去初始化解码器
	if (!isMediaCodecInit) {
		initializeMediaCodec();

		isMediaCodecInit = true;
	}

	int pktSize = packet.size;

	int64_t videoPos = 0;
	if (packet.pts == AV_NOPTS_VALUE)
		videoPos = av_rescale_q(packet.dts, pFormatCtx->streams[videoStreamIndex]->time_base, AV_TIME_BASE_Q);
	else
		videoPos = av_rescale_q(packet.pts, pFormatCtx->streams[videoStreamIndex]->time_base, AV_TIME_BASE_Q);

	convertPacket(&packetTemp);

	env->SetByteArrayRegion(inputBuffer, 0, pktSize, (jbyte*) packetTemp.data);
	jmethodID decodeFrameFunc = env->GetMethodID(jcls, "decodeFrameFromNative", "([BIJ)I");
	// note that packet.size may be 0 here even if av_read_frame returned 0.
	// so input EOF for java maybe wrong
	while (true) {
		int status = (int) env->CallIntMethod(obj, decodeFrameFunc, inputBuffer, packet.size, videoPos);
		if (status == ERROR_OK) {
			this->uploadTexture();
			suc = true;
			break;
		} else if (status == ERROR_UNUSUAL) {
			LOGI("MediaCodec decode returned ERROR_UNUSUAL, may cause endless loop");
			continue;
		} else {
			LOGI("Can't decode a video frame %d", status);
			break;
		}
	}

	if (needAttach) {
		if (g_jvm->DetachCurrentThread() != JNI_OK) {
			LOGE("%s: DetachCurrentThread() failed", __FUNCTION__);
		}
	}

	return suc;
}

void MediaCodecVideoDecoder::seek_frame(){
	VideoDecoder::seek_frame();
	flushMediaCodecBuffers();
}

void MediaCodecVideoDecoder::closeVideoStream() {
	VideoDecoder::closeVideoStream();
	if (isMediaCodecInit) {
		this->destroyMediaCodec();
	}
}

bool MediaCodecVideoDecoder::initializeMediaCodec() {
	JNIEnv *env;
	int status = 0;
	bool needAttach = false;
	status = g_jvm->GetEnv((void **) (&env), JNI_VERSION_1_4);
	// don't know why, if detach directly, will crash
	if (status < 0) {
		if (g_jvm->AttachCurrentThread(&env, NULL) != JNI_OK) {
			LOGE("%s: AttachCurrentThread() failed", __FUNCTION__);
			return false;
		}

		needAttach = true;
	}
	jclass jcls = env->GetObjectClass(obj);
	// 0 allocate decoder input packet memory
	jbyteArray tempInputBuffer = env->NewByteArray(width * height * 3 / 2);	// big enough

	inputBuffer = static_cast<jbyteArray>(env->NewGlobalRef(tempInputBuffer));
	env->DeleteLocalRef(tempInputBuffer);

	decodeTexId = ((GPUTextureFrameUploader *) textureFrameUploader)->getDecodeTexId();

	// 2 get SPS and PPS, set to decoder to create the MediaFormat
	uint8_t* dummy = NULL;
	int dummy_len;

	AVBitStreamFilterContext* bsfc = av_bitstream_filter_init("h264_mp4toannexb");

	av_bitstream_filter_filter(bsfc, videoCodecCtx, NULL, &dummy, &dummy_len, NULL, 0, 0);

	uint8_t* bufSPS = 0;
	uint8_t* bufPPS = 0;

	int sizeSPS = 0;
	int sizePPS = 0;

	parseH264SequenceHeader((uint8_t*) videoCodecCtx->extradata, (uint32_t) videoCodecCtx->extradata_size, &bufSPS, sizeSPS, &bufPPS, sizePPS);

	jbyteArray sps = env->NewByteArray(sizeSPS);
	env->SetByteArrayRegion(sps, 0, sizeSPS, (jbyte*) bufSPS);

	jbyteArray pps = env->NewByteArray(sizePPS);
	env->SetByteArrayRegion(pps, 0, sizePPS, (jbyte*) bufPPS);

	jmethodID createVideoDecoderFunc = env->GetMethodID(jcls, "createVideoDecoderFromNative", "(III[BI[BI)Z");
	bool suc = (bool) env->CallBooleanMethod(obj, createVideoDecoderFunc, width, height, (int) decodeTexId, sps, sizeSPS, pps, sizePPS);

	if (!suc) {
		LOGE("Create MediaCodec decoder failed, use FFMPEG decoder instead");
	}

	env->DeleteLocalRef(sps);
	env->DeleteLocalRef(pps);
	av_bitstream_filter_close(bsfc);
	free(dummy);

	if (needAttach) {
		if (g_jvm->DetachCurrentThread() != JNI_OK) {
			LOGE("%s: DetachCurrentThread() failed", __FUNCTION__);
		}
	}
	return suc;
}

void MediaCodecVideoDecoder::parseH264SequenceHeader(uint8_t* in_pBuffer, uint32_t in_ui32Size,
                                           uint8_t** inout_pBufferSPS, int& inout_ui32SizeSPS,
                                           uint8_t** inout_pBufferPPS, int& inout_ui32SizePPS)
{
    uint32_t ui32StartCode = 0x0ff;

    uint8_t* pBuffer = in_pBuffer;
    uint32_t ui32BufferSize = in_ui32Size;

    uint32_t sps = 0;
    uint32_t pps = 0;

    uint32_t idr = in_ui32Size;

    do
    {
        uint32_t ui32ProcessedBytes = 0;
        ui32StartCode = findStartCode(pBuffer, ui32BufferSize, ui32StartCode, ui32ProcessedBytes);
        pBuffer += ui32ProcessedBytes;
        ui32BufferSize -= ui32ProcessedBytes;

        if (ui32BufferSize < 1)
            break;

        uint8_t val = (*pBuffer & 0x1f);

        if (val == 5)
        	idr = pps+ui32ProcessedBytes-4;

        if (val == 7)
            sps = ui32ProcessedBytes;

        if (val == 8)
            pps = sps+ui32ProcessedBytes;

    } while (ui32BufferSize > 0);

    *inout_pBufferSPS = in_pBuffer + sps - 4;
    inout_ui32SizeSPS = pps-sps;

    *inout_pBufferPPS = in_pBuffer + pps - 4;
    inout_ui32SizePPS = idr - pps + 4;
}

uint32_t MediaCodecVideoDecoder::findStartCode(uint8_t* in_pBuffer, uint32_t in_ui32BufferSize,
                                     uint32_t in_ui32Code, uint32_t& out_ui32ProcessedBytes)
{
    uint32_t ui32Code = in_ui32Code;

    const uint8_t * ptr = in_pBuffer;
    while (ptr < in_pBuffer + in_ui32BufferSize)
    {
        ui32Code = *ptr++ + (ui32Code << 8);
        if (is_start_code(ui32Code))
            break;
    }

    out_ui32ProcessedBytes = (uint32_t)(ptr - in_pBuffer);

    return ui32Code;
}

void MediaCodecVideoDecoder::convertPacket(FramePacket* packet) {
	uint8_t* data = 0;
	int pos = 0;
	long sum = 0;
	uint8_t header[4];
	header[0] = 0;
	header[1] = 0;
	header[2] = 0;
	header[3] = 1;
	while (pos < packet->size) {
		data = packet->data+pos;
		sum = data[0]*16777216+data[1]*65536+data[2]*256+data[3];
		memcpy(data, header, 4);
		pos += (int)sum;
		pos += 4;
	}
}

void MediaCodecVideoDecoder::flushMediaCodecBuffers() {
	JNIEnv *env;
	int status = 0;
	bool needAttach = false;
	status = g_jvm->GetEnv((void **) (&env), JNI_VERSION_1_4);
	// don't know why, if detach directly, will crash
	if (status < 0) {
		if (g_jvm->AttachCurrentThread(&env, NULL) != JNI_OK) {
			LOGE("%s: AttachCurrentThread() failed", __FUNCTION__);
			return;
		}

		needAttach = true;
	}
	jclass jcls = env->GetObjectClass(obj);
	jmethodID beforeSeekFunc = env->GetMethodID(jcls, "flushMediaCodecBuffersFromNative", "()V");
	env->CallVoidMethod(obj, beforeSeekFunc);
	if (needAttach) {
		if (g_jvm->DetachCurrentThread() != JNI_OK) {
			LOGE("%s: DetachCurrentThread() failed", __FUNCTION__);
		}
	}
}

void MediaCodecVideoDecoder::destroyMediaCodec() {
	JNIEnv *env;
	int status = 0;
	bool needAttach = false;
	status = g_jvm->GetEnv((void **) (&env), JNI_VERSION_1_4);
	// don't know why, if detach directly, will crash
	if (status < 0) {
		if (g_jvm->AttachCurrentThread(&env, NULL) != JNI_OK) {
			LOGE("%s: AttachCurrentThread() failed", __FUNCTION__);
			return;
		}
		needAttach = true;
	}
	if(NULL != inputBuffer){
		env->DeleteGlobalRef(inputBuffer);
		inputBuffer = NULL;
	}
	// release MediaCodec
	jclass jcls = env->GetObjectClass(obj);
	jmethodID clearUpFunc = env->GetMethodID(jcls, "cleanupDecoderFromNative", "()V");
	env->CallVoidMethod(obj, clearUpFunc);
	if (needAttach) {
		if (g_jvm->DetachCurrentThread() != JNI_OK) {
			LOGE("%s: DetachCurrentThread() failed", __FUNCTION__);
		}
	}
}
