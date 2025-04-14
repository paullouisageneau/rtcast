/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AUDIO_SINK_H
#define AUDIO_SINK_H

#include "common.hpp"

#include <chrono>

namespace rtcast {

class AudioSink {
public:
	AudioSink() = default;
	virtual ~AudioSink() = default;

	struct Config {
		int sampleRate = 48000;
		int sampleBits = 16;
		int nbChannels = 2;
	};

	virtual void init(const Config &config) = 0;
	virtual void play(void *data, size_t size) = 0;
};

} // namespace rtcast

#endif
