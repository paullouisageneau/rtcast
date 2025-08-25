/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AUDIO_ENCODER_H
#define AUDIO_ENCODER_H

#include "encoder.hpp"
#include "endpoint.hpp"

extern "C" {
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}

#include <chrono>

namespace rtcast {

class AudioEncoder : public Encoder {
public:
	AudioEncoder(string codecName, shared_ptr<Endpoint> endpoint);
	virtual ~AudioEncoder();

	int sampleRate() const;
	int channelsCount() const;

	using finished_callback_t = std::function<void()>;

	struct InputFrame {
		AVSampleFormat format = AV_SAMPLE_FMT_S16;
		int sampleRate = 48000;
		int nbChannels = 2;
		int nbSamples = 0;
		void *data = nullptr;
		size_t size = 0;
		finished_callback_t finished;
	};

	virtual void push(shared_ptr<AVFrame> frame) override;
	virtual void push(InputFrame input);

protected:
	void output(AVPacket *packet) override;

private:
	shared_ptr<Endpoint> mEndpoint;

	unique_ptr_deleter<AVAudioFifo> mAudioFifo;
	unique_ptr_deleter<SwrContext> mSwrContext;
	AVSampleFormat mSwrInputSampleFormat;
	int mSwrInputNbChannels;
	int mSwrInputSampleRate;
	std::int64_t mSamplesCount = 0;
};

} // namespace rtcast

#endif
