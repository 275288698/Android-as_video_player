#include "../sync/av_synchronizer.h"
#include "video_decoder.h"

#define min(a, b)  (((a) < (b)) ? (a) : (b))
#define LOG_TAG "VideoDecoder"

// 埋点，主要用于直播间，这些埋点很难写到一个类似于live_的派生文件里，只能在这里做全局变量了
bool isNeedBuriedPoint = false;
BuriedPoint buriedPoint;
long long buriedPointStart = 0;

VideoDecoder::VideoDecoder(JavaVM *g_jvm, jobject obj) {
	this->g_jvm = g_jvm;
	this->obj = obj;

	connectionRetry = 0;
}

VideoDecoder::VideoDecoder() {
}

VideoDecoder::~VideoDecoder() {
}

static float update_tex_image_callback(TextureFrame* textureFrame, void *context) {
	VideoDecoder* videoDecoder = (VideoDecoder*) context;
	return videoDecoder->updateTexImage(textureFrame);
}

static void signal_decode_thread_callback(void *context) {
	VideoDecoder* videoDecoder = (VideoDecoder*) context;
	return videoDecoder->signalDecodeThread();
}

void VideoDecoder::initFFMpegContext() {
	//注册所有支持的文件格式以及编解码器 之后就可以用所有ffmpeg支持的codec了
	avcodec_register_all();
	av_register_all();
}

int VideoDecoder::openFile(DecoderRequestHeader *requestHeader) {
	isSubscribe = true;
	isOpenInputSuccess = false;
	position = 0.0f;
	seek_req = false;
	seek_resp = false;
	pFormatCtx = NULL;
	subscribeTimeOutTimeMills = SUBSCRIBE_VIDEO_DATA_TIME_OUT;

	videoCodecCtx = NULL;
	videoFrame = NULL;
	videoStreams = NULL;

	swrContext = NULL;
	swrBuffer = NULL;
	audioCodecCtx = NULL;
	audioStreams = NULL;
	audioFrame = NULL;

	textureFrameUploader = NULL;
	int initLockCode = pthread_mutex_init(&mLock, NULL);
	int initConditionCode = pthread_cond_init(&mCondition, NULL);

	subtitleStreams = NULL;
	this->requestHeader = requestHeader;

	this->initFFMpegContext();
	if (isNeedBuriedPoint) {
		buriedPointStart = currentTimeMills();
		buriedPoint.beginOpen = buriedPointStart;
		buriedPoint.duration = 0.0f;
	}
	long long startTimeMills = currentTimeMills();
	int errorCode = openInput();
//	LOGI("openInput [%s] waste TimeMills is %d", requestHeader->getURI(), (int )(currentTimeMills() - startTimeMills));
	//现在 pFormatCtx->streams 中已经有所有流了，因此现在我们遍历它找出对应的视频流、音频流、字幕流等：
	if (errorCode > 0) {
		if (isNeedBuriedPoint) {
			long long curTime = currentTimeMills();
			buriedPoint.successOpen = (curTime - buriedPointStart) / 1000.0f;
			buriedPoint.failOpen = 0.0f;
			buriedPoint.failOpenType = 1;
			LOGI("successOpen is %f", buriedPoint.successOpen);
		}
		int videoErr = openVideoStream();
		int audioErr = openAudioStream();
		if (videoErr < 0 && audioErr < 0) {
			errorCode = -1; // both fails
		} else {
			//解析字幕流
			subtitleStreams = collectStreams(AVMEDIA_TYPE_SUBTITLE);
		}
	} else {
		LOGE("open input failed, have to return");
		if (isNeedBuriedPoint) {
			long long curTime = currentTimeMills();
			buriedPoint.failOpen = (curTime - buriedPointStart) / 1000.0f;
			buriedPoint.successOpen = 0.0f;
			buriedPoint.failOpenType = errorCode;
			LOGI("failOpen is %f", buriedPoint.failOpen);
		}
		return errorCode;
	}
	isOpenInputSuccess = true;
	isVideoOutputEOF = false;
	isAudioOutputEOF = false;
	return errorCode;
}

void VideoDecoder::startUploader(UploaderCallback * pUploaderCallback) {
	mUploaderCallback = pUploaderCallback;
	textureFrameUploader = createTextureFrameUploader();
	textureFrameUploader->registerUpdateTexImageCallback(update_tex_image_callback, signal_decode_thread_callback, this);
	textureFrameUploader->setUploaderCallback(pUploaderCallback);
	textureFrameUploader->start(width, height, degress);
	//wait EGL Context initialize success
	int getLockCode = pthread_mutex_lock(&mLock);
	pthread_cond_wait(&mCondition, &mLock);
	pthread_mutex_unlock(&mLock);
}

int VideoDecoder::openFormatInput(char *videoSourceURI) {
	//打开一个文件 只是读文件头，并不会填充流信息 需要注意的是，此处的pFormatContext必须为NULL或由avformat_alloc_context分配得到
	return avformat_open_input(&pFormatCtx, videoSourceURI, NULL, NULL);
}

