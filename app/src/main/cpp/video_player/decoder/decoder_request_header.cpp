#include "decoder_request_header.h"

#define LOG_TAG "DecoderRequestHeader"

DecoderRequestHeader::DecoderRequestHeader() {
	uri = NULL;
	maxAnalyzeDurations = NULL;
	extraData = NULL;
}
DecoderRequestHeader::~DecoderRequestHeader() {
}
