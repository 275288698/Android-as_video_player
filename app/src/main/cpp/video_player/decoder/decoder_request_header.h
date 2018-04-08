#ifndef VIDEOPLAYER_DECODER_REQUEST_HEADER_H_
#define VIDEOPLAYER_DECODER_REQUEST_HEADER_H_

#include "CommonTools.h"
#include <map>
#include <string>

class DecoderRequestHeader {
private:
	char *uri;
	int* maxAnalyzeDurations;
	int analyzeCnt;
	int probesize;
	bool fpsProbeSizeConfigured;

	std::map<std::string, void*>* extraData;

public:
	DecoderRequestHeader();
	~DecoderRequestHeader();

	void init(char *uriParam) {
		int length = strlen(uriParam);
		uri = new char[length + 1];
		memcpy(uri, uriParam, sizeof(char) * (length + 1));
		extraData = new std::map<std::string, void*>();
		fpsProbeSizeConfigured = false;
	};
	void init(char *uriParam, int* max_analyze_duration, int analyzeCnt, int probesize, bool fpsProbeSizeConfigured) {
		int length = strlen(uriParam);
		uri = new char[length + 1];
		memcpy(uri, uriParam, sizeof(char) * (length + 1));
		maxAnalyzeDurations = new int[analyzeCnt];
		memcpy(maxAnalyzeDurations, max_analyze_duration, sizeof(int) * analyzeCnt);
		this->analyzeCnt = analyzeCnt;
		this->probesize = probesize;
		this->fpsProbeSizeConfigured = fpsProbeSizeConfigured;
		extraData = new std::map<std::string, void*>();
	};

	void destroy() {
		if (NULL != uri) {
			delete[] uri;
			uri = NULL;
		}
		if (NULL != maxAnalyzeDurations) {
			delete[] maxAnalyzeDurations;
			maxAnalyzeDurations = NULL;
		}
		if (NULL != extraData) {
			std::map<std::string, void*>::iterator itr = extraData->begin();
			for (; itr != extraData->end(); ++itr) {
				void* value = itr->second;
				delete value;
			}
			extraData->clear();
			delete extraData;
		}
	};

	char *getURI() {
		return uri;
	};
	int* getMaxAnalyzeDurations() {
		return maxAnalyzeDurations;
	};
	int getAnalyzeCnt() {
		return analyzeCnt;
	};
	int getProbeSize() {
		return probesize;
	};
	bool getFPSProbeSizeConfigured(){
		return fpsProbeSizeConfigured;
	};

	void put(std::string key, void* value) {
		extraData->insert(std::pair<std::string, void*>(key, value));
	};

	void* get(std::string key) {
		std::map<std::string, void*>::iterator itr = extraData->find(key);
		if (itr != extraData->end()) {
			return itr->second;
		}
		return NULL;
	};
};

#endif /* VIDEOPLAYER_DECODER_REQUEST_HEADER_H_ */