int VideoDecoder::initAnalyzeDurationAndProbesize(int* max_analyze_durations, int analyzeDurationSize, int probesize, bool fpsProbeSizeConfigured) {
	return 1;
}

static int determinable_frame_size(AVCodecContext *avctx)
{
    if (/*avctx->codec_id == AV_CODEC_ID_AAC ||*/
            avctx->codec_id == AV_CODEC_ID_MP1 ||
            avctx->codec_id == AV_CODEC_ID_MP2 ||
            avctx->codec_id == AV_CODEC_ID_MP3/* ||
        avctx->codec_id == AV_CODEC_ID_CELT*/)
        return 1;
    return 0;
}

static int has_codec_parameters(AVStream *st)
{
    AVCodecContext *avctx = st->codec;

#define FAIL(errmsg) do {                                         \
        LOGI(errmsg);                                             \
        return 0;                                                 \
    } while (0)

    switch (avctx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            LOGI("check AVMEDIA_TYPE_AUDIO");
            if (!avctx->frame_size && determinable_frame_size(avctx))
                FAIL("unspecified frame size");
            if (!avctx->sample_rate)
                FAIL("unspecified sample rate");
            if (!avctx->channels)
                FAIL("unspecified number of channels");
            if (avctx->sample_fmt == AV_SAMPLE_FMT_NONE)
                FAIL("unspecified sample format");

            if (avctx->codec_id == AV_CODEC_ID_DTS)
                FAIL("no decodable DTS frames");

            break;
        case AVMEDIA_TYPE_VIDEO:
            LOGI("check AVMEDIA_TYPE_VIDEO");
            if (!avctx->width)
                FAIL("unspecified size");
            if (avctx->pix_fmt == AV_PIX_FMT_NONE)
                FAIL("unspecified pixel format");

            if (st->codec->codec_id == AV_CODEC_ID_RV30 || st->codec->codec_id == AV_CODEC_ID_RV40)
                if (!st->sample_aspect_ratio.num && !st->codec->sample_aspect_ratio.num && !st->codec_info_nb_frames)
                    FAIL("no frame in rv30/40 and no sar");
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            LOGI("check AVMEDIA_TYPE_SUBTITLE");
            if (avctx->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE && !avctx->width)
                FAIL("unspecified size");
            break;
        case AVMEDIA_TYPE_DATA:
            LOGI("check AVMEDIA_TYPE_DATA");
            if(avctx->codec_id == AV_CODEC_ID_NONE) return 1;
    }

    if (avctx->codec_id == AV_CODEC_ID_NONE)
        FAIL("unknown codec");

    return 1;
}

bool VideoDecoder::hasAllCodecParameters(){
	if (!pFormatCtx){
		return false;
	}

	for (int i = 0; i < pFormatCtx->nb_streams; i++) {
		AVStream* st = pFormatCtx->streams[i];
		if (!has_codec_parameters(st)) {
			return false;
		}
	}

	return true;
}

bool VideoDecoder::isNeedRetry(){
	return !hasAllCodecParameters();
}

int VideoDecoder::openInput() {
//	LOGI("VideoDecoder::openInput");
	char *videoSourceURI = requestHeader->getURI();
	int* max_analyze_durations = requestHeader->getMaxAnalyzeDurations();
	int analyzeDurationSize = requestHeader->getAnalyzeCnt();

	int tryNum = (connectionRetry <= 0) ? 1 : connectionRetry;
	LOGI("tryNum ===== %d", tryNum);
	int probesize = requestHeader->getProbeSize() + (tryNum-1)*20*1024;

	bool fpsProbeSizeConfigured = requestHeader->getFPSProbeSizeConfigured();
	if (-1 == probesize) {
		probesize = DECODE_PROBESIZE_DEFAULT_VALUE;
	}

	LOGI("probesize ===== %d", probesize);

	readLatestFrameTimemills = currentTimeMills();
	isTimeout = false;
	pFormatCtx = avformat_alloc_context();
	int_cb = {VideoDecoder::interrupt_cb, this};
	pFormatCtx->interrupt_callback = int_cb;
	//打开一个文件 只是读文件头，并不会填充流信息 需要注意的是，此处的pFormatContext必须为NULL或由avformat_alloc_context分配得到
	int openInputErrCode = 0;
	if ((openInputErrCode = this->openFormatInput(videoSourceURI)) != 0) {
		LOGI("Video decoder open input file failed... videoSourceURI is %s openInputErr is %s", videoSourceURI, av_err2str(openInputErrCode));
		return -1;
	}
	this->initAnalyzeDurationAndProbesize(max_analyze_durations, analyzeDurationSize, probesize, fpsProbeSizeConfigured);
//	LOGI("pFormatCtx->max_analyze_duration is %d", pFormatCtx->max_analyze_duration);
//	LOGI("pFormatCtx->probesize is %d", pFormatCtx->probesize);
	//获取文件中的流信息，此函数会读取packet，并确定文件中所有的流信息  设置pFormatCtx->streams指向文件中的流，但此函数并不会改变文件指针，读取的packet会给后面的解码进行处理
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
//		avformat_close_input(&pFormatCtx);
		LOGI("Video decoder Stream info not found...");
		return -1;
	}
	is_eof = false;
	//输出文件的信息，也就是我们在使用ffmpeg时能看到的文件详细信息
