/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "videoencoder.hpp"

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace rtcast {

extern "C" {

static void free_buffer_release_func(void *opaque, [[maybe_unused]] uint8_t *data) {
	auto func = reinterpret_cast<std::function<void()> *>(opaque);
	if (*func)
		(*func)();
	delete func;
}

static void free_buffer_shared_ptr(void *opaque, [[maybe_unused]] uint8_t *data) {
	auto ptr = reinterpret_cast<shared_ptr<void> *>(opaque);
	delete ptr;
}

}

VideoEncoder::VideoEncoder(string codecName, std::shared_ptr<Endpoint> endpoint)
    : Encoder(std::move(codecName)), mEndpoint(std::move(endpoint)) {

	mCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
	mCodecContext->sw_pix_fmt = AV_PIX_FMT_YUV420P;
	mCodecContext->time_base = {1, 1000000}; // usec
	mCodecContext->max_b_frames = 0;         // do not emit B-frames
	mCodecContext->me_range = 16;
	mCodecContext->me_cmp = 1;
	mCodecContext->me_subpel_quality = 0;

	av_opt_set(mCodecContext->priv_data, "preset", "ultrafast", 0);
	av_opt_set(mCodecContext->priv_data, "tune", "zerolatency", 0);

	Endpoint::VideoCodec endpointCodec;
	switch (mCodec->id) {
	case AV_CODEC_ID_H264:
		endpointCodec = Endpoint::VideoCodec::H264;
		mCodecContext->profile = FF_PROFILE_H264_CONSTRAINED_BASELINE;
		mCodecContext->level = FF_LEVEL_UNKNOWN;
		av_opt_set(mCodecContext->priv_data, "profile", "baseline", 0);
		av_opt_set(mCodecContext->priv_data, "x264opts", "no-scenecut", 0);
		break;
	case AV_CODEC_ID_H265:
		endpointCodec = Endpoint::VideoCodec::H265;
		break;
	case AV_CODEC_ID_VP8:
		endpointCodec = Endpoint::VideoCodec::VP8;
		break;
	case AV_CODEC_ID_VP9:
		endpointCodec = Endpoint::VideoCodec::VP9;
		break;
	case AV_CODEC_ID_AV1:
		endpointCodec = Endpoint::VideoCodec::AV1;
		break;
	default:
		throw std::runtime_error("Unsupported video codec");
	}

	mEndpoint->setVideo(endpointCodec);

	// Defaults
	setSize(1280, 720);
	setColorSettings({});
	setFramerate(30);
	setGopSize(60);
	setBitrate(4000000);
}

VideoEncoder::~VideoEncoder() { stop(); }

void VideoEncoder::setSize(int width, int height) {
	mCodecContext->width = width;
	mCodecContext->height = height;
}

void VideoEncoder::setFramerate(AVRational framerate) { mCodecContext->framerate = framerate; }

void VideoEncoder::setFramerate(int framerate) { setFramerate({framerate, 1}); }

void VideoEncoder::setGopSize(int gopsize) { mCodecContext->gop_size = gopsize; }

void VideoEncoder::setColorSettings(ColorSettings settings) {
	mCodecContext->color_primaries = settings.primaries;
	mCodecContext->color_trc = settings.transferCharacteristic;
	mCodecContext->colorspace = settings.space;
	mCodecContext->color_range = settings.range;
}

void VideoEncoder::push(shared_ptr<AVFrame> frame) {
	if(mEndpoint->clientsCount() == 0)
		return; // no clients, no need to encode

	// MJPEG may output deprecated pixel formats
	switch (static_cast<AVPixelFormat>(frame->format)) {
	case AV_PIX_FMT_YUVJ420P:
		frame->format = AV_PIX_FMT_YUV420P;
		frame->color_range = AVCOL_RANGE_JPEG;
		break;
	case AV_PIX_FMT_YUVJ422P:
		frame->format = AV_PIX_FMT_YUV422P;
		frame->color_range = AVCOL_RANGE_JPEG;
		break;
	case AV_PIX_FMT_YUVJ444P:
		frame->format = AV_PIX_FMT_YUV444P;
		frame->color_range = AVCOL_RANGE_JPEG;
	default:
		break;
	}

	if (frame->width == mCodecContext->width && frame->height == mCodecContext->height &&
	    static_cast<AVPixelFormat>(frame->format) == mCodecContext->pix_fmt) {
		Encoder::push(std::move(frame));
		return;
	}

	auto framePixelFormat = static_cast<AVPixelFormat>(frame->format);
	if (!mSwsContext || mSwsInputWidth != frame->width || mSwsInputHeight != frame->height ||
	    mSwsInputPixelFormat != framePixelFormat) {
		mSwsContext = unique_ptr_deleter<SwsContext>(
		    sws_getContext(frame->width, frame->height, framePixelFormat, //
		                   mCodecContext->width, mCodecContext->height, mCodecContext->pix_fmt,
		                   SWS_FAST_BILINEAR | SWS_FULL_CHR_H_INT | SWS_ACCURATE_RND, NULL, NULL,
		                   NULL),
		    sws_freeContext);
		if (!mSwsContext)
			throw std::runtime_error("Failed to get SWS context");

		mSwsInputWidth = frame->width;
		mSwsInputHeight = frame->height;
		mSwsInputPixelFormat = framePixelFormat;
	}

	auto converted = shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *p) { av_frame_free(&p); });
	if (!converted)
		throw std::runtime_error("Failed to allocate AVFrame");

	converted->width = mCodecContext->width;
	converted->height = mCodecContext->height;
	converted->format = mCodecContext->pix_fmt;
	converted->color_range = frame->color_range;
	converted->time_base = frame->time_base;
	converted->pts = frame->pts;

	int ret = av_frame_get_buffer(converted.get(), 32);
	if (ret < 0)
		throw std::runtime_error("Failed to allocate buffer for converted frame");

	ret = sws_scale(mSwsContext.get(), frame->data, frame->linesize, 0, frame->height,
	                converted->data, converted->linesize);
	if (ret < 0)
		throw std::runtime_error("Video frame conversion failed");

	Encoder::push(std::move(converted));
}

