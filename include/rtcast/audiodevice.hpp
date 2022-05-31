/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AUDIO_DEVICE_H
#define AUDIO_DEVICE_H

#include "audioencoder.hpp"
#include "common.hpp"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
}

#include <atomic>

namespace rtcast {

class AudioDevice {
public:
	AudioDevice(string deviceName, shared_ptr<AudioEncoder> encoder);
	virtual ~AudioDevice();

	void start();
	void stop();

private:
	static std::once_flag OnceFlag;

	void run();

	shared_ptr<AudioEncoder> mEncoder;

	unique_ptr_deleter<AVFormatContext> mFormatContext;
	std::thread mThread;
	std::atomic<bool> mRunning = false;

	AVStream *mInputStream;
	const AVCodec *mInputCodec;
	unique_ptr_deleter<AVCodecContext> mInputCodecContext;
};

} // namespace rtcast

#endif