//	av_dump_format(pFormatCtx, -1, videoSourceURI, 0);
	if (this->isNeedRetry()) {
		if (isNeedBuriedPoint) {
			long long curTime = currentTimeMills();
			float retryTime = (curTime - buriedPointStart) / 1000.0f;
			buriedPoint.retryOpen.push_back(retryTime);
			LOGI("retryTime is %f", retryTime);
		}
		avformat_close_input(&pFormatCtx);
		avformat_free_context(pFormatCtx);
		return openInput();
	} else {
		LOGI("retry finish");
		return (hasAllCodecParameters() ? 1 : -1);
	}
	return 1;
}

int VideoDecoder::openVideoStream() {
	int errCode = -1;
	videoStreamIndex = -1;
	artworkStreamIndex = -1;
	videoStreams = collectStreams(AVMEDIA_TYPE_VIDEO);
	LOGI("videoStreams size is %d", videoStreams->size());
	std::list<int>::iterator i;
	for (i = videoStreams->begin(); i != videoStreams->end(); ++i) {
		int iStream = *i;
		LOGI("videoStreamIndex is %d", iStream);
		if (0 == (pFormatCtx->streams[iStream]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
			errCode = openVideoStream(iStream);
			if (errCode < 0)
				break;
		} else {
			artworkStreamIndex = iStream;
		}
	}
	return errCode;
}

int VideoDecoder::openVideoStream(int streamIndex) {
	LOGI("VideoDecoder::openVideoStream");
	//1、get a pointer to the codec context for the video stream
	AVStream *videoStream = pFormatCtx->streams[streamIndex];
	degress = 0;
	AVDictionary *videoStreamMetadata = videoStream->metadata;
	AVDictionaryEntry* entry = NULL;
    while ((entry = av_dict_get(videoStreamMetadata, "", entry, AV_DICT_IGNORE_SUFFIX))){
		printf("entry: key is %s value is %s\n", entry->key, entry->value);
		if (0 == strcmp(entry->key, "rotate")) {
			//pull video orientation hint
			degress = atoi(entry->value);
		}
	}
    int* rotate = (int*)requestHeader->get(DECODER_HEADER_FORCE_ROTATE);
    if(NULL != rotate){
    		degress = (*rotate);
    }
    LOGI("degress is %d", degress);
	videoCodecCtx = videoStream->codec;
	//2、通过codecContext的codec_id 找出对应的decoder
	videoCodec = avcodec_find_decoder(videoCodecCtx->codec_id);
	LOGI("CODEC_ID_H264 is %d videoCodecCtx->codec_id is %d", CODEC_ID_H264, videoCodecCtx->codec_id);
	if (videoCodec == NULL) {
		LOGI("can not find the videoStream's Codec ...");
		return -1;
	}
	//3、打开找出的decoder
	if (avcodec_open2(videoCodecCtx, videoCodec, NULL) < 0) {
		LOGI("open video codec failed...");
		return -1;
	}
	//4、分配图像缓存:准备给即将解码的图片分配内存空间 调用 avcodec_alloc_frame 分配帧,videoFrame用于存储解码后的数据
	videoFrame = avcodec_alloc_frame();
	if (videoFrame == NULL) {
		LOGI("alloc video frame failed...");
		avcodec_close(videoCodecCtx);
		return -1;
	}
	//5、now: we think we can Correctly identify the video stream
	this->videoStreamIndex = streamIndex;
	//6、determine fps and videoTimeBase
	avStreamFPSTimeBase(videoStream, 0.04, &fps, &videoTimeBase);
	float* actualFps = (float*)requestHeader->get(DECODER_HEADER_FORCE_FPS);
	if(NULL != actualFps){
		fps = (*actualFps);
	}
	if(fps > 30.0f || fps < 5.0f){
		fps = 24.0f;
	}
	LOGI("video codec size: fps: %.3f tb: %f", fps, videoTimeBase);
//	LOGI("video start time %f", videoStream->start_time * videoTimeBase);
//	LOGI("video disposition %d", videoStream->disposition);
	LOGI("videoCodecCtx->pix_fmt is %d {%d, %d}", videoCodecCtx->pix_fmt, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P);
	if (videoCodecCtx->pix_fmt != AV_PIX_FMT_YUV420P && videoCodecCtx->pix_fmt != AV_PIX_FMT_YUVJ420P) {
		LOGI("NOW we only surpport Format is regular YUV we can render it to OpenGL");
		LOGI("very sorry for this format we must convert to RGB");
		avcodec_close(videoCodecCtx);
		return -1;
	}
	//get video width and height
	width = videoCodecCtx->width;
	height = videoCodecCtx->height;
	LOGI("width is %d height is %d degress is %d", width, height, degress);
	return 1;
}

int VideoDecoder::openAudioStream() {
	int errCode = -1;
	audioStreamIndex = -1;
	audioStreams = collectStreams(AVMEDIA_TYPE_AUDIO);
	LOGI("audioStreams size is %d", audioStreams->size());
	std::list<int>::iterator i;
	for (i = audioStreams->begin(); i != audioStreams->end(); ++i) {
		int iStream = *i;
		LOGI("audioStreamIndex is %d", iStream);
		errCode = openAudioStream(iStream);
		if (errCode > 0) {
			break;
		}
	}
	return errCode;
}

int VideoDecoder::openAudioStream(int streamIndex) {
	LOGI("VideoDecoder::openAudioStream");
	//1、get a pointer to the codec context for the audio stream
	AVStream *audioStream = pFormatCtx->streams[streamIndex];
	audio_stream_duration = pFormatCtx->duration;
	audioCodecCtx = audioStream->codec;
	//2、通过codecContext的codec_id 找出对应的decoder
	LOGI("CODEC_ID_AAC is %d audioCodecCtx->codec_id is %d", CODEC_ID_AAC, audioCodecCtx->codec_id);
	audioCodec = avcodec_find_decoder(audioCodecCtx->codec_id);
	//audioCodec->name is pcm_s16le audioCodec->id is 65536 AV_CODEC_ID_PCM_S16LE is 65536
//	LOGI("audioCodec->name is %s audioCodec->id is %d AV_CODEC_ID_PCM_S16LE is %d", audioCodec->name, audioCodec->id, AV_CODEC_ID_PCM_S16LE);
	if (audioCodec == NULL) {
		LOGI("can not find the audioStream's Codec ...");
		return -1;
	}
	//3、打开找出的decoder
	if (avcodec_open2(audioCodecCtx, audioCodec, NULL) < 0) {
		LOGI("open audio codec failed...");
		avcodec_close(videoCodecCtx);
		return -1;
	}
	//4、判断是否需要resampler
	if (!audioCodecIsSupported(audioCodecCtx)) {
		LOGI("because of audio Codec Is Not Supported so we will init swresampler...");
		/**
		 * 初始化resampler
		 * @param s               Swr context, can be NULL
		 * @param out_ch_layout   output channel layout (AV_CH_LAYOUT_*)
		 * @param out_sample_fmt  output sample format (AV_SAMPLE_FMT_*).
		 * @param out_sample_rate output sample rate (frequency in Hz)
		 * @param in_ch_layout    input channel layout (AV_CH_LAYOUT_*)
		 * @param in_sample_fmt   input sample format (AV_SAMPLE_FMT_*).
		 * @param in_sample_rate  input sample rate (frequency in Hz)
		 * @param log_offset      logging level offset
		 * @param log_ctx         parent logging context, can be NULL
		 */
		swrContext = swr_alloc_set_opts(NULL, av_get_default_channel_layout(audioCodecCtx->channels), AV_SAMPLE_FMT_S16, audioCodecCtx->sample_rate,
				av_get_default_channel_layout(audioCodecCtx->channels), audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate, 0, NULL);
		if (!swrContext || swr_init(swrContext)) {
			if (swrContext)
				swr_free(&swrContext);
			avcodec_close(audioCodecCtx);
			LOGI("init resampler failed...");
			return -1;
		}
	}
	//5、分配audio frame
	audioFrame = avcodec_alloc_frame();
	if (audioFrame == NULL) {
		LOGI("alloc audio frame failed...");
		if (swrContext)
			swr_free(&swrContext);
		avcodec_close(audioCodecCtx);
		return -1;
	}
	audioStreamIndex = streamIndex;
	//6、determine audioTimeBase
	avStreamFPSTimeBase(audioStream, 0.025, 0, &audioTimeBase);
	LOGI(
			"sample rate is %d channels is %d audioCodecCtx->sample_fmt is %d and AV_SAMPLE_FMT_S16 is %d audioTimeBase: %f", audioCodecCtx->sample_rate, audioCodecCtx->channels, audioCodecCtx->sample_fmt, AV_SAMPLE_FMT_S16, audioTimeBase);
	return 1;
}

bool VideoDecoder::audioCodecIsSupported(AVCodecContext *audioCodecCtx) {
	if (audioCodecCtx->sample_fmt == AV_SAMPLE_FMT_S16) {
		return true;
	}
	return false;
}

std::list<int>* VideoDecoder::collectStreams(enum AVMediaType codecType) {
	std::list<int> *ma = new std::list<int>();
	for (int i = 0; i < pFormatCtx->nb_streams; i++) {
		if (codecType == pFormatCtx->streams[i]->codec->codec_type) {
			ma->push_back(i);
		}
	}
	return ma;
}

int VideoDecoder::interrupt_cb(void *ctx) {
	VideoDecoder* videoDecoder = (VideoDecoder*) ctx;
	return videoDecoder->detectInterrupted();
}

int VideoDecoder::detectInterrupted() {
	if (currentTimeMills() - readLatestFrameTimemills > subscribeTimeOutTimeMills) {
		LOGI("VideoDecoder::interrupt_cb");
		isTimeout = true;
		return 1;
	} else {
		return 0;
	}
}

void VideoDecoder::avStreamFPSTimeBase(AVStream *st, float defaultTimeBase, float *pFPS, float *pTimeBase) {
	float fps, timebase;

	if (st->time_base.den && st->time_base.num)
		timebase = av_q2d(st->time_base);
	else if (st->codec->time_base.den && st->codec->time_base.num)
		timebase = av_q2d(st->codec->time_base);
	else
		timebase = defaultTimeBase;

	if (st->codec->ticks_per_frame != 1) {
		LOGI("WARNING: st.codec.ticks_per_frame=%d", st->codec->ticks_per_frame);
	}

	if (st->avg_frame_rate.den && st->avg_frame_rate.num){
		fps = av_q2d(st->avg_frame_rate);
		LOGI("Calculate By St avg_frame_rate : fps is %.3f", fps);
	} else if (st->r_frame_rate.den && st->r_frame_rate.num){
		fps = av_q2d(st->r_frame_rate);
		LOGI("Calculate By St r_frame_rate : fps is %.3f", fps);
	} else {
		fps = 1.0 / timebase;
		LOGI("Calculate By 1.0 / timebase : fps is %.3f", fps);
	}
	if (pFPS) {
		LOGI("assign fps value fps is %.3f", fps);
		*pFPS = fps;
	}
	if (pTimeBase) {
		LOGI("assign pTimeBase value");
		*pTimeBase = timebase;
	}
}

void VideoDecoder::closeFile() {
	subscribeTimeOutTimeMills = -1;
	if (isNeedBuriedPoint && buriedPoint.failOpenType == 1) {
		buriedPoint.duration = (currentTimeMills() - buriedPoint.beginOpen) / 1000.0f;
	}
	if(textureFrameUploader){
		textureFrameUploader->stop();
		delete textureFrameUploader;
		textureFrameUploader = NULL;
	}
	pthread_mutex_lock(&mLock);
	pthread_cond_signal(&mCondition);
	pthread_mutex_unlock(&mLock);

	this->stopSubscribe();
	LOGI("closeAudioStream...");
	closeAudioStream();
	LOGI("closeVideoStream...");
	closeVideoStream();
	LOGI("closeSubtitleStream...");
	closeSubtitleStream();

	if (NULL != pFormatCtx) {
		pFormatCtx->interrupt_callback.opaque = NULL;
		pFormatCtx->interrupt_callback.callback = NULL;
		LOGI("avformat_close_input(&pFormatCtx)");
		avformat_close_input(&pFormatCtx);
		avformat_free_context(pFormatCtx);
		pFormatCtx = NULL;
	}

	pthread_mutex_destroy(&mLock);
	pthread_cond_destroy(&mCondition);
}

void VideoDecoder::closeSubtitleStream() {
	subtitleStreamIndex = -1;
	if (NULL != subtitleStreams) {
		delete subtitleStreams;
		subtitleStreams = 0;
	}
}

void VideoDecoder::closeVideoStream() {
	videoStreamIndex = -1;

	if (NULL != videoFrame) {
		av_free(videoFrame);
		videoFrame = NULL;
	}
	if (NULL != videoCodecCtx) {
		avcodec_close(videoCodecCtx);
		videoCodecCtx = NULL;
	}
	if (NULL != videoStreams) {
		delete videoStreams;
		videoStreams = NULL;
	}
}

void VideoDecoder::closeAudioStream() {
	audioStreamIndex = -1;
	//这里需要判断是否删除resampler(重采样音频格式/声道/采样率等)相关的资源
	if (NULL != swrBuffer) {
		free(swrBuffer);
		swrBuffer = NULL;
		swrBufferSize = 0;
	}
	if (NULL != swrContext) {
		swr_free(&swrContext);
		swrContext = NULL;
	}
	if (NULL != audioFrame) {
		av_free(audioFrame);
		audioFrame = NULL;
	}
	if (NULL != audioCodecCtx) {
		avcodec_close(audioCodecCtx);
		audioCodecCtx = NULL;
	}

	if (NULL != audioStreams) {
		delete audioStreams;
		audioStreams = NULL;
	}
}

void VideoDecoder::seek_frame() {
//	LOGI("enter VideoDecoder::seek_frame seek_seconds is %f", seek_seconds);
	int64_t seek_target = seek_seconds * 1000000;
	int64_t seek_min = INT64_MIN;
	int64_t seek_max = INT64_MAX;
//	LOGI("before avformat_seek_file...");
	int ret = avformat_seek_file(pFormatCtx, -1, seek_min, seek_target, seek_max, 0);
//	LOGI("after avformat_seek_file... ret is %d", ret);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "cc: error while seeking\n");
	} else {
		if(-1 != audioStreamIndex){
			avcodec_flush_buffers(audioCodecCtx);
		}
		avcodec_flush_buffers(videoCodecCtx);
	}

	if (mUploaderCallback)
		mUploaderCallback->onSeekCallback(seek_seconds);
	else
		LOGE("VideoDecoder::mUploaderCallback is NULL");

	seek_resp = true;
//	LOGI("leave VideoDecoder::seek_frame");
}