void VideoEncoder::push(InputFrame input) {
	if (input.planes.empty())
		throw std::logic_error("Input frame has no planes");

	if(mEndpoint->clientsCount() == 0)
		return; // no clients, no need to encode

	auto frame = shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *p) { av_frame_free(&p); });
	if (!frame)
		throw std::runtime_error("Failed to allocate AVFrame");

	frame->pts = input.ts.count();
	frame->format = input.pixelFormat;
	frame->width = input.width;
	frame->height = input.height;
	for (int i = 0; i < std::min(int(input.linesize.size()), AV_NUM_DATA_POINTERS); ++i)
		frame->linesize[i] = input.linesize[i];

	struct FinishedWrapper {
		std::function<void()> finished;
		~FinishedWrapper() {
			if(finished)
				finished();
		}
	};
	auto finishedWrapper = std::make_shared<FinishedWrapper>();

	int nbPlanes = std::min(int(input.planes.size()), int(AV_NUM_DATA_POINTERS));
	for (int i = 0; i < nbPlanes; ++i) {
		auto &plane = input.planes[i];
		if (plane.fd >= 0) {
#ifdef _WIN32
			throw std::logic_error("Memory mapping is not implemented on Windows");
#else
			plane.data = ::mmap(NULL, plane.size, PROT_READ, MAP_SHARED, plane.fd, 0);
			if (plane.data == MAP_FAILED)
				throw std::runtime_error("Memory mapping failed");

			auto release = new std::function<void()>(
			    [plane, finishedWrapper]() { ::munmap(plane.data, plane.size); });
			frame->buf[i] = av_buffer_create(reinterpret_cast<uint8_t *>(plane.data), plane.size,
			                                 free_buffer_release_func, release, 0);
#endif
		} else {
			frame->buf[i] =
			    av_buffer_create(reinterpret_cast<uint8_t *>(plane.data), plane.size,
			                     free_buffer_shared_ptr, new shared_ptr<void>(finishedWrapper), 0);
		}

		if(!frame->buf[i])
			throw std::runtime_error("Failed to create AVBuffer");
	}

	if (nbPlanes == 1) {
		av_image_fill_pointers(frame->data, input.pixelFormat, frame->height,
		                       frame->buf[0]->data, frame->linesize);
	} else {
		for (int i = 0; i < nbPlanes; ++i)
			frame->data[i] = frame->buf[i]->data;
	}

	finishedWrapper->finished = std::move(input.finished);
	push(std::move(frame));
}

void VideoEncoder::output(AVPacket *packet) {
	int64_t usecs = av_rescale_q(packet->pts, mCodecContext->time_base, AVRational{1, 1000000});
	mEndpoint->broadcastVideo(reinterpret_cast<const byte *>(packet->data), packet->size,
	                          std::chrono::microseconds(usecs));
}

} // namespace rtcast
