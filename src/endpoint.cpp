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
using json = nlohmann::json;

Endpoint::Endpoint(uint16_t port) {
	rtc::InitLogger(rtc::LogLevel::Warning);

	rtc::WebSocketServer::Configuration config;
	config.port = port;

	mWebSocketServer = std::make_unique<rtc::WebSocketServer>(std::move(config));
	mWebSocketServer->onClient(std::bind(&Endpoint::connect, this, _1));
}

Endpoint::~Endpoint() {}

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
			std::cout << "Failed to send video: " << e.what() << std::endl;
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
			std::cout << "Failed to send audio: " << e.what() << std::endl;
			client->pc->close();
		}
	}
}

int Endpoint::connect(shared_ptr<rtc::WebSocket> ws) {
	int id = mNextClientId++;
	auto client = std::make_shared<Client>();
	auto wclient = weak_ptr<Client>(client);

	rtc::Configuration config;
	config.iceServers.emplace_back("stun:stun.l.google.com:19302");
	config.disableAutoNegotiation = true;
	client->pc = std::make_shared<rtc::PeerConnection>(std::move(config));

	client->pc->onStateChange([this, id](rtc::PeerConnection::State state) {
		std::cout << "State: " << state << std::endl;
		if (state == rtc::PeerConnection::State::Disconnected ||
		    state == rtc::PeerConnection::State::Failed ||
		    state == rtc::PeerConnection::State::Closed) {
			remove(id);
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

	client->dc = client->pc->createDataChannel("datachannel");

	client->dc->onMessage([id](auto data) {
		if (std::holds_alternative<string>(data)) {
			auto str = std::get<string>(data);
			std::cout << "Message from " << id << " received: " << str << std::endl;
		}
	});

	ws->onOpen([this, wclient]() {
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
			rtc::Description::Video videoDescription(videoMid);
			videoDescription.addSSRC(videoSsrc, videoName);

			switch (mVideoCodec) {
			case VideoCodec::H264:
				videoDescription.addH264Codec(videoPayloadType);
				break;
			case VideoCodec::H265:
				videoDescription.addH265Codec(videoPayloadType);
				break;
			case VideoCodec::VP8:
				videoDescription.addVP8Codec(videoPayloadType);
				break;
			case VideoCodec::VP9:
				videoDescription.addVP9Codec(videoPayloadType);
				break;
			case VideoCodec::AV1:
				videoDescription.addAV1Codec(videoPayloadType);
				break;
			default:
				throw std::logic_error("Unknown video codec");
			}

			client->video = client->pc->addTrack(videoDescription);

			auto videoConfig = std::make_shared<rtc::RtpPacketizationConfig>(
			    videoSsrc, videoName, videoPayloadType, rtc::H264RtpPacketizer::ClockRate);
			auto videoPacketizer = std::make_shared<rtc::H264RtpPacketizer>(
			    rtc::H264RtpPacketizer::Separator::ShortStartSequence, videoConfig);
			videoPacketizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(videoConfig));
			videoPacketizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());

			client->video->setMediaHandler(videoPacketizer);
		}

		if (mAudioCodec != AudioCodec::None) {
			const string audioMid = "audio";
			const string audioName = "audio-stream";
			const int audioPayloadType = 97;
			const uint32_t audioSsrc = dist32(gen);
			rtc::Description::Audio audioDescription(audioMid);
			audioDescription.addSSRC(audioSsrc, audioName);

			switch (mAudioCodec) {
			case AudioCodec::OPUS:
				audioDescription.addOpusCodec(audioPayloadType);
				break;
			case AudioCodec::PCMU:
				audioDescription.addPCMUCodec(audioPayloadType);
				break;
			case AudioCodec::PCMA:
				audioDescription.addPCMACodec(audioPayloadType);
				break;
			case AudioCodec::AAC:
				audioDescription.addAACCodec(audioPayloadType);
				break;
			default:
				throw std::logic_error("Unknown audio codec");
			}

			client->audio = client->pc->addTrack(audioDescription);

			auto audioConfig = std::make_shared<rtc::RtpPacketizationConfig>(
			    audioSsrc, audioName, audioPayloadType, rtc::OpusRtpPacketizer::DefaultClockRate);
			auto audioPacketizer = std::make_shared<rtc::OpusRtpPacketizer>(audioConfig);
			audioPacketizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(audioConfig));
			audioPacketizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());

			client->audio->setMediaHandler(audioPacketizer);
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