//void VideoDecoder::seek_frame() {
////	int64_t video_seek_frame = av_rescale_q(seek_seconds * 1000000, AV_TIME_BASE_Q, pFormatCtx->streams[videoStreamIndex]->time_base);
////	av_seek_frame(pFormatCtx, videoStreamIndex, video_seek_frame, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
//	int64_t audio_seek_frame = av_rescale_q(seek_seconds * 1000000, AV_TIME_BASE_Q, pFormatCtx->streams[audioStreamIndex]->time_base);
//	LOGI("video VideoDecoder::seek_frame ............... audio_seek_frame is %lld", audio_seek_frame);
//	av_seek_frame(pFormatCtx, audioStreamIndex, audio_seek_frame, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
//	avcodec_flush_buffers(videoCodecCtx);
//	avcodec_flush_buffers(audioCodecCtx);
//	seek_resp = true;
//}

bool VideoDecoder::decodeVideoTexIdByPosition(float position) {
//	LOGI("enter VideoDecoder::decodeVideoFrame...");

	readLatestFrameTimemills = currentTimeMills();

	AVPacket packet;
	this->seek_seconds = position;
	this->seek_frame();
//	LOGI("read a packet...");
	while(true){
		if (av_read_frame(pFormatCtx, &packet) < 0) {
			av_free_packet(&packet);
			return false;
		}
		if (packet.stream_index == videoStreamIndex) {
			int decodeVideoErrorState = 0;
			bool suc = this->decodeVideoFrame(packet, &decodeVideoErrorState);

			if (suc) {
				av_free_packet(&packet);
				break;
			}

			else {
				av_free_packet(&packet);
				continue;
			}
		}
	}
//	LOGI("leave VideoDecoder::decodeVideoFrame...");
	return true;
}

