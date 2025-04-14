/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "audiodecoder.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace rtcast {

AudioDecoder::AudioDecoder(string codecName, shared_ptr<AudioSink> sink)
    : Decoder(std::move(codecName)), mSink(std::move(sink)) {

	switch (mCodec->id) {
	case AV_CODEC_ID_OPUS:
		mCodecContext->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
		mCodecContext->sample_rate = 48000;
		mCodecContext->request_sample_fmt = AV_SAMPLE_FMT_S16;
		break;
	case AV_CODEC_ID_PCM_MULAW:
		mCodecContext->ch_layout = AV_CHANNEL_LAYOUT_MONO;
		mCodecContext->sample_rate = 8000;
		mCodecContext->request_sample_fmt = AV_SAMPLE_FMT_U8;
		break;
	case AV_CODEC_ID_PCM_ALAW:
		mCodecContext->ch_layout = AV_CHANNEL_LAYOUT_MONO;
		mCodecContext->sample_rate = 8000;
		mCodecContext->request_sample_fmt = AV_SAMPLE_FMT_U8;
		break;
	case AV_CODEC_ID_AAC:
		mCodecContext->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
		mCodecContext->sample_rate = 48000;
		mCodecContext->request_sample_fmt = AV_SAMPLE_FMT_S16;
		break;
	default:
		break;
	}

	mCodecContext->time_base = AVRational{1, mCodecContext->sample_rate};
}

AudioDecoder::~AudioDecoder() {}

void AudioDecoder::output(AVFrame *frame) {
	int bytesPerSample = av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format));

	// TODO: init on start with context info
	if (!std::exchange(mGotFirstFrame, true)) {
		AudioSink::Config config = {};
		config.sampleRate = frame->sample_rate;
		config.sampleBits = bytesPerSample * 8;
		config.nbChannels = frame->ch_layout.nb_channels;
		mSink->init(config);
	}

	size_t size = frame->nb_samples * frame->ch_layout.nb_channels * bytesPerSample;
	mSink->play(reinterpret_cast<byte *>(frame->extended_data[0]), size);
}

} // namespace rtcast
