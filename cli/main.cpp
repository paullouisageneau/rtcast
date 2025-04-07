/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "rtcast/rtcast.hpp"

#include <chrono>
#include <iostream>
#include <thread>

using std::make_shared;
using std::string;

int main() {
	try {
		auto endpoint = make_shared<rtcast::Endpoint>(8888);
		auto videoEncoder = make_shared<rtcast::VideoEncoder>("libx264", endpoint);
		auto audioEncoder = make_shared<rtcast::AudioEncoder>("libopus", endpoint);

		videoEncoder->setBitrate(4000000);

#ifdef RTCAST_HAS_LIBCAMERA
		rtcast::CameraDevice video("default", videoEncoder);
		video.start();
#else
		rtcast::VideoDevice video("default", videoEncoder);
		video.start();
#endif
		rtcast::AudioDevice audio("default", audioEncoder);
		audio.start();

		endpoint->onMessage([](int id, string message) {
			std::cout << "Message from " << id << ": " << message << std::endl;
		});

		std::this_thread::sleep_for(std::chrono::seconds::max());

	} catch (const std::exception &e) {
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