bool VideoDecoder::decodeAudioFrames(AVPacket* packet, std::list<MovieFrame*> * result, float& decodedDuration,
		float minDuration, int* decodeVideoErrorState) {
	bool finished = false;

	int pktSize = packet->size;

	while (pktSize > 0) {
		int gotframe = 0;
		int len = avcodec_decode_audio4(audioCodecCtx, audioFrame, &gotframe,
				packet);
		if (len < 0) {
			LOGI("decode audio error, skip packet");
			*decodeVideoErrorState = 1;
			break;
		}
		if (gotframe) {
			AudioFrame * frame = handleAudioFrame();
			if (NULL != frame) {
				result->push_back(frame);
				position = frame->position;
				decodedDuration += frame->duration;
				if (decodedDuration > minDuration) {
					finished = true;
				}
			} else {
				LOGI("skip audio...");
			}
		}
		if (0 == len) {
			break;
		}
		pktSize -= len;
	}

	return finished;
}

void VideoDecoder::flushAudioFrames(AVPacket* packet, std::list<MovieFrame*> * result,
		float minDuration, int* decodeVideoErrorState) {
	if (audioCodecCtx->codec->capabilities & CODEC_CAP_DELAY) {
		float decodedDuration = 0.0f;

		while (true) {
			packet->data = 0;
			packet->size = 0;
			av_init_packet (packet);

			int gotframe = 0;
			int len = avcodec_decode_audio4(audioCodecCtx, audioFrame,
					&gotframe, packet);

			if (len < 0) {
				LOGI("decode audio error, skip packet");
				*decodeVideoErrorState = 1;

				break;
			}
			if (gotframe) {
				AudioFrame * frame = handleAudioFrame();
				if (NULL != frame) {
					result->push_back(frame);
					position = frame->position;
					decodedDuration += frame->duration;
					if (decodedDuration > minDuration) {
						break;
					}
				} else {
					LOGI("skip audio...");
				}
			}

			else {
				isAudioOutputEOF = true;
				break;
			}
		}
	}
}

