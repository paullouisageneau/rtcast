/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H

#include "encoder.hpp"
#include "endpoint.hpp"

extern "C" {
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <chrono>

namespace rtcast {

class VideoEncoder : public Encoder {
public:
	VideoEncoder(string codecName, shared_ptr<Endpoint> endpoint);
	virtual ~VideoEncoder();

	void setSize(int width, int height);
	void setFramerate(AVRational framerate);
	void setFramerate(int framerate);
	void setGopSize(int gopsize);

	struct ColorSettings {
		AVColorPrimaries primaries = AVCOL_PRI_BT709;
		AVColorTransferCharacteristic transferCharacteristic = AVCOL_TRC_BT709;
		AVColorSpace space = AVCOL_SPC_BT709;
		AVColorRange range = AVCOL_RANGE_JPEG;
	};

	void setColorSettings(ColorSettings settings);

	using finished_callback_t = std::function<void()>;

	struct Plane {
		int fd = -1;
		void *data = nullptr;
		size_t size = 0;
	};

	struct InputFrame {
		std::chrono::microseconds ts;
		AVPixelFormat pixelFormat = AV_PIX_FMT_YUV420P;
		int width = 0;
		int height = 0;
		std::vector<Plane> planes;
		std::vector<int> linesize;
		finished_callback_t finished;
	};

	virtual void push(shared_ptr<AVFrame> frame) override;
	virtual void push(InputFrame input);

protected:
	void output(AVPacket *packet) override;

private:
	shared_ptr<Endpoint> mEndpoint;

	unique_ptr_deleter<SwsContext> mSwsContext;
	int mSwsInputWidth;
	int mSwsInputHeight;
	AVPixelFormat mSwsInputPixelFormat;
};

} // namespace rtcast

#endif
