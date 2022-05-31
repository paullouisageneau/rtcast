/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef ENDPOINT_H
#define ENDPOINT_H

#include "common.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <shared_mutex>

namespace rtc {

class WebSocket;
class WebSocketServer;
class PeerConnection;
class DataChannel;
class Track;

} // namespace rtc

namespace rtcast {

class Endpoint final {
public:
	Endpoint(uint16_t port);
	~Endpoint();

	enum class VideoCodec {
		None,
		H264,
		H265,
		VP8,
		VP9,
		AV1,
	};

	enum class AudioCodec {
		None,
		OPUS,
		PCMU,
		PCMA,
		AAC,
	};

	void setVideo(VideoCodec codec);
	void setAudio(AudioCodec codec);

	void broadcastVideo(const byte *data, size_t size, std::chrono::microseconds timestamp);
	void broadcastAudio(const byte *data, size_t size, uint32_t timestamp);

private:
	int connect(shared_ptr<rtc::WebSocket> ws);
	void remove(int id);

	VideoCodec mVideoCodec = VideoCodec::None;
	AudioCodec mAudioCodec = AudioCodec::None;

	unique_ptr<rtc::WebSocketServer> mWebSocketServer;

	struct Client {
		std::shared_ptr<rtc::PeerConnection> pc;
		std::shared_ptr<rtc::DataChannel> dc;
		std::shared_ptr<rtc::Track> video;
		std::shared_ptr<rtc::Track> audio;
	};

	std::shared_mutex mMutex;
	std::atomic<int> mNextClientId;
	std::map<int, shared_ptr<Client>> mClients;
};

} // namespace rtcast

#endif
