/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#if RTCAST_HAS_LIBAO

#include "audiosink.hpp"

struct ao_device;

namespace rtcast {

class AudioPlayer final : public AudioSink {
public:
	AudioPlayer(string driverName);
	~AudioPlayer();

	void init(const Config &config) override;
	void play(void *data, size_t size) override;

private:
	int mDriverId;
	unique_ptr_deleter<ao_device> mDevice;
};

} // namespace rtcast

#endif

#endif

