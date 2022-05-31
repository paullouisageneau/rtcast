/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef CAMERA_DEVICE_H
#define CAMERA_DEVICE_H

#if RTCAST_HAS_LIBCAMERA

#include "videoencoder.hpp"

extern "C" {
#include "libavcodec/avcodec.h"
}

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace libcamera {

class Camera;
class CameraManager;
class CameraConfiguration;
class FrameBuffer;
class FrameBufferAllocator;
class Request;

} // namespace libcamera

namespace rtcast {

class CameraDevice final {
public:
	CameraDevice(string deviceName, shared_ptr<VideoEncoder> encoder);
	~CameraDevice();

	void initInputCodec(AVCodecID codecId);

	void start();
	void stop();

private:
	static std::once_flag OnceFlag;
	static std::unique_ptr<libcamera::CameraManager> CameraManager;

	void requestComplete(libcamera::Request *request);

	shared_ptr<VideoEncoder> mEncoder;

	shared_ptr<libcamera::Camera> mCamera;
	shared_ptr<libcamera::FrameBufferAllocator> mAllocator;
	unique_ptr<libcamera::CameraConfiguration> mConfig;

	std::vector<std::unique_ptr<libcamera::FrameBuffer>> mBuffers;
	std::vector<std::unique_ptr<libcamera::Request>> mRequests;

	unique_ptr_deleter<AVCodecContext> mInputCodecContext;
};

} // namespace rtcast

#endif

#endif
