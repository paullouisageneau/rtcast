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
#include <map>
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
class Stream;

} // namespace libcamera

namespace rtcast {

class CameraDevice final {
public:
	struct Settings {
		static Settings Default() { return {}; }
		int width = 0;
		int height = 0;
		int framerate = 0;
	};

	CameraDevice(string deviceName, shared_ptr<VideoEncoder> encoder,
	             Settings settings = Settings::Default());
	~CameraDevice();

	void initInputCodec(AVCodecID codecId);

	void start();
	void stop();

private:
	class DmaFrameBufferAllocator final {
	public:
		using FrameBuffer = libcamera::FrameBuffer;

		DmaFrameBufferAllocator();
		~DmaFrameBufferAllocator();

		void allocate(libcamera::Stream *stream);
		void free(libcamera::Stream *stream);
		bool allocated() const;

		const std::vector<std::unique_ptr<FrameBuffer>> &buffers(libcamera::Stream *stream) const;

	private:
		std::map<libcamera::Stream *, std::vector<unique_ptr<FrameBuffer>>> mBuffers;
		int mDmaHeap = -1;
	};

	static std::once_flag OnceFlag;
	static std::unique_ptr<libcamera::CameraManager> CameraManager;

	void requestComplete(libcamera::Request *request);

	shared_ptr<VideoEncoder> mEncoder;
	Settings mSettings;

	shared_ptr<libcamera::Camera> mCamera;
	unique_ptr<libcamera::CameraConfiguration> mConfig;
	std::vector<std::unique_ptr<libcamera::Request>> mRequests;
	shared_ptr<DmaFrameBufferAllocator> mDmaAllocator;
	shared_ptr<libcamera::FrameBufferAllocator> mAllocator;

	unique_ptr_deleter<AVCodecContext> mInputCodecContext;
};

} // namespace rtcast

#endif

#endif
