/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "rtcast/rtcast.hpp"
#include "nlohmann/json.hpp"

#include "serial.hpp"

#include <chrono>
#include <iostream>
#include <thread>

using std::make_shared;
using std::string;
using nlohmann::json;

int main() {
	try {
		auto endpoint = make_shared<rtcast::Endpoint>(8888);
		auto videoEncoder = make_shared<rtcast::DrmVideoEncoder>("h264_v4l2m2m", endpoint);
		auto audioEncoder = make_shared<rtcast::AudioEncoder>("libopus", endpoint);

		videoEncoder->setBitrate(4000000);

#if RTCAST_HAS_LIBCAMERA
		rtcast::CameraDevice video("default", videoEncoder);
		video.start();
#else
		rtcast::VideoDevice video("default", videoEncoder);
		video.start();
#endif
		rtcast::AudioDevice audio("default:1", audioEncoder);
		audio.start();

#if RTCAST_HAS_LIBAO
		endpoint->receiveAudio([]([[maybe_unused]] int id) {
			auto player = std::make_shared<rtcast::AudioPlayer>("default");
			auto decoder = std::make_shared<rtcast::AudioDecoder>("libopus", player);
			decoder->start();
			return decoder;
		});
#endif

		auto serial = std::make_shared<Serial>("/dev/ttyAMA0", 9600);

		endpoint->receiveMessage([serial](int id, string data) {
			std::cout << "Message from " << id << ": " << data << std::endl;
			json message = json::parse(data);
			if(auto control = message["control"]; control.is_object()) {
				auto left = control["left"].get<int>();
				auto right = control["right"].get<int>();
				serial->write("L" + std::to_string(left) + "\n");
				serial->write("R" + std::to_string(right) + "\n");
				serial->write("C\n");
			}
		});

		std::this_thread::sleep_for(std::chrono::seconds::max());

	} catch (const std::exception &e) {
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
