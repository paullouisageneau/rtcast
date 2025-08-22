/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "audioencoder.hpp"

#include <iostream>
#include <stdexcept>

namespace rtcast {

const int DefaultFrameSizeMs = 20;

extern "C" {

static void free_buffer_shared_ptr(void *opaque, [[maybe_unused]] uint8_t *data) {
	auto ptr = reinterpret_cast<shared_ptr<void> *>(opaque);
	delete ptr;
}

}

AudioEncoder::AudioEncoder(string codecName, shared_ptr<Endpoint> endpoint)
    : Encoder(std::move(codecName)), mEndpoint(std::move(endpoint)) {

	av_opt_set(mCodecContext->priv_data, "preset", "ultrafast", 0);
	av_opt_set(mCodecContext->priv_data, "tune", "zerolatency", 0);

	Endpoint::AudioCodec endpointCodec;
	switch (mCodec->id) {
	case AV_CODEC_ID_OPUS:
		endpointCodec = Endpoint::AudioCodec::OPUS;
		mCodecContext->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
		mCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
		mCodecContext->sample_rate = 48000;
		setBitrate(128000); // default
		break;
	case AV_CODEC_ID_AAC:
		endpointCodec = Endpoint::AudioCodec::AAC;
		mCodecContext->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
		mCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
		mCodecContext->sample_rate = 48000;
		setBitrate(128000); // default
		break;
	case AV_CODEC_ID_PCM_MULAW:
		endpointCodec = Endpoint::AudioCodec::PCMU;
		mCodecContext->ch_layout = AV_CHANNEL_LAYOUT_MONO;
		mCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
		mCodecContext->sample_rate = 8000;
		break;
	case AV_CODEC_ID_PCM_ALAW:
		endpointCodec = Endpoint::AudioCodec::PCMA;
		mCodecContext->ch_layout = AV_CHANNEL_LAYOUT_MONO;
		mCodecContext->sample_fmt = AV_SAMPLE_FMT_S16;
		mCodecContext->sample_rate = 8000;
		break;
	default:
		throw std::runtime_error("Unsupported audio codec");
	}

	mEndpoint->setAudio(endpointCodec);

	mAudioFifo = unique_ptr_deleter<AVAudioFifo>(
	    av_audio_fifo_alloc(mCodecContext->sample_fmt, mCodecContext->ch_layout.nb_channels,
	                        mCodecContext->sample_rate),
	    av_audio_fifo_free);
}

AudioEncoder::~AudioEncoder() { stop(); }

void AudioEncoder::push(shared_ptr<AVFrame> frame) {
	if(mEndpoint->clientsCount() == 0)
		return; // no clients, no need to encode

	auto frameSampleFormat = static_cast<AVSampleFormat>(frame->format);
	if (!mSwrContext || mSwrInputSampleFormat != frameSampleFormat ||
	    mSwrInputNbChannels != frame->ch_layout.nb_channels ||
	    mSwrInputSampleRate != frame->sample_rate) {
		SwrContext *swrContext = nullptr;
		if (swr_alloc_set_opts2(&swrContext, &mCodecContext->ch_layout, mCodecContext->sample_fmt,
		                        mCodecContext->sample_rate, &frame->ch_layout, frameSampleFormat,
		                        frame->sample_rate, 0, nullptr) < 0)
			throw std::runtime_error("Failed to set up SWR context");

		mSwrContext =
		    unique_ptr_deleter<SwrContext>(swrContext, [](SwrContext *p) { swr_free(&p); });

		if (swr_init(mSwrContext.get()) < 0)
			throw std::runtime_error("Failed to initialize SWR context");

		mSwrInputSampleFormat = frameSampleFormat;
		mSwrInputNbChannels = frame->ch_layout.nb_channels;
		mSwrInputSampleRate = frame->sample_rate;
	}

	uint8_t **samples = nullptr;
	int ret =
	    av_samples_alloc_array_and_samples(&samples, NULL, mCodecContext->ch_layout.nb_channels,
	                                       frame->nb_samples, mCodecContext->sample_fmt, 0);
	if (ret < 0)
		throw std::runtime_error("Failed to allocate samples array");

	try {
		auto in = const_cast<const uint8_t **>(static_cast<uint8_t **>(frame->extended_data));
		ret = swr_convert(mSwrContext.get(), samples, frame->nb_samples, in, frame->nb_samples);
		if (ret < 0)
			throw std::runtime_error("Audio samples conversion failed");

		if (av_audio_fifo_space(mAudioFifo.get()) < frame->nb_samples)
			throw std::runtime_error("Audio FIFO buffer is too small");

		ret = av_audio_fifo_write(mAudioFifo.get(), reinterpret_cast<void **>(samples),
		                          frame->nb_samples);
		if (ret < 0)
			throw std::runtime_error("Failed to write samples to audio FIFO buffer");

	} catch (...) {
		av_freep(samples);
		throw;
	}

	// If the codec is variable frame size, use the default one
	int frame_size = mCodecContext->frame_size > 0 ? mCodecContext->frame_size : mCodecContext->sample_rate * DefaultFrameSizeMs / 1000;

	while (av_audio_fifo_size(mAudioFifo.get()) >= frame_size) {
		auto frame = shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *p) { av_frame_free(&p); });
		frame->format = mCodecContext->sample_fmt;
		frame->ch_layout = mCodecContext->ch_layout;
		frame->sample_rate = mCodecContext->sample_rate;
		frame->nb_samples = frame_size;
		frame->time_base = AVRational{1, mCodecContext->sample_rate};
		frame->pts = mSamplesCount;
		mSamplesCount += frame_size;

		ret = av_frame_get_buffer(frame.get(), 0);
		if (ret < 0)
			throw std::runtime_error("Failed to allocate buffer for frame");

		ret = av_audio_fifo_read(mAudioFifo.get(), reinterpret_cast<void **>(frame->data), frame_size);
		if (ret < 0)
			throw std::runtime_error("Failed to read samples from audio FIFO buffer");

		Encoder::push(std::move(frame));
	}
}

void AudioEncoder::push(InputFrame input) {
	if(mEndpoint->clientsCount() == 0) {
		// no clients, no need to encode
		if (input.finished)
			input.finished();

		return;
	}

	auto frame = shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *p) { av_frame_free(&p); });
	if (!frame)
		throw std::runtime_error("Failed to allocate AVFrame");

	frame->pts = 0; // ignored
	frame->format = input.format;
	frame->sample_rate = input.sampleRate;
	frame->nb_samples = input.nbSamples;
	av_channel_layout_default(&frame->ch_layout, input.nbChannels);

	struct FinishedWrapper {
		std::function<void()> finished;
		~FinishedWrapper() {
			if (finished)
				finished();
		}
	};
	auto finishedWrapper = std::make_shared<FinishedWrapper>();

	frame->buf[0] =
	    av_buffer_create(reinterpret_cast<uint8_t *>(input.data), input.size,
	                     free_buffer_shared_ptr, new shared_ptr<void>(finishedWrapper), 0);
	if (!frame->buf[0])
		throw std::runtime_error("Failed to create AVBuffer");

	frame->data[0] = frame->buf[0]->data;

	finishedWrapper->finished = std::move(input.finished);
	push(std::move(frame));
}

void AudioEncoder::output(AVPacket *packet) {
	mEndpoint->broadcastAudio(reinterpret_cast<const byte *>(packet->data), packet->size,
	                          uint32_t(packet->pts));
}

} // namespace rtcast
