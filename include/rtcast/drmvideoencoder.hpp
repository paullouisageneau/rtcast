/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef DRM_VIDEO_ENCODER_H
#define DRM_VIDEO_ENCODER_H

#include "videoencoder.hpp"

extern "C" {
#include <libavutil/hwcontext_drm.h>
#include <libdrm/drm_fourcc.h>
}

#include <mutex>
#include <queue>

namespace rtcast {

class DrmVideoEncoder : public VideoEncoder {
public:
	DrmVideoEncoder(string codecName, shared_ptr<Endpoint> endpoint);
	virtual ~DrmVideoEncoder();

	virtual void push(shared_ptr<AVFrame> frame) override;
	virtual void push(InputFrame input) override;
};

} // namespace rtcast

#endif
