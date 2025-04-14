/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "decoder.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace rtcast {

Decoder::Decoder(string codecName) : mCodecName(std::move(codecName)) {

	// av_log_set_level(AV_LOG_VERBOSE);

	mCodec = avcodec_find_decoder_by_name(mCodecName.c_str());
	if (!mCodec)
		throw std::runtime_error("Failed to find decoder");

	mCodecContext = unique_ptr_deleter<AVCodecContext>(
	    avcodec_alloc_context3(mCodec), [](AVCodecContext *p) { avcodec_free_context(&p); });
	if (!mCodecContext)
		throw std::runtime_error("Failed to allocate decoder context");
}

Decoder::~Decoder() { stop(); }

string Decoder::codecName() const { return mCodecName; }

AVCodecID Decoder::codecID() const { return mCodecContext->codec_id; }

void Decoder::start() {
	int ret = avcodec_open2(mCodecContext.get(), mCodec, nullptr);
	if (ret < 0)
		throw std::runtime_error("Failed to initialize decoder context, ret=" +
		                         std::to_string(ret));

	mRunning = true;
	mThread = std::thread(std::bind(&Decoder::run, this));
}

void Decoder::stop() {
	if (mRunning.exchange(false)) {
		mCondition.notify_all();
		mThread.join();
	}
}

void Decoder::push(shared_ptr<AVPacket> packet) {
	std::unique_lock<std::mutex> lock(mMutex);
	mPacketQueue.emplace(std::move(packet));
	mCondition.notify_all();
}

void Decoder::push(const void *data, size_t size, uint32_t ts) {
	auto packet = shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket *p) { av_packet_free(&p); });
	if (!packet)
		throw std::runtime_error("Failed to allocate packet");

	packet->pts = ts; // TODO: wrap
	packet->time_base = AVRational{1, mCodecContext->sample_rate};

	packet->buf = av_buffer_alloc(size); // TODO: prevent copy
	std::memcpy(packet->buf->data, data, size);
	packet->data = packet->buf->data;
	packet->size = size;

	push(std::move(packet));
}

shared_ptr<AVPacket> Decoder::pop() {
	std::unique_lock<std::mutex> lock(mMutex);
	mCondition.wait(lock, [this]() { return !mPacketQueue.empty() || !mRunning; });
	if (mPacketQueue.empty())
		return nullptr;

	auto frame = std::move(mPacketQueue.front());
	mPacketQueue.pop();
	return frame;
}

void Decoder::run() {
	auto frame = shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *p) { av_frame_free(&p); });
	if (!frame)
		throw std::runtime_error("Failed to allocate AVFrame");

	while (auto packet = pop()) {
		std::unique_lock<std::mutex> lock(mCodecContextMutex);
		std::cout << "Decoding frame, pts=" << packet->pts << ", size=" << packet->size
		          << std::endl;
		int ret = avcodec_send_packet(mCodecContext.get(), packet.get());
		if (ret < 0)
			throw std::runtime_error("Error sending frame for decoding");

		while (ret >= 0) {
			ret = avcodec_receive_frame(mCodecContext.get(), frame.get());
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			else if (ret < 0)
				throw std::runtime_error("Error during decoding");

			std::cout << "Decoded frame, pts=" << frame->pts << std::endl;

			lock.unlock();
			output(frame.get());
			lock.lock();
		}
	}
}

} // namespace rtcast
