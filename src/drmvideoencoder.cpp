/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "drmvideoencoder.hpp"

extern "C" {
#include <libavutil/hwcontext_drm.h>
#include <libdrm/drm_fourcc.h>
}

#include <iostream>
#include <stdexcept>

namespace rtcast {

DrmVideoEncoder::DrmVideoEncoder(string codecName, shared_ptr<Endpoint> endpoint)
    : VideoEncoder(std::move(codecName), std::move(endpoint)) {

	mCodecContext->pix_fmt = AV_PIX_FMT_DRM_PRIME;
}

DrmVideoEncoder::~DrmVideoEncoder() {}

void DrmVideoEncoder::push(shared_ptr<AVFrame> frame) { VideoEncoder::push(std::move(frame)); }

void DrmVideoEncoder::push(InputFrame input) {
	if (input.planes.empty())
		throw std::logic_error("Input frame has no planes");

	if (input.pixelFormat != AV_PIX_FMT_YUV420P)
		throw std::logic_error("Unexpected pixel format for DRM video encoder");

	auto desc = std::make_shared<AVDRMFrameDescriptor>();
	auto frame =
	    shared_ptr<AVFrame>(av_frame_alloc(), [finished = std::move(input.finished)](AVFrame *p) {
		    av_frame_free(&p);
		    if (finished)
			    finished();
	    });
	if (!frame)
		throw std::runtime_error("Failed to allocate AVFrame");

	int nbPlanes = std::min(int(input.planes.size()), AV_NUM_DATA_POINTERS);

	frame->data[0] = reinterpret_cast<uint8_t *>(desc.get());
	frame->pts = input.ts.count();
	frame->format = input.pixelFormat;
	frame->width = input.width;
	frame->height = input.height;
	for (int i = 0; i < nbPlanes; ++i) {
		if (i < int(input.linesize.size()))
			frame->linesize[i] = input.linesize[i];
		else
			frame->linesize[i] = i > 0 ? frame->linesize[i - 1] : 0;
	}

	// planes are mapped to objects
	desc->nb_objects = nbPlanes;
	for (int i = 0; i < std::min(nbPlanes, int(AV_DRM_MAX_PLANES)); ++i) {
		desc->objects[i].fd = input.planes[i].fd;
		desc->objects[i].size = input.planes[i].size;
		desc->objects[i].format_modifier = DRM_FORMAT_MOD_INVALID;
	}

	// actual planes
	const auto height = input.height;
	const auto &linesize = input.linesize;
	desc->nb_layers = 1;
	desc->layers[0].format = DRM_FORMAT_YUV420;
	desc->layers[0].nb_planes = 3;
	switch (nbPlanes) {
	case 1:
		desc->layers[0].planes[0].object_index = 0;
		desc->layers[0].planes[0].offset = 0;
		desc->layers[0].planes[0].pitch = linesize[0];
		desc->layers[0].planes[1].object_index = 0;
		desc->layers[0].planes[1].offset = linesize[0] * height;
		desc->layers[0].planes[1].pitch = linesize[1];
		desc->layers[0].planes[2].object_index = 0;
		desc->layers[0].planes[2].offset = (linesize[0] + linesize[1]) * height;
		desc->layers[0].planes[2].pitch = linesize[2];
		break;
	case 3:
		desc->layers[0].planes[0].object_index = 0;
		desc->layers[0].planes[0].offset = 0;
		desc->layers[0].planes[0].pitch = linesize[0];
		desc->layers[0].planes[1].object_index = 1;
		desc->layers[0].planes[1].offset = 0;
		desc->layers[0].planes[1].pitch = linesize[1];
		desc->layers[0].planes[2].object_index = 2;
		desc->layers[0].planes[2].offset = 0;
		desc->layers[0].planes[2].pitch = linesize[2];
		break;
	default:
		throw std::logic_error("Unexpected number of planes for YUV420");
	}

	push(std::move(frame));
}

} // namespace rtcast
