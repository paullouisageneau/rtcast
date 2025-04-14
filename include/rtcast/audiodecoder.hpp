/**
 * Copyright (c) 2025 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include "decoder.hpp"
#include "audiosink.hpp"

#include <chrono>

namespace rtcast {

class AudioDecoder : public Decoder {
public:
	AudioDecoder(string codecName, shared_ptr<AudioSink> sink);
	virtual ~AudioDecoder();

protected:
	void output(AVFrame *frame) override;

private:
	shared_ptr<AudioSink> mSink;
	bool mGotFirstFrame = false;
};

} // namespace rtcast

#endif
