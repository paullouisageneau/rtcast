/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "endpoint.hpp"

#include "nlohmann/json.hpp"
#include "rtc/rtc.hpp"

#include <iostream>
#include <random>
#include <stdexcept>

namespace {

template <class T> std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr) { return ptr; }

} // namespace

namespace rtcast {

using namespace std::placeholders;
using std::chrono::duration_cast;
using json = nlohmann::json;

Endpoint::Endpoint(uint16_t port) {
	rtc::InitLogger(rtc::LogLevel::Warning);

	rtc::WebSocketServer::Configuration config;
	config.port = port;

	mWebSocketServer = std::make_unique<rtc::WebSocketServer>(std::move(config));
	mWebSocketServer->onClient(std::bind(&Endpoint::connect, this, _1));
}

Endpoint::~Endpoint() {
	// TODO: close everything
}

void Endpoint::setVideo(VideoCodec codec) {
	if (mVideoCodec != VideoCodec::None)
		throw std::logic_error("Video is already set for the endpoint");

	mVideoCodec = codec;
}

void Endpoint::setAudio(AudioCodec codec) {
	if (mAudioCodec != AudioCodec::None)
		throw std::logic_error("Audio is already set for the endpoint");

	mAudioCodec = codec;
}

void Endpoint::broadcastVideo(const byte *data, size_t size, std::chrono::microseconds timestamp) {
	if (mVideoCodec == VideoCodec::None)
		return;

	std::shared_lock lock(mMutex);
	for (const auto &[id, client] : mClients) {
		try {
			if (client->video && client->video->isOpen())
				client->video->sendFrame(data, size, std::chrono::duration<double>(timestamp));

		} catch (const std::exception &e) {
			std::cerr << "Failed to send video: " << e.what() << std::endl;
			client->pc->close();
		}
	}
}

void Endpoint::broadcastAudio(const byte *data, size_t size, uint32_t timestamp) {
	if (mAudioCodec == AudioCodec::None)
		return;

	std::shared_lock lock(mMutex);
	for (const auto &[id, client] : mClients) {
		try {
			if (client->audio && client->audio->isOpen())
				client->audio->sendFrame(data, size, timestamp);

		} catch (const std::exception &e) {
			std::cerr << "Failed to send audio: " << e.what() << std::endl;
			client->pc->close();
		}
	}
}

void Endpoint::broadcastMessage(string message) {
	std::shared_lock lock(mMutex);
	for (const auto &[id, client] : mClients) {
		try {
			if (client->dc && client->dc->isOpen())
				client->dc->send(message);

		} catch (const std::exception &e) {
			std::cerr << "Failed to send message: " << e.what() << std::endl;
			client->pc->close();
		}
	}
}

void Endpoint::sendMessage(int id, string message) {
	std::shared_lock lock(mMutex);
	if (auto it = mClients.find(id); it != mClients.end()) {
		const auto &client = it->second;
		try {
			if (client->dc && client->dc->isOpen())
				client->dc->send(message);

		} catch (const std::exception &e) {
			std::cerr << "Failed to send message: " << e.what() << std::endl;
			client->pc->close();
		}
	}
}

void Endpoint::receiveMessage(message_callback callback) {
	std::lock_guard lock(mMessageCallbackMutex);
	mMessageCallback = std::move(callback);
}

void Endpoint::receiveAudio(audio_decoder_callback callback) {
	std::lock_guard lock(mDecoderCallbackMutex);
	mAudioDecoderCallback = std::move(callback);
	mReceiveAudio = mAudioDecoderCallback != nullptr;
}

int Endpoint::connect(shared_ptr<rtc::WebSocket> ws) {
	int id = mNextClientId++;
	auto client = std::make_shared<Client>();
	auto wclient = weak_ptr<Client>(client);

	rtc::Configuration config;
	config.iceServers.emplace_back("stun:stun.l.google.com:19302");
	config.disableAutoNegotiation = true;
	client->pc = std::make_shared<rtc::PeerConnection>(std::move(config));

	client->pc->onStateChange([this, id, wclient](rtc::PeerConnection::State state) {
		std::cout << "State: " << state << std::endl;
		using State = rtc::PeerConnection::State;
		switch (state) {
		case State::Disconnected:
		case State::Failed:
			if (auto client = wclient.lock())
				client->pc->close();
			break;
		case State::Closed:
			remove(id);
			break;
		default:
			break;
		}
	});

	client->pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) {
		std::cout << "Gathering State: " << state << std::endl;
	});

	client->pc->onLocalDescription([ws](rtc::Description description) {
		json message = {{"type", description.typeString()}, {"description", string(description)}};
		ws->send(message.dump());
	});

	client->pc->onLocalCandidate([ws](rtc::Candidate candidate) {
		json message = {
		    {"type", "candidate"}, {"candidate", string(candidate)}, {"mid", candidate.mid()}};
		ws->send(message.dump());
	});

	client->dc = client->pc->createDataChannel("default");

	client->dc->onMessage([this, id](auto data) {
		if (std::holds_alternative<string>(data)) {
			auto str = std::get<string>(data);
			std::lock_guard lock(mMessageCallbackMutex);
			if (mMessageCallback)
				mMessageCallback(id, std::move(str));
		}
	});

	ws->onOpen([this, id, wclient]() {
		std::cout << "WebSocket connected" << std::endl;
		auto client = wclient.lock();
		if (!client)
			return;

		std::random_device rnd;
		std::mt19937 gen(rnd());
		std::uniform_int_distribution<uint32_t> dist32;

		if (mVideoCodec != VideoCodec::None) {
			const string videoMid = "video";
			const string videoName = "video-stream";
			const int videoPayloadType = 96;
			const uint32_t videoSsrc = dist32(gen);

			const auto direction = mReceiveVideo ? rtc::Description::Direction::SendRecv
			                                     : rtc::Description::Direction::SendOnly;

			rtc::Description::Video description(videoMid, direction);
			description.addSSRC(videoSsrc, videoName);

			auto packetizerConfig = std::make_shared<rtc::RtpPacketizationConfig>(
			    videoSsrc, videoName, videoPayloadType, rtc::H264RtpPacketizer::ClockRate);

			shared_ptr<rtc::MediaHandler> packetizer;
			switch (mVideoCodec) {
			case VideoCodec::H264:
				description.addH264Codec(videoPayloadType);
				packetizer = std::make_shared<rtc::H264RtpPacketizer>(
				    rtc::H264RtpPacketizer::Separator::ShortStartSequence, packetizerConfig);
				break;
			case VideoCodec::H265:
				description.addH265Codec(videoPayloadType);
				packetizer = std::make_shared<rtc::H265RtpPacketizer>(
				    rtc::H265RtpPacketizer::Separator::ShortStartSequence, packetizerConfig);
				break;
			case VideoCodec::VP8:
				description.addVP8Codec(videoPayloadType);
				throw std::logic_error("VP8 packetizer not implemented");
				break;
			case VideoCodec::VP9:
				description.addVP9Codec(videoPayloadType);
				throw std::logic_error("VP9 packetizer not implemented");
				break;
			case VideoCodec::AV1:
				description.addAV1Codec(videoPayloadType);
				throw std::logic_error("AV1 packetizer not implemented");
				break;
			default:
				throw std::logic_error("Unknown video codec");
			}

			auto track = client->pc->addTrack(std::move(description));
			track->chainMediaHandler(packetizer);
			track->chainMediaHandler(std::make_shared<rtc::RtcpSrReporter>(packetizerConfig));
			track->chainMediaHandler(std::make_shared<rtc::RtcpNackResponder>());
			if (mReceiveVideo) {
				switch (mVideoCodec) {
				case VideoCodec::H264:
					track->chainMediaHandler(std::make_shared<rtc::H264RtpDepacketizer>(
					    rtc::H264RtpDepacketizer::Separator::ShortStartSequence));
					break;
				case VideoCodec::H265:
					track->chainMediaHandler(std::make_shared<rtc::H265RtpDepacketizer>(
					    rtc::H265RtpDepacketizer::Separator::ShortStartSequence));
					break;
				case VideoCodec::VP8:
					throw std::logic_error("VP8 depacketizer not implemented");
					break;
				case VideoCodec::VP9:
					throw std::logic_error("VP9 depacketizer not implemented");
					break;
				case VideoCodec::AV1:
					throw std::logic_error("AV1 depacketizer not implemented");
					break;
				default:
					throw std::logic_error("Unknown video codec");
				}

				track->onFrame([](binary, rtc::FrameInfo) {
					// TODO
				});
			}

			client->video = std::move(track);
		}

		if (mAudioCodec != AudioCodec::None) {
			const string audioMid = "audio";
			const string audioName = "audio-stream";
			const int audioPayloadType = 97;
			const uint32_t audioSsrc = dist32(gen);

			const auto direction = mReceiveAudio ? rtc::Description::Direction::SendRecv
			                                     : rtc::Description::Direction::SendOnly;

			rtc::Description::Audio description(audioMid, direction);
			description.addSSRC(audioSsrc, audioName);

			switch (mAudioCodec) {
			case AudioCodec::OPUS:
				description.addOpusCodec(audioPayloadType);
				break;
			case AudioCodec::PCMU:
				description.addPCMUCodec(audioPayloadType);
				break;
			case AudioCodec::PCMA:
				description.addPCMACodec(audioPayloadType);
				break;
			case AudioCodec::AAC:
				description.addAACCodec(audioPayloadType);
				break;
			default:
				throw std::logic_error("Unknown audio codec");
			}

			auto track = client->pc->addTrack(std::move(description));

			auto packetizerConfig = std::make_shared<rtc::RtpPacketizationConfig>(
			    audioSsrc, audioName, audioPayloadType, rtc::OpusRtpPacketizer::DefaultClockRate);

			if (mAudioCodec == AudioCodec::PCMU || mAudioCodec == AudioCodec::PCMA)
				track->chainMediaHandler(
				    std::make_shared<rtc::AudioRtpPacketizer<8000>>(packetizerConfig));
			else
				track->chainMediaHandler(
				    std::make_shared<rtc::AudioRtpPacketizer<48000>>(packetizerConfig));

			track->chainMediaHandler(std::make_shared<rtc::RtcpSrReporter>(packetizerConfig));
			track->chainMediaHandler(std::make_shared<rtc::RtcpNackResponder>());
			if (mReceiveAudio) {
				if (mAudioCodec == AudioCodec::PCMU || mAudioCodec == AudioCodec::PCMA)
					track->chainMediaHandler(std::make_shared<rtc::RtpDepacketizer>(8000));
				else
					track->chainMediaHandler(std::make_shared<rtc::RtpDepacketizer>(48000));

				std::lock_guard lock(mDecoderCallbackMutex);
				auto decoder = mAudioDecoderCallback ? mAudioDecoderCallback(id) : nullptr;

				track->onFrame([decoder](binary data, rtc::FrameInfo info) {
					if(decoder)
						decoder->push(data.data(), data.size(), info.timestamp);
				});
			}

			client->audio = std::move(track);
		}

		client->pc->setLocalDescription();
	});

	ws->onClosed([]() { std::cout << "WebSocket closed" << std::endl; });

	ws->onError([](string error) { std::cout << "WebSocket failed: " << error << std::endl; });

	ws->onMessage([wclient](auto data) {
		auto client = wclient.lock();
		if (!client)
			return;

		if (std::holds_alternative<string>(data)) {
			json message = json::parse(std::get<string>(data));
			auto type = message["type"].get<string>();
			if (type == "offer" || type == "answer") {
				auto sdp = message["description"].get<string>();
				client->pc->setRemoteDescription(rtc::Description(sdp, type));
			} else if (type == "candidate") {
				auto sdp = message["candidate"].get<string>();
				auto mid = message["mid"].get<string>();
				client->pc->addRemoteCandidate(rtc::Candidate(sdp, mid));
			}
		}
	});

	std::unique_lock lock(mMutex);
	mClients[id] = client;
	return id;
}

void Endpoint::remove(int id) {
	std::unique_lock lock(mMutex);
	mClients.erase(id);
}

} // namespace rtcast