std::list<MovieFrame*>* VideoDecoder::decodeFrames(float minDuration, int* decodeVideoErrorState) {
	if (!isSubscribe || NULL == pFormatCtx) {
		return NULL;
	}
	if (-1 == audioStreamIndex && -1 == videoStreamIndex) {
		return NULL;
	}

	readLatestFrameTimemills = currentTimeMills();
	std::list<MovieFrame*> *result = new std::list<MovieFrame*>();
	AVPacket packet;
	float decodedDuration = 0.0f;
	bool finished = false;

	if (seek_req) {
		//需要先清空视频队列
		this->seek_frame();
	}

	int ret = 0;
	char errString[128];

	while (!finished) {
		ret = av_read_frame(pFormatCtx, &packet);
		if (ret < 0) {
			LOGE("av_read_frame return an error");
			if (ret != AVERROR_EOF) {
				av_strerror(ret, errString, 128);
				LOGE("av_read_frame return an not AVERROR_EOF error : %s", errString);
			} else {
				LOGI("input EOF");
				is_eof = true;
			}
			av_free_packet(&packet);
			break;
		}
		if (packet.stream_index == videoStreamIndex) {
			this->decodeVideoFrame(packet, decodeVideoErrorState);
		} else if (packet.stream_index == audioStreamIndex) {
			finished = decodeAudioFrames(&packet, result, decodedDuration, minDuration, decodeVideoErrorState);
		}
		av_free_packet(&packet);
	}

	// flush video and audio decoder
	// input for decoder end of file
	if (is_eof) {
		// video
		flushVideoFrames(packet, decodeVideoErrorState);
		// audio
		flushAudioFrames(&packet, result, minDuration, decodeVideoErrorState);
	}
	return result;
}

