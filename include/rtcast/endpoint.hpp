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
#include "audiodecoder.hpp"

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
	void broadcastMessage(string message);
	void sendMessage(int id, string message);

	using message_callback = std::function<void(int id, string message)>;
	void receiveMessage(message_callback callback);

	using audio_decoder_callback = std::function<shared_ptr<AudioDecoder>(int id)>;
	void receiveAudio(audio_decoder_callback callback);

	unsigned int clientsCount() const;

private:
	int connect(shared_ptr<rtc::WebSocket> ws);
	void remove(int id);

	std::atomic<VideoCodec> mVideoCodec = VideoCodec::None;
	std::atomic<AudioCodec> mAudioCodec = AudioCodec::None;
	std::atomic<bool> mReceiveVideo = false;
	std::atomic<bool> mReceiveAudio = false;

	unique_ptr<rtc::WebSocketServer> mWebSocketServer;

	struct Client {
		std::shared_ptr<rtc::PeerConnection> pc;
		std::shared_ptr<rtc::DataChannel> dc;
		std::shared_ptr<rtc::Track> video;
		std::shared_ptr<rtc::Track> audio;
	};

	std::shared_mutex mMutex;
	std::atomic<int> mNextClientId = 0;
	std::map<int, shared_ptr<Client>> mClients;

	std::mutex mMessageCallbackMutex;
	message_callback mMessageCallback;

	std::mutex mDecoderCallbackMutex;
	audio_decoder_callback mAudioDecoderCallback;
};

} // namespace rtcast

#endif
