/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "audioplayer.hpp"

#if RTCAST_HAS_LIBAO

extern "C" {
#include <ao/ao.h>
}

#include <iostream>
#include <mutex>
#include <stdexcept>

namespace rtcast {

AudioPlayer::AudioPlayer(string driverName) {
	struct Global {
		Global() { ao_initialize(); }
		~Global() { ao_shutdown(); }
	};
	static std::once_flag onceFlag;
	static unique_ptr<Global> global;
	std::call_once(onceFlag, []() { global = std::make_unique<Global>(); });

	if (driverName.empty() || driverName == "default")
		mDriverId = ao_default_driver_id();
	else
		mDriverId = ao_driver_id(driverName.c_str());

	if (mDriverId < 0)
		throw std::runtime_error("Failed to find audio output driver");
}

AudioPlayer::~AudioPlayer() {}

void AudioPlayer::init(const Config &config) {
	ao_sample_format format;
	format.rate = config.sampleRate;
	format.bits = config.sampleBits;
	format.channels = config.nbChannels;
	format.byte_format = AO_FMT_NATIVE;
	format.matrix = nullptr;

	mDevice = unique_ptr_deleter<ao_device>(ao_open_live(mDriverId, &format, NULL),
	                                        [](ao_device *device) { ao_close(device); });
	if (!mDevice)
		throw std::runtime_error("Failed to open audio output");

	std::cout << "Audio player output device opened" << std::endl;
}

void AudioPlayer::play(void *data, size_t size) {
	if (mDevice)
		ao_play(mDevice.get(), static_cast<char *>(data), size);
}

} // namespace rtcast

#endif