std::list<MovieFrame*>* VideoDecoder::decodeFrames(float minDuration) {
	int decodeVideoErrorState = 0;
	return this->decodeFrames(minDuration, &decodeVideoErrorState);
}

VideoFrame * VideoDecoder::handleVideoFrame() {
//	LOGI("enter VideoDecoder::handleVideoFrame()...");
	if (!videoFrame->data[0]) {
		LOGI("videoFrame->data[0] is 0... why...");
		return NULL;
	}
	VideoFrame *yuvFrame = new VideoFrame();
	int width = MIN(videoFrame->linesize[0], videoCodecCtx->width);
	int height = videoCodecCtx->height;
	int lumaLength = width * height;
	uint8_t * luma = new uint8_t[lumaLength];
	copyFrameData(luma, videoFrame->data[0], width, height, videoFrame->linesize[0]);
	yuvFrame->luma = luma;

	width = MIN(videoFrame->linesize[1], videoCodecCtx->width / 2);
	height = videoCodecCtx->height / 2;
	int chromaBLength = width * height;
	uint8_t * chromaB = new uint8_t[chromaBLength];
	copyFrameData(chromaB, videoFrame->data[1], width, height, videoFrame->linesize[1]);
	yuvFrame->chromaB = chromaB;

	width = MIN(videoFrame->linesize[2], videoCodecCtx->width / 2);
	height = videoCodecCtx->height / 2;
	int chromaRLength = width * height;
	uint8_t * chromaR = new uint8_t[chromaRLength];
	copyFrameData(chromaR, videoFrame->data[2], width, height, videoFrame->linesize[2]);
	yuvFrame->chromaR = chromaR;

	yuvFrame->width = videoCodecCtx->width;
	yuvFrame->height = videoCodecCtx->height;
	/** av_frame_get_best_effort_timestamp 实际上获取AVFrame的 int64_t best_effort_timestamp; 这个Filed **/
	yuvFrame->position = av_frame_get_best_effort_timestamp(videoFrame) * videoTimeBase;

	const int64_t frameDuration = av_frame_get_pkt_duration(videoFrame);
	if (frameDuration) {
		yuvFrame->duration = frameDuration * videoTimeBase;
		yuvFrame->duration += videoFrame->repeat_pict * videoTimeBase * 0.5;
	} else {
		yuvFrame->duration = 1.0 / fps;
	}
//	LOGI("VFD: %.4f %.4f | %lld ", yuvFrame->position, yuvFrame->duration, av_frame_get_pkt_pos(videoFrame));
//	LOGI("leave VideoDecoder::handleVideoFrame()...");
	return yuvFrame;
}

void VideoDecoder::uploadTexture() {
	int getLockCode = pthread_mutex_lock(&mLock);
	textureFrameUploader->signalFrameAvailable();
	//wait EGL Context copy frame
	pthread_cond_wait(&mCondition, &mLock);
	pthread_mutex_unlock(&mLock);
}

void VideoDecoder::signalDecodeThread(){
	int getLockCode = pthread_mutex_lock(&mLock);
	//signal decode thread
	pthread_cond_signal(&mCondition);
	pthread_mutex_unlock(&mLock);
}

