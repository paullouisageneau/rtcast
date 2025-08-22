/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "encoder.hpp"

#include <iostream>
#include <stdexcept>

namespace rtcast {

const int MaxFrameQueueSize = 10;

Encoder::Encoder(string codecName) : mCodecName(std::move(codecName)) {

	// av_log_set_level(AV_LOG_VERBOSE);

	mCodec = avcodec_find_encoder_by_name(mCodecName.c_str());
	if (!mCodec)
		throw std::runtime_error("Failed to find encoder");

	mCodecContext = unique_ptr_deleter<AVCodecContext>(
	    avcodec_alloc_context3(mCodec), [](AVCodecContext *p) { avcodec_free_context(&p); });
	if (!mCodecContext)
		throw std::runtime_error("Failed to allocate encoder context");
}

Encoder::~Encoder() { stop(); }

string Encoder::codecName() const { return mCodecName; }

AVCodecID Encoder::codecID() const { return mCodecContext->codec_id; }

void Encoder::setBitrate(int64_t bitrate) {
	std::unique_lock<std::mutex> lock(mCodecContextMutex);
	mCodecContext->bit_rate = bitrate;
}

void Encoder::start() {
	int ret = avcodec_open2(mCodecContext.get(), mCodec, nullptr);
	if (ret < 0)
		throw std::runtime_error("Failed to initialize encoder context, ret=" + std::to_string(ret));

	mRunning = true;
	mThread = std::thread(std::bind(&Encoder::run, this));
}

void Encoder::stop() {
	if (mRunning.exchange(false)) {
		mCondition.notify_all();
		mThread.join();
	}
}

void Encoder::push(shared_ptr<AVFrame> frame) {
	std::unique_lock<std::mutex> lock(mMutex);
	if (mFrameQueue.size() >= MaxFrameQueueSize) {
        std::cout << "Dropping frame (queue is full), pts=" << frame->pts << std::endl;
        return;
    }
	mFrameQueue.emplace(std::move(frame));
	mCondition.notify_all();
}

shared_ptr<AVFrame> Encoder::pop() {
	std::unique_lock<std::mutex> lock(mMutex);
	mCondition.wait(lock, [this]() { return !mFrameQueue.empty() || !mRunning; });
	if (mFrameQueue.empty())
		return nullptr;

	auto frame = std::move(mFrameQueue.front());
	mFrameQueue.pop();
	return frame;
}

void Encoder::run() {
	auto packet = shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket *p) { av_packet_free(&p); });
	if (!packet)
		throw std::runtime_error("Failed to allocate AVPacket");

	while (auto frame = pop()) {
		std::unique_lock<std::mutex> lock(mCodecContextMutex);
		std::cout << "Encoding frame, pts=" << frame->pts << std::endl;
		int ret = avcodec_send_frame(mCodecContext.get(), frame.get());
		if (ret < 0)
			throw std::runtime_error("Error sending frame for encoding");

		while (ret >= 0) {
			ret = avcodec_receive_packet(mCodecContext.get(), packet.get());
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			else if (ret < 0)
				throw std::runtime_error("Error during encoding");

			std::cout << "Encoded frame, pts=" << packet->pts << ", size=" << packet->size
			          << std::endl;

			lock.unlock();
			output(packet.get());
			lock.lock();
		}
	}
}

} // namespace rtcast
