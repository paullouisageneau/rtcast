/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef ENCODER_H
#define ENCODER_H

#include "common.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
}

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace rtcast {

class Encoder {
public:
	Encoder(string codecName);
	virtual ~Encoder();

	string codecName() const;
	AVCodecID codecID() const;

	void setBitrate(int64_t bitrate);

	void start();
	void stop();

	virtual void push(shared_ptr<AVFrame> frame);

protected:
	virtual void output(AVPacket *packet) = 0;

	const AVCodec *mCodec;
	unique_ptr_deleter<AVCodecContext> mCodecContext;
	std::mutex mCodecContextMutex;

private:
	shared_ptr<AVFrame> pop();
	void run();

	string mCodecName;
	std::thread mThread;
	std::mutex mMutex;
	std::condition_variable mCondition;
	std::atomic<bool> mRunning = false;

	std::queue<shared_ptr<AVFrame>> mFrameQueue;
};

} // namespace rtcast

#endif