AudioFrame * VideoDecoder::handleAudioFrame() {
//	LOGI("enter VideoDecoder::handleAudioFrame()...");
	if (!audioFrame->data[0]) {
		LOGI("audioFrame->data[0] is 0... why...");
		return NULL;
	}
	int numChannels = audioCodecCtx->channels;
	int numFrames = 0;
	void * audioData;
	if (swrContext) {
//		LOGI("start resample audio...");
		const int ratio = 2;
		const int bufSize = av_samples_get_buffer_size(NULL, numChannels, audioFrame->nb_samples * ratio, AV_SAMPLE_FMT_S16, 1);
		if (!swrBuffer || swrBufferSize < bufSize) {
//			LOGI("start realloc buffer and bufSize is %d...", bufSize);
			swrBufferSize = bufSize;
			/**
			 * 指针名=（数据类型*）realloc（要改变内存大小的指针名，新的大小）。
			 * 新的大小一定要大于原来的大小，不然的话会导致数据丢失！
			 * 不考虑数据内容，新的大小可大可小
			 * 头文件
			 * #include <stdlib.h> 有些编译器需要#include <malloc.h>，在TC2.0中可以使用alloc.h头文件
			 * 功能
			 * 先判断当前的指针是否有足够的连续空间，如果有，扩大mem_address指向的地址，并且将mem_address返回，如果空间不够，先按照newsize指定的大小分配空间，将原有数据从头到尾拷贝到新分配的内存区域，而后释放原来mem_address所指内存区域（注意：原来指针是自动释放，不需要使用free），同时返回新分配的内存区域的首地址。即重新分配存储器块的地址。
			 * 返回值
			 * 如果重新分配成功则返回指向被分配内存的指针，否则返回空指针NULL。
			 * 注意
			 * 这里原始内存中的数据还是保持不变的。当内存不再使用时，应使用free()函数将内存块释放。
			 */
			swrBuffer = realloc(swrBuffer, swrBufferSize);
//			LOGI("realloc buffer success");
		}
//		LOGI("define and assign outbuf");
		byte *outbuf[2] = { (byte*) swrBuffer, NULL };
//		LOGI("start swr_convert");
		numFrames = swr_convert(swrContext, outbuf, audioFrame->nb_samples * ratio, (const uint8_t **) audioFrame->data, audioFrame->nb_samples);
//		LOGI("swr_convert success and numFrames is %d", numFrames);
		if (numFrames < 0) {
			LOGI("fail resample audio");
			return NULL;
		}
		audioData = swrBuffer;
	} else {
		if (audioCodecCtx->sample_fmt != AV_SAMPLE_FMT_S16) {
			LOGI("bucheck, audio format is invalid");
			return NULL;
		}
		audioData = audioFrame->data[0];
		numFrames = audioFrame->nb_samples;
	}
//	LOGI("start process audioData and numFrames is %d numChannels is %d", numFrames, numChannels);
	const int numElements = numFrames * numChannels;
	float position = av_frame_get_best_effort_timestamp(audioFrame) * audioTimeBase;
//	LOGI("begin processAudioData...");
//	LOGI("decode audio bufferSize expected 1024  actual is %d audio position is %.4f", numElements, position);
	byte* buffer = NULL;

	int actualSize = -1;
	if (mUploaderCallback)
		actualSize = mUploaderCallback->processAudioData((short*)audioData, numElements, position, &buffer);
	else
		LOGE("VideoDecoder::mUploaderCallback is NULL");
	if (actualSize  < 0) {
		//如果处理失败 有可能是position对应不上或者size不对等等
//		LOGI("可能是position对应不上 ..........");
		return NULL;
	}

	AudioFrame *frame = new AudioFrame();
	/** av_frame_get_best_effort_timestamp 实际上获取AVFrame的 int64_t best_effort_timestamp; 这个Filed **/
	frame->position = position;
	frame->samples = buffer;
	frame->size = actualSize;
	frame->duration = av_frame_get_pkt_duration(audioFrame) * audioTimeBase;
	if (frame->duration == 0) {
		// sometimes ffmpeg can't determine the duration of audio frame
		// especially of wma/wmv format
		// so in this case must compute duration
		frame->duration = frame->size / (sizeof(float) * numChannels * 2 * audioCodecCtx->sample_rate);
	}
//	LOGI("AFD: %.4f %.4f | %.4f ", frame->position, frame->duration, frame->size / (8.0 * 44100.0));
//	LOGI("leave VideoDecoder::handleAudioFrame()...");
	return frame;
}

void VideoDecoder::copyFrameData(uint8_t * dst, uint8_t * src, int width, int height, int linesize) {
	for (int i = 0; i < height; ++i) {
		memcpy(dst, src, width);
		dst += width;
		src += linesize;
	}
}

void VideoDecoder::setPosition(float seconds) {
//	LOGI("enter VideoDecoder::seekToPosition...");
	this->seek_seconds = seconds;
	this->seek_req = true;
	this->seek_resp = false;
	this->is_eof = false;

    isVideoOutputEOF = false;
	isAudioOutputEOF = false;
}
