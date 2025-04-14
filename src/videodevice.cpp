/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "videodevice.hpp"

#include <iostream>
#include <stdexcept>

namespace rtcast {

VideoDevice::VideoDevice(string deviceName, shared_ptr<VideoEncoder> encoder) : mEncoder(encoder) {
	static std::once_flag onceFlag;
	std::call_once(onceFlag, []() { avdevice_register_all(); });

#ifdef _WIN32
	const std::string name = "dshow";
#elif __APPLE__
	const std::string name = "avfoundation";
#else
	const std::string name = "v4l2";
#endif

	const AVInputFormat *inputFormat = av_find_input_format(name.c_str());
	if (!inputFormat)
		throw std::runtime_error("Failed to find input format:" + name);

	AVDictionary *options = NULL;
	// av_dict_set(&options, "video_size", "1280x720", 0);
	// av_dict_set(&options, "framerate", "30", 0);

	AVFormatContext *formatContext = nullptr;
	if (avformat_open_input(&formatContext, deviceName.c_str(), inputFormat, &options) < 0)
		throw std::runtime_error("Failed to open input");

	mFormatContext = unique_ptr_deleter<AVFormatContext>(
	    formatContext, [](AVFormatContext *p) { avformat_close_input(&p); });
	if (!mFormatContext)
		throw std::runtime_error("Failed to allocate format context");

	if (avformat_find_stream_info(mFormatContext.get(), NULL) < 0)
		throw std::runtime_error("Failed to find input stream information");

	mInputStream = nullptr;
	for (unsigned int i = 0; i < mFormatContext->nb_streams; ++i) {
		auto stream = mFormatContext->streams[i];
		if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			mInputStream = stream;
			break;
		}
	}

	if (!mInputStream)
		throw std::runtime_error("Failed to find an input video stream");

	mInputCodec = avcodec_find_decoder(mInputStream->codecpar->codec_id);
	if (!mInputCodec)
		throw std::runtime_error("Failed to find codec for input video stream");

	mInputCodecContext = unique_ptr_deleter<AVCodecContext>(
	    avcodec_alloc_context3(mInputCodec), [](AVCodecContext *p) { avcodec_free_context(&p); });

	if (avcodec_parameters_to_context(mInputCodecContext.get(), mInputStream->codecpar) < 0)
		throw std::runtime_error("Failed to set codec parameters");

	mInputCodecContext->framerate = mInputStream->avg_frame_rate;
	mInputCodecContext->time_base = mInputStream->time_base;

	mEncoder->setSize(mInputCodecContext->width, mInputCodecContext->height);
	mEncoder->setFramerate(mInputStream->avg_frame_rate);
	mEncoder->setColorSettings({
	    mInputCodecContext->color_primaries,
	    mInputCodecContext->color_trc,
	    mInputCodecContext->colorspace,
	    mInputCodecContext->color_range,
	});
}

VideoDevice::~VideoDevice() {}

void VideoDevice::start() {
	if (avcodec_open2(mInputCodecContext.get(), mInputCodec, NULL) < 0)
		throw std::runtime_error("Failed to open codec for input video stream");

	mEncoder->start();
	mRunning = true;
	mThread = std::thread(std::bind(&VideoDevice::run, this));
}

void VideoDevice::stop() {
	if (mRunning.exchange(false)) {
		mThread.join();
		mEncoder->stop();
	}
}

void VideoDevice::run() {
	std::cout << "Starting loop" << std::endl;

	auto frame = shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *p) { av_frame_free(&p); });
	if (!frame)
		throw std::runtime_error("Failed to allocate frame");

	auto packet = shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket *p) { av_packet_free(&p); });
	if (!packet)
		throw std::runtime_error("Failed to allocate packet");

	while (mRunning) {
		int ret = av_read_frame(mFormatContext.get(), packet.get());
		if (ret < 0)
			throw std::runtime_error("Failed to read frame");

		avcodec_send_packet(mInputCodecContext.get(), packet.get());
		av_packet_unref(packet.get());

		while ((ret = avcodec_receive_frame(mInputCodecContext.get(), frame.get())) == 0) {
			mEncoder->push(std::move(frame));

			frame = shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *p) { av_frame_free(&p); });
			if (!frame)
				throw std::runtime_error("Failed to allocate frame");
		}
	}
}

} // namespace rtcast
