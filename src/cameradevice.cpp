/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "cameradevice.hpp"

#if RTCAST_HAS_LIBCAMERA

#include <libcamera/libcamera.h>

#include <sys/mman.h>

#include <iostream>
#include <stdexcept>

namespace rtcast {

namespace {

shared_ptr<AVPacket> make_packet(int fd, size_t len) {
	AVPacket *packet = av_packet_alloc();
	if (!packet)
		return nullptr;

	void *mem = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
	if (mem == MAP_FAILED) {
		av_packet_free(&packet);
		return nullptr;
	}

	packet->data = static_cast<uint8_t *>(mem);
	packet->size = static_cast<int>(len);

	return shared_ptr<AVPacket>(packet, [mem, len](AVPacket *packet) {
		munmap(mem, len);
		av_packet_free(&packet);
	});
}

} // namespace

std::once_flag CameraDevice::OnceFlag;
std::unique_ptr<libcamera::CameraManager> CameraDevice::CameraManager;

CameraDevice::CameraDevice(string deviceName, shared_ptr<VideoEncoder> encoder)
    : mEncoder(encoder) {
	std::call_once(OnceFlag, []() {
		CameraManager = std::make_unique<libcamera::CameraManager>();
		CameraManager->start();

		std::cout << "Available cameras:" << std::endl;
		for (const auto &camera : CameraManager->cameras())
			std::cout << camera->id() << std::endl;
	});

	std::string cameraId;
	if (deviceName.empty() || deviceName == "default") {
		auto cameras = CameraManager->cameras();
		if (cameras.empty())
			throw std::runtime_error("No camera found");

		cameraId = cameras[0]->id();
	} else {
		cameraId = deviceName;
	}

	std::cout << "Using camera: " << cameraId << std::endl;
	mCamera = CameraManager->get(cameraId);
	if (!mCamera)
		throw std::runtime_error("Failed to get camera");

	if (mCamera->acquire() < 0)
		throw std::runtime_error("Failed to acquire");

	mConfig = mCamera->generateConfiguration({libcamera::StreamRole::VideoRecording});

	auto &streamConfig = mConfig->at(0);
	std::cout << "Default configuration is: " << streamConfig.toString() << std::endl;

	// streamConfig.size.width = 1280;
	// streamConfig.size.height = 720;

	if (mConfig->validate() == libcamera::CameraConfiguration::Invalid)
		throw std::runtime_error("Failed to validate configuration");

	std::cout << "Validated configuration is: " << streamConfig.toString() << std::endl;

	if (mCamera->configure(mConfig.get()) < 0)
		throw std::runtime_error("Failed to apply camera configuration");

	if (streamConfig.pixelFormat == libcamera::formats::MJPEG)
		initInputCodec(AV_CODEC_ID_MJPEG);

	mEncoder->setSize(streamConfig.size.width, streamConfig.size.height);

	if (auto colorSpace = streamConfig.colorSpace) {
		using libcamera::ColorSpace;

		VideoEncoder::ColorSettings settings = {};

		switch (colorSpace->primaries) {
		case ColorSpace::Primaries::Raw:
			settings.primaries = AVCOL_PRI_UNSPECIFIED;
			break;
		case ColorSpace::Primaries::Smpte170m:
			settings.primaries = AVCOL_PRI_SMPTE170M;
			break;
		case ColorSpace::Primaries::Rec709:
			settings.primaries = AVCOL_PRI_BT709;
			break;
		case ColorSpace::Primaries::Rec2020:
			settings.primaries = AVCOL_PRI_BT2020;
			break;
		default:
			throw std::runtime_error("Unknown color primaries in " + colorSpace->toString());
		}

		switch (colorSpace->transferFunction) {
		case ColorSpace::TransferFunction::Linear:
			settings.transferCharacteristic = AVCOL_TRC_LINEAR;
			break;
		case ColorSpace::TransferFunction::Srgb:
			settings.transferCharacteristic = AVCOL_TRC_IEC61966_2_1;
			break;
		case ColorSpace::TransferFunction::Rec709:
			settings.transferCharacteristic = AVCOL_TRC_BT709;
			break;
		default:
			throw std::runtime_error("Unknown color transfer function in " +
			                         colorSpace->toString());
		}

		switch (colorSpace->ycbcrEncoding) {
		case ColorSpace::YcbcrEncoding::None:
			settings.space = AVCOL_SPC_UNSPECIFIED;
			break;
		case ColorSpace::YcbcrEncoding::Rec601:
			settings.space = AVCOL_SPC_SMPTE170M;
			break;
		case ColorSpace::YcbcrEncoding::Rec709:
			settings.space = AVCOL_SPC_BT709;
			break;
		case ColorSpace::YcbcrEncoding::Rec2020:
			settings.space = AVCOL_SPC_BT2020_CL;
			break;
		default:
			throw std::runtime_error("Unknown color YCBCR encoding in " + colorSpace->toString());
		}

		switch (colorSpace->range) {
		case ColorSpace::Range::Full:
			settings.range = AVCOL_RANGE_JPEG;
			break;
		case ColorSpace::Range::Limited:
			settings.range = AVCOL_RANGE_MPEG;
			break;
		default:
			throw std::runtime_error("Unknown color range in " + colorSpace->toString());
		}

		mEncoder->setColorSettings(std::move(settings));
	}

	mAllocator = std::make_unique<libcamera::FrameBufferAllocator>(mCamera);

	for (libcamera::StreamConfiguration &cfg : *mConfig) {
		int ret = mAllocator->allocate(cfg.stream());
		if (ret < 0)
			throw std::runtime_error("Failed to allocate buffers");

		size_t allocated = mAllocator->buffers(cfg.stream()).size();
		std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;
	}

	libcamera::Stream *stream = streamConfig.stream();
	const std::vector<std::unique_ptr<libcamera::FrameBuffer>> &buffers =
	    mAllocator->buffers(stream);

	for (unsigned int i = 0; i < buffers.size(); ++i) {
		std::unique_ptr<libcamera::Request> request = mCamera->createRequest();
		if (!request)
			throw std::runtime_error("Failed to create request");

		const std::unique_ptr<libcamera::FrameBuffer> &buffer = buffers[i];
		int ret = request->addBuffer(stream, buffer.get());
		if (ret < 0)
			throw std::runtime_error("Failed to set buffer for request");

		mRequests.push_back(std::move(request));
	}

	mCamera->requestCompleted.connect(this, &CameraDevice::requestComplete);

	std::cout << "CameraDevice created" << std::endl;
}

CameraDevice::~CameraDevice() {}

void CameraDevice::initInputCodec(AVCodecID codecId) {
	const AVCodec *inputCodec = avcodec_find_decoder(codecId);
	if (!inputCodec)
		throw std::runtime_error("Unable to find codec for input video stream");

	mInputCodecContext = unique_ptr_deleter<AVCodecContext>(
	    avcodec_alloc_context3(inputCodec), [](AVCodecContext *p) { avcodec_free_context(&p); });
	if (!mInputCodecContext)
		throw std::runtime_error("Unable to allocate codec context for input video stream");

	if (avcodec_open2(mInputCodecContext.get(), inputCodec, nullptr) < 0)
		throw std::runtime_error("Unable to open codec for input video stream");
}

void CameraDevice::start() {
	mEncoder->start();
	mCamera->start();
	for (std::unique_ptr<libcamera::Request> &request : mRequests)
		mCamera->queueRequest(request.get());
}

void CameraDevice::stop() {
	mCamera->stop();

	for (libcamera::StreamConfiguration &cfg : *mConfig)
		mAllocator->free(cfg.stream());

	mCamera->release();
}

void CameraDevice::requestComplete(libcamera::Request *request) {
	if (request->status() == libcamera::Request::RequestCancelled)
		return;

	for (auto bufferPair : request->buffers()) {
		libcamera::FrameBuffer *buffer = bufferPair.second;
		const libcamera::FrameMetadata &metadata = buffer->metadata();

		std::cout << " seq: " << metadata.sequence << " bytesused: ";

		unsigned int nplane = 0;
		for (const libcamera::FrameMetadata::Plane &plane : metadata.planes()) {
			std::cout << plane.bytesused;
			if (++nplane < metadata.planes().size())
				std::cout << "/";
		}

		std::cout << std::endl;

		auto finished = [this, request]() {
			request->reuse(libcamera::Request::ReuseBuffers);
			mCamera->queueRequest(request);
		};

		const libcamera::FrameBuffer::Plane &plane = buffer->planes().front();

		if (mInputCodecContext) {
			auto packet = make_packet(plane.fd.get(), plane.length);
			packet->time_base = {1, 1000000}; // usec
			packet->pts = metadata.timestamp / 1000;
			if (avcodec_send_packet(mInputCodecContext.get(), packet.get()) < 0)
				throw std::runtime_error("Error sending frame for decoding");

			auto frame = shared_ptr<AVFrame>(av_frame_alloc(), //
			                                 [finished = std::move(finished)](AVFrame *p) {
				                                 av_frame_free(&p);
				                                 finished();
			                                 });

			if (avcodec_receive_frame(mInputCodecContext.get(), frame.get()) < 0)
				throw std::runtime_error("Error getting decoded frame");

			mEncoder->push(std::move(frame));
			return;
		}

		const auto &streamConfig = mConfig->at(0);

		VideoEncoder::InputFrame frame = {};
		frame.ts = std::chrono::microseconds(metadata.timestamp / 1000);
		frame.width = streamConfig.size.width;
		frame.height = streamConfig.size.height;
		frame.finished = std::move(finished);
		for (const auto &plane : buffer->planes()) {
			VideoEncoder::Plane p;
			p.fd = plane.fd.get();
			p.size = plane.length;
			frame.planes.push_back(std::move(p));
        }

		int stride = streamConfig.stride;
		switch (streamConfig.pixelFormat) {
		case libcamera::formats::YUV420:
			frame.pixelFormat = AV_PIX_FMT_YUV420P;
			frame.linesize.push_back(stride);
			frame.linesize.push_back(stride / 2);
			frame.linesize.push_back(stride / 2);
			break;
		case libcamera::formats::YUV422:
			frame.pixelFormat = AV_PIX_FMT_YUV422P;
			frame.linesize.push_back(stride);
			frame.linesize.push_back(stride / 2);
			frame.linesize.push_back(stride / 2);
			break;
		case libcamera::formats::YUV444:
			frame.pixelFormat = AV_PIX_FMT_YUV444P;
			frame.linesize.push_back(stride);
			frame.linesize.push_back(stride);
			frame.linesize.push_back(stride);
			break;
		case libcamera::formats::YUYV:
			frame.pixelFormat = AV_PIX_FMT_YUYV422;
			frame.linesize.push_back(stride);
			frame.linesize.push_back(stride);
			frame.linesize.push_back(stride);
			break;
		default:
			throw std::runtime_error("Unknown pixel format: " +
			                         streamConfig.pixelFormat.toString());
		}

		mEncoder->push(std::move(frame));
	}
}

} // namespace rtcast

#endif
